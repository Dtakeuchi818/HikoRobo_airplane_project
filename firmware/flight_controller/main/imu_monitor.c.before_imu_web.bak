#include "imu_monitor.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
    // CTRL2_G: 104 Hz, full-scale +/- 245 dps (raw angular-rate counts).
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