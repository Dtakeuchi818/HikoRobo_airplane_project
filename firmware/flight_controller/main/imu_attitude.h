#pragma once

#include <stdbool.h>
#include <stdint.h>

#define IMU_ATTITUDE_GAP_US 500000

typedef enum {
    IMU_FILTER_COMPLEMENTARY,
    IMU_FILTER_MAHONY,
} imu_filter_t;

typedef struct {
    imu_filter_t filter;
    unsigned tau_ms;
    unsigned kp_milli;
    unsigned ki_milli;
} imu_attitude_config_t;

typedef struct {
    double q[4];         // Unit quaternion, sensor frame to world frame (w, x, y, z).
    double integral[3];  // Mahony integral feedback in rad/s, in the sensor frame.
    int64_t last_us;
    bool initialized;
} imu_attitude_t;

typedef struct {
    double roll_deg;
    double pitch_deg;
    double yaw_deg;
} imu_euler_t;

// Update with a fresh sample. Acceleration includes gravity. World Z points up.
bool imu_attitude_update(imu_attitude_t *state, const imu_attitude_config_t *config,
                         int64_t t_us, const double accel_mps2[3], const double gyro_dps[3],
                         imu_euler_t *out);

// Relative rotation from the saved sensor axes to the current sensor axes.
void imu_attitude_relative(const double reference[4], const double current[4], imu_euler_t *out);
