#pragma once

#include <stddef.h>

// Protocol core does not depend on Wi-Fi or USB drivers.
// A Wi-Fi status callback may be registered once, before starting tasks.
typedef void (*command_wifi_status_provider_t)(char *response, size_t capacity);
void command_protocol_set_wifi_status_provider(command_wifi_status_provider_t provider);

// Parses one ASCII command; response does not contain trailing LF.
void command_protocol_handle(const char *command, char *response, size_t response_size);

// Provider is registered from app_main() to avoid a component dependency cycle.
typedef void (*command_imu_raw_provider_t)(char *reply, size_t capacity);
void command_protocol_set_imu_raw_provider(command_imu_raw_provider_t provider);

typedef void (*command_imu_bias_handler_t)(const char *command, char *reply, size_t capacity);
void command_protocol_set_imu_bias_handler(command_imu_bias_handler_t handler);
