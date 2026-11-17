
#include "LCD.h"

#include <cstdlib>
#include <cstring>

#include "esp_log.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_ili9341.h"

static const char *TAG = "LCD";

uint16_t LCD::toWireColor(uint16_t color)
{
    return static_cast<uint16_t>((color << 8) | (color >> 8));
}

// Constructor
LCD::LCD(
    gpio_num_t sck,
    gpio_num_t mosi,
    gpio_num_t miso,
    gpio_num_t cs,
    gpio_num_t dc,
    gpio_num_t rst,
    gpio_num_t bl)
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
        HEIGHT * 40 * sizeof(uint16_t);

    esp_err_t err = spi_bus_initialize(
        _spiHost,
        &busConfig,
        SPI_DMA_CH_AUTO);

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
        &_ioHandle);

    if (err != ESP_OK)
        return err;

    ESP_LOGI(TAG, "LCD SPI interface initialized");

    esp_lcd_panel_dev_config_t panelConfig = {};

    panelConfig.reset_gpio_num = _rst;
    panelConfig.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    panelConfig.bits_per_pixel = 16;

    err = esp_lcd_new_panel_ili9341(
        _ioHandle,
        &panelConfig,
        &_panelHandle);

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
    bool mirrorX = (rotation == 0 || rotation == 1);
    bool mirrorY = (rotation == 1 || rotation == 2);

    esp_err_t err = esp_lcd_panel_swap_xy(
        _panelHandle,
        swapXY);

    if (err != ESP_OK)
        return err;

    err = esp_lcd_panel_mirror(
        _panelHandle,
        mirrorX,
        mirrorY);

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
        buffer[i] = toWireColor(color);
    }

    ESP_ERROR_CHECK(
        esp_lcd_panel_draw_bitmap(
            _panelHandle,
            0, 0,
            _width, _height,
            buffer));
    return ESP_OK;
}

// Draw rectangle
esp_err_t LCD::fillRect(
    int x,
    int y,
    int width,
    int height,
    uint16_t color)
{
    if (_panelHandle == nullptr)
        return ESP_ERR_INVALID_STATE;

    if (width <= 0 || height <= 0 ||
        x < 0 || y < 0 ||
        x + width > _width ||
        y + height > _height)
        return ESP_ERR_INVALID_ARG;

    static uint16_t buffer[HEIGHT * 40];

    for (int row = 0; row < height;)
    {
        int rows = (height - row < 40) ? height - row : 40;

        for (int i = 0; i < width * rows; i++)
            buffer[i] = toWireColor(color);

        esp_err_t err = esp_lcd_panel_draw_bitmap(
            _panelHandle,
            x, y + row,
            x + width, y + row + rows,
            buffer);

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
    const uint16_t *pixels)
{
    if (_panelHandle == nullptr || pixels == nullptr)
        return ESP_ERR_INVALID_ARG;

    if (width <= 0 || height <= 0 ||
        x < 0 || y < 0 ||
        x + width > _width ||
        y + height > _height)
        return ESP_ERR_INVALID_ARG;

    static uint16_t buffer[HEIGHT * 40];

    for (int row = 0; row < height;)
    {
        int rows = (height - row < 40) ? height - row : 40;

        for (int i = 0; i < width * rows; i++)
            buffer[i] = toWireColor(pixels[row * width + i]);

        esp_err_t err = esp_lcd_panel_draw_bitmap(
            _panelHandle,
            x, y + row,
            x + width, y + row + rows,
            buffer);

        if (err != ESP_OK)
            return err;

        row += rows;
    }

    return ESP_OK;
}

// Print ASCII text using a compact 5x7 bitmap font.
esp_err_t LCD::print(
    const char *text,
    int x,
    int y,
    uint16_t textColor,
    uint16_t backgroundColor,
    uint8_t textSize)
{
    if (_panelHandle == nullptr)
        return ESP_ERR_INVALID_STATE;

    if (text == nullptr || textSize == 0 || x < 0 || y < 0 ||
        x >= _width || y >= _height)
        return ESP_ERR_INVALID_ARG;

    const int characterWidth = 6 * textSize;
    const int characterHeight = 8 * textSize;
    if (characterWidth > _width || characterHeight > _height ||
        x > _width - characterWidth || y > _height - characterHeight)
        return ESP_ERR_INVALID_ARG;

    static const uint8_t font[36][7] = {
        {14, 17, 17, 31, 17, 17, 17}, // A
        {30, 17, 17, 30, 17, 17, 30}, // B
        {14, 17, 16, 16, 16, 17, 14}, // C
        {30, 17, 17, 17, 17, 17, 30}, // D
        {31, 16, 16, 30, 16, 16, 31}, // E
        {31, 16, 16, 30, 16, 16, 16}, // F
        {14, 17, 16, 23, 17, 17, 15}, // G
        {17, 17, 17, 31, 17, 17, 17}, // H
        {14, 4, 4, 4, 4, 4, 14},      // I
        {7, 2, 2, 2, 2, 18, 12},      // J
        {17, 18, 20, 24, 20, 18, 17}, // K
        {16, 16, 16, 16, 16, 16, 31}, // L
        {17, 27, 21, 21, 17, 17, 17}, // M
        {17, 25, 21, 19, 17, 17, 17}, // N
        {14, 17, 17, 17, 17, 17, 14}, // O
        {30, 17, 17, 30, 16, 16, 16}, // P
        {14, 17, 17, 17, 21, 18, 13}, // Q
        {30, 17, 17, 30, 20, 18, 17}, // R
        {15, 16, 16, 14, 1, 1, 30},   // S
        {31, 4, 4, 4, 4, 4, 4},       // T
        {17, 17, 17, 17, 17, 17, 14}, // U
        {17, 17, 17, 17, 17, 10, 4},  // V
        {17, 17, 17, 21, 21, 21, 10}, // W
        {17, 17, 10, 4, 10, 17, 17},  // X
        {17, 17, 10, 4, 4, 4, 4},     // Y
        {31, 1, 2, 4, 8, 16, 31},     // Z
        {14, 17, 19, 21, 25, 17, 14}, // 0
        {4, 12, 4, 4, 4, 4, 14},      // 1
        {14, 17, 1, 2, 4, 8, 31},     // 2
        {30, 1, 1, 14, 1, 1, 30},     // 3
        {2, 6, 10, 18, 31, 2, 2},     // 4
        {31, 16, 16, 30, 1, 1, 30},   // 5
        {14, 16, 16, 30, 17, 17, 14}, // 6
        {31, 1, 2, 4, 8, 8, 8},       // 7
        {14, 17, 17, 14, 17, 17, 14}, // 8
        {14, 17, 17, 15, 1, 1, 14}    // 9
    };

    int cursorX = x;
    int cursorY = y;
    int maxX = x;
    int maxY = y;
    for (const char *character = text; *character != '\0'; ++character)
    {
        if (*character == '\r')
            continue;

        if (*character == '\n')
        {
            cursorX = x;
            cursorY += characterHeight;
            continue;
        }

        if (cursorX > _width - characterWidth)
        {
            cursorX = x;
            cursorY += characterHeight;
        }

        if (cursorY > _height - characterHeight)
            return ESP_ERR_INVALID_ARG;

        maxX = (cursorX + characterWidth > maxX)
                   ? cursorX + characterWidth
                   : maxX;
        maxY = (cursorY + characterHeight > maxY)
                   ? cursorY + characterHeight
                   : maxY;
        cursorX += characterWidth;
    }

    if (maxX == x || maxY == y)
        return ESP_OK;

    const int bufferWidth = maxX - x;
    const int bufferHeight = maxY - y;
    const size_t pixelCount =
        static_cast<size_t>(bufferWidth) * bufferHeight;
    uint16_t *pixels = static_cast<uint16_t *>(
        std::malloc(pixelCount * sizeof(uint16_t)));

    if (pixels == nullptr)
        return ESP_ERR_NO_MEM;

    for (size_t i = 0; i < pixelCount; ++i)
        pixels[i] = toWireColor(backgroundColor);

    auto drawGlyph = [bufferWidth, x, y, textColor, textSize, pixels](
                         int glyphX,
                         int glyphY,
                         unsigned char code)
    {
        uint8_t glyph[7] = {};

        if (code >= 'a' && code <= 'z')
            code = static_cast<unsigned char>(code - 'a' + 'A');

        if (code >= 'A' && code <= 'Z')
        {
            for (int row = 0; row < 7; ++row)
                glyph[row] = font[code - 'A'][row];
        }
        else if (code >= '0' && code <= '9')
        {
            for (int row = 0; row < 7; ++row)
                glyph[row] = font[26 + code - '0'][row];
        }
        else
        {
            switch (code)
            {
            case '.':
                glyph[5] = 12;
                glyph[6] = 12;
                break;
            case ',':
                glyph[5] = 12;
                glyph[6] = 8;
                break;
            case ':':
                glyph[1] = 4;
                glyph[2] = 4;
                glyph[4] = 4;
                glyph[5] = 4;
                break;
            case '!':
                glyph[0] = 4;
                glyph[1] = 4;
                glyph[2] = 4;
                glyph[3] = 4;
                glyph[4] = 4;
                glyph[6] = 4;
                break;
            case '?':
                glyph[0] = 14;
                glyph[1] = 17;
                glyph[2] = 1;
                glyph[3] = 2;
                glyph[4] = 4;
                glyph[6] = 4;
                break;
            case '-':
                glyph[3] = 14;
                break;
            case '_':
                glyph[6] = 31;
                break;
            case '+':
                glyph[2] = 4;
                glyph[3] = 14;
                glyph[4] = 4;
                break;
            case '/':
                glyph[0] = 1;
                glyph[1] = 1;
                glyph[2] = 2;
                glyph[3] = 4;
                glyph[4] = 8;
                glyph[5] = 16;
                glyph[6] = 16;
                break;
            case '(':
                glyph[0] = 2;
                glyph[1] = 4;
                glyph[2] = 8;
                glyph[3] = 8;
                glyph[4] = 8;
                glyph[5] = 4;
                glyph[6] = 2;
                break;
            case ')':
                glyph[0] = 8;
                glyph[1] = 4;
                glyph[2] = 2;
                glyph[3] = 2;
                glyph[4] = 2;
                glyph[5] = 4;
                glyph[6] = 8;
                break;
            case '\'':
                glyph[0] = 4;
                glyph[1] = 4;
                break;
            case '"':
                glyph[0] = 10;
                glyph[1] = 10;
                break;
            case ' ':
                break;
            default:
                glyph[0] = 14;
                glyph[1] = 17;
                glyph[2] = 1;
                glyph[3] = 2;
                glyph[4] = 4;
                glyph[6] = 4;
                break;
            }
        }

        for (int row = 0; row < 7; ++row)
        {
            for (int column = 0; column < 5; ++column)
            {
                if ((glyph[row] & (1U << (4 - column))) != 0)
                {
                    for (int scaleY = 0; scaleY < textSize; ++scaleY)
                    {
                        for (int scaleX = 0; scaleX < textSize; ++scaleX)
                        {
                            size_t pixelIndex =
                                static_cast<size_t>(
                                    glyphY - y + row * textSize + scaleY) *
                                    bufferWidth +
                                glyphX - x + column * textSize + scaleX;
                            pixels[pixelIndex] = toWireColor(textColor);
                        }
                    }
                }
            }
        }
    };

    cursorX = x;
    cursorY = y;
    for (const char *character = text; *character != '\0'; ++character)
    {
        if (*character == '\r')
            continue;

        if (*character == '\n')
        {
            cursorX = x;
            cursorY += characterHeight;
            continue;
        }

        if (cursorX > _width - characterWidth)
        {
            cursorX = x;
            cursorY += characterHeight;
        }

        drawGlyph(
            cursorX,
            cursorY,
            static_cast<unsigned char>(*character));
        cursorX += characterWidth;
    }

    esp_err_t err = esp_lcd_panel_draw_bitmap(
        _panelHandle,
        x, y,
        maxX, maxY,
        pixels);

    if (err == ESP_OK)
        // The panel queues color transfers; this command waits for them to finish.
        err = esp_lcd_panel_disp_on_off(_panelHandle, true);

    std::free(pixels);
    return err;
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