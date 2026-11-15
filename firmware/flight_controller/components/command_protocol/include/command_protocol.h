#pragma once

#include <stddef.h>

// Parses one ASCII command line and writes one NUL-terminated response.
// Response does not include the trailing LF.
void command_protocol_handle(const char *command, char *response, size_t response_size);
