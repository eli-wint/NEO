
#include "LCD.h"

#include <cstring>

#include "esp_log.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_ili9341.h"

static const char *TAG = "LCD";

// Constructor
LCD::LCD(
    gpio_num_t sck,
    gpio_num_t mosi,
    gpio_num_t miso,
    gpio_num_t cs,
    gpio_num_t dc,
    gpio_num_t rst,
    gpio_num_t bl
)
    : _sck(sck),
      _mosi(mosi),
      _miso(miso),
      _cs(cs),
      _dc(dc),
      _rst(rst),
      _bl(bl)
{
}

// Initialize LCD
esp_err_t LCD::init()
{
    ESP_LOGI(TAG, "Initializing LCD...");

    esp_err_t err = initSPI();

    if (err != ESP_OK)
        return err;

    err = initPanel();

    if (err != ESP_OK)
        return err;

    // Turn display on
    err = esp_lcd_panel_disp_on_off(_panelHandle, true);

    if (err != ESP_OK)
        return err;

    err = setBacklight(true);

    if (err != ESP_OK)
        return err;

    ESP_LOGI(TAG, "LCD initialization complete");

    return ESP_OK;
}

// Initialize SPI
esp_err_t LCD::initSPI()
{
    spi_bus_config_t busConfig = {};

    busConfig.sclk_io_num = _sck;
    busConfig.mosi_io_num = _mosi;
    busConfig.miso_io_num = _miso;
    busConfig.quadwp_io_num = -1;
    busConfig.quadhd_io_num = -1;

    busConfig.max_transfer_sz =
        WIDTH * 40 * sizeof(uint16_t);

    esp_err_t err = spi_bus_initialize(
        _spiHost,
        &busConfig,
        SPI_DMA_CH_AUTO
    );

    if (err != ESP_OK)
        return err;

    ESP_LOGI(TAG, "SPI initialized");

    return ESP_OK;
}

// Initialize ILI9341 panel
esp_err_t LCD::initPanel()
{
    esp_lcd_panel_io_spi_config_t ioConfig = {};

    ioConfig.dc_gpio_num = _dc;
    ioConfig.cs_gpio_num = _cs;
    ioConfig.pclk_hz = _spiClock;
    ioConfig.lcd_cmd_bits = 8;
    ioConfig.lcd_param_bits = 8;
    ioConfig.spi_mode = 0;
    ioConfig.trans_queue_depth = 10;

    esp_err_t err = esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)_spiHost,
        &ioConfig,
        &_ioHandle
    );

    if (err != ESP_OK)
        return err;

    ESP_LOGI(TAG, "LCD SPI interface initialized");

    esp_lcd_panel_dev_config_t panelConfig = {};

    panelConfig.reset_gpio_num = _rst;
    panelConfig.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panelConfig.bits_per_pixel = 16;

    err = esp_lcd_new_panel_ili9341(
        _ioHandle,
        &panelConfig,
        &_panelHandle
    );

    if (err != ESP_OK)
        return err;

    ESP_LOGI(TAG, "ILI9341 panel created");

    err = esp_lcd_panel_reset(_panelHandle);

    if (err != ESP_OK)
        return err;

    err = esp_lcd_panel_init(_panelHandle);

    if (err != ESP_OK)
        return err;

    ESP_LOGI(TAG, "ILI9341 initialized");

    return ESP_OK;
}

// Set rotation
esp_err_t LCD::setRotation(uint8_t rotation)
{
    if (_panelHandle == nullptr || rotation > 3)
        return ESP_ERR_INVALID_ARG;

    bool swapXY = (rotation == 1 || rotation == 3);
    bool mirrorX = (rotation == 1 || rotation == 2);
    bool mirrorY = (rotation == 2 || rotation == 3);

    esp_err_t err = esp_lcd_panel_swap_xy(
        _panelHandle,
        swapXY
    );

    if (err != ESP_OK)
        return err;

    err = esp_lcd_panel_mirror(
        _panelHandle,
        mirrorX,
        mirrorY
    );

    if (err != ESP_OK)
        return err;

    _rotation = rotation;

    if (swapXY)
    {
        _width = HEIGHT;
        _height = WIDTH;
    }
    else
    {
        _width = WIDTH;
        _height = HEIGHT;
    }

    return ESP_OK;
}

// Backlight control
esp_err_t LCD::setBacklight(bool enabled)
{
    gpio_config_t config = {};

    config.pin_bit_mask = (1ULL << _bl);
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;

    esp_err_t err = gpio_config(&config);

    if (err != ESP_OK)
        return err;

    return gpio_set_level(_bl, enabled ? 1 : 0);
}

// Fill entire screen
esp_err_t LCD::fillScreen(uint16_t color)
{
    static uint16_t buffer[240 * 320];

    for (int i = 0; i < 240 * 320; i++)
    {
        buffer[i] = color;
    }

    ESP_ERROR_CHECK(
        esp_lcd_panel_draw_bitmap(
            _panelHandle,
            0, 0,
            _width, _height,
            buffer
        )
    );
    return ESP_OK;
}

// Draw rectangle
esp_err_t LCD::fillRect(
    int x,
    int y,
    int width,
    int height,
    uint16_t color
)
{
    if (_panelHandle == nullptr)
        return ESP_ERR_INVALID_STATE;

    if (width <= 0 || height <= 0 ||
        x < 0 || y < 0 ||
        x + width > _width ||
        y + height > _height)
        return ESP_ERR_INVALID_ARG;

    static uint16_t buffer[WIDTH * 40];

    for (int i = 0; i < width * height && i < WIDTH * 40; i++)
        buffer[i] = color;

    for (int row = 0; row < height; )
    {
        int rows = (height - row < 40) ? height - row : 40;

        for (int i = 0; i < width * rows; i++)
            buffer[i] = color;

        esp_err_t err = esp_lcd_panel_draw_bitmap(
            _panelHandle,
            x, y + row,
            x + width, y + row + rows,
            buffer
        );

        if (err != ESP_OK)
            return err;

        row += rows;
    }

    return ESP_OK;
}

// Draw bitmap
esp_err_t LCD::drawBitmap(
    int x,
    int y,
    int width,
    int height,
    const uint16_t *pixels
)
{
    if (_panelHandle == nullptr || pixels == nullptr)
        return ESP_ERR_INVALID_ARG;

    if (width <= 0 || height <= 0 ||
        x < 0 || y < 0 ||
        x + width > _width ||
        y + height > _height)
        return ESP_ERR_INVALID_ARG;

    return esp_lcd_panel_draw_bitmap(
        _panelHandle,
        x, y,
        x + width,
        y + height,
        pixels
    );
}

// Get dimensions
int LCD::getWidth() const
{
    return _width;
}

int LCD::getHeight() const
{
    return _height;
}