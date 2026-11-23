#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

#include <stdint.h>

class Keypad
{
public:
    static constexpr char KEY_MAP[4][4] = {
        {'D', '#', '0', '*'},
        {'C', '9', '8', '7'},
        {'B', '6', '5', '4'},
        {'A', '3', '2', '1'},
    };

    Keypad(
        gpio_num_t r1,
        gpio_num_t r2,
        gpio_num_t r3,
        gpio_num_t r4,
        gpio_num_t c1,
        gpio_num_t c2,
        gpio_num_t c3,
        gpio_num_t c4);

    esp_err_t init();
    esp_err_t readKey(char *key);

private:
    gpio_num_t _rows[4];
    gpio_num_t _cols[4];
    char _candidateKey = '\0';
    int64_t _candidateSinceUs = 0;
    bool _keyReported = false;
};
