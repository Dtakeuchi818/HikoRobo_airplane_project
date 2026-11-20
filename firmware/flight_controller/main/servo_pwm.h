#pragma once

#include "esp_err.h"

enum {
    SERVO_COUNT = 4,
    PULSE_MIN_US = 1000,
    PULSE_CENTER_US = 1500,
    PULSE_MAX_US = 2000,
};

esp_err_t servo_pwm_init(void);
esp_err_t servo_pwm_set_pulse(int channel, int pulse_us);
int servo_pwm_get_pulse(int channel);
int servo_pwm_angle_to_pulse(int angle_centideg);
