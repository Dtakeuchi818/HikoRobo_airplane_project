#include "imu_attitude.h"

#include <math.h>

static const double radians_per_degree = 0.017453292519943295;
static const double gravity_mps2 = 9.80665;

static double clamp(double value, double low, double high)
{
    return fmin(high, fmax(low, value));
}

static double norm3(const double v[3])
{
    return sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}

// Compose a sensor-frame rotation with the current attitude.
static void rotate(double q[4], const double axis[3], double angle)
{
    const double sine = sin(angle / 2);
    const double w = cos(angle / 2), x = axis[0]*sine, y = axis[1]*sine, z = axis[2]*sine;
    const double a = q[0], b = q[1], c = q[2], d = q[3];
    q[0] = a*w - b*x - c*y - d*z;
    q[1] = a*x + b*w + c*z - d*y;
    q[2] = a*y - b*z + c*w + d*x;
    q[3] = a*z + b*y - c*x + d*w;
    const double length = sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
    for (unsigned i = 0; i < 4; ++i) q[i] /= length;
}

static double gravity_error(const double q[4], const double accel[3], double error[3])
{
    const double w = q[0], x = q[1], y = q[2], z = q[3];
    const double up[] = {2*(x*z - w*y), 2*(w*x + y*z), 1 - 2*(x*x + y*y)};
    error[0] = accel[1]*up[2] - accel[2]*up[1];
    error[1] = accel[2]*up[0] - accel[0]*up[2];
    error[2] = accel[0]*up[1] - accel[1]*up[0];
    return accel[0]*up[0] + accel[1]*up[1] + accel[2]*up[2];
}

static void init_attitude(imu_attitude_t *state, const double accel[3])
{
    const double half_roll = atan2(accel[1], accel[2]) / 2;
    const double half_pitch = atan2(-accel[0], hypot(accel[1], accel[2])) / 2;
    const double cr = cos(half_roll), sr = sin(half_roll);
    const double cp = cos(half_pitch), sp = sin(half_pitch);
    state->q[0] = cr*cp;
    state->q[1] = sr*cp;
    state->q[2] = cr*sp;
    state->q[3] = -sr*sp;
    state->initialized = true;
    for (unsigned i = 0; i < 3; ++i) state->integral[i] = 0;
}

static void integrate_gyro(double q[4], double rate[3], double dt)
{
    const double magnitude = norm3(rate);
    if (magnitude > 0) {
        for (unsigned i = 0; i < 3; ++i) rate[i] /= magnitude;
        rotate(q, rate, magnitude * dt);
    }
}

static void complementary_update(double q[4], const double accel[3], double tau, double dt)
{
    double error[3];
    const double alignment = gravity_error(q, accel, error);
    const double magnitude = norm3(error);
    if (magnitude > 1e-12) {
        for (unsigned i = 0; i < 3; ++i) error[i] /= magnitude;
        const double angle = atan2(magnitude, alignment);
        rotate(q, error, angle * dt / (tau + dt));
    }
}

bool imu_attitude_update(imu_attitude_t *state, const imu_attitude_config_t *config,
                         int64_t t_us, const double accel_mps2[3], const double gyro_dps[3],
                         imu_euler_t *out)
{
    const double length = norm3(accel_mps2);
    const bool use_accel = length >= 0.8*gravity_mps2 && length <= 1.2*gravity_mps2;
    const int64_t elapsed = t_us - state->last_us;
    if (state->initialized && elapsed <= 0) return false;
    if (!state->initialized || elapsed > 500000) {
        state->initialized = false;
        if (!use_accel) return false;
        init_attitude(state, accel_mps2);
    } else {
        const double dt = elapsed / 1000000.0;
        double rate[] = {gyro_dps[0]*radians_per_degree, gyro_dps[1]*radians_per_degree,
                         gyro_dps[2]*radians_per_degree};
        const double accel[] = {use_accel ? accel_mps2[0]/length : 0,
                                use_accel ? accel_mps2[1]/length : 0,
                                use_accel ? accel_mps2[2]/length : 0};
        if (config->filter == IMU_FILTER_MAHONY) {
            double error[3] = {0};
            if (use_accel) gravity_error(state->q, accel, error);
            const double kp = config->kp_milli / 1000.0;
            const double ki = config->ki_milli / 1000.0;
            for (unsigned i = 0; i < 3; ++i) {
                state->integral[i] = ki > 0
                    ? clamp(state->integral[i] + ki*error[i]*dt, -0.5, 0.5) : 0;
                rate[i] += kp*error[i] + state->integral[i];
            }
            integrate_gyro(state->q, rate, dt);
        } else {
            integrate_gyro(state->q, rate, dt);
            if (use_accel) complementary_update(state->q, accel, config->tau_ms / 1000.0, dt);
        }
    }
    state->last_us = t_us;
    const double w = state->q[0], x = state->q[1], y = state->q[2], z = state->q[3];
    out->roll_deg = atan2(2*(w*x + y*z), 1 - 2*(x*x + y*y)) / radians_per_degree;
    out->pitch_deg = asin(clamp(2*(w*y - z*x), -1, 1)) / radians_per_degree;
    out->yaw_deg = atan2(2*(w*z + x*y), 1 - 2*(y*y + z*z)) / radians_per_degree;
    return true;
}
