idf_component_register(
    SRCS "touch.c"
    INCLUDE_DIRS "include"
    REQUIRES
        board
        i2c_bus
        ui
        esp_lcd
        espressif__esp_lcd_touch
        espressif__esp_lcd_touch_ft5x06
)
