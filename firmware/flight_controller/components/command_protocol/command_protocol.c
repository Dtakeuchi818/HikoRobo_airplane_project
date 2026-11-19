#include "command_protocol.h"

#include <errno.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

// Test-only parameter: not connected to a sensor yet; resets on reboot.
static atomic_int s_rate_hz = ATOMIC_VAR_INIT(100);
static command_wifi_status_provider_t s_wifi_provider;

static command_imu_raw_provider_t s_imu_provider;
static command_imu_control_handler_t s_imu_control_handler;
static command_imu_control_handler_t s_sbus_handler;

void command_protocol_set_sbus_handler(command_imu_control_handler_t handler)
{
    s_sbus_handler = handler;
}

void command_protocol_set_imu_control_handler(command_imu_control_handler_t handler)
{
    s_imu_control_handler = handler;
}

void command_protocol_set_imu_raw_provider(command_imu_raw_provider_t provider)
{
    s_imu_provider = provider;
}


void command_protocol_set_wifi_status_provider(command_wifi_status_provider_t provider)
{
    s_wifi_provider = provider;
}

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
        snprintf(response, response_size, "OK STATUS READY RATE=%d", atomic_load(&s_rate_hz));
    } else if (strcmp(command, "GET_IMU_RAW") == 0) {
        if (s_imu_provider != NULL) {
            s_imu_provider(response, response_size);
        } else {
            snprintf(response, response_size, "ERR IMU_NOT_READY");
        }
    } else if (strcmp(command, "GET_IMU_BIAS") == 0 || strncmp(command, "IMU_BIAS ", 9) == 0
               || strcmp(command, "GET_IMU_ATT") == 0 || strncmp(command, "SET_IMU_ATT ", 12) == 0
               || strncmp(command, "IMU_REF ", 8) == 0) {
        if (s_imu_control_handler != NULL) {
            s_imu_control_handler(command, response, response_size);
        } else {
            snprintf(response, response_size, "ERR IMU_NOT_READY");
        }
    } else if (strncmp(command, "SBUS ", 5) == 0 && s_sbus_handler != NULL) {
        s_sbus_handler(command, response, response_size);
    } else if (strcmp(command, "GET_RATE") == 0) {
        snprintf(response, response_size, "OK RATE %d", atomic_load(&s_rate_hz));
    } else if (strcmp(command, "HELP") == 0) {
        snprintf(response, response_size, "OK COMMANDS PING GET_STATUS GET_RATE SET_RATE GET_WIFI GET_IMU_RAW GET_IMU_BIAS IMU_BIAS GET_IMU_ATT SET_IMU_ATT IMU_REF HELP");
    } else if (strcmp(command, "GET_WIFI") == 0) {
        if (s_wifi_provider != NULL) {
            s_wifi_provider(response, response_size);
        } else {
            snprintf(response, response_size, "ERR WIFI_UNAVAILABLE");
        }
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
        atomic_store(&s_rate_hz, (int)value);
        snprintf(response, response_size, "OK RATE %d", atomic_load(&s_rate_hz));
    } else {
        snprintf(response, response_size, "ERR UNKNOWN_COMMAND");
    }
}
