#include "ui.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char *TAG = "ui";

static lv_obj_t *s_box;
static int s_w, s_h;
static int s_x;

void ui_init(lv_display_t* disp)
{
    ESP_LOGI(TAG, "ui_init called, disp=%p", disp);

    lvgl_port_lock(0);

    int w = lv_display_get_horizontal_resolution(disp);
    int h = lv_display_get_vertical_resolution(disp);

    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Create a full-screen root container
    lv_obj_t *root = lv_obj_create(scr);
    lv_obj_set_size(root, w, h);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    // Make it transparent so the bars show
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_layout(root, LV_LAYOUT_NONE);

    static const uint32_t colors[] = { 0xFFFFFF, 0x000000, 0xFF0000, 0x00FF00, 0x0000FF };
    int bar_w = w / 5;

    // Draw colored bars
    for (int i = 0; i < 5; ++i) {
        lv_obj_t *r = lv_obj_create(root);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, bar_w, h);
        lv_obj_set_pos(r, i * bar_w, 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(r, lv_color_hex(colors[i]), 0);
        lv_obj_move_background(r);
    }

    lv_obj_t *lbl = lv_label_create(root);
    lv_label_set_text(lbl, "Hello world!");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFF00FF), 0);
    lv_obj_center(lbl);

    lvgl_port_unlock();
}

static void anim_cb(lv_timer_t *t)
{
    (void)t;
    s_x += 5;
    if (s_x > (s_w - 40)) s_x = 0;
    lv_obj_set_pos(s_box, s_x, (s_h / 2) - 20);
}

void animated_box(lv_display_t *disp)
{
    ESP_LOGI(TAG, "ui_init called, disp=%p", disp);

    lvgl_port_lock(0);

    s_w = lv_display_get_horizontal_resolution(disp);
    s_h = lv_display_get_vertical_resolution(disp);
    s_x = 0;

    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *root = lv_obj_create(scr);
    lv_obj_set_size(root, s_w, s_h);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_layout(root, LV_LAYOUT_NONE);

    s_box = lv_obj_create(root);
    lv_obj_set_size(s_box, 40, 40);
    lv_obj_set_pos(s_box, 0, (s_h / 2) - 20);
    lv_obj_set_style_bg_opa(s_box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_box, lv_color_hex(0x00FF00), 0);

    lv_timer_create(anim_cb, 30, NULL);

    lvgl_port_unlock();
}
