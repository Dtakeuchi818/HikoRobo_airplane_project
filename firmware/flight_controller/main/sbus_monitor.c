#include "sbus_monitor.h"
#include "sbus_transform.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"
#include "servo_pwm.h"
#include "telemetry.h"
#include "wifi_manager.h"
#include "freertos/FreeRTOS.h"

#define SBUS_UART UART_NUM_1
#define SBUS_RX_GPIO 44
#define SBUS_FRAME_SIZE 25
#define SBUS_CHANNELS 16
#define SBUS_SERVO_TIMEOUT_US 100000

static const char *TAG = "SBUS";

typedef struct {
    uint16_t channel[SBUS_CHANNELS];
    bool lost;
    bool failsafe;
} sbus_sample_t;

static void decode(const uint8_t *frame, sbus_sample_t *sample)
{
    for (unsigned channel = 0; channel < SBUS_CHANNELS; ++channel) {
        unsigned bit = channel * 11;
        unsigned byte = 1 + bit / 8;
        uint32_t packed = frame[byte] | ((uint32_t)frame[byte + 1] << 8);
        if (byte + 2 < 23) {
            packed |= (uint32_t)frame[byte + 2] << 16;
        }
        sample->channel[channel] = (packed >> (bit % 8)) & 0x7ff;
    }
    sample->lost = (frame[23] & 0x04) != 0;
    sample->failsafe = (frame[23] & 0x08) != 0;
}

static void report_usb(const sbus_sample_t *sample, const sbus_command_t *command)
{
    (void)command;
    char line[256];
    int length;
    if (sample == NULL) {
        length = snprintf(line, sizeof(line), "SBUS waiting for valid frames on D7 (GPIO44)\r\n");
    } else {
        size_t used = 0;
        for (unsigned channel = 0; channel < SBUS_CHANNELS; ++channel) {
            int count = snprintf(line + used, sizeof(line) - used,
                                 "CH%02u=%4u ", channel + 1, (unsigned)sample->channel[channel]);
            if (count < 0 || (size_t)count >= sizeof(line) - used) {
                return;
            }
            used += (size_t)count;
        }
        length = (int)used + snprintf(line + used, sizeof(line) - used,
                                      "lost=%d failsafe=%d\r\n",
                                      sample->lost, sample->failsafe);
    }
    if (length > 0 && (size_t)length < sizeof(line)) {
        usb_serial_jtag_write_bytes(line, length, pdMS_TO_TICKS(20));
    }
}

static esp_err_t init_uart(void)
{
    const uart_config_t uart_config = {
        .baud_rate = 100000,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_EVEN,
        .stop_bits = UART_STOP_BITS_2,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_param_config(SBUS_UART, &uart_config);
    if (err != ESP_OK) return err;
    err = uart_set_pin(SBUS_UART, UART_PIN_NO_CHANGE, SBUS_RX_GPIO,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return err;
    err = uart_set_line_inverse(SBUS_UART, UART_SIGNAL_RXD_INV);
    if (err != ESP_OK) return err;
    return uart_driver_install(SBUS_UART, 1024, 0, 0, NULL, 0);
}

static void apply_servo_command(const sbus_command_t *command)
{
    // Zero-based indices for S.BUS CH1, CH2, CH4 and CH7.
    static const unsigned input[SERVO_COUNT] = {0, 1, 3, 6};
    for (int servo = 0; servo < SERVO_COUNT; ++servo) {
        unsigned channel = input[servo];
        int angle = 0;
        if (command->mode == 1 && (command->active_mask & (1u << channel))) {
            angle = command->angles[channel];
        }
        esp_err_t err = servo_pwm_set_pulse(servo, servo_pwm_angle_to_pulse(angle));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Servo %d PWM update failed: %s", servo + 1, esp_err_to_name(err));
        }
    }
}

static void receive_frames(int64_t period_us, bool control_servos,
                           void (*report)(const sbus_sample_t *, const sbus_command_t *))
{
    uint8_t frame[SBUS_FRAME_SIZE];
    sbus_sample_t latest;
    sbus_command_t command = {0};
    size_t used = 0;
    bool received_frame = false;
    int64_t last_report = esp_timer_get_time();
    int64_t last_frame = -1;
    for (;;) {
        uint8_t bytes[64];
        int count = uart_read_bytes(SBUS_UART, bytes, sizeof(bytes), pdMS_TO_TICKS(20));
        int64_t now = esp_timer_get_time();
        if (control_servos && last_frame >= 0 && now - last_frame >= SBUS_SERVO_TIMEOUT_US) {
            command = sbus_transform_sample(NULL, false, now);
            apply_servo_command(&command);
            last_frame = -1;
        }
        for (int i = 0; i < count; ++i) {
            if (used == 0 && bytes[i] != 0x0f) {
                continue;
            }
            frame[used++] = bytes[i];
            if (used != sizeof(frame)) {
                continue;
            }
            if (frame[24] == 0x00 && (frame[23] & 0xf0) == 0) {
                decode(frame, &latest);
                if (control_servos) {
                    last_frame = esp_timer_get_time();
                    command = sbus_transform_sample(latest.channel, !latest.lost && !latest.failsafe,
                                                    last_frame);
                    apply_servo_command(&command);
                }
                received_frame = true;
                used = 0;
            } else {
                // Resume at the next possible header inside the rejected frame.
                const uint8_t *header = memchr(frame + 1, 0x0f, sizeof(frame) - 1);
                used = header ? sizeof(frame) - (size_t)(header - frame) : 0;
                if (header) {
                    memmove(frame, header, used);
                }
            }
        }
        now = esp_timer_get_time();
        if (now - last_report >= period_us) {
            if (report != NULL) {
                report(received_frame ? &latest : NULL, &command);
            }
            received_frame = false;
            last_report = now;
        }
    }
}

void sbus_monitor_run(void)
{
    usb_serial_jtag_driver_config_t usb_config = {
        .rx_buffer_size = 512,
        .tx_buffer_size = 512,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_config));
    ESP_ERROR_CHECK(init_uart());
    receive_frames(200000, false, report_usb);
}

static struct sockaddr_in s_target;
static bool s_report_enabled;

static void report_udp(const sbus_sample_t *sample, const sbus_command_t *command)
{
    static int fd = -1;
    static uint32_t epoch;
    const bool connected = wifi_manager_connected();
    const uint32_t current_epoch = wifi_manager_epoch();
    if (fd >= 0 && (!connected || current_epoch != epoch)) {
        close(fd);
        fd = -1;
    }
    if (!connected) return;
    if (fd < 0) {
        fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (fd < 0) return;
        epoch = current_epoch;
    }

    const bool has_frame = sample != NULL;
    const sbus_sample_t empty_sample = {0};
    const sbus_command_t empty_command = {0};
    if (!has_frame) {
        sample = &empty_sample;
        command = &empty_command;
    }
    const uint16_t *ch = sample->channel;
    char packet[256];
    int size = snprintf(packet, sizeof(packet), "HK4 SBUS %d %d %u %u %u %u %u %u %u %u",
                        sample->lost, sample->failsafe, (unsigned)ch[0], (unsigned)ch[1],
                        (unsigned)ch[2], (unsigned)ch[3], (unsigned)ch[4], (unsigned)ch[5],
                        (unsigned)ch[6], (unsigned)ch[7]);
    if (size <= 0 || (size_t)size >= sizeof(packet)) return;
    size += snprintf(packet + size, sizeof(packet) - (size_t)size, " %d %u",
                     command->mode, command->active_mask);
    for (int i = 0; i < SBUS_CONTROL_CHANNELS; ++i) {
        if ((size_t)size >= sizeof(packet)) return;
        size += snprintf(packet + size, sizeof(packet) - (size_t)size, " %d %d",
                         command->normalized[i], command->angles[i]);
    }
    if (size <= 0 || (size_t)size >= sizeof(packet)) return;
    size += snprintf(packet + size, sizeof(packet) - (size_t)size, " %u %u %u",
                     sbus_attitude_channels[0], sbus_attitude_channels[1], sbus_attitude_channels[2]);
    if (size <= 0 || (size_t)size >= sizeof(packet)) return;
    size += snprintf(packet + size, sizeof(packet) - (size_t)size, " %d", has_frame);
    for (int servo = 0; servo < SERVO_COUNT; ++servo) {
        if ((size_t)size >= sizeof(packet)) return;
        size += snprintf(packet + size, sizeof(packet) - (size_t)size,
                         " %d", servo_pwm_get_pulse(servo));
    }
    if (size > 0 && (size_t)size < sizeof(packet)) {
        sendto(fd, packet, (size_t)size, MSG_DONTWAIT,
               (const struct sockaddr *)&s_target, sizeof(s_target));
    }
}

static void sbus_stream_task(void *arg)
{
    (void)arg;
    receive_frames(50000, true, s_report_enabled ? report_udp : NULL);
}

esp_err_t sbus_stream_start(void)
{
    s_target.sin_family = AF_INET;
    s_target.sin_port = htons(HIKOROBO_TELEMETRY_PORT);
    if (CONFIG_HIKOROBO_IMU_STREAM_HOST[0] != '\0') {
        s_report_enabled = inet_pton(AF_INET, CONFIG_HIKOROBO_IMU_STREAM_HOST, &s_target.sin_addr) == 1;
        if (!s_report_enabled) {
            ESP_LOGW(TAG, "Invalid S.BUS telemetry destination; telemetry disabled");
        }
    }
    esp_err_t err = servo_pwm_init();
    if (err != ESP_OK) return err;
    err = init_uart();
    if (err != ESP_OK) return err;
    if (xTaskCreate(sbus_stream_task, "sbus_stream", 4096, NULL, 4, NULL) != pdPASS) {
        uart_driver_delete(SBUS_UART);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
