idf_component_register(
    SRCS "display.c"
    INCLUDE_DIRS "include"
    REQUIRES
        board
        driver
        esp_lcd
        lvgl__lvgl
        espressif__esp_lvgl_port
        espressif__esp_lcd_panel_io_additions
        espressif__esp_lcd_gc9503
)
