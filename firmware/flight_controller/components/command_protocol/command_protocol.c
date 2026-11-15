#include "command_protocol.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test-only parameter: not connected to a sensor yet; resets on reboot.
static int s_rate_hz = 100;

void command_protocol_handle(const char *command, char *response, size_t response_size)
{
    if (response == NULL || response_size == 0) {
        return;
    }
    response[0] = '\0';

    if (command == NULL) {
        snprintf(response, response_size, "ERR BAD_COMMAND");
        return;
    }
    if (strcmp(command, "PING") == 0) {
        snprintf(response, response_size, "OK PONG");
    } else if (strcmp(command, "GET_STATUS") == 0) {
        snprintf(response, response_size, "OK STATUS READY RATE=%d", s_rate_hz);
    } else if (strcmp(command, "GET_RATE") == 0) {
        snprintf(response, response_size, "OK RATE %d", s_rate_hz);
    } else if (strcmp(command, "HELP") == 0) {
        snprintf(response, response_size, "OK COMMANDS PING GET_STATUS GET_RATE SET_RATE HELP");
    } else if (strncmp(command, "SET_RATE", 8) == 0) {
        if (command[8] != ' ') {
            snprintf(response, response_size, "ERR BAD_ARGUMENT");
            return;
        }
        const char *number = command + 9;
        char *end = NULL;
        errno = 0;
        long value = strtol(number, &end, 10);
        if (end == number || errno == ERANGE) {
            snprintf(response, response_size, "ERR BAD_ARGUMENT");
            return;
        }
        while (*end == ' ') {
            ++end;
        }
        if (*end != '\0') {
            snprintf(response, response_size, "ERR BAD_ARGUMENT");
            return;
        }
        if (value < 1 || value > 500) {
            snprintf(response, response_size, "ERR RATE_RANGE");
            return;
        }
        s_rate_hz = (int)value;
        snprintf(response, response_size, "OK RATE %d", s_rate_hz);
    } else {
        snprintf(response, response_size, "ERR UNKNOWN_COMMAND");
    }
}
