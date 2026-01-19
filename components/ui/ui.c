#include "ui.h"

#include "esp_log.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ui";

typedef struct {
    lv_display_t *disp;

    lv_obj_t *scr;      // active screen for this display (theme-owned)
    lv_obj_t *root;     // the full-screen container

    lv_obj_t *box;      // moving box demo object
    lv_timer_t *timer;  // moving box timer

    int w, h;
    int x;
} ui_state_t;

static ui_state_t s_ui;

#if CONFIG_UI_METRICS
static TaskHandle_t s_metrics_task;
static volatile uint32_t s_anim_steps;

static void ui_metrics_task(void *arg)
{
    (void)arg;
    uint32_t last = lv_tick_get();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        uint32_t now = lv_tick_get();
        uint32_t dt = now - last;
        last = now;

        uint32_t steps = s_anim_steps;
        s_anim_steps = 0;

        ESP_LOGI(TAG, "LVGL tick +%u ms, anim steps=%u/s", (unsigned)dt, (unsigned)steps);
    }
}
#endif

static lv_obj_t *ui_create_root(lv_display_t *disp)
{
    int w = lv_display_get_horizontal_resolution(disp);
    int h = lv_display_get_vertical_resolution(disp);

    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Delete only our content, not theme styling
    lv_obj_clean(scr);

    lv_obj_t *root = lv_obj_create(scr);
    lv_obj_set_size(root, w, h);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    // Root is a transparent, layout-free surface.
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_layout(root, LV_LAYOUT_NONE);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x262626), 0);

    // Save in state
    s_ui.disp = disp;
    s_ui.scr  = scr;
    s_ui.root = root;
    s_ui.w    = w;
    s_ui.h    = h;

    return root;
}

static void ui_destroy(void)
{
    // Stop animation first (timers can reference UI objects)
    if (s_ui.timer) {
        lv_timer_del(s_ui.timer);
        s_ui.timer = NULL;
    }

    // Delete root container (deletes all children: bars/labels/box/etc)
    if (s_ui.root) {
        lv_obj_del(s_ui.root);
        s_ui.root = NULL;
    }

    // Clear other pointers (children of root)
    s_ui.box = NULL;
    s_ui.scr = NULL;
    s_ui.disp = NULL;
    s_ui.w = 0;
    s_ui.h = 0;
    s_ui.x = 0;
}

static void demo_color_bars_create(lv_obj_t *root)
{
    static const uint32_t colors[] = { 0xFFFFFF, 0x000000, 0xFF0000, 0x00FF00, 0x0000FF };
    int bar_w = s_ui.w / 5;

    for (int i = 0; i < 5; ++i) {
        lv_obj_t *r = lv_obj_create(root);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, bar_w, s_ui.h);
        lv_obj_set_pos(r, i * bar_w, 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(r, lv_color_hex(colors[i]), 0);
        lv_obj_move_background(r);
    }

    lv_obj_t *lbl = lv_label_create(root);
    lv_label_set_text(lbl, "Hello world!");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFF00FF), 0);
    lv_obj_center(lbl);
}

static void anim_cb(lv_timer_t *t)
{
    (void)t;

    s_ui.x += 2;
    if (s_ui.x > (s_ui.w - 40)) {
        s_ui.x = 0;
    }

    if (s_ui.box) {
        lv_obj_set_pos(s_ui.box, s_ui.x, (s_ui.h / 2) - 20);
    }

#if CONFIG_UI_METRICS
    s_anim_steps++;
#endif
}

static void demo_moving_box_create(lv_obj_t *root)
{
    s_ui.x = 0;

    s_ui.box = lv_obj_create(root);
    lv_obj_set_size(s_ui.box, 40, 40);
    lv_obj_set_pos(s_ui.box, 0, (s_ui.h / 2) - 20);
    lv_obj_set_style_bg_opa(s_ui.box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_ui.box, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_radius(s_ui.box, 0, 0);
    lv_obj_set_style_border_width(s_ui.box, 0, 0);
    lv_obj_set_style_shadow_width(s_ui.box, 0, 0);

    s_ui.timer = lv_timer_create(anim_cb, 16, NULL);
}

void ui_init(lv_display_t *disp)
{
    ESP_LOGI(TAG, "ui_init called, disp=%p", disp);

    lvgl_port_lock(0);

    // Destroy previous UI if ui_init() is called again
    ui_destroy();

    lv_obj_t *root = ui_create_root(disp);

#if CONFIG_UI_DEMO_COLOR_BARS
    demo_color_bars_create(root);
#endif

#if CONFIG_UI_DEMO_MOVING_BOX
    demo_moving_box_create(root);
#endif

#if CONFIG_UI_METRICS
    if (s_metrics_task == NULL) {
        xTaskCreate(ui_metrics_task, "ui_metrics", 3072, NULL, 1, &s_metrics_task);
    }
#endif

    lvgl_port_unlock();
}

