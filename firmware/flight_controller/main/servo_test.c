#include "servo_test.h"
#include "servo_pwm.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

static void reply(const char *message)
{
    size_t remaining = strlen(message);
    while (remaining > 0) {
        int written = usb_serial_jtag_write_bytes(message, remaining, pdMS_TO_TICKS(200));
        if (written <= 0) {
            return;
        }
        message += written;
        remaining -= written;
    }
}

static void handle_command(const char *line)
{
    int servo;
    int pulse_us;
    char trailing;
    if (sscanf(line, "SET%*[ ]%1d%*[ ]%4d %c", &servo, &pulse_us, &trailing) != 2 ||
        servo < 0 || servo > SERVO_COUNT ||
        pulse_us < PULSE_MIN_US || pulse_us > PULSE_MAX_US) {
        reply("ERR Use SET <0=all, 1-4=servo> <1000-2000 us>\r\n");
        return;
    }

    int first = servo == 0 ? 0 : servo - 1;
    int end = servo == 0 ? SERVO_COUNT : servo;
    char response[96];
    for (int channel = first; channel < end; ++channel) {
        esp_err_t err = servo_pwm_set_pulse(channel, pulse_us);
        if (err != ESP_OK) {
            snprintf(response, sizeof(response), "ERR Servo %d: %s\r\n",
                     channel + 1, esp_err_to_name(err));
            reply(response);
            return;
        }
    }
    snprintf(response, sizeof(response), "OK SET %d %d\r\n", servo, pulse_us);
    reply(response);
}

void servo_test_run(void)
{
    usb_serial_jtag_driver_config_t usb = {
        .rx_buffer_size = 256,
        .tx_buffer_size = 256,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));

    ESP_ERROR_CHECK(servo_pwm_init());

    reply("Servo test ready at 1500 us. Use SET <0=all, 1-4=servo> <1000-2000 us>\r\n");
    char line[32];
    size_t length = 0;
    bool invalid = false;
    for (;;) {
        uint8_t byte;
        if (usb_serial_jtag_read_bytes(&byte, 1, pdMS_TO_TICKS(100)) != 1) {
            continue;
        }
        if (byte == '\r' || byte == '\n') {
            if (invalid) {
                reply("ERR Invalid command line\r\n");
            } else if (length > 0) {
                line[length] = '\0';
                handle_command(line);
            }
            length = 0;
            invalid = false;
        } else if (byte == '\b' || byte == 0x7f) {
            if (!invalid && length > 0) {
                --length;
            }
        } else if (byte < ' ' || byte > '~' || length == sizeof(line) - 1) {
            invalid = true;
        } else if (!invalid) {
            line[length++] = (char)byte;
        }
    }
}
