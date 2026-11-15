#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"

// Start the Wi-Fi station with credentials from menuconfig (non-blocking).
void wifi_manager_start(void);

// Thread-safe status/synchronization for the UDP task.
bool wifi_manager_wait_connected(TickType_t timeout);
bool wifi_manager_connected(void);
uint32_t wifi_manager_epoch(void);

// Human-readable reply for GET_WIFI (also reachable over USB).
void wifi_manager_format_status(char *response, size_t capacity);
