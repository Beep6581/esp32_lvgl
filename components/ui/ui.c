#include "ui.h"

#include "air_quality.h"
#include "board.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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

#define UI_COLOR_BG 0x101010
#define UI_COLOR_GRID 0x202020
#define UI_COLOR_BORDER 0x303030
#define UI_COLOR_TEXT 0xC0C0C0
#define UI_COLOR_SCD41 0x63D471
#define UI_COLOR_SHT20 0x4DA3FF

#define UI_MARGIN_X 0
#define CONTROL_WIDTH 448
#define CONTROL_HEIGHT 34
#define METRIC_SELECTOR_TOP_Y 0
#define SOURCE_SELECTOR_TOP_Y 38
#define CHART_TOP_Y 78
#define CHART_WIDTH 400
#define CHART_LABEL_WIDTH 48
#define CHART_SINGLE_HEIGHT 356
#define CHART_ALL_HEIGHT 112
#define CHART_ALL_GAP 6

#define TABLE_WIDTH 448
#define TABLE_COL_SOURCE_WIDTH 92
#define TABLE_COL_VALUE_WIDTH 72
#define TABLE_COL_STATUS_WIDTH 140

typedef enum {
    PLOT_MODE_TEMPERATURE = AIR_QUALITY_METRIC_TEMPERATURE,
    PLOT_MODE_HUMIDITY = AIR_QUALITY_METRIC_HUMIDITY,
    PLOT_MODE_CO2 = AIR_QUALITY_METRIC_CO2,
    PLOT_MODE_ALL = AIR_QUALITY_METRIC_COUNT,
    PLOT_MODE_COUNT,
} plot_mode_t;

typedef struct {
    bool any;
    int32_t min;
    int32_t max;
} value_range_t;

static lv_obj_t* s_tabview;
static lv_timer_t* s_ui_timer;
static lv_obj_t* s_crosshair;

static lv_obj_t* s_metric_selector;
static lv_obj_t* s_source_selector;
static lv_obj_t* s_source_status_dot[AIR_QUALITY_SOURCE_COUNT];
static lv_obj_t* s_chart[AIR_QUALITY_METRIC_COUNT];
static lv_obj_t* s_chart_title[AIR_QUALITY_METRIC_COUNT];
static lv_obj_t* s_chart_max_label[AIR_QUALITY_METRIC_COUNT];
static lv_obj_t* s_chart_min_label[AIR_QUALITY_METRIC_COUNT];
static lv_obj_t* s_chart_latest_label[AIR_QUALITY_METRIC_COUNT][AIR_QUALITY_SOURCE_COUNT];
static lv_chart_series_t* s_series[AIR_QUALITY_METRIC_COUNT][AIR_QUALITY_SOURCE_COUNT];
static lv_obj_t* s_lbl_x_left;
static lv_obj_t* s_lbl_x_right;
static lv_obj_t* s_comparison_table;

static air_quality_snapshot_t* s_history;
static uint32_t s_history_count;
static uint32_t s_history_next;
static int32_t* s_chart_values[AIR_QUALITY_METRIC_COUNT][AIR_QUALITY_SOURCE_COUNT];
static bool s_plot_buffers_ready;

static plot_mode_t s_plot_mode = PLOT_MODE_ALL;
static bool s_source_selected[AIR_QUALITY_SOURCE_COUNT];
static bool s_source_online[AIR_QUALITY_SOURCE_COUNT];
static bool s_source_selection_initialized;
static uint32_t s_last_sample_ms;

static const char* const s_metric_selector_map[] = {"ALL", "T", "RH", "CO2", ""};
static const char* const s_source_selector_map[] = {"SCD41", "SHT20", ""};

static const plot_mode_t s_metric_button_mode[PLOT_MODE_COUNT] = {
    PLOT_MODE_ALL,
    PLOT_MODE_TEMPERATURE,
    PLOT_MODE_HUMIDITY,
    PLOT_MODE_CO2,
};

static const char* const s_source_name[AIR_QUALITY_SOURCE_COUNT] = {"SCD41", "SHT20"};
static const char* const s_metric_name[AIR_QUALITY_METRIC_COUNT] = {"T", "RH", "CO2"};
static const char* const s_metric_unit[AIR_QUALITY_METRIC_COUNT] = {"C", "%", "ppm"};
static const uint32_t s_source_color[AIR_QUALITY_SOURCE_COUNT] = {UI_COLOR_SCD41, UI_COLOR_SHT20};

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

    int32_t tenths;
    if (milli >= 0) {
        tenths = (milli + 50) / 100;
    } else {
        tenths = (milli - 50) / 100;
    }

    const int32_t whole = tenths / 10;
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

    snprintf(out, out_len, "%02lu:%02lu:%02lu", (unsigned long)hours, (unsigned long)minutes, (unsigned long)seconds);
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

static void format_metric_value(char* out, size_t out_len, air_quality_metric_t metric, int32_t value) {
    if (metric == AIR_QUALITY_METRIC_CO2) {
        snprintf(out, out_len, "%ld", (long)value);
    } else {
        format_milli_1dp(out, out_len, value);
    }
}

static bool plot_buffers_init(void) {
    if (s_plot_buffers_ready) {
        return true;
    }

    if (s_history == NULL) {
        s_history = plot_alloc(sizeof(air_quality_snapshot_t) * PLOT_HISTORY_CAPACITY);
    }

    for (air_quality_metric_t metric = 0; metric < AIR_QUALITY_METRIC_COUNT; metric++) {
        for (air_quality_source_t source = 0; source < AIR_QUALITY_SOURCE_COUNT; source++) {
            if (s_chart_values[metric][source] == NULL) {
                s_chart_values[metric][source] = plot_alloc(sizeof(int32_t) * PLOT_HISTORY_CAPACITY);
            }
        }
    }

    if (s_history == NULL) {
        ESP_LOGE(TAG, "history buffer allocation failed (capacity=%lu)", (unsigned long)PLOT_HISTORY_CAPACITY);
        return false;
    }

    for (air_quality_metric_t metric = 0; metric < AIR_QUALITY_METRIC_COUNT; metric++) {
        for (air_quality_source_t source = 0; source < AIR_QUALITY_SOURCE_COUNT; source++) {
            if (s_chart_values[metric][source] == NULL) {
                ESP_LOGE(TAG, "chart buffer allocation failed (metric=%d source=%d)", (int)metric, (int)source);
                return false;
            }
            for (uint32_t i = 0; i < PLOT_HISTORY_CAPACITY; i++) {
                s_chart_values[metric][source][i] = LV_CHART_POINT_NONE;
            }
        }
    }

    s_plot_buffers_ready = true;
    ESP_LOGI(TAG, "plot history ready: %lu samples (%lu hours at %lu ms)", (unsigned long)PLOT_HISTORY_CAPACITY, (unsigned long)PLOT_HISTORY_HOURS, (unsigned long)PLOT_SAMPLE_PERIOD_MS);
    return true;
}

static uint32_t history_physical_index(uint32_t chronological_index) {
    if (s_history_count < PLOT_HISTORY_CAPACITY) {
        return chronological_index;
    }
    return (s_history_next + chronological_index) % PLOT_HISTORY_CAPACITY;
}

static void history_add_sample(const air_quality_snapshot_t* snapshot, uint32_t now_ms) {
    if (snapshot == NULL || !plot_buffers_init()) {
        return;
    }

    s_history[s_history_next] = *snapshot;
    s_history[s_history_next].timestamp_ms = now_ms;

    s_history_next = (s_history_next + 1U) % PLOT_HISTORY_CAPACITY;
    if (s_history_count < PLOT_HISTORY_CAPACITY) {
        s_history_count++;
    }
}

static void range_add(value_range_t* range, int32_t value) {
    if (!range->any) {
        range->any = true;
        range->min = value;
        range->max = value;
        return;
    }

    if (value < range->min) {
        range->min = value;
    }
    if (value > range->max) {
        range->max = value;
    }
}

static int32_t metric_minimum_padding(air_quality_metric_t metric) {
    switch (metric) {
    case AIR_QUALITY_METRIC_TEMPERATURE:
        return 500;
    case AIR_QUALITY_METRIC_HUMIDITY:
        return 1000;
    case AIR_QUALITY_METRIC_CO2:
        return 50;
    default:
        return 1;
    }
}

static void range_add_padding(value_range_t* range, air_quality_metric_t metric) {
    if (!range->any) {
        range->min = 0;
        range->max = 1;
        return;
    }

    const int64_t span = (int64_t)range->max - (int64_t)range->min;
    int32_t padding = (int32_t)(span / 10);
    const int32_t minimum = metric_minimum_padding(metric);
    if (padding < minimum) {
        padding = minimum;
    }

    range->min -= padding;
    range->max += padding;
}

static bool metric_is_visible(air_quality_metric_t metric) {
    return s_plot_mode == PLOT_MODE_ALL || s_plot_mode == (plot_mode_t)metric;
}

static void chart_layout(void) {
    uint32_t visible_index = 0;
    lv_obj_t* last_chart = NULL;

    for (air_quality_metric_t metric = 0; metric < AIR_QUALITY_METRIC_COUNT; metric++) {
        const bool visible = metric_is_visible(metric);
        if (!visible) {
            lv_obj_add_flag(s_chart[metric], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_chart_title[metric], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_chart_max_label[metric], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_chart_min_label[metric], LV_OBJ_FLAG_HIDDEN);
            for (air_quality_source_t source = 0; source < AIR_QUALITY_SOURCE_COUNT; source++) {
                lv_obj_add_flag(s_chart_latest_label[metric][source], LV_OBJ_FLAG_HIDDEN);
            }
            continue;
        }

        int32_t y = CHART_TOP_Y;
        int32_t height = CHART_SINGLE_HEIGHT;
        if (s_plot_mode == PLOT_MODE_ALL) {
            y += (int32_t)visible_index * (CHART_ALL_HEIGHT + CHART_ALL_GAP);
            height = CHART_ALL_HEIGHT;
        }

        lv_obj_clear_flag(s_chart[metric], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_chart_title[metric], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(s_chart[metric], CHART_WIDTH, height);
        lv_obj_align(s_chart[metric], LV_ALIGN_TOP_LEFT, UI_MARGIN_X, y);
        lv_obj_align_to(s_chart_title[metric], s_chart[metric], LV_ALIGN_TOP_LEFT, 4, 2);
        lv_obj_move_foreground(s_chart_title[metric]);

        last_chart = s_chart[metric];
        visible_index++;
    }

    if (last_chart != NULL) {
        lv_obj_align_to(s_lbl_x_left, last_chart, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
        lv_obj_align_to(s_lbl_x_right, last_chart, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 4);
    }
}

static void chart_redraw(void) {
    if (!s_plot_buffers_ready) {
        return;
    }

    chart_layout();

    const uint32_t point_count = (s_history_count >= 2U) ? s_history_count : 2U;

    for (air_quality_metric_t metric = 0; metric < AIR_QUALITY_METRIC_COUNT; metric++) {
        value_range_t range = {0};
        bool source_has_data[AIR_QUALITY_SOURCE_COUNT] = {0};
        uint32_t source_valid_point_count[AIR_QUALITY_SOURCE_COUNT] = {0};
        uint32_t source_latest_point[AIR_QUALITY_SOURCE_COUNT] = {0};

        for (uint32_t i = 0; i < s_history_count; i++) {
            const air_quality_snapshot_t* sample = &s_history[history_physical_index(i)];
            for (air_quality_source_t source = 0; source < AIR_QUALITY_SOURCE_COUNT; source++) {
                const air_quality_metric_data_t* value = &sample->source[source].metric[metric];
                if (s_source_selected[source] && value->supported && value->valid) {
                    range_add(&range, value->value);
                    source_has_data[source] = true;
                    source_valid_point_count[source]++;
                    source_latest_point[source] = i;
                }
            }
        }

        const bool range_has_data = range.any;
        range_add_padding(&range, metric);

        for (air_quality_source_t source = 0; source < AIR_QUALITY_SOURCE_COUNT; source++) {
            for (uint32_t i = 0; i < PLOT_HISTORY_CAPACITY; i++) {
                s_chart_values[metric][source][i] = LV_CHART_POINT_NONE;
            }

            if (s_source_selected[source]) {
                for (uint32_t i = 0; i < s_history_count; i++) {
                    const air_quality_snapshot_t* sample = &s_history[history_physical_index(i)];
                    const air_quality_metric_data_t* value = &sample->source[source].metric[metric];
                    if (value->supported && value->valid) {
                        s_chart_values[metric][source][i] = value->value;
                    }
                }
            }

            lv_chart_hide_series(s_chart[metric], s_series[metric][source], !source_has_data[source]);
        }

        bool show_single_points = false;
        bool has_line = false;
        for (air_quality_source_t source = 0; source < AIR_QUALITY_SOURCE_COUNT; source++) {
            if (source_valid_point_count[source] == 1U) {
                show_single_points = true;
            } else if (source_valid_point_count[source] >= 2U) {
                has_line = true;
            }
        }
        const int32_t point_size = (show_single_points && !has_line) ? 2 : 0;
        lv_obj_set_style_size(s_chart[metric], point_size, point_size, LV_PART_INDICATOR);

        lv_chart_set_point_count(s_chart[metric], point_count);
        lv_chart_set_axis_range(s_chart[metric], LV_CHART_AXIS_PRIMARY_Y, range.min, range.max);
        lv_chart_refresh(s_chart[metric]);

        if (range_has_data && metric_is_visible(metric)) {
            char value[16] = {0};
            format_metric_value(value, sizeof(value), metric, range.max);
            lv_label_set_text(s_chart_max_label[metric], value);
            lv_obj_clear_flag(s_chart_max_label[metric], LV_OBJ_FLAG_HIDDEN);
            lv_obj_align_to(s_chart_max_label[metric], s_chart[metric], LV_ALIGN_OUT_RIGHT_TOP, 0, 0);

            format_metric_value(value, sizeof(value), metric, range.min);
            lv_label_set_text(s_chart_min_label[metric], value);
            lv_obj_clear_flag(s_chart_min_label[metric], LV_OBJ_FLAG_HIDDEN);
            lv_obj_align_to(s_chart_min_label[metric], s_chart[metric], LV_ALIGN_OUT_RIGHT_BOTTOM, 0, 0);
        } else {
            lv_obj_add_flag(s_chart_max_label[metric], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_chart_min_label[metric], LV_OBJ_FLAG_HIDDEN);
        }

        lv_obj_update_layout(s_chart[metric]);
        for (air_quality_source_t source = 0; source < AIR_QUALITY_SOURCE_COUNT; source++) {
            lv_obj_t* label = s_chart_latest_label[metric][source];
            if (!metric_is_visible(metric) || !source_has_data[source]) {
                lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
                continue;
            }

            char value[16] = {0};
            const uint32_t point_id = source_latest_point[source];
            format_metric_value(value, sizeof(value), metric, s_chart_values[metric][source][point_id]);
            lv_label_set_text(label, value);
            lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_update_layout(label);

            lv_point_t point;
            lv_chart_get_point_pos_by_id(s_chart[metric], s_series[metric][source], point_id, &point);
            int32_t label_y = point.y - lv_obj_get_height(label) / 2;
            const int32_t max_y = lv_obj_get_height(s_chart[metric]) - lv_obj_get_height(label);
            if (label_y < 0) {
                label_y = 0;
            } else if (label_y > max_y) {
                label_y = max_y;
            }
            lv_obj_align_to(label, s_chart[metric], LV_ALIGN_OUT_RIGHT_TOP, 0, label_y);
            lv_obj_move_foreground(label);
        }
    }

    if (s_history_count > 0U) {
        char left[16] = {0};
        char right[16] = {0};
        format_duration_hm(left, sizeof(left), s_history[history_physical_index(0)].timestamp_ms);
        format_duration_hms(right, sizeof(right), s_history[history_physical_index(s_history_count - 1U)].timestamp_ms);
        lv_label_set_text(s_lbl_x_left, left);
        lv_label_set_text(s_lbl_x_right, right);
    } else {
        lv_label_set_text(s_lbl_x_left, "--:--");
        lv_label_set_text(s_lbl_x_right, "--:--:--");
    }
}

static void table_set_metric(uint16_t row, uint16_t col, const air_quality_metric_data_t* metric, air_quality_metric_t metric_id) {
    if (!metric->supported) {
        lv_table_set_cell_value(s_comparison_table, row, col, "--");
        return;
    }
    if (!metric->valid) {
        lv_table_set_cell_value(s_comparison_table, row, col, "...");
        return;
    }

    char value[16] = {0};
    format_metric_value(value, sizeof(value), metric_id, metric->value);
    lv_table_set_cell_value(s_comparison_table, row, col, value);
}

static void comparison_table_update(const air_quality_snapshot_t* snapshot) {
    if (snapshot == NULL || s_comparison_table == NULL) {
        return;
    }

    for (air_quality_source_t source_id = 0; source_id < AIR_QUALITY_SOURCE_COUNT; source_id++) {
        const uint16_t row = (uint16_t)source_id + 1U;
        const air_quality_source_data_t* source = &snapshot->source[source_id];

        lv_table_set_cell_value(s_comparison_table, row, 0, s_source_name[source_id]);
        table_set_metric(row, 1, &source->metric[AIR_QUALITY_METRIC_CO2], AIR_QUALITY_METRIC_CO2);
        table_set_metric(row, 2, &source->metric[AIR_QUALITY_METRIC_TEMPERATURE], AIR_QUALITY_METRIC_TEMPERATURE);
        table_set_metric(row, 3, &source->metric[AIR_QUALITY_METRIC_HUMIDITY], AIR_QUALITY_METRIC_HUMIDITY);

        const char* status = "--";
        if (source->configured && !source->online) {
            status = "Offline";
        } else if (source->configured && source->last_update_ms == 0U) {
            status = "Waiting";
        } else if (source->configured) {
            status = "OK";
        }
        lv_table_set_cell_value(s_comparison_table, row, 4, status);
    }
}

static void force_text_draw_cb(lv_event_t* e) {
    lv_draw_task_t* draw_task = lv_event_get_draw_task(e);
    lv_draw_label_dsc_t* label_dsc = lv_draw_task_get_label_dsc(draw_task);
    if (label_dsc != NULL) {
        label_dsc->color = lv_color_hex(UI_COLOR_TEXT);
    }
}

static void source_text_draw_cb(lv_event_t* e) {
    lv_draw_task_t* draw_task = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t* base_dsc = (lv_draw_dsc_base_t*)lv_draw_task_get_draw_dsc(draw_task);
    lv_draw_label_dsc_t* label_dsc = lv_draw_task_get_label_dsc(draw_task);
    if (base_dsc == NULL || label_dsc == NULL || base_dsc->part != LV_PART_ITEMS) {
        return;
    }

    if (base_dsc->id1 < AIR_QUALITY_SOURCE_COUNT) {
        label_dsc->color = lv_color_hex(s_source_color[base_dsc->id1]);
        if (!s_source_online[base_dsc->id1]) {
            label_dsc->opa = LV_OPA_40;
        }
    }
}

static void source_selector_update(const air_quality_snapshot_t* snapshot) {
    if (snapshot == NULL || s_source_selector == NULL) {
        return;
    }

    bool state_changed = false;
    for (air_quality_source_t source = 0; source < AIR_QUALITY_SOURCE_COUNT; source++) {
        const bool online = snapshot->source[source].configured && snapshot->source[source].online;
        if (s_source_online[source] != online) {
            s_source_online[source] = online;
            state_changed = true;
            lv_obj_t* dot = s_source_status_dot[source];
            if (dot != NULL) {
                lv_obj_set_style_bg_opa(dot, online ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(dot, online ? 0 : 1, 0);
            }
        }
    }

    if (!s_source_selection_initialized && snapshot->timestamp_ms != 0U) {
        for (air_quality_source_t source = 0; source < AIR_QUALITY_SOURCE_COUNT; source++) {
            s_source_selected[source] = s_source_online[source];
            if (s_source_selected[source]) {
                lv_buttonmatrix_set_button_ctrl(s_source_selector, source, LV_BUTTONMATRIX_CTRL_CHECKED);
            } else {
                lv_buttonmatrix_clear_button_ctrl(s_source_selector, source, LV_BUTTONMATRIX_CTRL_CHECKED);
            }
        }
        s_source_selection_initialized = true;
        state_changed = true;
    }

    if (state_changed) {
        lv_obj_invalidate(s_source_selector);
    }
}

static void table_text_draw_cb(lv_event_t* e) {
    lv_draw_task_t* draw_task = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t* base_dsc = (lv_draw_dsc_base_t*)lv_draw_task_get_draw_dsc(draw_task);
    lv_draw_label_dsc_t* label_dsc = lv_draw_task_get_label_dsc(draw_task);
    if (base_dsc == NULL || label_dsc == NULL || base_dsc->part != LV_PART_ITEMS) {
        return;
    }

    if (base_dsc->id1 > 0 && base_dsc->id1 <= AIR_QUALITY_SOURCE_COUNT) {
        label_dsc->color = lv_color_hex(s_source_color[base_dsc->id1 - 1U]);
    } else {
        label_dsc->color = lv_color_hex(UI_COLOR_TEXT);
    }
}

static void metric_selector_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    const uint32_t selected = lv_buttonmatrix_get_selected_button(obj);
    if (selected >= PLOT_MODE_COUNT) {
        return;
    }

    s_plot_mode = s_metric_button_mode[selected];
    chart_redraw();
}

static void source_selector_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    const uint32_t selected = lv_buttonmatrix_get_selected_button(obj);
    if (selected >= AIR_QUALITY_SOURCE_COUNT) {
        return;
    }

    s_source_selected[selected] = lv_buttonmatrix_has_button_ctrl(obj, selected, LV_BUTTONMATRIX_CTRL_CHECKED);
    chart_redraw();
}

static void ui_timer_cb(lv_timer_t* t) {
    (void)t;

    air_quality_snapshot_t snapshot = air_quality_get_latest();
    source_selector_update(&snapshot);
    comparison_table_update(&snapshot);

    const uint32_t now_ms = (uint32_t)esp_log_timestamp();
    if (s_last_sample_ms == 0U || (now_ms - s_last_sample_ms) >= PLOT_SAMPLE_PERIOD_MS) {
        s_last_sample_ms = now_ms;
        history_add_sample(&snapshot, now_ms);
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
        lv_obj_set_pos(s_crosshair, (lv_coord_t)clamped_x - (TOUCH_CROSSHAIR_SIZE / 2), (lv_coord_t)clamped_y - (TOUCH_CROSSHAIR_SIZE / 2));
    }

    lvgl_port_unlock();
}

static void style_selector(lv_obj_t* selector) {
    lv_obj_set_style_bg_color(selector, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_border_color(selector, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(selector, 0, 0);
    lv_obj_set_style_pad_all(selector, 0, 0);
    lv_obj_set_style_text_font(selector, &lv_font_montserrat_16, LV_PART_ITEMS);
    lv_obj_set_style_text_color(selector, lv_color_hex(UI_COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(selector, lv_color_hex(UI_COLOR_GRID), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(selector, lv_color_hex(UI_COLOR_BORDER), LV_PART_ITEMS | LV_STATE_CHECKED);
}

static lv_obj_t* create_chart_value_label(lv_obj_t* parent, uint32_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_width(label, CHART_LABEL_WIDTH);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_bg_color(label, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(label, 1, 0);
    lv_obj_set_style_pad_right(label, 1, 0);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    return label;
}

static void create_chart(lv_obj_t* parent, air_quality_metric_t metric) {
    s_chart[metric] = lv_chart_create(parent);
    lv_chart_set_type(s_chart[metric], LV_CHART_TYPE_LINE);
    lv_chart_set_axis_range(s_chart[metric], LV_CHART_AXIS_PRIMARY_Y, 0, 1);
    lv_chart_set_div_line_count(s_chart[metric], 5, 7);
    lv_chart_set_point_count(s_chart[metric], 2);
    lv_obj_set_style_bg_color(s_chart[metric], lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_border_color(s_chart[metric], lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(s_chart[metric], 1, 0);
    lv_obj_set_style_line_color(s_chart[metric], lv_color_hex(UI_COLOR_GRID), LV_PART_MAIN);
    lv_obj_set_style_line_width(s_chart[metric], 2, LV_PART_ITEMS);
    lv_obj_set_style_size(s_chart[metric], 0, 0, LV_PART_INDICATOR);

    for (air_quality_source_t source = 0; source < AIR_QUALITY_SOURCE_COUNT; source++) {
        s_series[metric][source] = lv_chart_add_series(s_chart[metric], lv_color_hex(s_source_color[source]), LV_CHART_AXIS_PRIMARY_Y);
        if (s_chart_values[metric][source] != NULL) {
            lv_chart_set_series_ext_y_array(s_chart[metric], s_series[metric][source], s_chart_values[metric][source]);
        }
    }

    s_chart_max_label[metric] = create_chart_value_label(parent, UI_COLOR_TEXT);
    s_chart_min_label[metric] = create_chart_value_label(parent, UI_COLOR_TEXT);
    for (air_quality_source_t source = 0; source < AIR_QUALITY_SOURCE_COUNT; source++) {
        s_chart_latest_label[metric][source] = create_chart_value_label(parent, s_source_color[source]);
    }

    s_chart_title[metric] = lv_label_create(parent);
    lv_obj_set_style_text_color(s_chart_title[metric], lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_chart_title[metric], &lv_font_montserrat_12, 0);
    lv_label_set_text_fmt(s_chart_title[metric], "%s [%s]", s_metric_name[metric], s_metric_unit[metric]);
}

static void plot_screen_create(lv_obj_t* parent) {
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(parent, lv_color_hex(UI_COLOR_BG), 0);

    s_metric_selector = lv_buttonmatrix_create(parent);
    lv_buttonmatrix_set_map(s_metric_selector, s_metric_selector_map);
    lv_buttonmatrix_set_button_ctrl_all(s_metric_selector, LV_BUTTONMATRIX_CTRL_CHECKABLE | LV_BUTTONMATRIX_CTRL_CLICK_TRIG);
    lv_buttonmatrix_set_one_checked(s_metric_selector, true);
    lv_buttonmatrix_set_button_ctrl(s_metric_selector, 0, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_obj_set_size(s_metric_selector, CONTROL_WIDTH, CONTROL_HEIGHT);
    lv_obj_align(s_metric_selector, LV_ALIGN_TOP_LEFT, UI_MARGIN_X, METRIC_SELECTOR_TOP_Y);
    style_selector(s_metric_selector);
    lv_obj_add_event_cb(s_metric_selector, metric_selector_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_metric_selector, force_text_draw_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
    lv_obj_add_flag(s_metric_selector, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

    s_source_selector = lv_buttonmatrix_create(parent);
    lv_buttonmatrix_set_map(s_source_selector, s_source_selector_map);
    lv_buttonmatrix_set_button_ctrl_all(s_source_selector, LV_BUTTONMATRIX_CTRL_CHECKABLE | LV_BUTTONMATRIX_CTRL_CLICK_TRIG);
    lv_buttonmatrix_set_one_checked(s_source_selector, false);
    lv_obj_set_size(s_source_selector, CONTROL_WIDTH, CONTROL_HEIGHT);
    lv_obj_align(s_source_selector, LV_ALIGN_TOP_LEFT, UI_MARGIN_X, SOURCE_SELECTOR_TOP_Y);
    style_selector(s_source_selector);
    lv_obj_add_event_cb(s_source_selector, source_selector_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_source_selector, source_text_draw_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
    lv_obj_add_flag(s_source_selector, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

    const int32_t source_button_width = CONTROL_WIDTH / AIR_QUALITY_SOURCE_COUNT;
    const int32_t dot_size = 10;
    const int32_t dot_padding = (CONTROL_HEIGHT - dot_size) / 2;
    for (air_quality_source_t source = 0; source < AIR_QUALITY_SOURCE_COUNT; source++) {
        lv_obj_t* dot = lv_obj_create(s_source_selector);
        s_source_status_dot[source] = dot;
        lv_obj_set_size(dot, dot_size, dot_size);
        lv_obj_set_pos(dot, ((int32_t)source + 1) * source_button_width - dot_size - dot_padding, dot_padding);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(s_source_color[source]), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(dot, lv_color_hex(UI_COLOR_BORDER), 0);
        lv_obj_set_style_border_width(dot, 1, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    const bool buffers_ready = plot_buffers_init();
    for (air_quality_metric_t metric = 0; metric < AIR_QUALITY_METRIC_COUNT; metric++) {
        create_chart(parent, metric);
    }
    if (!buffers_ready) {
        ESP_LOGE(TAG, "charts created without external data buffers");
    }

    s_lbl_x_left = lv_label_create(parent);
    lv_obj_set_style_text_color(s_lbl_x_left, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_lbl_x_left, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_lbl_x_left, "--:--");

    s_lbl_x_right = lv_label_create(parent);
    lv_obj_set_style_text_color(s_lbl_x_right, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_lbl_x_right, &lv_font_montserrat_12, 0);
    lv_obj_set_width(s_lbl_x_right, 120);
    lv_obj_set_style_text_align(s_lbl_x_right, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(s_lbl_x_right, "--:--:--");

    chart_layout();
}

static void table_screen_create(lv_obj_t* parent) {
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(parent, lv_color_hex(UI_COLOR_BG), 0);

    s_comparison_table = lv_table_create(parent);
    lv_obj_set_size(s_comparison_table, TABLE_WIDTH, LV_SIZE_CONTENT);
    lv_obj_align(s_comparison_table, LV_ALIGN_TOP_LEFT, UI_MARGIN_X, 0);
    lv_obj_remove_flag(s_comparison_table, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(s_comparison_table, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_comparison_table, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_bg_color(s_comparison_table, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_comparison_table, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_comparison_table, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(s_comparison_table, 1, 0);
    lv_obj_set_style_pad_all(s_comparison_table, 0, 0);
    lv_obj_set_style_pad_left(s_comparison_table, 2, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(s_comparison_table, 2, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_comparison_table, lv_color_hex(UI_COLOR_BG), LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_comparison_table, lv_color_hex(UI_COLOR_GRID), LV_PART_ITEMS);
    lv_table_set_column_count(s_comparison_table, 5);
    lv_table_set_row_count(s_comparison_table, AIR_QUALITY_SOURCE_COUNT + 1U);
    lv_table_set_column_width(s_comparison_table, 0, TABLE_COL_SOURCE_WIDTH);
    lv_table_set_column_width(s_comparison_table, 1, TABLE_COL_VALUE_WIDTH);
    lv_table_set_column_width(s_comparison_table, 2, TABLE_COL_VALUE_WIDTH);
    lv_table_set_column_width(s_comparison_table, 3, TABLE_COL_VALUE_WIDTH);
    lv_table_set_column_width(s_comparison_table, 4, TABLE_COL_STATUS_WIDTH);
    lv_table_set_cell_value(s_comparison_table, 0, 0, "Source");
    lv_table_set_cell_value(s_comparison_table, 0, 1, "CO2");
    lv_table_set_cell_value(s_comparison_table, 0, 2, "T");
    lv_table_set_cell_value(s_comparison_table, 0, 3, "RH");
    lv_table_set_cell_value(s_comparison_table, 0, 4, "Status");
    lv_obj_add_event_cb(s_comparison_table, table_text_draw_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
    lv_obj_add_flag(s_comparison_table, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
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
    lv_tabview_set_tab_bar_size(s_tabview, 0);

    lv_obj_t* plot_tab = lv_tabview_add_tab(s_tabview, "Plot");
    lv_obj_t* table_tab = lv_tabview_add_tab(s_tabview, "Table");
    plot_screen_create(plot_tab);
    table_screen_create(table_tab);
    lv_tabview_set_active(s_tabview, 0, LV_ANIM_OFF);

    air_quality_snapshot_t snapshot = air_quality_get_latest();
    source_selector_update(&snapshot);
    comparison_table_update(&snapshot);
    chart_redraw();

    s_ui_timer = lv_timer_create(ui_timer_cb, 1000, NULL);

    crosshair_create(scr);

    lvgl_port_unlock();
}
