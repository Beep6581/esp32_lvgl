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
#define SCREEN_DIAGNOSTIC_ANIMATION_MS 1056
#define TIMING_BUTTON_WIDTH 72
#define TIMING_BUTTON_HEIGHT 42
#define TIMING_BUTTON_GAP 14
#define TIMING_GRID_COLUMNS 3
#define TIMING_GRID_BUTTON_WIDTH 100
#define TIMING_GRID_COLUMN_GAP 8
#define TIMING_GRID_ROW_GAP 6
#define TIMING_GRID_TO_BUTTON_GAP 10

static const char* TAG = "screen_diagnostics";
static uint8_t* s_gradient_buffer;
static const display_timing_mode_t s_standard_timing_mode[] = {
    DISPLAY_TIMING_BS,
    DISPLAY_TIMING_WT,
};
static const char* const s_standard_timing_label[] = {"BS", "WT"};
static const display_timing_mode_t s_diagnostic_timing_mode[] = {
    DISPLAY_TIMING_MIN_HBP,   DISPLAY_TIMING_MIN_BAL, DISPLAY_TIMING_MIN_HSYNC, DISPLAY_TIMING_MID_HBP,   DISPLAY_TIMING_MID_BAL,
    DISPLAY_TIMING_MID_HSYNC, DISPLAY_TIMING_MAX_HBP, DISPLAY_TIMING_MAX_BAL,   DISPLAY_TIMING_MAX_HSYNC,
};
static const char* const s_diagnostic_timing_label[] = {
    "MIN HBP", "MIN BAL", "MIN HSYNC", "MID HBP", "MID BAL", "MID HSYNC", "MAX HBP", "MAX BAL", "MAX HSYNC",
};

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

static lv_obj_t* color_bar_create(lv_obj_t* parent, int32_t x, int32_t width, uint32_t color) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_pos(bar, x, 0);
    lv_obj_set_size(bar, width, BOARD_LCD_VRES);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(bar, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    return bar;
}

static void color_bar_set_x(void* bar, int32_t x) {
    lv_obj_set_x(bar, x);
}

static void color_bars_create(lv_obj_t* parent) {
    const int32_t bar_width = BOARD_LCD_HRES / SCREEN_DIAGNOSTIC_BAR_COUNT;

    color_bar_create(parent, 0, bar_width, 0xFF0000);
    color_bar_create(parent, BOARD_LCD_HRES - bar_width, bar_width, 0x0000FF);
    lv_obj_t* green_bar = color_bar_create(parent, 0, bar_width, 0x00FF00);

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, green_bar);
    lv_anim_set_exec_cb(&animation, color_bar_set_x);
    lv_anim_set_values(&animation, 0, BOARD_LCD_HRES - bar_width);
    lv_anim_set_duration(&animation, SCREEN_DIAGNOSTIC_ANIMATION_MS);
    lv_anim_set_reverse_duration(&animation, SCREEN_DIAGNOSTIC_ANIMATION_MS);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&animation, lv_anim_path_linear);
    lv_anim_start(&animation);

    const display_rgb_timing_t* active_timing = display_get_rgb_timing();
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text_fmt(label, "mode = %s", active_timing->name);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 8);
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

    const display_rgb_timing_t* active_timing = display_get_rgb_timing();
    const esp_lcd_rgb_timing_t* timing = &active_timing->timing;
    char pclk_text[48];
    if ((timing->pclk_hz % 1000000U) == 0U) {
        snprintf(pclk_text, sizeof(pclk_text), "%lu * 1000 * 1000", (unsigned long)(timing->pclk_hz / 1000000U));
    } else {
        snprintf(pclk_text, sizeof(pclk_text), "%lu", (unsigned long)timing->pclk_hz);
    }

    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text_fmt(label,
                          "mode = %s\n"
                          "pclk_hz = %s\n"
                          "hsync_pulse_width = %lu\n"
                          "hsync_back_porch = %lu\n"
                          "hsync_front_porch = %lu\n"
                          "vsync_pulse_width = %lu\n"
                          "vsync_back_porch = %lu\n"
                          "vsync_front_porch = %lu",
                          active_timing->name, pclk_text, (unsigned long)timing->hsync_pulse_width, (unsigned long)timing->hsync_back_porch, (unsigned long)timing->hsync_front_porch,
                          (unsigned long)timing->vsync_pulse_width, (unsigned long)timing->vsync_back_porch, (unsigned long)timing->vsync_front_porch);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -30);
}

static void timing_button_cb(lv_event_t* e) {
    const display_timing_mode_t* mode = lv_event_get_user_data(e);
    if (mode == NULL) {
        return;
    }

    const esp_err_t err = display_set_timing_mode_and_restart(*mode);
    ESP_LOGE(TAG, "failed to save timing mode: %s", esp_err_to_name(err));
}

static void timing_button_create(lv_obj_t* parent, const display_timing_mode_t* mode, const char* text, int32_t x, int32_t y, int32_t width, int32_t height) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0xC0C0C0), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_add_event_cb(button, timing_button_cb, LV_EVENT_CLICKED, (void*)mode);

    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xC0C0C0), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_center(label);
}

static void timing_buttons_create(lv_obj_t* parent) {
    const size_t standard_count = sizeof(s_standard_timing_mode) / sizeof(s_standard_timing_mode[0]);
    const int32_t standard_width = (TIMING_BUTTON_WIDTH * (int32_t)standard_count) + (TIMING_BUTTON_GAP * ((int32_t)standard_count - 1));
    const int32_t standard_start_x = (BOARD_LCD_HRES - standard_width) / 2;
    const int32_t standard_y = BOARD_LCD_VRES - TIMING_BUTTON_HEIGHT - 14;

    for (size_t i = 0; i < standard_count; i++) {
        timing_button_create(parent, &s_standard_timing_mode[i], s_standard_timing_label[i], standard_start_x + (int32_t)i * (TIMING_BUTTON_WIDTH + TIMING_BUTTON_GAP), standard_y, TIMING_BUTTON_WIDTH,
                             TIMING_BUTTON_HEIGHT);
    }

    const size_t diagnostic_count = sizeof(s_diagnostic_timing_mode) / sizeof(s_diagnostic_timing_mode[0]);
    const int32_t grid_width = (TIMING_GRID_BUTTON_WIDTH * TIMING_GRID_COLUMNS) + (TIMING_GRID_COLUMN_GAP * (TIMING_GRID_COLUMNS - 1));
    const int32_t grid_rows = (int32_t)diagnostic_count / TIMING_GRID_COLUMNS;
    const int32_t grid_height = (TIMING_BUTTON_HEIGHT * grid_rows) + (TIMING_GRID_ROW_GAP * (grid_rows - 1));
    const int32_t grid_start_x = (BOARD_LCD_HRES - grid_width) / 2;
    const int32_t grid_start_y = standard_y - TIMING_GRID_TO_BUTTON_GAP - grid_height;

    for (size_t i = 0; i < diagnostic_count; i++) {
        const int32_t column = (int32_t)i % TIMING_GRID_COLUMNS;
        const int32_t row = (int32_t)i / TIMING_GRID_COLUMNS;
        timing_button_create(parent, &s_diagnostic_timing_mode[i], s_diagnostic_timing_label[i], grid_start_x + column * (TIMING_GRID_BUTTON_WIDTH + TIMING_GRID_COLUMN_GAP),
                             grid_start_y + row * (TIMING_BUTTON_HEIGHT + TIMING_GRID_ROW_GAP), TIMING_GRID_BUTTON_WIDTH, TIMING_BUTTON_HEIGHT);
    }
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
    timing_buttons_create(gradient_page);
    lv_tabview_set_active(tabview, 1, LV_ANIM_OFF);

    lvgl_port_unlock();
}
