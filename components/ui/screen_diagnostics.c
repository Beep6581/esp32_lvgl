#include "ui.h"

#include "board.h"
#include "display.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#define SCREEN_DIAGNOSTIC_BAR_COUNT 3

static const char* TAG = "screen_diagnostics";
static uint8_t* s_gradient_buffer;

static uint8_t blend_channel(uint8_t from, uint8_t to, uint32_t position, uint32_t distance) {
    return (uint8_t)((from * (distance - position) + to * position) / distance);
}

static uint32_t quadratic_scale(uint32_t position, uint32_t distance) {
    return position * position / distance;
}

static void screen_page_style(lv_obj_t* page) {
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(page, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
}

static void color_bars_create(lv_obj_t* parent) {
    static const uint32_t bar_color[SCREEN_DIAGNOSTIC_BAR_COUNT] = {
        0xFF0000,
        0x00FF00,
        0x0000FF,
    };
    const int32_t bar_width = BOARD_LCD_HRES / SCREEN_DIAGNOSTIC_BAR_COUNT;

    for (size_t i = 0; i < SCREEN_DIAGNOSTIC_BAR_COUNT; i++) {
        lv_obj_t* bar = lv_obj_create(parent);
        lv_obj_set_pos(bar, (int32_t)i * bar_width, 0);
        lv_obj_set_size(bar, bar_width, BOARD_LCD_VRES);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(bar, lv_color_hex(bar_color[i]), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 0, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
    }
}

static void hue_gradient_create(lv_obj_t* parent) {
    const uint32_t width = BOARD_LCD_HRES;
    const uint32_t height = BOARD_LCD_VRES;
    const uint32_t middle_y = height / 2U;
    const uint32_t bottom_y = height - 1U;
    const uint32_t stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_RGB565);

    if (s_gradient_buffer == NULL) {
        s_gradient_buffer = heap_caps_malloc(stride * height, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (s_gradient_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate hue gradient buffer");
        return;
    }

    for (uint32_t x = 0; x < width; x++) {
        uint16_t hue = (uint16_t)((x * 360U) / (width - 1U));
        if (hue == 360U) {
            hue = 0;
        }
        const lv_color_t saturated = lv_color_hsv_to_rgb(hue, 100, 100);

        for (uint32_t y = 0; y < height; y++) {
            uint8_t red;
            uint8_t green;
            uint8_t blue;

            if (y <= middle_y) {
                const uint32_t remaining = middle_y - y;
                const uint32_t position = middle_y - quadratic_scale(remaining, middle_y);
                red = blend_channel(255, saturated.red, position, middle_y);
                green = blend_channel(255, saturated.green, position, middle_y);
                blue = blend_channel(255, saturated.blue, position, middle_y);
            } else {
                const uint32_t distance = bottom_y - middle_y;
                const uint32_t position = quadratic_scale(y - middle_y, distance);
                red = blend_channel(saturated.red, 0, position, distance);
                green = blend_channel(saturated.green, 0, position, distance);
                blue = blend_channel(saturated.blue, 0, position, distance);
            }

            uint16_t* row = (uint16_t*)(s_gradient_buffer + y * stride);
            row[x] = lv_color_to_u16(lv_color_make(red, green, blue));
        }
    }

    lv_obj_t* canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, s_gradient_buffer, width, height, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(canvas, 0, 0);

    const esp_lcd_rgb_timing_t* timing = display_get_rgb_timing();
    char pclk_text[48];
    if ((timing->pclk_hz % 1000000U) == 0U) {
        snprintf(pclk_text, sizeof(pclk_text), "%lu * 1000 * 1000", (unsigned long)(timing->pclk_hz / 1000000U));
    } else {
        snprintf(pclk_text, sizeof(pclk_text), "%lu", (unsigned long)timing->pclk_hz);
    }

    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text_fmt(label,
                          "pclk_hz = %s\n"
                          "hsync_pulse_width = %lu\n"
                          "hsync_back_porch = %lu\n"
                          "hsync_front_porch = %lu\n"
                          "vsync_pulse_width = %lu\n"
                          "vsync_back_porch = %lu\n"
                          "vsync_front_porch = %lu",
                          pclk_text, (unsigned long)timing->hsync_pulse_width, (unsigned long)timing->hsync_back_porch, (unsigned long)timing->hsync_front_porch,
                          (unsigned long)timing->vsync_pulse_width, (unsigned long)timing->vsync_back_porch, (unsigned long)timing->vsync_front_porch);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_center(label);
}

void ui_screen_diagnostics_init(lv_display_t* disp) {
    lvgl_port_lock(0);

    lv_obj_t* screen = lv_display_get_screen_active(disp);
    lv_obj_clean(screen);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(screen, 0, 0);

    lv_obj_t* tabview = lv_tabview_create(screen);
    lv_obj_set_size(tabview, BOARD_LCD_HRES, BOARD_LCD_VRES);
    lv_obj_set_style_border_width(tabview, 0, 0);
    lv_obj_set_style_pad_all(tabview, 0, 0);
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tabview, 0);

    lv_obj_t* content = lv_tabview_get_content(tabview);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_pad_column(content, 0, 0);

    lv_obj_t* color_bars_page = lv_tabview_add_tab(tabview, "RGB");
    lv_obj_t* gradient_page = lv_tabview_add_tab(tabview, "Hue");
    screen_page_style(color_bars_page);
    screen_page_style(gradient_page);
    color_bars_create(color_bars_page);
    hue_gradient_create(gradient_page);
    lv_tabview_set_active(tabview, 1, LV_ANIM_OFF);

    lvgl_port_unlock();
}
