#include "ui.h"

#include "air_quality.h"

#include <stdio.h>
#include <stddef.h>

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char* TAG = "ui";

static lv_obj_t* s_lbl_title;
static lv_obj_t* s_table;
static lv_timer_t* s_timer;

static void format_milli_1dp(char* out, size_t out_len, int32_t milli) {
    if (out == NULL || out_len == 0) {
        return;
    }

    // Convert milli-units to 0.1 units with rounding, without using floats.
    // Example: 23456 mdegC -> 23.5
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

    snprintf(out, out_len, "%ld.%ld", (long)whole, (long)frac);
}

static void table_set_str(uint16_t row, uint16_t col, const char* s) {
    if (s_table == NULL) {
        return;
    }
    lv_table_set_cell_value(s_table, row, col, (s != NULL) ? s : "");
}

static void table_set_u16(uint16_t row, uint16_t col, bool present, uint16_t val) {
    if (!present) {
        table_set_str(row, col, "...");
        return;
    }

    char buf[16] = {0};
    snprintf(buf, sizeof(buf), "%u", (unsigned)val);
    table_set_str(row, col, buf);
}

static void table_set_rht(uint16_t row, uint16_t col_t, uint16_t col_rh,
                          bool present, int32_t t_m, int32_t rh_m) {
    if (!present) {
        table_set_str(row, col_t, "...");
        table_set_str(row, col_rh, "...");
        return;
    }

    char t_buf[16] = {0};
    char rh_buf[16] = {0};
    format_milli_1dp(t_buf, sizeof(t_buf), t_m);
    format_milli_1dp(rh_buf, sizeof(rh_buf), rh_m);
    table_set_str(row, col_t, t_buf);
    table_set_str(row, col_rh, rh_buf);
}

static void ui_timer_cb(lv_timer_t* t) {
    (void)t;

    air_quality_data_t d = air_quality_get_latest();

    // Header row (0) is static. Rows:
    // 1 = SCD41, 2 = STCC4, 3 = SHT20

    // SCD41
    if (d.scd41_detected) {
        if (d.scd41_asc_enabled) {
            table_set_str(1, 1, "yes (ASC)");
        } else {
            table_set_str(1, 1, "yes");
        }
    } else {
        table_set_str(1, 1, "no");
    }
    table_set_u16(1, 2, d.scd41_detected && d.scd41_has_co2, d.scd41_co2_ppm);
    table_set_rht(1, 3, 4, d.scd41_detected && d.scd41_has_rht,
                  d.scd41_temperature_m_deg_c, d.scd41_humidity_m_percent_rh);

    // STCC4
    table_set_str(2, 1, d.stcc4_detected ? "yes" : "no");
    table_set_u16(2, 2, d.stcc4_detected && d.stcc4_has_co2, d.stcc4_co2_ppm);
    table_set_rht(2, 3, 4, d.stcc4_detected && d.stcc4_has_rht,
                  d.stcc4_temperature_m_deg_c, d.stcc4_humidity_m_percent_rh);

    // SHT20
    table_set_str(3, 1, d.sht20_detected ? "yes" : "no");
    table_set_str(3, 2, "--"); // no CO2
    table_set_rht(3, 3, 4, d.sht20_detected && d.sht20_has_rht,
                  d.sht20_temperature_m_deg_c, d.sht20_humidity_m_percent_rh);
}

void ui_init(lv_display_t* disp) {
    ESP_LOGI(TAG, "ui_init called");

    lvgl_port_lock(0);

    lv_obj_t* scr = lv_disp_get_scr_act(disp);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101010), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clean(scr);

    s_lbl_title = lv_label_create(scr);
    lv_label_set_text(s_lbl_title, "Air quality");
    lv_obj_set_style_text_color(s_lbl_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_lbl_title, LV_ALIGN_TOP_LEFT, 8, 8);

    s_table = lv_table_create(scr);
    lv_obj_set_style_text_font(s_table, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_table, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_bg_opa(s_table, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_table, lv_color_hex(0x404040), 0);
    lv_obj_set_style_border_width(s_table, 1, 0);

    // Empty line after title -> start table a bit lower.
    lv_obj_align(s_table, LV_ALIGN_TOP_LEFT, 8, 40);

    lv_table_set_col_cnt(s_table, 5);
    lv_table_set_row_cnt(s_table, 4);

    // Column widths (tweak later if you want).
    lv_table_set_col_width(s_table, 0, 90); // Sensor
    lv_table_set_col_width(s_table, 1, 110); // Detected
    lv_table_set_col_width(s_table, 2, 90); // CO2
    lv_table_set_col_width(s_table, 3, 80); // T
    lv_table_set_col_width(s_table, 4, 80); // RH

    // Header
    table_set_str(0, 0, "Sensor");
    table_set_str(0, 1, "Detected");
    table_set_str(0, 2, "CO2");
    table_set_str(0, 3, "T");
    table_set_str(0, 4, "RH");

    // Sensor names (static)
    table_set_str(1, 0, "SCD41");
    table_set_str(2, 0, "STCC4");
    table_set_str(3, 0, "SHT20");

    // Fill defaults
    table_set_str(1, 1, "no");
    table_set_str(2, 1, "no");
    table_set_str(3, 1, "no");

    table_set_str(1, 2, "...");
    table_set_str(2, 2, "...");
    table_set_str(3, 2, "--");

    table_set_str(1, 3, "...");
    table_set_str(1, 4, "...");
    table_set_str(2, 3, "...");
    table_set_str(2, 4, "...");
    table_set_str(3, 3, "...");
    table_set_str(3, 4, "...");

    // 1 Hz UI update
    s_timer = lv_timer_create(ui_timer_cb, 1000, NULL);

    lvgl_port_unlock();
}
