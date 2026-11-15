#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


void app_main(void)
{
    printf("ESP32-S3 Started!\n");

    while(1)
    {
        printf("Hello World\n");
        fflush(stdout);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}