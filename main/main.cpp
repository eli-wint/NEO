
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

#include "LCD.h"
#include "RC522.h"
#include "Keypad.h"

#include "string"
#include "chrono"
#include "iomanip"

// SPI GPIO configuration
#define SPI_SCK GPIO_NUM_12
#define SPI_MOSI GPIO_NUM_11
#define SPI_MISO GPIO_NUM_13

// RC522 GPIO configuration
#define RC522_SDA GPIO_NUM_15
#define RC522_RST GPIO_NUM_16

// LCD GPIO configuration
#define LCD_CS GPIO_NUM_10
#define LCD_DC GPIO_NUM_9
#define LCD_RST GPIO_NUM_8
#define LCD_BL GPIO_NUM_14

// Keypad GPIO configuration
#define R1 GPIO_NUM_4
#define R2 GPIO_NUM_5
#define R3 GPIO_NUM_6
#define R4 GPIO_NUM_7
#define C1 GPIO_NUM_17
#define C2 GPIO_NUM_18
#define C3 GPIO_NUM_21
#define C4 GPIO_NUM_1

std::chrono::steady_clock::time_point start_time;
double time_elapsed = 0.0;
double timer = 0.0;
static std::chrono::steady_clock::time_point rfid_timer_start;
static bool rfid_timer_active = false;
static bool main_timer_stopped = false;
static bool timers_finished = false;
static bool rfid_poll_error_logged = false;
auto current_time = std::chrono::steady_clock::now();
static double factors[] = {0.0, 10.0, 10.0, 10.0, 6.0, 10.0}; // Factors for ms-ms, ms-ss, ss-ss, ss-mm, mm-mm
int factor_index = 0;                                       // Index to track the current factor

enum modes
{
    NEO_SETUP,    // Keypad input to set timer
    NEO_ONLINE,   // Normal operation, waiting for RFID card
    NEO_DEFUSING, // RFID card detected, countdown to defuse
    NEO_OFFLINE,  // Timer expired, NEO is offline
    NEO_IDLE      // Game end, waiting for reset
};

static modes mode = NEO_SETUP;

static const char *TAG = "MAIN";

// Create LCD object
LCD lcd(
    SPI_SCK,
    SPI_MOSI,
    SPI_MISO,
    LCD_CS,
    LCD_DC,
    LCD_RST,
    LCD_BL);

RC522 rc522(SPI2_HOST, RC522_SDA, RC522_RST);
Keypad keypad(R1, R2, R3, R4, C1, C2, C3, C4);

namespace
{
    constexpr double RFID_TIMER_SECONDS = 5.0;
    constexpr int RFID_TIMER_TEXT_SIZE = 2;
    constexpr int RFID_TIMER_TEXT_Y = 145;
    constexpr int RFID_TIMER_TEXT_WIDTH = 6 * RFID_TIMER_TEXT_SIZE * 5;

    esp_err_t readRC522(bool *cardPresent)
    {
        return rc522.readCardPresent(cardPresent);
    }
}

extern "C" void loop();
extern "C" void updateCountdown();
extern "C" void updateRFIDTimer();
extern "C" void readKeypad();

extern "C" void app_main(void)
{
    bool LEDs = false;
    bool Buzzer = false;

    std::string time = "00:00:00";

    ESP_LOGI(TAG, "Starting N.E.O.");

    // Initialize peripherals
    ESP_ERROR_CHECK(lcd.init());
    ESP_ERROR_CHECK(rc522.init());
    ESP_ERROR_CHECK(keypad.init());

    // Landscape orientation
    ESP_ERROR_CHECK(lcd.setRotation(3));

    // Initialize
    lcd.fillScreen(0x0000);
    lcd.print("Initializing N.E.O...", 10, 10, 0xFFE0, 0x0000, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));

    lcd.print("LCD: Init...", 10, 20, 0xFFE0, 0x0000, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));

    lcd.print("RC522: Init...", 10, 30, 0xFFE0, 0x0000, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));

    lcd.print("LCD: OK", 10, 40, 0x07E0, 0x0000, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));

    lcd.print("RC522: OK", 10, 50, 0x07E0, 0x0000, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));

    lcd.fillScreen(0x0000);
    lcd.print("N.E.O. ONLINE  ", 10, 10, 0xFFE0, 0x0000, 1);

    std::string text = (LEDs ? "LEDs: ON" : "LEDS: OFF");
    lcd.print(text.c_str(), 10, 20, 0xFFE0, 0x0000, 1);
    text = (Buzzer ? "Buzzer: ON" : "Buzzer: OFF");
    lcd.print(text.c_str(), 10, 30, 0xFFE0, 0x0000, 1);

    lcd.print("00:00:00", 90, 110, 0xFFE0, 0x0000, 3);

    ESP_LOGI(TAG, "Display test complete");

    while (true)
    {
        loop();
    }
}

extern "C" void loop()
{
    switch (mode)
    {
    case NEO_SETUP:
        readKeypad();
        break;
    case NEO_ONLINE:
        updateCountdown();
        updateRFIDTimer();
        break;
    case NEO_DEFUSING:
        updateCountdown();
        updateRFIDTimer();
        break;
    case NEO_OFFLINE:
        break;
    case NEO_IDLE:
        break;
    default:
        mode = NEO_IDLE;
        break;
    }
}

extern "C" void updateCountdown()
{
    auto current_time = std::chrono::steady_clock::now();
    if (timers_finished)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
        return;
    }

    time_elapsed =
        timer - std::chrono::duration<double>(current_time - start_time).count();
    if (time_elapsed <= 0.0 && !main_timer_stopped)
    {
        time_elapsed = 0.0;
        main_timer_stopped = true;
        lcd.print("00:00:00", 90, 110, 0xFFE0, 0x0000, 3);
    }

    if (!main_timer_stopped)
    {
        int mm = static_cast<int>(time_elapsed / 60);
        int ss = static_cast<int>(time_elapsed) % 60;
        int ms = static_cast<int>(time_elapsed * 100) % 100;
        std::ostringstream oss;
        oss << std::setw(2) << std::setfill('0') << mm << ":"
            << std::setw(2) << std::setfill('0') << ss << ":"
            << std::setw(2) << std::setfill('0') << ms;
        std::string time = oss.str();
        lcd.print(time.c_str(), 90, 110, 0xFFE0, 0x0000, 3);
    }

    vTaskDelay(pdMS_TO_TICKS(33));
}

extern "C" void updateRFIDTimer()
{
    if (timers_finished)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
        return;
    }

    auto current_time = std::chrono::steady_clock::now();
    bool card_present = false;
    esp_err_t err = readRC522(&card_present);
    if (err != ESP_OK)
    {
        if (!rfid_poll_error_logged)
        {
            ESP_LOGE(TAG, "RC522 poll failed: %s", esp_err_to_name(err));
            rfid_poll_error_logged = true;
        }
    }
    else
    {
        rfid_poll_error_logged = false;
        auto now = std::chrono::steady_clock::now();
        if (card_present && !rfid_timer_active)
        {
            rfid_timer_active = true;
            rfid_timer_start = now;
            ESP_LOGI(TAG, "Card detected; starting 5-second timer");
            lcd.print("N.E.O. DEFUSING", 10, 10, 0xFD20, 0x0000, 1);
            mode = NEO_DEFUSING;
        }
        else if (!card_present && rfid_timer_active)
        {
            rfid_timer_active = false;
            lcd.fillRect(
                (lcd.getWidth() - RFID_TIMER_TEXT_WIDTH) / 2,
                RFID_TIMER_TEXT_Y,
                RFID_TIMER_TEXT_WIDTH,
                8 * RFID_TIMER_TEXT_SIZE,
                0x0000);
            lcd.print("N.E.O. ONLINE  ", 10, 10, 0xFFE0, 0x0000, 1);
            mode = NEO_ONLINE;
            ESP_LOGI(TAG, "Card removed; hiding 5-second timer");
        }
    }

    if (rfid_timer_active)
    {
        const double rfid_time_elapsed =
            std::chrono::duration<double>(
                current_time - rfid_timer_start)
                .count();
        const double rfid_time_remaining =
            RFID_TIMER_SECONDS - rfid_time_elapsed;

        if (rfid_time_remaining <= 0.0)
        {
            timers_finished = true;
            main_timer_stopped = true;
            lcd.print("00:00:00", 90, 110, 0xFFE0, 0x0000, 3);
            std::string zeroTime = "00:00";
            lcd.print(
                zeroTime.c_str(),
                (lcd.getWidth() - RFID_TIMER_TEXT_WIDTH) / 2,
                RFID_TIMER_TEXT_Y,
                0xFFE0,
                0x0000,
                RFID_TIMER_TEXT_SIZE);
            lcd.print("N.E.O. OFFLINE ", 10, 10, 0xF800, 0x0000, 1);
            mode = NEO_OFFLINE;
            return;
        }

        int centiseconds =
            static_cast<int>(rfid_time_remaining * 100.0);
        int seconds = centiseconds / 100;
        int milliseconds = centiseconds % 100;
        std::ostringstream rfidOss;
        rfidOss << std::setw(2) << std::setfill('0') << seconds << ":"
                << std::setw(2) << std::setfill('0') << milliseconds;
        std::string rfidTime = rfidOss.str();
        lcd.print(
            rfidTime.c_str(),
            (lcd.getWidth() - RFID_TIMER_TEXT_WIDTH) / 2,
            RFID_TIMER_TEXT_Y,
            0xFFE0,
            0x0000,
            RFID_TIMER_TEXT_SIZE);
    }
}

extern "C" void readKeypad()
{
    /* Timer ms-ms = 10*ms
       Timer ms-ss = 10*ms
       Timer ss-ss = 10*ss
       Timer ss-mm = 6*ss
       Timer mm-mm = 60*mm
    */
    char key = '\0';
    esp_err_t err = keypad.readKey(&key);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Keypad read failed: %s", esp_err_to_name(err));
    }
    else if (key != '\0')
    {
        double add = 0.0;
        switch (key)
        {
        case '9':
            add += 0.01;
            [[fallthrough]];
        case '8':
            add += 0.01;
            [[fallthrough]];
        case '7':
            add += 0.01;
            [[fallthrough]];
        case '6':
            add += 0.01;
            [[fallthrough]];
        case '5':
            add += 0.01;
            [[fallthrough]];
        case '4':
            add += 0.01;
            [[fallthrough]];
        case '3':
            add += 0.1;
            [[fallthrough]];
        case '2':
            add += 0.01;
            [[fallthrough]];
        case '1':
            add += 0.01;
            [[fallthrough]];
        case '0':
            if (factor_index <= 5)
            {
                timer *= factors[factor_index];
                factor_index++;
                timer += add;
            }
            else
            {
                ESP_LOGW(TAG, "Maximum factor index reached; ignoring input");
            }
            break;
        case 'A':
            timer = 900;
            break;
        case 'B':
            timer = 600;
            break;
        case 'C':
            timer = 300;
            break;
        case 'D':
            timer = 0;
            break;
        case '*':
            timer = 0;
            factor_index = 0;
            break;
        case '#':
            timer = timer;
            mode = NEO_ONLINE;
            start_time = std::chrono::steady_clock::now();
            break;
        default:
            break;
        }
        std::ostringstream oss;
        int mm = static_cast<int>(timer / 60);
        int ss = static_cast<int>(timer) % 60;
        int ms = static_cast<int>(timer * 100) % 100;
        oss << std::setw(2) << std::setfill('0') << mm << ":"
            << std::setw(2) << std::setfill('0') << ss << ":"
            << std::setw(2) << std::setfill('0') << ms;
        std::string time = oss.str();
        lcd.print(time.c_str(), 90, 110, 0xFFE0, 0x0000, 3);

        ESP_LOGI(TAG, "Keypad input: %c, Timer: %.2f seconds", key, timer);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
}