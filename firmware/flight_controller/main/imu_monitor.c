#include "imu_monitor.h"
#include "imu_attitude.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"
#include "wifi_manager.h"

#define IMU_SDA_GPIO             GPIO_NUM_5   // XIAO D4
#define IMU_SCL_GPIO             GPIO_NUM_6   // XIAO D5
#define IMU_I2C_PORT             I2C_NUM_0
#define IMU_I2C_SPEED_HZ         100000
#define IMU_TIMEOUT_MS           1000
#define IMU_PRINT_PERIOD_MS      100       // Only console display rate, not IMU ODR.

#define LSM_WHO_AM_I             0x0F
#define LSM_WHO_AM_I_EXPECTED    0x6A
#define LSM_CTRL1_XL             0x10
#define LSM_CTRL2_G              0x11
#define LSM_CTRL3_C              0x12
#define LSM_STATUS_REG           0x1E
#define LSM_OUT_TEMP_L           0x20

static const char *TAG = "IMU";
static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static bool s_started = false;

// Protected snapshot avoids concurrent I2C access from the UDP task.
static portMUX_TYPE s_sample_mux = portMUX_INITIALIZER_UNLOCKED;
static imu_raw_sample_t s_latest = {0};
static bool s_has_sample = false;


#define IMU_STREAM_PORT 5006
// LSM6DS3TR-C datasheet sensitivities for ±2 g and ±250 dps.
static const double ACCEL_MPS2_PER_COUNT = 0.061 / 1000.0 * 9.80665;
static const double GYRO_DPS_PER_COUNT = 8.75 / 1000.0;
#define IMU_BIAS_SAMPLE_COUNT 30
#define IMU_BIAS_TIMEOUT_US 8000000
#define IMU_GRAVITY_MPS2 9.80665

typedef struct {
    double accel[3];
    double gyro[3];
    bool applied;
} imu_bias_t;

typedef struct {
    imu_raw_sample_t raw;
    imu_measurement_t measurement;
    imu_euler_t attitude;
    imu_euler_t relative;
    uint32_t reference_seq;
    bool attitude_valid;
} imu_stream_sample_t;

static imu_bias_t s_bias;
static imu_bias_t s_latest_bias;
static imu_attitude_config_t s_attitude_config = {
    .filter = IMU_FILTER_COMPLEMENTARY,
    .tau_ms = 500,
    .kp_milli = 1000,
    .ki_milli = 50,
};
static unsigned s_attitude_revision;
static struct {
    double latest_q[4];
    int64_t latest_us;
    uint32_t latest_seq;
    double reference_q[4];
    uint32_t reference_seq;  // Zero when no reference is set.
} s_attitude_reference;
static struct {
    int64_t deadline_us;
    int64_t last_sample_us;
    unsigned count;
    unsigned gravity_axis;
    double expected_gravity;
    int16_t min[6], max[6];
    int32_t sum[6];
    const char *state;
} s_calibration = {.state = "IDLE"};
static QueueHandle_t s_stream_queue;

static void convert_measurement(const imu_raw_sample_t *raw, const imu_bias_t *bias,
                                imu_measurement_t *out)
{
    *out = (imu_measurement_t){
        .seq = raw->seq,
        .t_us = raw->t_us,
        .accel_mps2 = {
            raw->ax * ACCEL_MPS2_PER_COUNT - bias->accel[0],
            raw->ay * ACCEL_MPS2_PER_COUNT - bias->accel[1],
            raw->az * ACCEL_MPS2_PER_COUNT - bias->accel[2],
        },
        .gyro_dps = {
            raw->gx * GYRO_DPS_PER_COUNT - bias->gyro[0],
            raw->gy * GYRO_DPS_PER_COUNT - bias->gyro[1],
            raw->gz * GYRO_DPS_PER_COUNT - bias->gyro[2],
        },
        .bias_applied = bias->applied,
    };
}

// The caller holds s_sample_mux for calibration state and bias updates.
static void expire_calibration(int64_t now_us)
{
    if (s_calibration.deadline_us && now_us >= s_calibration.deadline_us) {
        s_calibration.deadline_us = 0;
        s_calibration.state = "TIMEOUT";
    }
}

static void collect_calibration(const imu_raw_sample_t *raw)
{
    expire_calibration(raw->t_us);
    if (!s_calibration.deadline_us || raw->t_us < s_calibration.deadline_us - IMU_BIAS_TIMEOUT_US
        || (raw->status & 0x03) != 0x03) return;
    if (s_calibration.count && raw->t_us - s_calibration.last_sample_us > 500000) {
        s_calibration.deadline_us = 0;
        s_calibration.state = "TIMEOUT";
        return;
    }
    s_calibration.last_sample_us = raw->t_us;

    const int16_t axes[] = {raw->ax, raw->ay, raw->az, raw->gx, raw->gy, raw->gz};
    for (unsigned i = 0; i < 6; ++i) {
        if (!s_calibration.count || axes[i] < s_calibration.min[i]) s_calibration.min[i] = axes[i];
        if (!s_calibration.count || axes[i] > s_calibration.max[i]) s_calibration.max[i] = axes[i];
        s_calibration.sum[i] += axes[i];
    }
    if (++s_calibration.count < IMU_BIAS_SAMPLE_COUNT) return;
    s_calibration.deadline_us = 0;

    imu_bias_t candidate = {.applied = true};
    for (unsigned i = 0; i < 6; ++i) {
        const bool accel = i < 3;
        const double scale = accel ? ACCEL_MPS2_PER_COUNT : GYRO_DPS_PER_COUNT;
        const double span = (s_calibration.max[i] - s_calibration.min[i]) * scale;
        const double expected = i == s_calibration.gravity_axis ? s_calibration.expected_gravity : 0;
        const double offset = s_calibration.sum[i] * scale / IMU_BIAS_SAMPLE_COUNT - expected;
        const double limit = accel ? 1.0 : 5.0;
        if (span > (accel ? 0.35 : 1.5) || offset < -limit || offset > limit) {
            s_calibration.state = "UNSTABLE";
            return;
        }
        if (accel) candidate.accel[i] = offset;
        else candidate.gyro[i - 3] = offset;
    }
    s_bias = candidate;
    ++s_attitude_revision;
    s_calibration.state = "DONE";
}

static void imu_stream_task(void *arg)
{
    struct sockaddr_in target = {
        .sin_family = AF_INET,
        .sin_port = htons(IMU_STREAM_PORT),
    };
    target.sin_addr = *(const struct in_addr *)arg;

    const uint32_t boot_id = esp_random();
    int fd = -1;
    uint32_t epoch = 0;
    imu_stream_sample_t frame;
    while (true) {
        if (xQueueReceive(s_stream_queue, &frame, portMAX_DELAY) != pdTRUE) continue;
        uint32_t current_epoch = wifi_manager_epoch();
        bool connected = wifi_manager_connected();
        if (fd >= 0 && (!connected || current_epoch != epoch)) {
            close(fd);
            fd = -1;
        }
        if (!connected) continue;
        if (fd < 0) {
            fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
            if (fd < 0) continue;
            epoch = current_epoch;
        }

        const imu_raw_sample_t sample = frame.raw;
        const imu_measurement_t *measurement = &frame.measurement;
        char packet[272];
        int size = snprintf(packet, sizeof(packet),
                            "%s IMU %08" PRIx32 " %" PRIu32 " %" PRIi64
                            " %d %d %d %d %d %d %d 0x%02X %.6f %.6f %.6f %.6f %.6f %.6f",
                            frame.reference_seq ? "HK4" : frame.attitude_valid ? "HK3" : "HK2",
                            boot_id, sample.seq, sample.t_us, sample.ax, sample.ay, sample.az,
                            sample.gx, sample.gy, sample.gz, sample.temp, (unsigned)sample.status,
                            measurement->accel_mps2[0], measurement->accel_mps2[1],
                            measurement->accel_mps2[2], measurement->gyro_dps[0],
                            measurement->gyro_dps[1], measurement->gyro_dps[2]);
        if (frame.attitude_valid && size > 0 && (size_t)size < sizeof(packet)) {
            const int extra = snprintf(packet + size, sizeof(packet) - (size_t)size, " %.6f %.6f %.6f",
                                       frame.attitude.roll_deg, frame.attitude.pitch_deg, frame.attitude.yaw_deg);
            if (extra < 0) continue;
            size += extra;
        }
        if (frame.reference_seq && size > 0 && (size_t)size < sizeof(packet)) {
            const int extra = snprintf(packet + size, sizeof(packet) - (size_t)size,
                                       " %" PRIu32 " %.6f %.6f %.6f", frame.reference_seq,
                                       frame.relative.roll_deg, frame.relative.pitch_deg, frame.relative.yaw_deg);
            if (extra < 0) continue;
            size += extra;
        }
        if (size > 0 && (size_t)size < sizeof(packet)) {
            sendto(fd, packet, (size_t)size, MSG_DONTWAIT,
                   (const struct sockaddr *)&target, sizeof(target));
        }
    }
}

static void imu_stream_start(void)
{
    if (CONFIG_HIKOROBO_IMU_STREAM_HOST[0] == '\0') return;
    static struct in_addr address;
    if (inet_pton(AF_INET, CONFIG_HIKOROBO_IMU_STREAM_HOST, &address) != 1) {
        ESP_LOGE(TAG, "Invalid IMU stream destination");
        return;
    }

    // A single-slot queue keeps the newest sample if transmission falls behind.
    s_stream_queue = xQueueCreate(1, sizeof(imu_stream_sample_t));
    if (s_stream_queue == NULL ||
        xTaskCreate(imu_stream_task, "imu_stream", 4096, &address, 4, NULL) != pdPASS) {
        if (s_stream_queue != NULL) vQueueDelete(s_stream_queue);
        s_stream_queue = NULL;
        ESP_LOGE(TAG, "Could not start IMU stream task");
    }
}

static esp_err_t read_reg(uint8_t reg, uint8_t *out, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, len, IMU_TIMEOUT_MS);
}

static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    const uint8_t bytes[] = {reg, value};
    return i2c_master_transmit(s_dev, bytes, sizeof(bytes), IMU_TIMEOUT_MS);
}

static int16_t signed_le16(const uint8_t *p)
{
    // LSM6DS3TR-C output samples use little-endian signed two's complement.
    const uint16_t bits = ((uint16_t)p[1] << 8) | p[0];
    return (int16_t)bits;
}

static void imu_raw_print_task(void *arg)
{
    (void)arg;
    imu_stream_start();
    imu_attitude_t attitude = {0};
    unsigned attitude_revision = 0;
    ESP_LOGI(TAG, "Header: IMU_RAW,t_us,ax,ay,az,gx,gy,gz,temp,status");
    while (true) {
        uint8_t status = 0;
        uint8_t raw[14] = {0};

        esp_err_t err = read_reg(LSM_STATUS_REG, &status, sizeof(status));
        if (err == ESP_OK) {
            // 0x20-0x21: temp, 0x22-0x27: gyro XYZ, 0x28-0x2D: accel XYZ.
            err = read_reg(LSM_OUT_TEMP_L, raw, sizeof(raw));
        }

        if (err == ESP_OK) {
            int16_t temp = signed_le16(&raw[0]);
            int16_t gx = signed_le16(&raw[2]);
            int16_t gy = signed_le16(&raw[4]);
            int16_t gz = signed_le16(&raw[6]);
            int16_t ax = signed_le16(&raw[8]);
            int16_t ay = signed_le16(&raw[10]);
            int16_t az = signed_le16(&raw[12]);
            // Timestamp is from ESP32's timer, NOT the IMU's internal timestamp.
            int64_t t_us = esp_timer_get_time();

            imu_raw_sample_t sample = {
                .t_us = t_us,
                .ax = ax, .ay = ay, .az = az,
                .gx = gx, .gy = gy, .gz = gz,
                .temp = temp, .status = status,
            };
            taskENTER_CRITICAL(&s_sample_mux);
            sample.seq = s_latest.seq + 1;
            collect_calibration(&sample);
            const imu_bias_t bias = s_bias;
            s_latest = sample;
            s_latest_bias = bias;
            s_has_sample = true;
            const imu_attitude_config_t config = s_attitude_config;
            const unsigned revision = s_attitude_revision;
            taskEXIT_CRITICAL(&s_sample_mux);

            if (revision != attitude_revision) {
                memset(attitude.integral, 0, sizeof(attitude.integral));
                attitude_revision = revision;
            }
            imu_stream_sample_t frame = {.raw = sample};
            convert_measurement(&sample, &bias, &frame.measurement);
            const bool attitude_gap = attitude.last_us && t_us - attitude.last_us > IMU_ATTITUDE_GAP_US;
            if ((status & 0x03) == 0x03) {
                frame.attitude_valid = imu_attitude_update(&attitude, &config, t_us,
                                                           frame.measurement.accel_mps2,
                                                           frame.measurement.gyro_dps,
                                                           &frame.attitude);
            }
            double reference_q[4];
            taskENTER_CRITICAL(&s_sample_mux);
            if (attitude_gap) s_attitude_reference.reference_seq = 0;
            if (frame.attitude_valid) {
                memcpy(s_attitude_reference.latest_q, attitude.q, sizeof(attitude.q));
                s_attitude_reference.latest_us = t_us;
                s_attitude_reference.latest_seq = sample.seq;
                frame.reference_seq = s_attitude_reference.reference_seq;
                memcpy(reference_q, s_attitude_reference.reference_q, sizeof(reference_q));
            }
            taskEXIT_CRITICAL(&s_sample_mux);
            if (frame.reference_seq) imu_attitude_relative(reference_q, attitude.q, &frame.relative);
            if (s_stream_queue != NULL) xQueueOverwrite(s_stream_queue, &frame);
            printf("IMU_RAW,%" PRIi64 ",%d,%d,%d,%d,%d,%d,%d,0x%02X\n",
                   t_us, ax, ay, az, gx, gy, gz, temp, (unsigned)status);
        } else {
            ESP_LOGW(TAG, "I2C read failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(IMU_PRINT_PERIOD_MS));
    }
}

esp_err_t imu_monitor_start(void)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err;

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = IMU_I2C_PORT,
        .sda_io_num = IMU_SDA_GPIO,
        .scl_io_num = IMU_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not allocate I2C bus: %s", esp_err_to_name(err));
        return err;
    }

    // Adafruit board default address is 0x6A; AD0 jumper can change it to 0x6B.
    uint8_t addr = 0;
    if (i2c_master_probe(s_bus, 0x6A, 100) == ESP_OK) {
        addr = 0x6A;
    } else if (i2c_master_probe(s_bus, 0x6B, 100) == ESP_OK) {
        addr = 0x6B;
    } else {
        ESP_LOGE(TAG, "No device at I2C 0x6A or 0x6B. Check VIN/GND/SDA/SCL.");
        err = ESP_ERR_NOT_FOUND;
        goto cleanup;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = IMU_I2C_SPEED_HZ,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        goto cleanup;
    }

    uint8_t who = 0;
    err = read_reg(LSM_WHO_AM_I, &who, 1);
    if (err != ESP_OK || who != LSM_WHO_AM_I_EXPECTED) {
        ESP_LOGE(TAG, "WHO_AM_I: read=%s value=0x%02X (expect 0x%02X)",
                 esp_err_to_name(err), who, LSM_WHO_AM_I_EXPECTED);
        if (err == ESP_OK) {
            err = ESP_ERR_INVALID_RESPONSE;
        }
        goto cleanup;
    }
    ESP_LOGI(TAG, "LSM6DS3TR-C detected (I2C=0x%02X, WHO_AM_I=0x%02X)", addr, who);

    // CTRL3_C: BDU=1 (block data update), IF_INC=1 (auto increment addresses).
    err = write_reg(LSM_CTRL3_C, 0x44);
    if (err != ESP_OK) goto cleanup;
    // CTRL1_XL: 104 Hz, full-scale +/- 2 g (raw acceleration counts).
    err = write_reg(LSM_CTRL1_XL, 0x40);
    if (err != ESP_OK) goto cleanup;
    // CTRL2_G: 104 Hz, full-scale +/- 250 dps (raw angular-rate counts).
    err = write_reg(LSM_CTRL2_G, 0x40);
    if (err != ESP_OK) goto cleanup;

    vTaskDelay(pdMS_TO_TICKS(20));
    if (xTaskCreate(imu_raw_print_task, "imu_raw", 4096, NULL, 5, NULL) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    s_started = true;
    ESP_LOGI(TAG, "IMU raw monitor started; 104 Hz sensor ODR, ~10 Hz console output");
    return ESP_OK;

cleanup:
    if (s_dev != NULL) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
    if (s_bus != NULL) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
    }
    return err;
}

bool imu_monitor_get_latest(imu_raw_sample_t *out)
{
    if (out == NULL) return false;
    bool ready;
    taskENTER_CRITICAL(&s_sample_mux);
    ready = s_has_sample;
    if (ready) *out = s_latest;
    taskEXIT_CRITICAL(&s_sample_mux);
    return ready;
}

void imu_monitor_format_latest(char *reply, size_t capacity)
{
    if (reply == NULL || capacity == 0) return;
    imu_raw_sample_t sample;
    if (!imu_monitor_get_latest(&sample)) {
        snprintf(reply, capacity, "ERR IMU_NOT_READY");
        return;
    }
    // Simple, versioned-by-command ASCII reply; fits 128 byte command buffer.
    snprintf(reply, capacity,
             "OK IMU_RAW %" PRIu32 " %" PRIi64 " %d %d %d %d %d %d %d 0x%02X",
             sample.seq, sample.t_us,
             sample.ax, sample.ay, sample.az,
             sample.gx, sample.gy, sample.gz,
             sample.temp, (unsigned)sample.status);
}

bool imu_monitor_get_measurement(imu_measurement_t *out)
{
    if (out == NULL) return false;
    taskENTER_CRITICAL(&s_sample_mux);
    const bool ready = s_has_sample;
    const imu_raw_sample_t raw = s_latest;
    const imu_bias_t bias = s_latest_bias;
    taskEXIT_CRITICAL(&s_sample_mux);
    if (ready) convert_measurement(&raw, &bias, out);
    return ready;
}

static void handle_bias_command(const char *command, char *reply, size_t capacity)
{
    const bool query = strcmp(command, "GET_IMU_BIAS") == 0;
    const bool reset = strcmp(command, "IMU_BIAS RESET") == 0;
    unsigned axis = 0;
    double expected_gravity = 0;
    if (!query && !reset) {
        const char *prefix = "IMU_BIAS START ";
        if (strncmp(command, prefix, strlen(prefix)) != 0) {
            snprintf(reply, capacity, "ERR BAD_ARGUMENT");
            return;
        }
        const char *direction = command + strlen(prefix);
        if (strlen(direction) != 2 || (direction[0] != '+' && direction[0] != '-')
            || direction[1] < 'X' || direction[1] > 'Z') {
            snprintf(reply, capacity, "ERR BAD_ARGUMENT");
            return;
        }
        axis = (unsigned)(direction[1] - 'X');
        expected_gravity = direction[0] == '+' ? IMU_GRAVITY_MPS2 : -IMU_GRAVITY_MPS2;
    }

    const int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&s_sample_mux);
    expire_calibration(now);
    const char *error = NULL;
    if (reset) {
        if (s_bias.applied) ++s_attitude_revision;
        s_bias = (imu_bias_t){0};
        s_calibration.deadline_us = 0;
        s_calibration.count = 0;
        s_calibration.state = "IDLE";
    } else if (!query) {
        if (!s_has_sample || now - s_latest.t_us > 500000) {
            error = "IMU_NOT_READY";
        } else if (!s_calibration.deadline_us) {
            memset(&s_calibration, 0, sizeof(s_calibration));
            s_calibration.deadline_us = now + IMU_BIAS_TIMEOUT_US;
            s_calibration.gravity_axis = axis;
            s_calibration.expected_gravity = expected_gravity;
            s_calibration.state = "RUNNING";
        }
    }
    const char *state = s_calibration.state;
    const unsigned count = s_calibration.count;
    const unsigned applied = s_bias.applied;
    taskEXIT_CRITICAL(&s_sample_mux);
    if (error) snprintf(reply, capacity, "ERR %s", error);
    else snprintf(reply, capacity, "OK IMU_BIAS %s %u %u", state, count, applied);
}

static bool parse_attitude_config(const char *args, imu_attitude_config_t *config)
{
    if (strncmp(args, "COMPLEMENTARY ", 14) == 0) {
        config->filter = IMU_FILTER_COMPLEMENTARY;
        args += 14;
    } else if (strncmp(args, "MAHONY ", 7) == 0) {
        config->filter = IMU_FILTER_MAHONY;
        args += 7;
    } else {
        return false;
    }

    unsigned *const values[] = {&config->tau_ms, &config->kp_milli, &config->ki_milli};
    const unsigned limits[] = {30000, 10000, 2000};
    for (unsigned i = 0; i < 3; ++i) {
        const size_t digits = strspn(args, "0123456789");
        if (!digits || digits > 5 || args[digits] != (i == 2 ? '\0' : ' ')) return false;
        const unsigned long value = strtoul(args, NULL, 10);
        if (value > limits[i]) return false;
        *values[i] = (unsigned)value;
        if (i < 2) args += digits + 1;
    }
    return config->tau_ms >= 50;
}

static void handle_reference_command(const char *command, char *reply, size_t capacity)
{
    const bool set = strcmp(command, "IMU_REF SET") == 0;
    if (!set && strcmp(command, "IMU_REF CLEAR") != 0) {
        snprintf(reply, capacity, "ERR BAD_ARGUMENT");
        return;
    }

    taskENTER_CRITICAL(&s_sample_mux);
    const bool ready = s_attitude_reference.latest_seq
        && esp_timer_get_time() - s_attitude_reference.latest_us <= IMU_ATTITUDE_GAP_US;
    if (set && ready) {
        memcpy(s_attitude_reference.reference_q, s_attitude_reference.latest_q,
               sizeof(s_attitude_reference.reference_q));
        s_attitude_reference.reference_seq = s_attitude_reference.latest_seq;
    } else if (!set) {
        s_attitude_reference.reference_seq = 0;
    }
    const uint32_t seq = s_attitude_reference.reference_seq;
    taskEXIT_CRITICAL(&s_sample_mux);
    if (set && !ready) snprintf(reply, capacity, "ERR ATTITUDE_NOT_READY");
    else snprintf(reply, capacity, "OK IMU_REF %" PRIu32, seq);
}

void imu_monitor_handle_control_command(const char *command, char *reply, size_t capacity)
{
    if (strncmp(command, "IMU_REF ", 8) == 0) {
        handle_reference_command(command, reply, capacity);
        return;
    }
    const bool set_attitude = strncmp(command, "SET_IMU_ATT ", 12) == 0;
    if (!set_attitude && strcmp(command, "GET_IMU_ATT") != 0) {
        handle_bias_command(command, reply, capacity);
        return;
    }

    imu_attitude_config_t config;
    if (set_attitude && !parse_attitude_config(command + 12, &config)) {
        snprintf(reply, capacity, "ERR BAD_ARGUMENT");
        return;
    }
    taskENTER_CRITICAL(&s_sample_mux);
    if (set_attitude && (config.filter != s_attitude_config.filter
        || config.tau_ms != s_attitude_config.tau_ms
        || config.kp_milli != s_attitude_config.kp_milli
        || config.ki_milli != s_attitude_config.ki_milli)) {
        s_attitude_config = config;
        ++s_attitude_revision;
    }
    config = s_attitude_config;
    taskEXIT_CRITICAL(&s_sample_mux);
    snprintf(reply, capacity, "OK IMU_ATT %s %u %u %u",
             config.filter == IMU_FILTER_MAHONY ? "MAHONY" : "COMPLEMENTARY",
             config.tau_ms, config.kp_milli, config.ki_milli);
}
