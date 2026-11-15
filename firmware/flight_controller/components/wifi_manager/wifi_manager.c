#include "wifi_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#define WIFI_HAS_IP_BIT BIT0

static EventGroupHandle_t s_wifi_events;
static esp_netif_t *s_station_netif;
// Incremented on disconnection and each IP acquisition so UDP sockets are recreated.
static uint32_t s_network_epoch;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        (void)esp_wifi_connect();
    } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, WIFI_HAS_IP_BIT);
        __atomic_add_fetch(&s_network_epoch, 1u, __ATOMIC_RELAXED);
        // This demo retries indefinitely. Add retry/backoff policy for production.
        (void)esp_wifi_connect();
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        __atomic_add_fetch(&s_network_epoch, 1u, __ATOMIC_RELAXED);
        xEventGroupSetBits(s_wifi_events, WIFI_HAS_IP_BIT);
    }
}

void wifi_manager_start(void)
{
    const char *ssid = CONFIG_HIKOROBO_WIFI_SSID;
    const char *password = CONFIG_HIKOROBO_WIFI_PASSWORD;
    const size_t ssid_len = strlen(ssid);
    const size_t pass_len = strlen(password);

    // The 32-byte SSID field is kept NUL-terminated in this demonstration.
    if (ssid_len == 0 || ssid_len > 31 || pass_len < 8 || pass_len > 63) {
        // Prevent silent attempts with a default/empty password.
        abort();
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_wifi_events = xEventGroupCreate();
    configASSERT(s_wifi_events != NULL);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_station_netif = esp_netif_create_default_wifi_sta();
    configASSERT(s_station_netif != NULL);

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    wifi_config_t config = {0};
    memcpy(config.sta.ssid, ssid, ssid_len);
    memcpy(config.sta.password, password, pass_len);
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Wi-Fiの省電力モードを無効化
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
}

bool wifi_manager_wait_connected(TickType_t timeout)
{
    return (xEventGroupWaitBits(s_wifi_events, WIFI_HAS_IP_BIT, pdFALSE,
                                pdTRUE, timeout) & WIFI_HAS_IP_BIT) != 0;
}

bool wifi_manager_connected(void)
{
    return (xEventGroupGetBits(s_wifi_events) & WIFI_HAS_IP_BIT) != 0;
}

uint32_t wifi_manager_epoch(void)
{
    return __atomic_load_n(&s_network_epoch, __ATOMIC_RELAXED);
}

void wifi_manager_format_status(char *response, size_t capacity)
{
    if (response == NULL || capacity == 0) {
        return;
    }
    if (!wifi_manager_connected()) {
        snprintf(response, capacity, "OK WIFI DISCONNECTED");
        return;
    }
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(s_station_netif, &info) != ESP_OK) {
        snprintf(response, capacity, "ERR WIFI_IP");
        return;
    }
    snprintf(response, capacity, "OK WIFI CONNECTED IP=" IPSTR, IP2STR(&info.ip));
}
