#include "board.h"
#include "rgb565.h"
#include "fb_text.h"

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

    uint16_t color;
    color = RGB565_REDMID;
    ESP_LOGI(TAG, "Mid-red  0x%04X", color);
    fill_screen_u16(fb1, w, h, color);
    fill_screen_u16(fb2, w, h, color);
    // snprintf(label, sizeof(label), "0x%04X", (unsigned)color);
    u16_to_binary_string(color, label);
    fb_draw_text_2fb(fb1, fb2, w, h, label, RGB565_MAGENTA, 4, FB_FONT_5X7, FB_ALIGN_CENTER);
    vTaskDelay(pdMS_TO_TICKS(1000));

    color = RGB565_RED;
    ESP_LOGI(TAG, "Full-red 0x%04X", color);
    fill_screen_u16(fb1, w, h, 0xF800);
    fill_screen_u16(fb2, w, h, 0xF800);
    u16_to_binary_string(color, label);
    fb_draw_text_2fb(fb1, fb2, w, h, label, RGB565_MAGENTA, 4, FB_FONT_5X7, FB_ALIGN_CENTER);
    vTaskDelay(pdMS_TO_TICKS(1000));

    color = RGB565_GREENMID;
    ESP_LOGI(TAG, "Mid-grn  0x%04X", color);
    fill_screen_u16(fb1, w, h, 0x03E0);
    fill_screen_u16(fb2, w, h, 0x03E0);
    u16_to_binary_string(color, label);
    fb_draw_text_2fb(fb1, fb2, w, h, label, RGB565_MAGENTA, 4, FB_FONT_5X7, FB_ALIGN_CENTER);
    vTaskDelay(pdMS_TO_TICKS(1000));

    color = RGB565_GREEN;
    ESP_LOGI(TAG, "Full-grn 0x%04X", color);
    fill_screen_u16(fb1, w, h, 0x07E0);
    fill_screen_u16(fb2, w, h, 0x07E0);
    u16_to_binary_string(color, label);
    fb_draw_text_2fb(fb1, fb2, w, h, label, RGB565_MAGENTA, 4, FB_FONT_5X7, FB_ALIGN_CENTER);
    vTaskDelay(pdMS_TO_TICKS(1000));

    color = RGB565_BLUEMID;
    ESP_LOGI(TAG, "Mid-blu  0x%04X", color);
    fill_screen_u16(fb1, w, h, 0x000F);
    fill_screen_u16(fb2, w, h, 0x000F);
    u16_to_binary_string(color, label);
    fb_draw_text_2fb(fb1, fb2, w, h, label, RGB565_MAGENTA, 4, FB_FONT_5X7, FB_ALIGN_CENTER);
    vTaskDelay(pdMS_TO_TICKS(1000));

    color = RGB565_BLUE;
    ESP_LOGI(TAG, "Full-blu 0x%04X", color);
    fill_screen_u16(fb1, w, h, 0x001F);
    fill_screen_u16(fb2, w, h, 0x001F);
    u16_to_binary_string(color, label);
    fb_draw_text_2fb(fb1, fb2, w, h, label, RGB565_MAGENTA, 4, FB_FONT_5X7, FB_ALIGN_CENTER);
    vTaskDelay(pdMS_TO_TICKS(1000));

    for (int bit = 0; bit < 16; bit++) {        // bit=12
        uint16_t pixel = (uint16_t)(1u << bit); // pixel=4096
        u16_to_binary_string(pixel, label);     // label=0001000000000000
        ESP_LOGI(TAG, "bit=%2d  pixel=0x%04X  binary=%s", bit, pixel, label);
        fill_screen_u16(fb1, w, h, pixel);
        fill_screen_u16(fb2, w, h, pixel);
        u16_to_binary_string(pixel, label);
        fb_draw_text_2fb(fb1, fb2, w, h, label, RGB565_MAGENTA, 4, FB_FONT_5X7, FB_ALIGN_CENTER);
        vTaskDelay(pdMS_TO_TICKS(1000));
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
    for (int i = 0; i < W * H; ++i)
        block[i] = RGB565_WHITE;
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, W * 0, 0, W * 1, H, block));
    for (int i = 0; i < W * H; ++i)
        block[i] = RGB565_BLACK;
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, W * 1, 0, W * 2, H, block));
    for (int i = 0; i < W * H; ++i)
        block[i] = RGB565_RED;
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, W * 2, 0, W * 3, H, block));
    for (int i = 0; i < W * H; ++i)
        block[i] = RGB565_GREEN;
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, W * 3, 0, W * 4, H, block));
    for (int i = 0; i < W * H; ++i)
        block[i] = RGB565_BLUE;
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, W * 4, 0, W * 5, H, block));
}
