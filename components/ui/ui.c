#include "ui.h"

#include "air_quality.h"

#include <stdio.h>
#include <stddef.h>

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char* TAG = "ui";

static lv_obj_t* s_lbl_title;
static lv_obj_t* s_lbl_values;
static lv_obj_t* s_lbl_status;
static lv_timer_t* s_timer;

static void format_milli_1dp(char* out, size_t out_len, int32_t milli) {
    if (out == NULL || out_len == 0) {
        return;
    }

    // Convert milli-units to 0.1 units with rounding, without using floats.
    // Example: 23456 mdegC -> 23.5 C
    int32_t tenths = 0;
    if (milli >= 0) {
        tenths = (milli + 50) / 100;
    } else {
        tenths = (milli - 50) / 100;
    }

    int32_t whole = tenths / 10;
    int32_t frac = tenths % 10;
    if (frac < 0) {
        frac = -frac;
    }

    // Use standard snprintf; LVGL float formatting is disabled in lv_conf.h.
    snprintf(out, out_len, "%ld.%ld", (long)whole, (long)frac);
}

static void ui_timer_cb(lv_timer_t* t) {
    (void)t;

    air_quality_data_t d = air_quality_get_latest();
    uint32_t now = (uint32_t)esp_log_timestamp();

    const uint8_t scd4x_addr = air_quality_get_scd4x_addr();
    const uint8_t stcc4_addr = air_quality_get_stcc4_addr();

    if (!d.has_co2 && !d.has_rht && !d.has_stcc4) {
        lv_label_set_text(s_lbl_values, "CO2: ...\nT: ...\nRH: ...\nSTCC4: ...");
        lv_label_set_text_fmt(s_lbl_status,
                              "SCD4x: 0x%02X\nSTCC4: 0x%02X  (awaiting sample)",
                              (unsigned)scd4x_addr, (unsigned)stcc4_addr);
        return;
    }

    char t_buf[16] = {0};
    char rh_buf[16] = {0};

    if (d.has_rht) {
        format_milli_1dp(t_buf, sizeof(t_buf), d.temperature_m_deg_c);
        format_milli_1dp(rh_buf, sizeof(rh_buf), d.humidity_m_percent_rh);
    } else {
        snprintf(t_buf, sizeof(t_buf), "...");
        snprintf(rh_buf, sizeof(rh_buf), "...");
    }

    if (d.has_stcc4) {
        lv_label_set_text_fmt(s_lbl_values,
                              "CO2: %u ppm\nT: %s C\nRH: %s %%\nSTCC4: %u ppm",
                              (unsigned)d.co2_ppm, t_buf, rh_buf,
                              (unsigned)d.stcc4_co2_ppm);
    } else {
        lv_label_set_text_fmt(s_lbl_values,
                              "CO2: %u ppm\nT: %s C\nRH: %s %%\nSTCC4: ...",
                              (unsigned)d.co2_ppm, t_buf, rh_buf);
    }

    uint32_t age_ms = 0;
    if (d.last_co2_ms != 0 && now >= d.last_co2_ms) {
        age_ms = now - d.last_co2_ms;
    }

    uint32_t stcc4_age_ms = 0;
    if (d.last_stcc4_ms != 0 && now >= d.last_stcc4_ms) {
        stcc4_age_ms = now - d.last_stcc4_ms;
    }

    lv_label_set_text_fmt(s_lbl_status,
                          "SCD4x: 0x%02X age: %u.%us\nSTCC4: 0x%02X age: %u.%us",
                          (unsigned)scd4x_addr,
                          (unsigned)(age_ms / 1000), (unsigned)((age_ms % 1000) / 100),
                          (unsigned)stcc4_addr,
                          (unsigned)(stcc4_age_ms / 1000), (unsigned)((stcc4_age_ms % 1000) / 100));
}

void ui_init(lv_display_t* disp) {
    ESP_LOGI(TAG, "ui_init called");

    lvgl_port_lock(0);

    lv_obj_t* scr = lv_disp_get_scr_act(disp);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101010), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clean(scr);

    s_lbl_title = lv_label_create(scr);
    lv_label_set_text(s_lbl_title, "Air quality (SCD4x + STCC4)");
    lv_obj_set_style_text_color(s_lbl_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_lbl_title, LV_ALIGN_TOP_LEFT, 8, 8);

    s_lbl_values = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_values, lv_color_hex(0x00FF00), 0);
    lv_label_set_text(s_lbl_values, "CO2: ...\nT: ...\nRH: ...\nSTCC4: ...");
    lv_obj_align(s_lbl_values, LV_ALIGN_TOP_LEFT, 8, 40);

    s_lbl_status = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(s_lbl_status, "SCD4x: ...  STCC4: ...");
    lv_obj_align(s_lbl_status, LV_ALIGN_BOTTOM_LEFT, 8, -8);

    s_timer = lv_timer_create(ui_timer_cb, 1000, NULL);

    lvgl_port_unlock();
}
