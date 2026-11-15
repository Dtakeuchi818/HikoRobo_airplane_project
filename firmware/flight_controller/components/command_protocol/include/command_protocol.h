#pragma once

#include <stddef.h>

// Protocol core does not depend on Wi-Fi or USB drivers.
// A Wi-Fi status callback may be registered once, before starting tasks.
typedef void (*command_wifi_status_provider_t)(char *response, size_t capacity);
void command_protocol_set_wifi_status_provider(command_wifi_status_provider_t provider);

// Parses one ASCII command; response does not contain trailing LF.
void command_protocol_handle(const char *command, char *response, size_t response_size);
