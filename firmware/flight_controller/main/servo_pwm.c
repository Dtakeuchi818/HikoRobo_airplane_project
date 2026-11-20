#include "servo_pwm.h"

#include <stdint.h>
#include "driver/ledc.h"

enum {
    PWM_HZ = 50,
    PWM_BITS = 14,
    PWM_PERIOD_US = 1000000 / PWM_HZ,
    SERVO_TRAVEL_CENTIDEG = 9000,
};

static const int servo_gpio[SERVO_COUNT] = {1, 2, 4, 7}; // D0, D1, D3, D8

static uint32_t pulse_to_duty(int pulse_us)
{
    return (pulse_us * (1u << PWM_BITS) + PWM_PERIOD_US / 2) / PWM_PERIOD_US;
}

static int last_pulse[SERVO_COUNT];

esp_err_t servo_pwm_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_14_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) return err;
    for (int channel = 0; channel < SERVO_COUNT; ++channel) {
        const ledc_channel_config_t output = {
            .gpio_num = servo_gpio[channel],
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = (ledc_channel_t)channel,
            .timer_sel = LEDC_TIMER_0,
            .duty = pulse_to_duty(PULSE_CENTER_US),
        };
        err = ledc_channel_config(&output);
        if (err != ESP_OK) return err;
    }
    for (int channel = 0; channel < SERVO_COUNT; ++channel) {
        last_pulse[channel] = PULSE_CENTER_US;
    }
    return ESP_OK;
}

esp_err_t servo_pwm_set_pulse(int channel, int pulse_us)
{
    if (channel < 0 || channel >= SERVO_COUNT ||
        pulse_us < PULSE_MIN_US || pulse_us > PULSE_MAX_US) {
        return ESP_ERR_INVALID_ARG;
    }
    if (pulse_us == last_pulse[channel]) return ESP_OK;

    esp_err_t err = ledc_set_duty(
        LEDC_LOW_SPEED_MODE, (ledc_channel_t)channel, pulse_to_duty(pulse_us));
    if (err == ESP_OK) {
        err = ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)channel);
    }
    if (err == ESP_OK) {
        last_pulse[channel] = pulse_us;
    }
    return err;
}

int servo_pwm_get_pulse(int channel)
{
    return last_pulse[channel];
}

int servo_pwm_angle_to_pulse(int angle_centideg)
{
    if (angle_centideg <= -SERVO_TRAVEL_CENTIDEG / 2) return PULSE_MIN_US;
    if (angle_centideg >= SERVO_TRAVEL_CENTIDEG / 2) return PULSE_MAX_US;

    // Round to the nearest microsecond, symmetrically around neutral.
    const int offset = angle_centideg * (PULSE_MAX_US - PULSE_MIN_US);
    const int half = SERVO_TRAVEL_CENTIDEG / 2;
    const int rounded = offset < 0 ? offset - half : offset + half;
    return PULSE_CENTER_US + rounded / SERVO_TRAVEL_CENTIDEG;
}
