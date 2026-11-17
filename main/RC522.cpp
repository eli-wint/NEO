#include "RC522.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "chrono"

namespace
{
static const char *TAG = "RC522";

constexpr uint8_t REG_COMMAND = 0x01;
constexpr uint8_t REG_COM_IRQ = 0x04;
constexpr uint8_t REG_FIFO_DATA = 0x09;
constexpr uint8_t REG_FIFO_LEVEL = 0x0A;
constexpr uint8_t REG_BIT_FRAMING = 0x0D;
constexpr uint8_t REG_TX_CONTROL = 0x14;
constexpr uint8_t REG_TX_ASK = 0x15;
constexpr uint8_t REG_MODE = 0x11;
constexpr uint8_t REG_T_MODE = 0x2A;
constexpr uint8_t REG_T_PRESCALER = 0x2B;
constexpr uint8_t REG_T_RELOAD_H = 0x2C;
constexpr uint8_t REG_T_RELOAD_L = 0x2D;
constexpr uint8_t REG_VERSION = 0x37;

constexpr uint8_t COMMAND_IDLE = 0x00;
constexpr uint8_t COMMAND_TRANSCEIVE = 0x0C;
constexpr uint8_t COMMAND_SOFT_RESET = 0x0F;
constexpr uint8_t PICC_WAKEUP = 0x52;

uint16_t calculateCrcA(const uint8_t *data, size_t length)
{
    uint16_t crc = 0x6363;
    for (size_t i = 0; i < length; ++i)
    {
        uint8_t value = data[i] ^ static_cast<uint8_t>(crc);
        value ^= static_cast<uint8_t>(value << 4);
        crc = (crc >> 8) ^
              (static_cast<uint16_t>(value) << 8) ^
              (static_cast<uint16_t>(value) << 3) ^
              (static_cast<uint16_t>(value) >> 4);
    }
    return crc;
}
}

RC522::RC522(
    spi_host_device_t spiHost,
    gpio_num_t chipSelect,
    gpio_num_t reset)
    : _spiHost(spiHost),
      _chipSelect(chipSelect),
      _reset(reset)
{
}

esp_err_t RC522::writeRegister(uint8_t reg, uint8_t value)
{
    if (_spiDevice == nullptr)
        return ESP_ERR_INVALID_STATE;

    uint8_t txData[2] = {
        static_cast<uint8_t>((reg << 1) & 0x7E),
        value};
    spi_transaction_t transaction = {};
    transaction.length = 16;
    transaction.tx_buffer = txData;
    return spi_device_transmit(_spiDevice, &transaction);
}

esp_err_t RC522::readRegister(uint8_t reg, uint8_t *value)
{
    if (_spiDevice == nullptr || value == nullptr)
        return ESP_ERR_INVALID_ARG;

    uint8_t txData[2] = {
        static_cast<uint8_t>(((reg << 1) & 0x7E) | 0x80),
        0};
    uint8_t rxData[2] = {};
    spi_transaction_t transaction = {};
    transaction.length = 16;
    transaction.tx_buffer = txData;
    transaction.rx_buffer = rxData;

    esp_err_t err = spi_device_transmit(_spiDevice, &transaction);
    if (err != ESP_OK)
        return err;

    *value = rxData[1];
    return ESP_OK;
}

esp_err_t RC522::transceive(
    const uint8_t *transmitData,
    size_t transmitLength,
    uint8_t transmitLastBits,
    uint8_t *receiveData,
    size_t receiveCapacity,
    size_t *receiveLength,
    bool *responseReceived)
{
    if (transmitData == nullptr || transmitLength == 0 ||
        receiveLength == nullptr || responseReceived == nullptr ||
        (receiveCapacity > 0 && receiveData == nullptr))
        return ESP_ERR_INVALID_ARG;

    *receiveLength = 0;
    *responseReceived = false;

    esp_err_t err = writeRegister(REG_BIT_FRAMING, transmitLastBits & 0x07);
    if (err != ESP_OK)
        return err;
    err = writeRegister(REG_COM_IRQ, 0x7F);
    if (err != ESP_OK)
        return err;
    err = writeRegister(REG_FIFO_LEVEL, 0x80);
    if (err != ESP_OK)
        return err;

    for (size_t i = 0; i < transmitLength; ++i)
    {
        err = writeRegister(REG_FIFO_DATA, transmitData[i]);
        if (err != ESP_OK)
            return err;
    }

    err = writeRegister(REG_COMMAND, COMMAND_TRANSCEIVE);
    if (err != ESP_OK)
        return err;
    err = writeRegister(
        REG_BIT_FRAMING,
        static_cast<uint8_t>(0x80 | (transmitLastBits & 0x07)));
    if (err != ESP_OK)
        return err;

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(25);
    while (std::chrono::steady_clock::now() < deadline)
    {
        uint8_t irq = 0;
        err = readRegister(REG_COM_IRQ, &irq);
        if (err != ESP_OK)
            return err;

        if ((irq & 0x31) != 0)
        {
            *responseReceived = (irq & 0x20) != 0;
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    err = writeRegister(REG_COMMAND, COMMAND_IDLE);
    if (err != ESP_OK)
        return err;
    if (!*responseReceived)
        return ESP_OK;

    uint8_t fifoLevel = 0;
    err = readRegister(REG_FIFO_LEVEL, &fifoLevel);
    if (err != ESP_OK)
        return err;

    *receiveLength = fifoLevel & 0x7F;
    if (*receiveLength > receiveCapacity)
        return ESP_ERR_INVALID_SIZE;

    for (size_t i = 0; i < *receiveLength; ++i)
    {
        err = readRegister(REG_FIFO_DATA, &receiveData[i]);
        if (err != ESP_OK)
            return err;
    }

    return ESP_OK;
}

esp_err_t RC522::init()
{
    if (_spiDevice != nullptr)
        return ESP_ERR_INVALID_STATE;

    gpio_config_t resetConfig = {};
    resetConfig.pin_bit_mask = 1ULL << _reset;
    resetConfig.mode = GPIO_MODE_OUTPUT;
    resetConfig.pull_up_en = GPIO_PULLUP_DISABLE;
    resetConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
    resetConfig.intr_type = GPIO_INTR_DISABLE;

    esp_err_t err = gpio_config(&resetConfig);
    if (err != ESP_OK)
        return err;

    err = gpio_set_level(_reset, 0);
    if (err != ESP_OK)
        return err;
    vTaskDelay(pdMS_TO_TICKS(10));

    err = gpio_set_level(_reset, 1);
    if (err != ESP_OK)
        return err;
    vTaskDelay(pdMS_TO_TICKS(50));

    spi_device_interface_config_t deviceConfig = {};
    deviceConfig.clock_speed_hz = 1 * 1000 * 1000;
    deviceConfig.mode = 0;
    deviceConfig.spics_io_num = _chipSelect;
    deviceConfig.queue_size = 1;

    err = spi_bus_add_device(_spiHost, &deviceConfig, &_spiDevice);
    if (err != ESP_OK)
        return err;

    uint8_t version = 0;
    err = readRegister(REG_VERSION, &version);
    if (err != ESP_OK)
        return err;
    if (version == 0x00 || version == 0xFF)
        return ESP_ERR_NOT_FOUND;

    ESP_LOGI(TAG, "RC522 version: 0x%02X", version);

    err = writeRegister(REG_COMMAND, COMMAND_SOFT_RESET);
    if (err != ESP_OK)
        return err;
    vTaskDelay(pdMS_TO_TICKS(50));

    err = writeRegister(REG_T_MODE, 0x8D);
    if (err != ESP_OK)
        return err;
    err = writeRegister(REG_T_PRESCALER, 0x3E);
    if (err != ESP_OK)
        return err;
    err = writeRegister(REG_T_RELOAD_H, 0x00);
    if (err != ESP_OK)
        return err;
    err = writeRegister(REG_T_RELOAD_L, 0x1E);
    if (err != ESP_OK)
        return err;
    err = writeRegister(REG_TX_ASK, 0x40);
    if (err != ESP_OK)
        return err;
    err = writeRegister(REG_MODE, 0x3D);
    if (err != ESP_OK)
        return err;

    uint8_t txControl = 0;
    err = readRegister(REG_TX_CONTROL, &txControl);
    if (err != ESP_OK)
        return err;
    return writeRegister(
        REG_TX_CONTROL,
        static_cast<uint8_t>(txControl | 0x03));
}

esp_err_t RC522::readCardPresent(bool *cardPresent)
{
    if (cardPresent == nullptr)
        return ESP_ERR_INVALID_ARG;
    if (_spiDevice == nullptr)
        return ESP_ERR_INVALID_STATE;

    *cardPresent = false;

    const uint8_t wakeupCommand = PICC_WAKEUP;
    uint8_t atqa[2] = {};
    size_t atqaLength = 0;
    bool responseReceived = false;
    esp_err_t err = transceive(
        &wakeupCommand,
        1,
        7,
        atqa,
        sizeof(atqa),
        &atqaLength,
        &responseReceived);
    if (err != ESP_OK)
        return err;
    if (!responseReceived || atqaLength < sizeof(atqa))
        return ESP_OK;

    *cardPresent = true;

    uint8_t haltCommand[4] = {0x50, 0x00, 0, 0};
    const uint16_t crc = calculateCrcA(haltCommand, 2);
    haltCommand[2] = static_cast<uint8_t>(crc);
    haltCommand[3] = static_cast<uint8_t>(crc >> 8);

    size_t haltResponseLength = 0;
    bool haltResponseReceived = false;
    return transceive(
        haltCommand,
        sizeof(haltCommand),
        0,
        nullptr,
        0,
        &haltResponseLength,
        &haltResponseReceived);
}
