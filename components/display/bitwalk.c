#include "board.h"
#include "rgb565.h"
#include "fb_text.h"

#include <stddef.h>

#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

static const char* TAG = "bitwalk";

static void fill_screen_u16(uint16_t* fb, int w, int h, uint16_t pixel) {
    int count = w * h;
    for (int i = 0; i < count; i++) {
        fb[i] = pixel;
    }
}

static void u16_to_binary_string(uint16_t v, char out[17]) {
    for (int bit = 15; bit >= 0; bit--) {
        out[15 - bit] = (v & (1u << bit)) ? '1' : '0';
    }
    out[16] = '\0';
}

static void show_color(uint16_t* fb1, uint16_t* fb2, int w, int h, uint16_t color) {
    char label[17];
    fill_screen_u16(fb1, w, h, color);
    fill_screen_u16(fb2, w, h, color);
    u16_to_binary_string(color, label);
    fb_draw_text_2fb(fb1, fb2, w, h, label, RGB565_MAGENTA, 4, FB_ALIGN_CENTER);
    vTaskDelay(pdMS_TO_TICKS(1000));
}

static void lcd_bitwalk_test(esp_lcd_panel_handle_t panel_handle, int w, int h) {
    const char* TAG = "BITWALK";

    void* fb1_void = NULL;
    void* fb2_void = NULL;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &fb1_void, &fb2_void));

    // Cast void* to uint16_t* because the frame buffer is RGB565 pixels (16-bit each)
    uint16_t* fb1 = (uint16_t*)fb1_void;
    uint16_t* fb2 = (uint16_t*)fb2_void;

    char label[17];

    ESP_LOGI(TAG, "Cleared to white, starting bit-walk in 1s...");
    fill_screen_u16(fb1, w, h, RGB565_WHITE);
    fill_screen_u16(fb2, w, h, RGB565_WHITE);
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "Mid-red  0x%04X", RGB565_REDMID);
    show_color(fb1, fb2, w, h, RGB565_REDMID);
    ESP_LOGI(TAG, "Full-red 0x%04X", RGB565_RED);
    show_color(fb1, fb2, w, h, RGB565_RED);
    ESP_LOGI(TAG, "Mid-grn  0x%04X", RGB565_GREENMID);
    show_color(fb1, fb2, w, h, RGB565_GREENMID);
    ESP_LOGI(TAG, "Full-grn 0x%04X", RGB565_GREEN);
    show_color(fb1, fb2, w, h, RGB565_GREEN);
    ESP_LOGI(TAG, "Mid-blu  0x%04X", RGB565_BLUEMID);
    show_color(fb1, fb2, w, h, RGB565_BLUEMID);
    ESP_LOGI(TAG, "Full-blu 0x%04X", RGB565_BLUE);
    show_color(fb1, fb2, w, h, RGB565_BLUE);

    for (int bit = 0; bit < 16; bit++) {        // bit=12
        uint16_t pixel = (uint16_t)(1u << bit); // pixel=4096
        u16_to_binary_string(pixel, label);     // label=0001000000000000
        ESP_LOGI(TAG, "bit=%2d  pixel=0x%04X  binary=%s", bit, pixel, label);
        show_color(fb1, fb2, w, h, pixel);
    }
}

void demo_bitwalk(esp_lcd_panel_handle_t panel_handle) {
    // Non-LVGL test.
    lcd_bitwalk_test(panel_handle, BOARD_LCD_HRES, BOARD_LCD_VRES);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Draw diagonal line and colored squares.
    ESP_LOGI(TAG, "Test shapes");
    uint16_t px = RGB565_RED;
    for (int i = 0; i < 480; ++i) {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, i, i, i + 1, i + 1, &px));
    }
    for (int i = 0; i < 480; i++) {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, i, 239, i + 1, 240, &px));
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, i, 214, i + 1, 215, &px));
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, i, 264, i + 1, 265, &px));
    }
    enum { W = 50, H = 50 };
    static uint16_t block[W * H];
    const uint16_t colors[] = {RGB565_WHITE, RGB565_BLACK, RGB565_RED, RGB565_GREEN, RGB565_BLUE};
    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
        fill_screen_u16(block, W, H, colors[i]);
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, W * i, 0, W * (i + 1), H, block));
    }
}
