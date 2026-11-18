#pragma once

#include "esp_err.h"

// Initialize an LSM6DS3TR-C on XIAO ESP32S3 Plus (GPIO5 SDA / GPIO6 SCL).
// Starts a 10 Hz FreeRTOS task printing raw 16-bit samples to the IDF console.
// Call once from the existing app_main().
esp_err_t imu_monitor_start(void);

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// Snapshot of the most recently sampled LSM6DS3TR-C raw registers.
// The IMU task updates this; UDP commands only read the cached snapshot.
typedef struct {
    uint32_t seq;
    int64_t t_us;  // ESP32 monotonic timestamp, NOT IMU timestamp
    int16_t ax, ay, az;
    int16_t gx, gy, gz;
    int16_t temp;
    uint8_t status;
} imu_raw_sample_t;

bool imu_monitor_get_latest(imu_raw_sample_t *out);
void imu_monitor_format_latest(char *reply, size_t capacity);

// Calibrated sensor-frame measurement. Acceleration includes gravity.
typedef struct {
    uint32_t seq;
    int64_t t_us;
    double accel_mps2[3];  // X, Y, Z
    double gyro_dps[3];    // X, Y, Z
    bool bias_applied;
} imu_measurement_t;

// Copy the latest measurement and its acquisition timestamp from task context.
bool imu_monitor_get_measurement(imu_measurement_t *out);
void imu_monitor_handle_bias_command(const char *command, char *reply, size_t capacity);
