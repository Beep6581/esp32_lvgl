idf_component_register(
    SRCS "display.c" "bitwalk.c" "fb_text.c" "glyph_5x7.c" "fonts/glcdfont_5x7.c"
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
