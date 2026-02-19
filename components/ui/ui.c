#include "ui.h"

#include "air_quality.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char* TAG = "ui";

static lv_obj_t* s_lbl_title;
static lv_obj_t* s_lbl_values;
static lv_obj_t* s_lbl_status;
static lv_timer_t* s_timer;

static void ui_timer_cb(lv_timer_t* t) {
    (void)t;

    air_quality_data_t d = air_quality_get_latest();
    uint32_t now = (uint32_t)esp_log_timestamp();

    if (!d.has_co2 && !d.has_rht) {
        lv_label_set_text(s_lbl_values, "CO2: ...\nT: ...\nRH: ...");
        lv_label_set_text_fmt(s_lbl_status, "SCD4x addr: 0x%02X  (waiting for first sample)",
                              (unsigned)air_quality_get_scd4x_addr());
        return;
    }

    // Temperature / RH are milli-units.
    double t_c = (double)d.temperature_m_deg_c / 1000.0;
    double rh = (double)d.humidity_m_percent_rh / 1000.0;

    lv_label_set_text_fmt(s_lbl_values,
                          "CO2: %u ppm\nT: %0.2f C\nRH: %0.2f %%",
                          (unsigned)d.co2_ppm, t_c, rh);

    uint32_t age_ms = 0;
    if (d.last_co2_ms != 0 && now >= d.last_co2_ms) {
        age_ms = now - d.last_co2_ms;
    }

    lv_label_set_text_fmt(s_lbl_status,
                          "SCD4x addr: 0x%02X  last update: %u.%us ago",
                          (unsigned)air_quality_get_scd4x_addr(),
                          (unsigned)(age_ms / 1000), (unsigned)((age_ms % 1000) / 100));
}

void ui_init(lv_display_t* disp) {
    ESP_LOGI(TAG, "ui_init called");

    lvgl_port_lock(0);

    lv_obj_t* scr = lv_disp_get_scr_act(disp);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101010), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clean(scr);

    s_lbl_title = lv_label_create(scr);
    lv_label_set_text(s_lbl_title, "Air quality (SCD41)");
    lv_obj_set_style_text_color(s_lbl_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_lbl_title, LV_ALIGN_TOP_LEFT, 8, 8);

    s_lbl_values = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_values, lv_color_hex(0x00FF00), 0);
    lv_label_set_text(s_lbl_values, "CO2: ...\nT: ...\nRH: ...");
    lv_obj_align(s_lbl_values, LV_ALIGN_TOP_LEFT, 8, 40);

    s_lbl_status = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_text(s_lbl_status, "SCD4x addr: ...");
    lv_obj_align(s_lbl_status, LV_ALIGN_BOTTOM_LEFT, 8, -8);

    if (s_timer) {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }
    s_timer = lv_timer_create(ui_timer_cb, 1000, NULL);

    lvgl_port_unlock();
}
