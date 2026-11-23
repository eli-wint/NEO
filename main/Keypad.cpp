#include "Keypad.h"

#include "esp_timer.h"

namespace
{
constexpr int64_t KEY_DEBOUNCE_US = 30000;
}

Keypad::Keypad(
    gpio_num_t r1,
    gpio_num_t r2,
    gpio_num_t r3,
    gpio_num_t r4,
    gpio_num_t c1,
    gpio_num_t c2,
    gpio_num_t c3,
    gpio_num_t c4)
    : _rows{r1, r2, r3, r4},
      _cols{c1, c2, c3, c4}
{
}

esp_err_t Keypad::init()
{
    _candidateKey = '\0';
    _candidateSinceUs = 0;
    _keyReported = false;

    gpio_config_t rowConfig = {};
    rowConfig.mode = GPIO_MODE_OUTPUT;
    rowConfig.pull_up_en = GPIO_PULLUP_DISABLE;
    rowConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
    rowConfig.intr_type = GPIO_INTR_DISABLE;

    for (gpio_num_t row : _rows)
    {
        rowConfig.pin_bit_mask = 1ULL << row;
        esp_err_t err = gpio_config(&rowConfig);
        if (err != ESP_OK)
            return err;

        err = gpio_set_level(row, 1);
        if (err != ESP_OK)
            return err;
    }

    gpio_config_t columnConfig = {};
    columnConfig.mode = GPIO_MODE_INPUT;
    columnConfig.pull_up_en = GPIO_PULLUP_ENABLE;
    columnConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
    columnConfig.intr_type = GPIO_INTR_DISABLE;

    for (gpio_num_t column : _cols)
    {
        columnConfig.pin_bit_mask = 1ULL << column;
        esp_err_t err = gpio_config(&columnConfig);
        if (err != ESP_OK)
            return err;
    }

    return ESP_OK;
}

esp_err_t Keypad::readKey(char *key)
{
    if (key == nullptr)
        return ESP_ERR_INVALID_ARG;

    *key = '\0';
    char detectedKey = '\0';

    for (gpio_num_t row : _rows)
    {
        esp_err_t err = gpio_set_level(row, 1);
        if (err != ESP_OK)
            return err;
    }

    for (int rowIndex = 0; rowIndex < 4; ++rowIndex)
    {
        esp_err_t err = gpio_set_level(_rows[rowIndex], 0);
        if (err != ESP_OK)
            return err;

        for (int columnIndex = 0; columnIndex < 4; ++columnIndex)
        {
            if (gpio_get_level(_cols[columnIndex]) == 0)
            {
                detectedKey = KEY_MAP[rowIndex][columnIndex];
                break;
            }
        }

        err = gpio_set_level(_rows[rowIndex], 1);
        if (err != ESP_OK)
            return err;

        if (detectedKey != '\0')
            break;
    }

    const int64_t nowUs = esp_timer_get_time();
    if (detectedKey != _candidateKey)
    {
        _candidateKey = detectedKey;
        _candidateSinceUs = nowUs;
        return ESP_OK;
    }

    if (nowUs - _candidateSinceUs < KEY_DEBOUNCE_US)
        return ESP_OK;

    if (detectedKey == '\0')
    {
        _keyReported = false;
        return ESP_OK;
    }

    if (!_keyReported)
    {
        *key = detectedKey;
        _keyReported = true;
    }

    return ESP_OK;
}
