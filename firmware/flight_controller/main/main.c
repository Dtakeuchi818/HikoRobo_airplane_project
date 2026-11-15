#include "usb_transport.h"

void app_main(void)
{
    // Blocking event loop. USB input is received in this FreeRTOS main task.
    usb_transport_run();
}
