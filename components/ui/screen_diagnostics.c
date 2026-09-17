#include "ui.h"

#include "board.h"

#include <stddef.h>
#include <stdint.h>

#include "esp_lvgl_port.h"
#include "lvgl.h"

#define SCREEN_DIAGNOSTIC_BAR_COUNT 3

void ui_screen_diagnostics_init(lv_display_t* disp) {
    static const uint32_t bar_color[SCREEN_DIAGNOSTIC_BAR_COUNT] = {
        0xFF0000,
        0x00FF00,
        0x0000FF,
    };
    const int32_t bar_width = BOARD_LCD_HRES / SCREEN_DIAGNOSTIC_BAR_COUNT;

    lvgl_port_lock(0);

    lv_obj_t* screen = lv_display_get_screen_active(disp);
    lv_obj_clean(screen);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(screen, 0, 0);

    for (size_t i = 0; i < SCREEN_DIAGNOSTIC_BAR_COUNT; i++) {
        lv_obj_t* bar = lv_obj_create(screen);
        lv_obj_set_pos(bar, (int32_t)i * bar_width, 0);
        lv_obj_set_size(bar, bar_width, BOARD_LCD_VRES);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(bar, lv_color_hex(bar_color[i]), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 0, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
    }

    lvgl_port_unlock();
}
