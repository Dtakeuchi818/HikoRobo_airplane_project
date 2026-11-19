#include "command_protocol.h"
#include "esp_err.h"
#include "esp_log.h"
#include "imu_monitor.h"
#include "sbus_monitor.h"
#include "sbus_transform.h"
#include "sdkconfig.h"
#include "udp_transport.h"
#include "usb_transport.h"
#include "wifi_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void usb_task(void *arg)
{
    (void)arg;
    usb_transport_run(); // Blocks forever; USB remains useful for diagnostics.
}

void app_main(void)
{
#ifdef CONFIG_HIKOROBO_SBUS_MONITOR
    sbus_monitor_run();
    return;
#endif

    wifi_manager_start(); // Wi-Fi joins Ubuntu hotspot asynchronously.
    sbus_transform_load();
    command_protocol_set_sbus_handler(sbus_transform_handle);
    esp_err_t sbus_err = sbus_stream_start();
    if (sbus_err != ESP_OK) {
        ESP_LOGE("APP", "S.BUS stream start failed: %s", esp_err_to_name(sbus_err));
    }
    command_protocol_set_wifi_status_provider(wifi_manager_format_status);
    command_protocol_set_imu_raw_provider(imu_monitor_format_latest);
    command_protocol_set_imu_control_handler(imu_monitor_handle_control_command);

    BaseType_t usb_result = xTaskCreate(usb_task, "usb_command", 4096, NULL, 4, NULL);
    BaseType_t udp_result = xTaskCreate(udp_transport_run, "udp_command", 6144, NULL, 4, NULL);
    configASSERT(usb_result == pdPASS && udp_result == pdPASS);

    esp_err_t imu_err = imu_monitor_start();

    if (imu_err != ESP_OK) {
        ESP_LOGE(
            "APP",
            "IMU start failed: %s",
            esp_err_to_name(imu_err)
        );
    }
}
