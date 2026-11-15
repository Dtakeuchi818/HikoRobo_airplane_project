#include "command_protocol.h"
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
    wifi_manager_start(); // Wi-Fi joins Ubuntu hotspot asynchronously.
    command_protocol_set_wifi_status_provider(wifi_manager_format_status);

    BaseType_t usb_result = xTaskCreate(usb_task, "usb_command", 4096, NULL, 4, NULL);
    BaseType_t udp_result = xTaskCreate(udp_transport_run, "udp_command", 6144, NULL, 4, NULL);
    configASSERT(usb_result == pdPASS && udp_result == pdPASS);
}
