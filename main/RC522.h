#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

class RC522
{
public:
    RC522(
        spi_host_device_t spiHost,
        gpio_num_t chipSelect,
        gpio_num_t reset);

    esp_err_t init();
    esp_err_t readCardPresent(bool *cardPresent);

private:
    esp_err_t writeRegister(uint8_t reg, uint8_t value);
    esp_err_t readRegister(uint8_t reg, uint8_t *value);
    esp_err_t transceive(
        const uint8_t *transmitData,
        size_t transmitLength,
        uint8_t transmitLastBits,
        uint8_t *receiveData,
        size_t receiveCapacity,
        size_t *receiveLength,
        bool *responseReceived);

    spi_host_device_t _spiHost;
    gpio_num_t _chipSelect;
    gpio_num_t _reset;
    spi_device_handle_t _spiDevice = nullptr;
};
