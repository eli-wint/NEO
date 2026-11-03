
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

#include "LCD.h"

// LCD GPIO configuration
#define SPI_SCK     GPIO_NUM_12
#define SPI_MOSI    GPIO_NUM_11
#define SPI_MISO    GPIO_NUM_13

#define LCD_CS      GPIO_NUM_10
#define LCD_DC      GPIO_NUM_9
#define LCD_RST     GPIO_NUM_8
#define LCD_BL      GPIO_NUM_14

static const char *TAG = "MAIN";

// Create LCD object
LCD lcd(
    SPI_SCK,
    SPI_MOSI,
    SPI_MISO,
    LCD_CS,
    LCD_DC,
    LCD_RST,
    LCD_BL
);

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting N.E.O.");

    // Initialize LCD
    ESP_ERROR_CHECK(lcd.init());

    // Landscape orientation
    ESP_ERROR_CHECK(lcd.setRotation(1));

    // Test display colors
    ESP_ERROR_CHECK(lcd.fillScreen(0xF800));
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_ERROR_CHECK(lcd.fillScreen(0x07E0));
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_ERROR_CHECK(lcd.fillScreen(0x001F));
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_ERROR_CHECK(lcd.fillScreen(0xFFFF));
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_ERROR_CHECK(lcd.fillScreen(0x0000));

    ESP_LOGI(TAG, "Display test complete");
}