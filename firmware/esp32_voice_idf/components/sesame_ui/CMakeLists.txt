idf_component_register(
    SRCS "oled_expression_display.cpp"
    INCLUDE_DIRS "include"
    REQUIRES arduino-esp32 esp_driver_i2c freertos
)
