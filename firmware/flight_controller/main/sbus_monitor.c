#include "sbus_monitor.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define SBUS_UART UART_NUM_1
#define SBUS_RX_GPIO 44
#define SBUS_FRAME_SIZE 25
#define SBUS_CHANNELS 16
#define SBUS_REPORT_US 200000

static void report(const uint8_t *frame)
{
    char line[256];
    int length;
    if (frame == NULL) {
        length = snprintf(line, sizeof(line), "SBUS waiting for valid frames on D7 (GPIO44)\r\n");
    } else {
        size_t used = 0;
        for (unsigned channel = 0; channel < SBUS_CHANNELS; ++channel) {
            unsigned bit = channel * 11;
            unsigned byte = 1 + bit / 8;
            uint32_t packed = frame[byte] | ((uint32_t)frame[byte + 1] << 8);
            if (byte + 2 < 23) {
                packed |= (uint32_t)frame[byte + 2] << 16;
            }
            unsigned value = (packed >> (bit % 8)) & 0x7ff;
            int count = snprintf(line + used, sizeof(line) - used,
                                 "CH%02u=%4u ", channel + 1, value);
            if (count < 0 || (size_t)count >= sizeof(line) - used) {
                return;
            }
            used += (size_t)count;
        }
        length = (int)used + snprintf(line + used, sizeof(line) - used,
                                      "lost=%d failsafe=%d\r\n",
                                      (frame[23] & 0x04) != 0, (frame[23] & 0x08) != 0);
    }
    if (length > 0 && (size_t)length < sizeof(line)) {
        usb_serial_jtag_write_bytes(line, length, pdMS_TO_TICKS(20));
    }
}

void sbus_monitor_run(void)
{
    usb_serial_jtag_driver_config_t usb_config = {
        .rx_buffer_size = 512,
        .tx_buffer_size = 512,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_config));

    const uart_config_t uart_config = {
        .baud_rate = 100000,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_EVEN,
        .stop_bits = UART_STOP_BITS_2,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(SBUS_UART, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(SBUS_UART, UART_PIN_NO_CHANGE, SBUS_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_set_line_inverse(SBUS_UART, UART_SIGNAL_RXD_INV));
    ESP_ERROR_CHECK(uart_driver_install(SBUS_UART, 1024, 0, 0, NULL, 0));

    uint8_t frame[SBUS_FRAME_SIZE];
    uint8_t latest[SBUS_FRAME_SIZE];
    size_t used = 0;
    bool received_frame = false;
    int64_t last_report = esp_timer_get_time();
    for (;;) {
        uint8_t bytes[64];
        int count = uart_read_bytes(SBUS_UART, bytes, sizeof(bytes), pdMS_TO_TICKS(20));
        for (int i = 0; i < count; ++i) {
            if (used == 0 && bytes[i] != 0x0f) {
                continue;
            }
            frame[used++] = bytes[i];
            if (used != sizeof(frame)) {
                continue;
            }
            if (frame[24] == 0x00 && (frame[23] & 0xf0) == 0) {
                memcpy(latest, frame, sizeof(latest));
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
        int64_t now = esp_timer_get_time();
        if (now - last_report >= SBUS_REPORT_US) {
            report(received_frame ? latest : NULL);
            received_frame = false;
            last_report = now;
        }
    }
}
