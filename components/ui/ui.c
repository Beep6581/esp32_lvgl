#include "ui.h"

#include "esp_lvgl_port.h"
#include "lvgl.h"

void ui_init(lv_display_t* disp) {
    lvgl_port_lock(0);

    lv_obj_t* scr = lv_disp_get_scr_act(disp);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t* lbl = lv_label_create(scr);
    lv_label_set_text(lbl, "Hello world!");
    lv_obj_center(lbl);

    lvgl_port_unlock();
}
