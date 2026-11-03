
#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

class LCD
{
public:

    // Display dimensions
    static constexpr int WIDTH = 240;
    static constexpr int HEIGHT = 320;

    // Constructor
    LCD(
        gpio_num_t sck,
        gpio_num_t mosi,
        gpio_num_t miso,
        gpio_num_t cs,
        gpio_num_t dc,
        gpio_num_t rst,
        gpio_num_t bl
    );

    // Initialize display
    esp_err_t init();

    // Display controls
    esp_err_t setRotation(uint8_t rotation);
    esp_err_t setBacklight(bool enabled);

    // Drawing functions
    esp_err_t fillScreen(uint16_t color);
    esp_err_t fillRect(
        int x,
        int y,
        int width,
        int height,
        uint16_t color
    );

    esp_err_t drawBitmap(
        int x,
        int y,
        int width,
        int height,
        const uint16_t *pixels
    );

    // Display information
    int getWidth() const;
    int getHeight() const;

private:

    // GPIO assignments
    gpio_num_t _sck;
    gpio_num_t _mosi;
    gpio_num_t _miso;
    gpio_num_t _cs;
    gpio_num_t _dc;
    gpio_num_t _rst;
    gpio_num_t _bl;

    // SPI configuration
    spi_host_device_t _spiHost = SPI2_HOST;
    int _spiClock = 3000000;  // Start at 1 MHz

    // Display handles
    esp_lcd_panel_handle_t _panelHandle = nullptr;
    esp_lcd_panel_io_handle_t _ioHandle = nullptr;

    // Current orientation
    uint8_t _rotation = 0;

    // Initialization helper
    esp_err_t initSPI();
    esp_err_t initPanel();

    // Display dimensions after rotation
    int _width = WIDTH;
    int _height = HEIGHT;
};