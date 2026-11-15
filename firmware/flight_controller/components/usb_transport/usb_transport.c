#include "usb_transport.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "command_protocol.h"
#include "driver/usb_serial_jtag.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define COMMAND_LENGTH 96
#define RESPONSE_LENGTH 128

static void send_response(const char *response)
{
    char text[RESPONSE_LENGTH + 2];
    int n = snprintf(text, sizeof(text), "%s\n", response);
    if (n <= 0 || (size_t)n >= sizeof(text)) {
        return;
    }
    size_t written = 0;
    while (written < (size_t)n) {
        int count = usb_serial_jtag_write_bytes(
            text + written, (size_t)n - written, pdMS_TO_TICKS(200));
        if (count <= 0) {
            return; // USB may be disconnected; the next request can retry.
        }
        written += (size_t)count;
    }
}

void usb_transport_run(void)
{
    usb_serial_jtag_driver_config_t config = {
        .rx_buffer_size = 512,
        .tx_buffer_size = 512,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));

    char line[COMMAND_LENGTH];
    size_t pos = 0;
    bool too_long = false;
    bool invalid_character = false;
    uint8_t received[64];

    for (;;) {
        int count = usb_serial_jtag_read_bytes(
            received, sizeof(received), pdMS_TO_TICKS(100));
        for (int i = 0; i < count; ++i) {
            uint8_t ch = received[i];
            if (ch == '\r') {
                continue;
            }
            if (ch == '\n') {
                if (too_long) {
                    send_response("ERR LINE_TOO_LONG");
                } else if (invalid_character) {
                    send_response("ERR BAD_CHARACTER");
                } else if (pos > 0) {
                    line[pos] = '\0';
                    char response[RESPONSE_LENGTH];
                    command_protocol_handle(line, response, sizeof(response));
                    send_response(response);
                }
                pos = 0;
                too_long = false;
                invalid_character = false;
                continue;
            }
            if (ch < 32 || ch > 126) {
                invalid_character = true;
            } else if (!too_long && pos < sizeof(line) - 1) {
                line[pos++] = (char)ch;
            } else {
                too_long = true;
            }
        }
    }
}
