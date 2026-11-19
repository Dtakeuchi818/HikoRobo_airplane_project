#include "sbus_transform.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "nvs.h"

typedef struct {
    int min, center, max;
    int surface_min, surface_max;
    int attitude_min, attitude_max;
} channel_config_t;

typedef struct {
    int channel;
    int surface_min, surface_max;
    int attitude_min, attitude_max;
} mode_config_t;

typedef struct {
    mode_config_t mode;
    channel_config_t channel[SBUS_CONTROL_CHANNELS];
} settings_t;

typedef struct {
    settings_t build_defaults;
    settings_t settings;
} saved_settings_t;

#define MODE_DWELL_US 100000

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static const settings_t s_defaults =
#include "sbus_defaults.inc"
;
static settings_t s_settings;
static unsigned s_mode_revision;

static bool valid_channel(channel_config_t c)
{
    return c.min >= 0 && c.min < c.center && c.center < c.max && c.max <= 2047
        && c.surface_min >= -18000 && c.surface_min <= 0 && c.surface_max >= 0 && c.surface_max <= 18000
        && c.attitude_min >= -18000 && c.attitude_min <= 0 && c.attitude_max >= 0 && c.attitude_max <= 18000;
}

static bool valid_mode(mode_config_t m)
{
    return m.channel >= 0 && m.channel <= SBUS_CONTROL_CHANNELS
        && m.surface_min >= 0 && m.surface_min <= m.surface_max && m.surface_max <= 2047
        && m.attitude_min >= 0 && m.attitude_min <= m.attitude_max && m.attitude_max <= 2047
        && (m.surface_max < m.attitude_min || m.attitude_max < m.surface_min);
}

void sbus_transform_load(void)
{
    s_settings = s_defaults;
    nvs_handle_t handle;
    if (nvs_open("sbus", NVS_READONLY, &handle) != ESP_OK) return;
    saved_settings_t saved;
    size_t size = sizeof(saved);
    esp_err_t err = nvs_get_blob(handle, "config_v2", &saved, &size);
    nvs_close(handle);
    if (err != ESP_OK || size != sizeof(saved)
        || memcmp(&saved.build_defaults, &s_defaults, sizeof(s_defaults)) != 0
        || !valid_mode(saved.settings.mode)) return;
    for (int i = 0; i < SBUS_CONTROL_CHANNELS; ++i) {
        if (!valid_channel(saved.settings.channel[i])) return;
    }
    s_settings = saved.settings; // Called before starting command or sampling tasks.
}

static bool parse_ints(const char *text, int *values, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (*text++ != ' ') return false;
        char *end;
        errno = 0;
        long value = strtol(text, &end, 10);
        if (end == text || errno || value < -18000 || value > 18000) return false;
        values[i] = (int)value;
        text = end;
    }
    return *text == '\0';
}

void sbus_transform_handle(const char *command, char *reply, size_t capacity)
{
    int v[8];
    if (strcmp(command, "SBUS APPLY_DEFAULTS") == 0) {
        portENTER_CRITICAL(&s_lock);
        s_settings = s_defaults;
        ++s_mode_revision;
        portEXIT_CRITICAL(&s_lock);
        snprintf(reply, capacity, "OK SBUS APPLY_DEFAULTS");
        return;
    }
    if (strcmp(command, "SBUS SAVE") == 0) {
        saved_settings_t snapshot = {.build_defaults = s_defaults};
        portENTER_CRITICAL(&s_lock);
        snapshot.settings = s_settings;
        portEXIT_CRITICAL(&s_lock);
        nvs_handle_t handle;
        esp_err_t err = nvs_open("sbus", NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            err = nvs_set_blob(handle, "config_v2", &snapshot, sizeof(snapshot));
            if (err == ESP_OK) err = nvs_commit(handle);
            nvs_close(handle);
        }
        snprintf(reply, capacity, "%s", err == ESP_OK ? "OK SBUS SAVE" : "ERR SBUS_SAVE");
        return;
    }

    bool set_mode = strncmp(command, "SBUS SET_MODE ", 14) == 0;
    if (set_mode || strcmp(command, "SBUS GET_MODE") == 0) {
        if (set_mode) {
            if (!parse_ints(command + 13, v, 5)) goto bad_argument;
            mode_config_t mode = {v[0], v[1], v[2], v[3], v[4]};
            if (!valid_mode(mode)) goto bad_argument;
            portENTER_CRITICAL(&s_lock);
            if (memcmp(&s_settings.mode, &mode, sizeof(mode)) != 0) {
                s_settings.mode = mode;
                ++s_mode_revision;
            }
            portEXIT_CRITICAL(&s_lock);
        }
        portENTER_CRITICAL(&s_lock);
        mode_config_t m = s_settings.mode;
        portEXIT_CRITICAL(&s_lock);
        snprintf(reply, capacity, "OK SBUS MODE %d %d %d %d %d",
                 m.channel, m.surface_min, m.surface_max, m.attitude_min, m.attitude_max);
        return;
    }

    bool set_channel = strncmp(command, "SBUS SET_CH ", 12) == 0;
    if ((set_channel || strncmp(command, "SBUS GET_CH ", 12) == 0)
        && parse_ints(command + 11, v, set_channel ? 8 : 1)
        && v[0] >= 1 && v[0] <= SBUS_CONTROL_CHANNELS) {
        int index = v[0] - 1;
        if (set_channel) {
            channel_config_t c = {v[1], v[2], v[3], v[4], v[5], v[6], v[7]};
            if (!valid_channel(c)) goto bad_argument;
            portENTER_CRITICAL(&s_lock);
            s_settings.channel[index] = c;
            portEXIT_CRITICAL(&s_lock);
        }
        portENTER_CRITICAL(&s_lock);
        channel_config_t c = s_settings.channel[index];
        portEXIT_CRITICAL(&s_lock);
        snprintf(reply, capacity, "OK SBUS CH %d %d %d %d %d %d %d %d",
                 v[0], c.min, c.center, c.max, c.surface_min, c.surface_max, c.attitude_min, c.attitude_max);
        return;
    }

bad_argument:
    snprintf(reply, capacity, "ERR SBUS_ARGUMENT");
}

sbus_command_t sbus_transform_sample(const uint16_t *raw, bool usable, int64_t now_us)
{
    static int candidate;
    static int64_t since;
    static unsigned last_revision;
    portENTER_CRITICAL(&s_lock);
    const settings_t settings = s_settings;
    const unsigned revision = s_mode_revision;
    portEXIT_CRITICAL(&s_lock);

    const mode_config_t m = settings.mode;
    int requested = 0;
    if (usable && m.channel != 0) {
        int value = raw[m.channel - 1];
        if (value >= m.surface_min && value <= m.surface_max) requested = 1;
        if (value >= m.attitude_min && value <= m.attitude_max) requested = 2;
    }
    if (requested != candidate || revision != last_revision) {
        candidate = requested;
        since = now_us;
        last_revision = revision;
    }

    sbus_command_t result = {0};
    if (candidate && now_us - since >= MODE_DWELL_US) result.mode = candidate;
    if (raw == NULL) return result;

    for (int i = 0; i < SBUS_CONTROL_CHANNELS; ++i) {
        const channel_config_t c = settings.channel[i];
        int delta = raw[i] - c.center;
        int span = delta >= 0 ? c.max - c.center : c.center - c.min;
        if (delta > span) delta = span;
        if (delta < -span) delta = -span;
        result.normalized[i] = delta * 1000 / span;

        if (result.mode == 0 || i + 1 == m.channel) continue;
        int low = result.mode == 1 ? c.surface_min : c.attitude_min;
        int high = result.mode == 1 ? c.surface_max : c.attitude_max;
        if (low == 0 && high == 0) continue;
        result.active_mask |= 1u << i;
        result.angles[i] = (delta >= 0 ? delta * high : -delta * low) / span;
    }
    return result;
}
