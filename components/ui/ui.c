#include "ui.h"

#include "air_quality.h"
#include "board.h"

#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char* TAG = "ui";

#define TOUCH_CROSSHAIR_SIZE 19
#define TOUCH_CROSSHAIR_THICKNESS 3

#define PLOT_SAMPLE_PERIOD_MS (10 * 1000U)
#define PLOT_HISTORY_HOURS (24U)
#define PLOT_HISTORY_CAPACITY ((PLOT_HISTORY_HOURS * 60U * 60U * 1000U) / PLOT_SAMPLE_PERIOD_MS)
#define PLOT_SCALE_MAX 1000
#define UI_COLOR_BG 0x101010
#define UI_COLOR_GRID 0x202020
#define UI_COLOR_BORDER 0x303030
#define UI_COLOR_TEXT 0xC0C0C0
#define UI_COLOR_CO2 0x4DA3FF
#define UI_COLOR_TEMP 0xFF6B4A
#define UI_COLOR_RH 0x63D471

#define UI_MARGIN_X 8
#define PLOT_TOP_Y 56
#define PLOT_WIDTH 448
#define PLOT_HEIGHT 258
#define STATS_TABLE_TOP_Y 338
#define STATS_TABLE_WIDTH PLOT_WIDTH
#define STATS_TABLE_HEIGHT 68
#define STATS_COL_PROPERTY_WIDTH 122
#define STATS_COL_VALUE_WIDTH 108

typedef enum {
    SENSOR_SCD41 = 0,
    SENSOR_STCC4,
    SENSOR_SHT20,
    SENSOR_COUNT,
} sensor_id_t;

typedef struct {
    bool detected;
    bool has_co2;
    bool has_rht;
    int32_t co2_ppm;
    int32_t temperature_m_deg_c;
    int32_t humidity_m_percent_rh;
} sensor_history_sample_t;

typedef struct {
    uint32_t ms;
    sensor_history_sample_t sensor[SENSOR_COUNT];
} plot_history_sample_t;

static lv_obj_t* s_tabview;
static lv_obj_t* s_table;
static lv_timer_t* s_ui_timer;
static lv_obj_t* s_crosshair;

static lv_obj_t* s_sensor_selector;
static lv_obj_t* s_chart;
static lv_chart_series_t* s_ser_co2;
static lv_chart_series_t* s_ser_temp;
static lv_chart_series_t* s_ser_rh;
static lv_obj_t* s_lbl_sensor;
static lv_obj_t* s_lbl_x_left;
static lv_obj_t* s_lbl_x_right;
static lv_obj_t* s_stats_table;

static plot_history_sample_t* s_history;
static uint32_t s_history_count;
static uint32_t s_history_next;
static int32_t* s_chart_co2;
static int32_t* s_chart_temp;
static int32_t* s_chart_rh;

static sensor_id_t s_selected_sensor = SENSOR_SCD41;
static bool s_user_selected_sensor;
static uint32_t s_last_sample_ms;

static const char* const s_sensor_selector_map[] = {"SCD41", "STCC4", "SHT20", ""};

static uint16_t clamp_touch_coord(uint16_t val, uint16_t max_size) {
    if (max_size == 0) {
        return 0;
    }
    if (val >= max_size) {
        return max_size - 1;
    }
    return val;
}

static void* plot_alloc(size_t size) {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) {
        p = malloc(size);
    }
    return p;
}

static void format_milli_1dp(char* out, size_t out_len, int32_t milli) {
    if (out == NULL || out_len == 0) {
        return;
    }

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

static void format_duration_hms(char* out, size_t out_len, uint32_t ms) {
    if (out == NULL || out_len == 0) {
        return;
    }

    uint32_t seconds = ms / 1000U;
    const uint32_t hours = seconds / 3600U;
    seconds %= 3600U;
    const uint32_t minutes = seconds / 60U;
    seconds %= 60U;

    snprintf(out, out_len, "%02lu:%02lu:%02lu",
             (unsigned long)hours, (unsigned long)minutes, (unsigned long)seconds);
}

static void format_duration_hm(char* out, size_t out_len, uint32_t ms) {
    if (out == NULL || out_len == 0) {
        return;
    }

    const uint32_t seconds = ms / 1000U;
    const uint32_t hours = seconds / 3600U;
    const uint32_t minutes = (seconds % 3600U) / 60U;

    snprintf(out, out_len, "%02lu:%02lu", (unsigned long)hours, (unsigned long)minutes);
}

static const char* sensor_name(sensor_id_t sensor) {
    switch (sensor) {
    case SENSOR_SCD41:
        return "SCD41";
    case SENSOR_STCC4:
        return "STCC4";
    case SENSOR_SHT20:
        return "SHT20";
    default:
        return "?";
    }
}

static sensor_history_sample_t sample_for_sensor(const air_quality_data_t* d, sensor_id_t sensor) {
    sensor_history_sample_t s = {0};
    if (d == NULL) {
        return s;
    }

    switch (sensor) {
    case SENSOR_SCD41:
        s.detected = d->scd41_detected;
        s.has_co2 = d->scd41_has_co2;
        s.has_rht = d->scd41_has_rht;
        s.co2_ppm = d->scd41_co2_ppm;
        s.temperature_m_deg_c = d->scd41_temperature_m_deg_c;
        s.humidity_m_percent_rh = d->scd41_humidity_m_percent_rh;
        break;
    case SENSOR_STCC4:
        s.detected = d->stcc4_detected;
        s.has_co2 = d->stcc4_has_co2;
        s.has_rht = d->stcc4_has_rht;
        s.co2_ppm = d->stcc4_co2_ppm;
        s.temperature_m_deg_c = d->stcc4_temperature_m_deg_c;
        s.humidity_m_percent_rh = d->stcc4_humidity_m_percent_rh;
        break;
    case SENSOR_SHT20:
        s.detected = d->sht20_detected;
        s.has_co2 = false;
        s.has_rht = d->sht20_has_rht;
        s.temperature_m_deg_c = d->sht20_temperature_m_deg_c;
        s.humidity_m_percent_rh = d->sht20_humidity_m_percent_rh;
        break;
    default:
        break;
    }
    return s;
}

static sensor_id_t default_sensor_for_data(const air_quality_data_t* d) {
    if (d != NULL) {
        if (d->scd41_detected) {
            return SENSOR_SCD41;
        }
        if (d->stcc4_detected) {
            return SENSOR_STCC4;
        }
        if (d->sht20_detected) {
            return SENSOR_SHT20;
        }
    }
    return SENSOR_SCD41;
}

static void selector_set_active(sensor_id_t sensor) {
    if (s_sensor_selector == NULL) {
        return;
    }

    lv_buttonmatrix_clear_button_ctrl_all(s_sensor_selector, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_buttonmatrix_set_selected_button(s_sensor_selector, (uint32_t)sensor);
    lv_buttonmatrix_set_button_ctrl(s_sensor_selector, (uint32_t)sensor,
                                    LV_BUTTONMATRIX_CTRL_CHECKED);
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

static void table_update(const air_quality_data_t* d) {
    if (d == NULL) {
        return;
    }

    if (d->scd41_detected) {
        table_set_str(1, 1, d->scd41_asc_enabled ? "yes (ASC)" : "yes");
    } else {
        table_set_str(1, 1, "no");
    }
    table_set_u16(1, 2, d->scd41_detected && d->scd41_has_co2, d->scd41_co2_ppm);
    table_set_rht(1, 3, 4, d->scd41_detected && d->scd41_has_rht,
                  d->scd41_temperature_m_deg_c, d->scd41_humidity_m_percent_rh);

    table_set_str(2, 1, d->stcc4_detected ? "yes" : "no");
    table_set_u16(2, 2, d->stcc4_detected && d->stcc4_has_co2, d->stcc4_co2_ppm);
    table_set_rht(2, 3, 4, d->stcc4_detected && d->stcc4_has_rht,
                  d->stcc4_temperature_m_deg_c, d->stcc4_humidity_m_percent_rh);

    table_set_str(3, 1, d->sht20_detected ? "yes" : "no");
    table_set_str(3, 2, "--");
    table_set_rht(3, 3, 4, d->sht20_detected && d->sht20_has_rht,
                  d->sht20_temperature_m_deg_c, d->sht20_humidity_m_percent_rh);
}

static bool plot_buffers_init(void) {
    if (s_history != NULL && s_chart_co2 != NULL && s_chart_temp != NULL && s_chart_rh != NULL) {
        return true;
    }

    if (s_history == NULL) {
        s_history = plot_alloc(sizeof(plot_history_sample_t) * PLOT_HISTORY_CAPACITY);
    }
    if (s_chart_co2 == NULL) {
        s_chart_co2 = plot_alloc(sizeof(int32_t) * PLOT_HISTORY_CAPACITY);
    }
    if (s_chart_temp == NULL) {
        s_chart_temp = plot_alloc(sizeof(int32_t) * PLOT_HISTORY_CAPACITY);
    }
    if (s_chart_rh == NULL) {
        s_chart_rh = plot_alloc(sizeof(int32_t) * PLOT_HISTORY_CAPACITY);
    }

    if (s_history == NULL || s_chart_co2 == NULL || s_chart_temp == NULL || s_chart_rh == NULL) {
        ESP_LOGE(TAG, "plot buffer allocation failed (capacity=%lu)",
                 (unsigned long)PLOT_HISTORY_CAPACITY);
        return false;
    }

    for (uint32_t i = 0; i < PLOT_HISTORY_CAPACITY; i++) {
        s_chart_co2[i] = LV_CHART_POINT_NONE;
        s_chart_temp[i] = LV_CHART_POINT_NONE;
        s_chart_rh[i] = LV_CHART_POINT_NONE;
    }

    ESP_LOGI(TAG, "plot history ready: %lu samples (%lu hours at %lu ms)",
             (unsigned long)PLOT_HISTORY_CAPACITY,
             (unsigned long)PLOT_HISTORY_HOURS,
             (unsigned long)PLOT_SAMPLE_PERIOD_MS);
    return true;
}

static uint32_t history_physical_index(uint32_t chronological_index) {
    if (s_history_count < PLOT_HISTORY_CAPACITY) {
        return chronological_index;
    }
    return (s_history_next + chronological_index) % PLOT_HISTORY_CAPACITY;
}

static void history_add_sample(const air_quality_data_t* d, uint32_t now_ms) {
    if (d == NULL || !plot_buffers_init()) {
        return;
    }

    plot_history_sample_t* out = &s_history[s_history_next];
    out->ms = now_ms;
    for (sensor_id_t sensor = SENSOR_SCD41; sensor < SENSOR_COUNT; sensor++) {
        out->sensor[sensor] = sample_for_sensor(d, sensor);
    }

    s_history_next = (s_history_next + 1U) % PLOT_HISTORY_CAPACITY;
    if (s_history_count < PLOT_HISTORY_CAPACITY) {
        s_history_count++;
    }
}

typedef struct {
    bool any;
    int32_t min;
    int32_t max;
    int32_t latest;
} series_stats_t;

static void stats_add(series_stats_t* stats, int32_t value) {
    if (stats == NULL) {
        return;
    }
    if (!stats->any) {
        stats->any = true;
        stats->min = value;
        stats->max = value;
    } else {
        if (value < stats->min) {
            stats->min = value;
        }
        if (value > stats->max) {
            stats->max = value;
        }
    }
    stats->latest = value;
}

static int32_t normalize_value(int32_t value, const series_stats_t* stats) {
    if (stats == NULL || !stats->any) {
        return LV_CHART_POINT_NONE;
    }
    if (stats->min == stats->max) {
        return PLOT_SCALE_MAX / 2;
    }
    const int64_t span = (int64_t)stats->max - (int64_t)stats->min;
    const int64_t offset = (int64_t)value - (int64_t)stats->min;
    return (int32_t)((offset * PLOT_SCALE_MAX) / span);
}

static void format_stat_value(char* out, size_t out_len, const series_stats_t* stats,
                              int32_t value, bool milli_1dp) {
    if (out == NULL || out_len == 0 || stats == NULL || !stats->any) {
        return;
    }

    if (milli_1dp) {
        format_milli_1dp(out, out_len, value);
    } else {
        snprintf(out, out_len, "%ld", (long)value);
    }
}

static void update_metric_row(uint16_t row, const char* name, const char* unit,
                              const series_stats_t* stats, bool milli_1dp) {
    if (s_stats_table == NULL || name == NULL || unit == NULL || stats == NULL) {
        return;
    }

    if (!stats->any) {
        lv_table_set_cell_value(s_stats_table, row, 0, "");
        lv_table_set_cell_value(s_stats_table, row, 1, "");
        lv_table_set_cell_value(s_stats_table, row, 2, "");
        lv_table_set_cell_value(s_stats_table, row, 3, "");
        return;
    }

    char property[16] = {0};
    char latest[16] = {0};
    char min[16] = {0};
    char max[16] = {0};
    snprintf(property, sizeof(property), "%s %s", name, unit);
    format_stat_value(latest, sizeof(latest), stats, stats->latest, milli_1dp);
    format_stat_value(min, sizeof(min), stats, stats->min, milli_1dp);
    format_stat_value(max, sizeof(max), stats, stats->max, milli_1dp);

    lv_table_set_cell_value(s_stats_table, row, 0, property);
    lv_table_set_cell_value(s_stats_table, row, 1, latest);
    lv_table_set_cell_value(s_stats_table, row, 2, min);
    lv_table_set_cell_value(s_stats_table, row, 3, max);
}

static lv_color_t stats_row_color(uint32_t row) {
    switch (row) {
    case 1:
        return lv_color_hex(UI_COLOR_CO2);
    case 2:
        return lv_color_hex(UI_COLOR_TEMP);
    case 3:
        return lv_color_hex(UI_COLOR_RH);
    default:
        return lv_color_hex(UI_COLOR_TEXT);
    }
}

static void stats_table_draw_cb(lv_event_t* e) {
    lv_draw_task_t* draw_task = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t* base_dsc = (lv_draw_dsc_base_t*)lv_draw_task_get_draw_dsc(draw_task);
    if (base_dsc == NULL || base_dsc->part != LV_PART_ITEMS) {
        return;
    }

    lv_draw_label_dsc_t* label_dsc = lv_draw_task_get_label_dsc(draw_task);
    if (label_dsc == NULL) {
        return;
    }

    label_dsc->color = stats_row_color(base_dsc->id1);
}

static void force_text_draw_cb(lv_event_t* e) {
    lv_draw_task_t* draw_task = lv_event_get_draw_task(e);
    lv_draw_label_dsc_t* label_dsc = lv_draw_task_get_label_dsc(draw_task);
    if (label_dsc != NULL) {
        label_dsc->color = lv_color_hex(UI_COLOR_TEXT);
    }
}

static void chart_redraw(void) {
    if (s_chart == NULL || s_chart_co2 == NULL || s_chart_temp == NULL || s_chart_rh == NULL) {
        return;
    }

    series_stats_t co2 = {0};
    series_stats_t temp = {0};
    series_stats_t rh = {0};

    for (uint32_t i = 0; i < s_history_count; i++) {
        const plot_history_sample_t* sample = &s_history[history_physical_index(i)];
        const sensor_history_sample_t* sensor = &sample->sensor[s_selected_sensor];

        if (sensor->detected && sensor->has_co2) {
            stats_add(&co2, sensor->co2_ppm);
        }
        if (sensor->detected && sensor->has_rht) {
            stats_add(&temp, sensor->temperature_m_deg_c);
            stats_add(&rh, sensor->humidity_m_percent_rh);
        }
    }

    for (uint32_t i = 0; i < PLOT_HISTORY_CAPACITY; i++) {
        s_chart_co2[i] = LV_CHART_POINT_NONE;
        s_chart_temp[i] = LV_CHART_POINT_NONE;
        s_chart_rh[i] = LV_CHART_POINT_NONE;
    }

    for (uint32_t i = 0; i < s_history_count; i++) {
        const plot_history_sample_t* sample = &s_history[history_physical_index(i)];
        const sensor_history_sample_t* sensor = &sample->sensor[s_selected_sensor];

        if (sensor->detected && sensor->has_co2) {
            s_chart_co2[i] = normalize_value(sensor->co2_ppm, &co2);
        }
        if (sensor->detected && sensor->has_rht) {
            s_chart_temp[i] = normalize_value(sensor->temperature_m_deg_c, &temp);
            s_chart_rh[i] = normalize_value(sensor->humidity_m_percent_rh, &rh);
        }
    }

    const uint32_t point_count = (s_history_count >= 2U) ? s_history_count : 2U;
    lv_chart_set_point_count(s_chart, point_count);
    lv_chart_hide_series(s_chart, s_ser_co2, !co2.any);
    lv_chart_hide_series(s_chart, s_ser_temp, !temp.any);
    lv_chart_hide_series(s_chart, s_ser_rh, !rh.any);
    lv_chart_refresh(s_chart);

    lv_label_set_text_fmt(s_lbl_sensor, "%s", sensor_name(s_selected_sensor));
    update_metric_row(1, "CO2", "ppm", &co2, false);
    update_metric_row(2, "T", "C", &temp, true);
    update_metric_row(3, "RH", "%", &rh, true);

    if (s_history_count > 0U) {
        char left[16] = {0};
        char right[16] = {0};
        format_duration_hm(left, sizeof(left), s_history[history_physical_index(0)].ms);
        format_duration_hms(right, sizeof(right), s_history[history_physical_index(s_history_count - 1U)].ms);
        lv_label_set_text(s_lbl_x_left, left);
        lv_label_set_text(s_lbl_x_right, right);
    } else {
        lv_label_set_text(s_lbl_x_left, "--:--");
        lv_label_set_text(s_lbl_x_right, "--:--:--");
    }

    selector_set_active(s_selected_sensor);
}

static void sensor_selector_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    const uint32_t selected = lv_buttonmatrix_get_selected_button(obj);
    if (selected >= SENSOR_COUNT) {
        return;
    }

    s_selected_sensor = (sensor_id_t)selected;
    s_user_selected_sensor = true;
    chart_redraw();
}

static void ui_timer_cb(lv_timer_t* t) {
    (void)t;

    air_quality_data_t d = air_quality_get_latest();
    table_update(&d);

    const uint32_t now_ms = (uint32_t)esp_log_timestamp();
    if (s_last_sample_ms == 0U || (now_ms - s_last_sample_ms) >= PLOT_SAMPLE_PERIOD_MS) {
        s_last_sample_ms = now_ms;
        history_add_sample(&d, now_ms);
        if (!s_user_selected_sensor) {
            s_selected_sensor = default_sensor_for_data(&d);
        }
        chart_redraw();
    }
}

static void crosshair_create(lv_obj_t* parent) {
    s_crosshair = lv_obj_create(parent);
    lv_obj_set_size(s_crosshair, TOUCH_CROSSHAIR_SIZE, TOUCH_CROSSHAIR_SIZE);
    lv_obj_remove_flag(s_crosshair, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_crosshair, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_crosshair, 0, 0);
    lv_obj_set_style_pad_all(s_crosshair, 0, 0);
    lv_obj_add_flag(s_crosshair, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* h = lv_obj_create(s_crosshair);
    lv_obj_set_size(h, TOUCH_CROSSHAIR_SIZE, TOUCH_CROSSHAIR_THICKNESS);
    lv_obj_set_style_bg_color(h, lv_color_hex(0xFF4040), 0);
    lv_obj_set_style_border_width(h, 0, 0);
    lv_obj_set_style_radius(h, 0, 0);
    lv_obj_remove_flag(h, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(h);

    lv_obj_t* v = lv_obj_create(s_crosshair);
    lv_obj_set_size(v, TOUCH_CROSSHAIR_THICKNESS, TOUCH_CROSSHAIR_SIZE);
    lv_obj_set_style_bg_color(v, lv_color_hex(0xFF4040), 0);
    lv_obj_set_style_border_width(v, 0, 0);
    lv_obj_set_style_radius(v, 0, 0);
    lv_obj_remove_flag(v, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(v);
}

void ui_touch_set_point(uint16_t x, uint16_t y) {
    const uint16_t clamped_x = clamp_touch_coord(x, BOARD_LCD_HRES);
    const uint16_t clamped_y = clamp_touch_coord(y, BOARD_LCD_VRES);

    lvgl_port_lock(0);

    if (s_crosshair != NULL) {
        lv_obj_clear_flag(s_crosshair, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_crosshair);
        lv_obj_set_pos(s_crosshair,
                       (lv_coord_t)clamped_x - (TOUCH_CROSSHAIR_SIZE / 2),
                       (lv_coord_t)clamped_y - (TOUCH_CROSSHAIR_SIZE / 2));
    }

    lvgl_port_unlock();
}

static void plot_screen_create(lv_obj_t* parent) {
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(parent, lv_color_hex(UI_COLOR_BG), 0);

    s_sensor_selector = lv_buttonmatrix_create(parent);
    lv_buttonmatrix_set_map(s_sensor_selector, s_sensor_selector_map);
    lv_buttonmatrix_set_button_ctrl_all(s_sensor_selector,
                                        LV_BUTTONMATRIX_CTRL_CHECKABLE |
                                            LV_BUTTONMATRIX_CTRL_CLICK_TRIG);
    lv_buttonmatrix_set_one_checked(s_sensor_selector, true);
    lv_obj_set_size(s_sensor_selector, 274, 38);
    lv_obj_align(s_sensor_selector, LV_ALIGN_TOP_LEFT, UI_MARGIN_X, 8);
    lv_obj_set_style_bg_color(s_sensor_selector, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_border_color(s_sensor_selector, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(s_sensor_selector, 0, 0);
    lv_obj_set_style_pad_all(s_sensor_selector, 0, 0);
    lv_obj_set_style_text_font(s_sensor_selector, &lv_font_montserrat_12, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_sensor_selector, lv_color_hex(UI_COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_sensor_selector, lv_color_hex(UI_COLOR_GRID), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_sensor_selector, lv_color_hex(UI_COLOR_BORDER), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(s_sensor_selector, lv_color_hex(UI_COLOR_TEXT), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_sensor_selector, sensor_selector_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_sensor_selector, force_text_draw_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
    lv_obj_add_flag(s_sensor_selector, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

    s_lbl_sensor = lv_label_create(parent);
    lv_obj_set_style_text_color(s_lbl_sensor, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_lbl_sensor, &lv_font_montserrat_14, 0);
    lv_obj_align(s_lbl_sensor, LV_ALIGN_TOP_RIGHT, -12, 18);

    s_chart = lv_chart_create(parent);
    lv_obj_set_size(s_chart, PLOT_WIDTH, PLOT_HEIGHT);
    lv_obj_align(s_chart, LV_ALIGN_TOP_LEFT, UI_MARGIN_X, PLOT_TOP_Y);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, PLOT_SCALE_MAX);
    lv_chart_set_div_line_count(s_chart, 5, 7);
    lv_chart_set_point_count(s_chart, 2);
    lv_obj_set_style_bg_color(s_chart, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_border_color(s_chart, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(s_chart, 1, 0);
    lv_obj_set_style_line_color(s_chart, lv_color_hex(UI_COLOR_GRID), LV_PART_MAIN);
    lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(s_chart, 0, 0, LV_PART_INDICATOR);

    s_ser_co2 = lv_chart_add_series(s_chart, lv_color_hex(UI_COLOR_CO2), LV_CHART_AXIS_PRIMARY_Y);
    s_ser_temp = lv_chart_add_series(s_chart, lv_color_hex(UI_COLOR_TEMP), LV_CHART_AXIS_PRIMARY_Y);
    s_ser_rh = lv_chart_add_series(s_chart, lv_color_hex(UI_COLOR_RH), LV_CHART_AXIS_PRIMARY_Y);

    if (plot_buffers_init()) {
        lv_chart_set_series_ext_y_array(s_chart, s_ser_co2, s_chart_co2);
        lv_chart_set_series_ext_y_array(s_chart, s_ser_temp, s_chart_temp);
        lv_chart_set_series_ext_y_array(s_chart, s_ser_rh, s_chart_rh);
    }

    s_lbl_x_left = lv_label_create(parent);
    lv_obj_set_style_text_color(s_lbl_x_left, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_lbl_x_left, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_lbl_x_left, "--:--");
    lv_obj_align_to(s_lbl_x_left, s_chart, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

    s_lbl_x_right = lv_label_create(parent);
    lv_obj_set_style_text_color(s_lbl_x_right, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_lbl_x_right, &lv_font_montserrat_12, 0);
    lv_obj_set_width(s_lbl_x_right, 120);
    lv_obj_set_style_text_align(s_lbl_x_right, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(s_lbl_x_right, "--:--:--");
    lv_obj_align_to(s_lbl_x_right, s_chart, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 4);

    s_stats_table = lv_table_create(parent);
    lv_obj_set_size(s_stats_table, STATS_TABLE_WIDTH, STATS_TABLE_HEIGHT);
    lv_obj_align(s_stats_table, LV_ALIGN_TOP_LEFT, UI_MARGIN_X, STATS_TABLE_TOP_Y);
    lv_obj_remove_flag(s_stats_table, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(s_stats_table, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_stats_table, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_bg_color(s_stats_table, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_stats_table, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_stats_table, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(s_stats_table, 1, 0);
    lv_obj_set_style_pad_all(s_stats_table, 0, 0);
    lv_obj_set_style_pad_top(s_stats_table, 0, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(s_stats_table, 0, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(s_stats_table, 2, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(s_stats_table, 2, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_stats_table, lv_color_hex(UI_COLOR_BG), LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_stats_table, lv_color_hex(UI_COLOR_BORDER), LV_PART_ITEMS);
    lv_table_set_column_count(s_stats_table, 4);
    lv_table_set_row_count(s_stats_table, 4);
    lv_table_set_column_width(s_stats_table, 0, STATS_COL_PROPERTY_WIDTH);
    lv_table_set_column_width(s_stats_table, 1, STATS_COL_VALUE_WIDTH);
    lv_table_set_column_width(s_stats_table, 2, STATS_COL_VALUE_WIDTH);
    lv_table_set_column_width(s_stats_table, 3, STATS_COL_VALUE_WIDTH);
    lv_table_set_cell_value(s_stats_table, 0, 0, "");
    lv_table_set_cell_value(s_stats_table, 0, 1, "Latest");
    lv_table_set_cell_value(s_stats_table, 0, 2, "Min");
    lv_table_set_cell_value(s_stats_table, 0, 3, "Max");
    lv_obj_add_event_cb(s_stats_table, stats_table_draw_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
    lv_obj_add_flag(s_stats_table, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
}

static void table_screen_create(lv_obj_t* parent) {
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(parent, lv_color_hex(UI_COLOR_BG), 0);

    lv_obj_t* lbl_title = lv_label_create(parent);
    lv_label_set_text(lbl_title, "Air quality");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, UI_MARGIN_X, 8);

    s_table = lv_table_create(parent);
    lv_obj_set_style_text_font(s_table, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_table, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_bg_opa(s_table, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_table, lv_color_hex(0x404040), 0);
    lv_obj_set_style_border_width(s_table, 1, 0);
    lv_obj_align(s_table, LV_ALIGN_TOP_LEFT, UI_MARGIN_X, 40);

    lv_table_set_column_count(s_table, 5);
    lv_table_set_row_count(s_table, 4);
    lv_table_set_column_width(s_table, 0, 90);
    lv_table_set_column_width(s_table, 1, 110);
    lv_table_set_column_width(s_table, 2, 90);
    lv_table_set_column_width(s_table, 3, 80);
    lv_table_set_column_width(s_table, 4, 80);

    table_set_str(0, 0, "Sensor");
    table_set_str(0, 1, "Detected");
    table_set_str(0, 2, "CO2");
    table_set_str(0, 3, "T");
    table_set_str(0, 4, "RH");

    table_set_str(1, 0, "SCD41");
    table_set_str(2, 0, "STCC4");
    table_set_str(3, 0, "SHT20");

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
}

void ui_init(lv_display_t* disp) {
    ESP_LOGI(TAG, "ui_init called");

    lvgl_port_lock(0);

    lv_obj_t* scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clean(scr);

    s_tabview = lv_tabview_create(scr);
    lv_obj_set_size(s_tabview, BOARD_LCD_HRES, BOARD_LCD_VRES);
    lv_obj_set_style_bg_color(s_tabview, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_border_width(s_tabview, 0, 0);
    lv_tabview_set_tab_bar_position(s_tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(s_tabview, 36);

    lv_obj_t* tab_bar = lv_tabview_get_tab_bar(s_tabview);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_border_width(tab_bar, 0, 0);
    lv_obj_set_style_pad_all(tab_bar, 0, 0);
    lv_obj_set_style_text_color(tab_bar, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_color(tab_bar, lv_color_hex(UI_COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(UI_COLOR_GRID), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(UI_COLOR_BORDER), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(tab_bar, lv_color_hex(UI_COLOR_TEXT), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(tab_bar, lv_color_hex(UI_COLOR_TEXT), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_event_cb(tab_bar, force_text_draw_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
    lv_obj_add_flag(tab_bar, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

    lv_obj_t* plot_tab = lv_tabview_add_tab(s_tabview, "Plot");
    lv_obj_t* table_tab = lv_tabview_add_tab(s_tabview, "Table");
    plot_screen_create(plot_tab);
    table_screen_create(table_tab);
    lv_tabview_set_active(s_tabview, 0, LV_ANIM_OFF);

    air_quality_data_t d = air_quality_get_latest();
    s_selected_sensor = default_sensor_for_data(&d);
    table_update(&d);
    chart_redraw();

    s_ui_timer = lv_timer_create(ui_timer_cb, 1000, NULL);

    crosshair_create(scr);

    lvgl_port_unlock();
}
