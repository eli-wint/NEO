idf_component_register(
    SRCS
        "main.cpp"
        "Indicators.cpp"

    INCLUDE_DIRS "."

    REQUIRES led_strip
)