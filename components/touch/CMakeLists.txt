idf_component_register(
    SRCS "touch.c"
    INCLUDE_DIRS "include"
    REQUIRES
        board
        i2c_bus
        esp_lcd
        lvgl__lvgl
        espressif__esp_lvgl_port
        espressif__esp_lcd_touch
        esp_lcd_touch_ft6336u
)
