#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SBUS_CONTROL_CHANNELS 8

typedef struct {
    int mode; // 0: inactive, 1: surface, 2: attitude
    unsigned active_mask;
    int normalized[SBUS_CONTROL_CHANNELS]; // thousandths
    int angles[SBUS_CONTROL_CHANNELS]; // centidegrees
} sbus_command_t;

void sbus_transform_load(void);
void sbus_transform_handle(const char *command, char *reply, size_t capacity);
sbus_command_t sbus_transform_sample(const uint16_t *raw, bool usable, int64_t now_us);
