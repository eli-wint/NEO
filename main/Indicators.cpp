#include "Indicators.h"

#include <stdio.h>

#include "esp_err.h"

#define RGB_LED_GPIO 48
#define RGB_LED_COUNT 1

void Indicators::init()
{
    printf("Init N.E.O. indicators\n");

    // LED CONFIG
    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = RGB_LED_GPIO;
    strip_config.max_leds = RGB_LED_COUNT;
    strip_config.led_model = LED_MODEL_WS2812;
    strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB;
    strip_config.flags.invert_out = false;

    // RMT CONFIG
    led_strip_rmt_config_t rmt_config = {};

    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = 10000000;
    rmt_config.mem_block_symbols = 64;
    rmt_config.flags.with_dma = false;

    // INIT LED STRIP
    esp_err_t result = led_strip_new_rmt_device(
        &strip_config,
        &rmt_config,
        &led_strip);

    if (result != ESP_OK)
    {
        printf("ERROR: RGB LED init failed!");
        led_strip = nullptr;
        return;
    }

    turnOff();

    printf("N.E.O. indicators initialized\n");
}

void Indicators::setRed()
{
    led_strip_set_pixel(led_strip, 0, 255, 0, 0);
    led_strip_refresh(led_strip);
}

void Indicators::setGreen()
{
    led_strip_set_pixel(led_strip, 0, 0, 255, 0);
    led_strip_refresh(led_strip);
}

void Indicators::setBlue()
{
    led_strip_set_pixel(led_strip, 0, 0, 0, 255);
    led_strip_refresh(led_strip);
}

void Indicators::turnOff()
{
    if (led_strip != nullptr)
    {
        led_strip_clear(led_strip);
    }
}
