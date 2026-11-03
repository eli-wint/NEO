idf_component_register(
    SRCS
        "main.cpp"
        "Indicators.cpp"
        "LCD.cpp"

    INCLUDE_DIRS "."

    REQUIRES 
        led_strip
        esp_lcd
        esp_lcd_ili9341
        esp_driver_spi
        esp_driver_gpio
)