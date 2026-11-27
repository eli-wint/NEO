
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"

#include "LCD.h"
#include "RC522.h"
#include "Keypad.h"

#include "string"
#include "chrono"
#include "iomanip"
#include <cstdint>
#include <cmath>

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

// 24v Rail GPIO configuration
#define LED_R GPIO_NUM_43
#define LED_G GPIO_NUM_42
#define LED_W GPIO_NUM_41
#define BUZZER GPIO_NUM_44

// User input GPIO configuration
#define RED_BUTTON GPIO_NUM_38
#define BUZZER_ENABLE GPIO_NUM_40
#define LEDS_ENABLE GPIO_NUM_39

enum LedState
{
    LED_OFF,
    LED_RED,
    LED_GREEN,
    LED_WHITE
};

constexpr int64_t pulse_period = 1000; // Pulse period in milliseconds
constexpr int64_t pulse_duration = 100;

std::chrono::steady_clock::time_point start_time;
double time_elapsed = 0.0;
double timer = 0.0;
static std::chrono::steady_clock::time_point rfid_timer_start;
static bool rfid_timer_active = false;
static bool rfid_timer_expired = false;
static bool main_timer_stopped = false;
static bool timers_finished = false;
static bool rfid_poll_error_logged = false;
static bool white_gpio_status_displayed = false;
static bool last_red_button_state = false;
auto current_time = std::chrono::steady_clock::now();
static std::string timer_input;
static std::string valid_keys[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
bool last_led_state = false;
bool last_buzzer_state = false;

enum modes
{
    NEO_SETUP,    // Keypad input to set timer
    NEO_ONLINE,   // Normal operation, waiting for RFID card
    NEO_DEFUSING, // RFID card detected, countdown to defuse
    NEO_OFFLINE,  // Defused, NEO is offline
    NEO_DETONATED,
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
    constexpr int RFID_OVERRIDE_TEXT_WIDTH = 15 * RFID_TIMER_TEXT_SIZE * 6;
    constexpr int MAIN_RESULT_TEXT_SIZE = 2;
    constexpr int MAIN_RESULT_TEXT_WIDTH = 18 * 6 * MAIN_RESULT_TEXT_SIZE;

    void displayMainResult(const char *text)
    {
        const int x = (lcd.getWidth() - MAIN_RESULT_TEXT_WIDTH) / 2;
        lcd.fillRect(0, 110, lcd.getWidth(), 24, 0x0000);
        lcd.print(text, x, 110, 0xFFE0, 0x0000, MAIN_RESULT_TEXT_SIZE);
    }

    bool pulseActive()
    {
        const auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time)
                .count();
        return elapsed_ms >= 0 &&
               elapsed_ms % pulse_period < pulse_duration;
    }

    esp_err_t readRC522(bool *cardPresent)
    {
        return rc522.readCardPresent(cardPresent);
    }
}

extern "C" void loop();
extern "C" void updateCountdown();
extern "C" void updateRFIDTimer();
extern "C" void readKeypad();
extern "C" void updateLEDs(LedState state);
extern "C" void updateBuzzer(bool enabled);
extern "C" void updateInputs();

extern "C" void app_main(void)
{
    bool LEDs = false;
    bool Buzzer = false;

    std::string time = "00:00:00";

    ESP_LOGI(TAG, "Starting N.E.O.");

    // Initialize peripherals
    gpio_config_t indicator_outputs = {};
    indicator_outputs.pin_bit_mask =
        (1ULL << LED_R) | (1ULL << LED_G) | (1ULL << LED_W) | (1ULL << BUZZER);
    indicator_outputs.mode = GPIO_MODE_INPUT_OUTPUT;
    indicator_outputs.pull_up_en = GPIO_PULLUP_DISABLE;
    indicator_outputs.pull_down_en = GPIO_PULLDOWN_DISABLE;
    indicator_outputs.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&indicator_outputs));
    ESP_ERROR_CHECK(gpio_set_level(LED_R, 0));
    ESP_ERROR_CHECK(gpio_set_level(LED_G, 0));
    ESP_ERROR_CHECK(gpio_set_level(LED_W, 0));
    ESP_ERROR_CHECK(gpio_set_level(BUZZER, 0));

    gpio_config_t red_button_input = {};
    red_button_input.pin_bit_mask = 1ULL << RED_BUTTON;
    red_button_input.mode = GPIO_MODE_INPUT;
    red_button_input.pull_up_en = GPIO_PULLUP_DISABLE;
    red_button_input.pull_down_en = GPIO_PULLDOWN_ENABLE;
    red_button_input.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&red_button_input));

    gpio_config_t buzzer_enable_input = {};
    buzzer_enable_input.pin_bit_mask = 1ULL << BUZZER_ENABLE;
    buzzer_enable_input.mode = GPIO_MODE_INPUT;
    buzzer_enable_input.pull_up_en = GPIO_PULLUP_DISABLE;
    buzzer_enable_input.pull_down_en = GPIO_PULLDOWN_ENABLE;
    buzzer_enable_input.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&buzzer_enable_input));

    gpio_config_t led_enable_input = {};
    led_enable_input.pin_bit_mask = 1ULL << LEDS_ENABLE;
    led_enable_input.mode = GPIO_MODE_INPUT;
    led_enable_input.pull_up_en = GPIO_PULLUP_DISABLE;
    led_enable_input.pull_down_en = GPIO_PULLDOWN_ENABLE;
    led_enable_input.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&led_enable_input));

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

    updateInputs();

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
    case NEO_DETONATED:
        break;
    case NEO_IDLE:
        break;
    default:
        mode = NEO_IDLE;
        break;
    }

    updateInputs();

    if (mode == NEO_OFFLINE)
    {
        updateLEDs(LED_GREEN);
        updateBuzzer(true);
    }
    else if (mode == NEO_DETONATED)
    {
        updateLEDs(LED_RED);
        updateBuzzer(false);
    }
    else
    {
        const bool countdown_active =
            (mode == NEO_ONLINE || mode == NEO_DEFUSING) &&
            !main_timer_stopped && !timers_finished;
        const bool pulse_active = countdown_active && pulseActive();
        updateLEDs(pulse_active ? LED_RED : LED_OFF);
        updateBuzzer(pulse_active);
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
        timers_finished = true;
        mode = NEO_DETONATED;
        lcd.fillRect(
            (lcd.getWidth() - RFID_OVERRIDE_TEXT_WIDTH) / 2,
            RFID_TIMER_TEXT_Y,
            RFID_OVERRIDE_TEXT_WIDTH,
            8 * RFID_TIMER_TEXT_SIZE,
            0x0000);
        lcd.print("N.E.O. DETONATED", 10, 10, 0xF800, 0x0000, 1);
        displayMainResult("DETONATION SUCCESS");
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
        if (card_present && !rfid_timer_active && !rfid_timer_expired)
        {
            rfid_timer_active = true;
            rfid_timer_expired = false;
            rfid_timer_start = now;
            ESP_LOGI(TAG, "Card detected; starting 5-second timer");
            lcd.print("N.E.O. DEFUSING", 10, 10, 0xFD20, 0x0000, 1);
            mode = NEO_DEFUSING;
        }
        else if (!card_present && rfid_timer_active && !rfid_timer_expired)
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
            rfid_timer_active = false;
            rfid_timer_expired = true;
            const int override_x =
                (lcd.getWidth() - RFID_OVERRIDE_TEXT_WIDTH) / 2;
            lcd.fillRect(
                override_x,
                RFID_TIMER_TEXT_Y,
                RFID_OVERRIDE_TEXT_WIDTH,
                8 * RFID_TIMER_TEXT_SIZE,
                0x0000);
            lcd.print(
                "FINISH OVERRIDE",
                override_x,
                RFID_TIMER_TEXT_Y,
                0xFFE0,
                0x0000,
                RFID_TIMER_TEXT_SIZE);
            ESP_LOGI(TAG, "RFID timer expired; waiting for red button");
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
    char key = '\0';
    esp_err_t err = keypad.readKey(&key);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Keypad read failed: %s", esp_err_to_name(err));
    }
    else if (key != '\0')
    {
        if (key >= '0' && key <= '9')
        {
            if (timer_input.size() < 6)
            {
                timer_input += key;
                std::string digits(6 - timer_input.size(), '0');
                digits += timer_input;
                const int minutes =
                    (digits[0] - '0') * 10 + (digits[1] - '0');
                const int seconds =
                    (digits[2] - '0') * 10 + (digits[3] - '0');
                const int centiseconds =
                    (digits[4] - '0') * 10 + (digits[5] - '0');
                timer =
                    minutes * 60 + seconds + centiseconds / 100.0;
            }
            else
            {
                ESP_LOGW(TAG, "Maximum timer input length reached; ignoring input");
            }
        }
        else
        {
            switch (key)
            {
            case 'A':
                timer = 1200;
                timer_input.clear();
                break;
            case 'B':
                timer = 900;
                timer_input.clear();
                break;
            case 'C':
                timer = 600;
                timer_input.clear();
                break;
            case 'D':
                timer = 300;
                timer_input.clear();
                break;
            case '*':
                timer = 0;
                timer_input.clear();
                break;
            case '#':
                mode = NEO_ONLINE;
                start_time = std::chrono::steady_clock::now();
                main_timer_stopped = false;
                white_gpio_status_displayed = false;
                break;
            default:
                break;
            }
        }
        std::ostringstream oss;
        int mm = static_cast<int>(timer / 60);
        int ss = static_cast<int>(timer) % 60;
        int ms = static_cast<int>(std::lround(timer * 100)) % 100;
        oss << std::setw(2) << std::setfill('0') << mm << ":"
            << std::setw(2) << std::setfill('0') << ss << ":"
            << std::setw(2) << std::setfill('0') << ms;
        std::string time = oss.str();
        lcd.print(time.c_str(), 90, 110, 0xFFE0, 0x0000, 3);

        ESP_LOGI(TAG, "Keypad input: %c, Timer: %.2f seconds", key, timer);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
}

extern "C" void updateLEDs(LedState state)
{
    if (!gpio_get_level(LEDS_ENABLE))
    {
        state = LED_OFF;
    }

    switch (state)
    {
    case LED_OFF:
        gpio_set_level(LED_R, 0);
        gpio_set_level(LED_G, 0);
        gpio_set_level(LED_W, 0);
        break;
    case LED_RED:
        gpio_set_level(LED_R, 1);
        gpio_set_level(LED_G, 0);
        gpio_set_level(LED_W, 0);
        break;
    case LED_GREEN:
        gpio_set_level(LED_R, 0);
        gpio_set_level(LED_G, 1);
        gpio_set_level(LED_W, 0);
        break;
    case LED_WHITE:
        gpio_set_level(LED_R, 0);
        gpio_set_level(LED_G, 0);
        gpio_set_level(LED_W, 1);
        break;
    default:
        break;
    }
}

extern "C" void updateBuzzer(bool enabled)
{
    const bool on = enabled && gpio_get_level(BUZZER_ENABLE);
    gpio_set_level(BUZZER, on ? 1 : 0);
}

extern "C" void updateInputs()
{
    // Read the state of the buttons
    const bool red_button_pressed = gpio_get_level(RED_BUTTON) == 1;
    bool buzzer_enabled = gpio_get_level(BUZZER_ENABLE) == 1; // Assuming active high
    bool leds_enabled = gpio_get_level(LEDS_ENABLE) == 1;

    const bool red_button_just_pressed =
        red_button_pressed && !last_red_button_state;
    last_red_button_state = red_button_pressed;

    if (red_button_just_pressed &&
        mode == NEO_DEFUSING &&
        rfid_timer_expired &&
        !main_timer_stopped)
    {
        mode = NEO_OFFLINE;
        main_timer_stopped = true;
        timers_finished = true;
        rfid_timer_active = false;
        lcd.print("N.E.O. OFFLINE ", 10, 10, 0xF800, 0x0000, 1);
        lcd.fillRect(
            (lcd.getWidth() - RFID_OVERRIDE_TEXT_WIDTH) / 2,
            RFID_TIMER_TEXT_Y,
            RFID_OVERRIDE_TEXT_WIDTH,
            8 * RFID_TIMER_TEXT_SIZE,
            0x0000);
        displayMainResult("DETONATION FAILED");
        ESP_LOGI(TAG, "Red button pressed; defusal complete");
    }

    if (buzzer_enabled != last_buzzer_state)
    {
        last_buzzer_state = buzzer_enabled;
        lcd.print(buzzer_enabled ? "Buzzer: ON " : "Buzzer: OFF", 10, 30, 0xFFE0, 0x0000, 1);
    }

    if (leds_enabled != last_led_state)
    {
        last_led_state = leds_enabled;
        lcd.print(leds_enabled ? "LEDs: ON " : "LEDs: OFF", 10, 20, 0xFFE0, 0x0000, 1);
    }
}