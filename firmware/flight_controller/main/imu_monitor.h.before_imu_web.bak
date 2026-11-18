#pragma once

#include "esp_err.h"

// Initialize an LSM6DS3TR-C on XIAO ESP32S3 Plus (GPIO5 SDA / GPIO6 SCL).
// Starts a 10 Hz FreeRTOS task printing raw 16-bit samples to the IDF console.
// Call once from the existing app_main().
esp_err_t imu_monitor_start(void);
