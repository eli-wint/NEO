
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "Indicators.h"

// Create Indicators object
Indicators indicators;

extern "C" void app_main(void)
{
    printf("\n");
    printf("============================\n");
    printf("       N.E.O. PROJECT\n");
    printf("       SYSTEM INITIALIZING\n");
    printf("============================\n");

    // Initialize indicators
    indicators.init();

    while (true)
    {
        printf("RED\n");
        indicators.setRed();
        vTaskDelay(pdMS_TO_TICKS(1000));

        printf("GREEN\n");
        indicators.setGreen();
        vTaskDelay(pdMS_TO_TICKS(1000));

        printf("BLUE\n");
        indicators.setBlue();
        vTaskDelay(pdMS_TO_TICKS(1000));

        printf("OFF\n");
        indicators.turnOff();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}