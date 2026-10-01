#include "ui.h"

#include "board.h"
#include "display.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#define SCREEN_DIAGNOSTIC_BAR_COUNT 3
#define SCREEN_DIAGNOSTIC_ANIMATION_MS 5333
#define SCREEN_DIAGNOSTIC_GRID_SPACING 40
#define SCREEN_DIAGNOSTIC_GRID_LINE_WIDTH 2
#define SCREEN_DIAGNOSTIC_STRIPE_HEIGHT 2
#define SWIPE_DIAGNOSTIC_ANIMATION_MS 2400
#define SWIPE_DIAGNOSTIC_CIRCLE_SIZE 300
#define SWIPE_DIAGNOSTIC_LINE_WIDTH 4
#define DASHBOARD_ARC_ANIMATION_MS 2200
#define DASHBOARD_BAR_ANIMATION_MS 1700
#define DASHBOARD_SLIDER_ANIMATION_MS 2600
#define DASHBOARD_CHART_UPDATE_MS 200
#define DASHBOARD_CHART_POINT_COUNT 32
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
static lv_obj_t* s_diagnostic_tabview;
static lv_obj_t* s_dashboard_value_label;
static lv_obj_t* s_dashboard_chart;
static lv_chart_series_t* s_dashboard_chart_series;
static uint32_t s_dashboard_chart_step;

enum {
    DIAGNOSTIC_TAB_RGB,
    DIAGNOSTIC_TAB_HUE,
    DIAGNOSTIC_TAB_SWIPE,
    DIAGNOSTIC_TAB_SOLID,
    DIAGNOSTIC_TAB_DASHBOARD,
};

#if CONFIG_UI_METRICS
static uint32_t s_diagnostic_animation_exec_count;
static uint32_t s_diagnostic_metrics_last_tick;
typedef struct {
    uint32_t refresh_count;
    uint32_t render_start;
    uint32_t render_elapsed_sum;
    uint32_t render_count;
    uint32_t flush_in_render_start;
    uint32_t flush_in_render_elapsed_sum;
    uint32_t flush_outside_render_start;
    uint32_t flush_outside_render_elapsed_sum;
    bool render_in_progress;
} diagnostic_display_metrics_t;
static diagnostic_display_metrics_t s_diagnostic_display_metrics;
#endif
static const display_timing_mode_t s_standard_timing_mode[] = {
    DISPLAY_TIMING_BS,
    DISPLAY_TIMING_WT,
};
static const char* const s_standard_timing_label[] = {"BS", "WT"};
static const display_timing_mode_t s_diagnostic_timing_mode[] = {
    DISPLAY_TIMING_MIN_HBP,   DISPLAY_TIMING_MIN_HBP_54HZ, DISPLAY_TIMING_MIN_HSYNC, DISPLAY_TIMING_MID_HBP,   DISPLAY_TIMING_MID_BAL,
    DISPLAY_TIMING_MID_HSYNC, DISPLAY_TIMING_MAX_HBP, DISPLAY_TIMING_MAX_BAL,   DISPLAY_TIMING_MAX_HSYNC,
};
static const char* const s_diagnostic_timing_label[] = {
    "MIN HBP", "MIN HBP 54Hz", "MIN HSYNC", "MID HBP", "MID BAL", "MID HSYNC", "MAX HBP", "MAX BAL", "MAX HSYNC",
};
static const lv_point_precise_t s_swipe_diagonal_points[] = {
    {60, BOARD_LCD_VRES - 60},
    {BOARD_LCD_HRES - 60, 60},
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

static lv_obj_t* diagnostic_rectangle_create(lv_obj_t* parent, int32_t x, int32_t y, int32_t width, int32_t height, lv_color_t color) {
    lv_obj_t* rectangle = lv_obj_create(parent);
    lv_obj_set_pos(rectangle, x, y);
    lv_obj_set_size(rectangle, width, height);
    lv_obj_remove_flag(rectangle, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(rectangle, color, 0);
    lv_obj_set_style_bg_opa(rectangle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(rectangle, 0, 0);
    lv_obj_set_style_radius(rectangle, 0, 0);
    lv_obj_set_style_pad_all(rectangle, 0, 0);
    return rectangle;
}

static void diagnostic_grid_create(lv_obj_t* parent) {
    const int32_t half_line_width = SCREEN_DIAGNOSTIC_GRID_LINE_WIDTH / 2;

    for (int32_t x = SCREEN_DIAGNOSTIC_GRID_SPACING; x < BOARD_LCD_HRES; x += SCREEN_DIAGNOSTIC_GRID_SPACING) {
        diagnostic_rectangle_create(parent, x, 0, half_line_width, BOARD_LCD_VRES, lv_color_black());
        diagnostic_rectangle_create(parent, x + half_line_width, 0, half_line_width, BOARD_LCD_VRES, lv_color_white());
    }

    for (int32_t y = SCREEN_DIAGNOSTIC_GRID_SPACING; y < BOARD_LCD_VRES; y += SCREEN_DIAGNOSTIC_GRID_SPACING) {
        diagnostic_rectangle_create(parent, 0, y, BOARD_LCD_HRES, half_line_width, lv_color_black());
        diagnostic_rectangle_create(parent, 0, y + half_line_width, BOARD_LCD_HRES, half_line_width, lv_color_white());
    }
}

static void swipe_test_page_create(lv_obj_t* parent, int32_t x, lv_color_t background_color, lv_color_t foreground_color, const char* text) {
    lv_obj_t* page = lv_obj_create(parent);
    lv_obj_set_pos(page, x, 0);
    lv_obj_set_size(page, BOARD_LCD_HRES, BOARD_LCD_VRES);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(page, background_color, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);

    diagnostic_grid_create(page);

    lv_obj_t* circle = lv_obj_create(page);
    lv_obj_set_size(circle, SWIPE_DIAGNOSTIC_CIRCLE_SIZE, SWIPE_DIAGNOSTIC_CIRCLE_SIZE);
    lv_obj_center(circle);
    lv_obj_remove_flag(circle, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(circle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(circle, foreground_color, 0);
    lv_obj_set_style_border_width(circle, SWIPE_DIAGNOSTIC_LINE_WIDTH, 0);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(circle, 0, 0);

    diagnostic_rectangle_create(page, 0, (BOARD_LCD_VRES - SWIPE_DIAGNOSTIC_LINE_WIDTH) / 2, BOARD_LCD_HRES, SWIPE_DIAGNOSTIC_LINE_WIDTH, foreground_color);
    diagnostic_rectangle_create(page, (BOARD_LCD_HRES - SWIPE_DIAGNOSTIC_LINE_WIDTH) / 2, 0, SWIPE_DIAGNOSTIC_LINE_WIDTH, BOARD_LCD_VRES, foreground_color);

    lv_obj_t* diagonal = lv_line_create(page);
    lv_line_set_points(diagonal, s_swipe_diagonal_points, 2);
    lv_obj_set_style_line_color(diagonal, foreground_color, 0);
    lv_obj_set_style_line_width(diagonal, SWIPE_DIAGNOSTIC_LINE_WIDTH, 0);

    lv_obj_t* label = lv_label_create(page);
    lv_label_set_text(label, text);
    lv_obj_set_style_bg_color(label, background_color, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(label, foreground_color, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_all(label, 8, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 16);
}

static void swipe_track_set_x(void* track, int32_t x) {
    lv_obj_set_x(track, x);
}

#if CONFIG_UI_METRICS
static void diagnostic_animation_callback_record(uint32_t tab_index) {
    if (s_diagnostic_tabview != NULL && lv_tabview_get_tab_active(s_diagnostic_tabview) == tab_index) {
        s_diagnostic_animation_exec_count++;
    }
}

static void monitored_swipe_track_set_x(void* track, int32_t x) {
    swipe_track_set_x(track, x);
    diagnostic_animation_callback_record(DIAGNOSTIC_TAB_SOLID);
}

static void diagnostic_display_event_cb(lv_event_t* event) {
    switch (lv_event_get_code(event)) {
        case LV_EVENT_REFR_READY:
            s_diagnostic_display_metrics.refresh_count++;
            break;
        case LV_EVENT_RENDER_START:
            s_diagnostic_display_metrics.render_in_progress = true;
            s_diagnostic_display_metrics.render_start = lv_tick_get();
            break;
        case LV_EVENT_RENDER_READY:
            s_diagnostic_display_metrics.render_in_progress = false;
            s_diagnostic_display_metrics.render_elapsed_sum += lv_tick_elaps(s_diagnostic_display_metrics.render_start);
            s_diagnostic_display_metrics.render_count++;
            break;
        case LV_EVENT_FLUSH_START:
        case LV_EVENT_FLUSH_WAIT_START:
            if (s_diagnostic_display_metrics.render_in_progress) {
                s_diagnostic_display_metrics.flush_in_render_start = lv_tick_get();
            } else {
                s_diagnostic_display_metrics.flush_outside_render_start = lv_tick_get();
            }
            break;
        case LV_EVENT_FLUSH_FINISH:
        case LV_EVENT_FLUSH_WAIT_FINISH:
            if (s_diagnostic_display_metrics.render_in_progress) {
                s_diagnostic_display_metrics.flush_in_render_elapsed_sum += lv_tick_elaps(s_diagnostic_display_metrics.flush_in_render_start);
            } else {
                s_diagnostic_display_metrics.flush_outside_render_elapsed_sum += lv_tick_elaps(s_diagnostic_display_metrics.flush_outside_render_start);
            }
            break;
        default:
            break;
    }
}

static const char* diagnostic_active_page_name(void) {
    static const char* const page_name[] = {"RGB", "Hue", "Swipe", "Solid", "Dashboard"};
    const uint32_t active_tab = lv_tabview_get_tab_active(s_diagnostic_tabview);
    if (active_tab < sizeof(page_name) / sizeof(page_name[0])) {
        return page_name[active_tab];
    }
    return "Unknown";
}

static void diagnostic_metrics_timer_cb(lv_timer_t* timer) {
    const uint32_t elapsed_ms = lv_tick_elaps(s_diagnostic_metrics_last_tick);
    const uint32_t exec_count = s_diagnostic_animation_exec_count;
    uint32_t callbacks_per_second = 0;
    uint32_t fps = 0;
    uint32_t render_ms = 0;
    uint32_t flush_ms = 0;

    if (elapsed_ms > 0) {
        callbacks_per_second = exec_count * 1000 / elapsed_ms;
        fps = s_diagnostic_display_metrics.refresh_count * 1000 / elapsed_ms;
    }

    const uint32_t maximum_fps = 1000 / LV_DEF_REFR_PERIOD;
    if (fps > maximum_fps) {
        fps = maximum_fps;
    }

    if (s_diagnostic_display_metrics.render_count > 0) {
        render_ms = (s_diagnostic_display_metrics.render_elapsed_sum - s_diagnostic_display_metrics.flush_in_render_elapsed_sum) /
                    s_diagnostic_display_metrics.render_count;
        flush_ms = (s_diagnostic_display_metrics.flush_in_render_elapsed_sum + s_diagnostic_display_metrics.flush_outside_render_elapsed_sum) /
                   s_diagnostic_display_metrics.render_count;
    }

    const uint32_t refresh_ms = render_ms + flush_ms;

    (void)timer;
    s_diagnostic_animation_exec_count = 0;
    s_diagnostic_display_metrics = (diagnostic_display_metrics_t){0};
    s_diagnostic_metrics_last_tick = lv_tick_get();
    ESP_LOGI(TAG, "%s: %lu animation callbacks/s | LVGL: %lu FPS, %lu ms (%lu render | %lu flush)", diagnostic_active_page_name(),
             (unsigned long)callbacks_per_second, (unsigned long)fps, (unsigned long)refresh_ms, (unsigned long)render_ms, (unsigned long)flush_ms);
}

static void diagnostic_metrics_start(lv_display_t* display) {
    s_diagnostic_animation_exec_count = 0;
    s_diagnostic_metrics_last_tick = lv_tick_get();
    s_diagnostic_display_metrics = (diagnostic_display_metrics_t){0};
    lv_display_add_event_cb(display, diagnostic_display_event_cb, LV_EVENT_ALL, NULL);
    lv_timer_create(diagnostic_metrics_timer_cb, 1000, NULL);
}
#endif

static void swipe_test_create(lv_obj_t* parent) {
    lv_obj_t* track = lv_obj_create(parent);
    lv_obj_set_pos(track, 0, 0);
    lv_obj_set_size(track, BOARD_LCD_HRES * 2, BOARD_LCD_VRES);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(track, 0, 0);
    lv_obj_set_style_radius(track, 0, 0);
    lv_obj_set_style_pad_all(track, 0, 0);

    const display_rgb_timing_t* active_timing = display_get_rgb_timing();
    swipe_test_page_create(track, 0, lv_color_hex(0x101820), lv_color_hex(0x00FFFF), active_timing->name);
    swipe_test_page_create(track, BOARD_LCD_HRES, lv_color_hex(0xF0E8D8), lv_color_hex(0xC00030), active_timing->name);

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, track);
    lv_anim_set_exec_cb(&animation, swipe_track_set_x);
    lv_anim_set_values(&animation, 0, -BOARD_LCD_HRES);
    lv_anim_set_duration(&animation, SWIPE_DIAGNOSTIC_ANIMATION_MS);
    lv_anim_set_reverse_duration(&animation, SWIPE_DIAGNOSTIC_ANIMATION_MS);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&animation, lv_anim_path_linear);
    lv_anim_start(&animation);
}

static void solid_swipe_test_create(lv_obj_t* parent) {
    lv_obj_t* track = lv_obj_create(parent);
    lv_obj_set_pos(track, 0, 0);
    lv_obj_set_size(track, BOARD_LCD_HRES * 2, BOARD_LCD_VRES);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(track, 0, 0);
    lv_obj_set_style_radius(track, 0, 0);
    lv_obj_set_style_pad_all(track, 0, 0);

    diagnostic_rectangle_create(track, 0, 0, BOARD_LCD_HRES, BOARD_LCD_VRES, lv_color_hex(0xFF0000));
    diagnostic_rectangle_create(track, BOARD_LCD_HRES, 0, BOARD_LCD_HRES, BOARD_LCD_VRES, lv_color_hex(0x00FF00));

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, track);
#if CONFIG_UI_METRICS
    lv_anim_set_exec_cb(&animation, monitored_swipe_track_set_x);
#else
    lv_anim_set_exec_cb(&animation, swipe_track_set_x);
#endif
    lv_anim_set_values(&animation, 0, -BOARD_LCD_HRES);
    lv_anim_set_duration(&animation, SWIPE_DIAGNOSTIC_ANIMATION_MS);
    lv_anim_set_reverse_duration(&animation, SWIPE_DIAGNOSTIC_ANIMATION_MS);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&animation, lv_anim_path_linear);
    lv_anim_start(&animation);
}

static lv_obj_t* dashboard_card_create(lv_obj_t* parent, int32_t x, int32_t y, int32_t width, int32_t height) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, width, height);
    lv_obj_set_scrollable(card, false);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x182431), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x31465A), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    return card;
}

static void dashboard_arc_set_value(void* arc, int32_t value) {
    lv_arc_set_value(arc, value);
    if (s_dashboard_value_label != NULL) {
        lv_label_set_text_fmt(s_dashboard_value_label, "%ld%%", (long)value);
    }
#if CONFIG_UI_METRICS
    diagnostic_animation_callback_record(DIAGNOSTIC_TAB_DASHBOARD);
#endif
}

static void dashboard_bar_set_value(void* bar, int32_t value) {
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
#if CONFIG_UI_METRICS
    diagnostic_animation_callback_record(DIAGNOSTIC_TAB_DASHBOARD);
#endif
}

static void dashboard_slider_set_value(void* slider, int32_t value) {
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
#if CONFIG_UI_METRICS
    diagnostic_animation_callback_record(DIAGNOSTIC_TAB_DASHBOARD);
#endif
}

static void dashboard_chart_timer_cb(lv_timer_t* timer) {
    static const int32_t values[DASHBOARD_CHART_POINT_COUNT] = {
        42, 45, 49, 53, 58, 62, 66, 69, 72, 74, 73, 71, 68, 64, 61, 57,
        54, 51, 48, 46, 44, 43, 45, 48, 52, 57, 61, 65, 62, 58, 53, 47,
    };

    (void)timer;
    lv_chart_set_next_value(s_dashboard_chart, s_dashboard_chart_series,
                            values[s_dashboard_chart_step % DASHBOARD_CHART_POINT_COUNT]);
    s_dashboard_chart_step++;
}

static void dashboard_animation_start(lv_obj_t* object, lv_anim_exec_xcb_t callback, int32_t start, int32_t end, uint32_t duration) {
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_exec_cb(&animation, callback);
    lv_anim_set_values(&animation, start, end);
    lv_anim_set_duration(&animation, duration);
    lv_anim_set_reverse_duration(&animation, duration);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&animation, lv_anim_path_linear);
    lv_anim_start(&animation);
}

static void dashboard_button_create(lv_obj_t* parent, int32_t x, const char* text) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_pos(button, x, 70);
    lv_obj_set_size(button, 136, 40);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x2463A8), 0);
    lv_obj_set_style_radius(button, 7, 0);

    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
}

static void dashboard_create(lv_obj_t* parent) {
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0C141D), 0);

    lv_obj_t* header = dashboard_card_create(parent, 12, 10, 456, 48);
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "DEVICE DASHBOARD");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t* status = lv_label_create(header);
    lv_label_set_text(status, "ONLINE");
    lv_obj_set_style_text_color(status, lv_color_hex(0x55D68B), 0);
    lv_obj_align(status, LV_ALIGN_RIGHT_MID, -14, 0);

    dashboard_button_create(parent, 12, "Overview");
    dashboard_button_create(parent, 172, "Control");
    dashboard_button_create(parent, 332, "History");

    lv_obj_t* gauge_card = dashboard_card_create(parent, 12, 122, 210, 184);
    lv_obj_t* gauge_title = lv_label_create(gauge_card);
    lv_label_set_text(gauge_title, "SYSTEM LOAD");
    lv_obj_set_style_text_color(gauge_title, lv_color_hex(0xAFC1D2), 0);
    lv_obj_align(gauge_title, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t* arc = lv_arc_create(gauge_card);
    lv_obj_set_size(arc, 136, 136);
    lv_obj_align(arc, LV_ALIGN_BOTTOM_MID, 0, -7);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_obj_set_clickable(arc, false);
    lv_obj_set_style_arc_width(arc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x263747), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x39B8FF), LV_PART_INDICATOR);

    s_dashboard_value_label = lv_label_create(gauge_card);
    lv_label_set_text(s_dashboard_value_label, "15%");
    lv_obj_set_style_text_color(s_dashboard_value_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_dashboard_value_label, &lv_font_montserrat_16, 0);
    lv_obj_align(s_dashboard_value_label, LV_ALIGN_CENTER, 0, 20);

    lv_obj_t* control_card = dashboard_card_create(parent, 234, 122, 234, 184);
    lv_obj_t* output_label = lv_label_create(control_card);
    lv_label_set_text(output_label, "OUTPUT");
    lv_obj_set_style_text_color(output_label, lv_color_hex(0xAFC1D2), 0);
    lv_obj_set_pos(output_label, 14, 12);

    lv_obj_t* bar = lv_bar_create(control_card);
    lv_obj_set_pos(bar, 14, 40);
    lv_obj_set_size(bar, 206, 20);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x263747), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x55D68B), LV_PART_INDICATOR);

    lv_obj_t* target_label = lv_label_create(control_card);
    lv_label_set_text(target_label, "TARGET");
    lv_obj_set_style_text_color(target_label, lv_color_hex(0xAFC1D2), 0);
    lv_obj_set_pos(target_label, 14, 88);

    lv_obj_t* slider = lv_slider_create(control_card);
    lv_obj_set_pos(slider, 14, 119);
    lv_obj_set_size(slider, 206, 20);
    lv_slider_set_range(slider, 0, 100);
    lv_obj_set_clickable(slider, false);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x263747), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xFFB84D), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);

    lv_obj_t* chart_card = dashboard_card_create(parent, 12, 318, 456, 150);
    lv_obj_t* chart_title = lv_label_create(chart_card);
    lv_label_set_text(chart_title, "ACTIVITY - LAST 6 SECONDS");
    lv_obj_set_style_text_color(chart_title, lv_color_hex(0xAFC1D2), 0);
    lv_obj_set_pos(chart_title, 12, 8);

    s_dashboard_chart = lv_chart_create(chart_card);
    lv_obj_set_pos(s_dashboard_chart, 10, 31);
    lv_obj_set_size(s_dashboard_chart, 436, 108);
    lv_chart_set_type(s_dashboard_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_axis_range(s_dashboard_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_point_count(s_dashboard_chart, DASHBOARD_CHART_POINT_COUNT);
    lv_chart_set_div_line_count(s_dashboard_chart, 4, 8);
    lv_chart_set_update_mode(s_dashboard_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_bg_color(s_dashboard_chart, lv_color_hex(0x111C27), 0);
    lv_obj_set_style_border_width(s_dashboard_chart, 0, 0);
    lv_obj_set_style_line_color(s_dashboard_chart, lv_color_hex(0x2A3B4C), LV_PART_MAIN);
    lv_obj_set_style_line_width(s_dashboard_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_width(s_dashboard_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(s_dashboard_chart, 0, 0, LV_PART_INDICATOR);
    s_dashboard_chart_series = lv_chart_add_series(s_dashboard_chart, lv_color_hex(0xB679FF), LV_CHART_AXIS_PRIMARY_Y);

    s_dashboard_chart_step = 0;
    for (uint32_t i = 0; i < DASHBOARD_CHART_POINT_COUNT; i++) {
        dashboard_chart_timer_cb(NULL);
    }
    lv_timer_create(dashboard_chart_timer_cb, DASHBOARD_CHART_UPDATE_MS, NULL);

    dashboard_animation_start(arc, dashboard_arc_set_value, 15, 92, DASHBOARD_ARC_ANIMATION_MS);
    dashboard_animation_start(bar, dashboard_bar_set_value, 20, 95, DASHBOARD_BAR_ANIMATION_MS);
    dashboard_animation_start(slider, dashboard_slider_set_value, 5, 95, DASHBOARD_SLIDER_ANIMATION_MS);
}

static void color_bars_create(lv_obj_t* parent) {
    const int32_t bar_width = BOARD_LCD_HRES / SCREEN_DIAGNOSTIC_BAR_COUNT;

    color_bar_create(parent, 0, bar_width, 0xFF0000);
    color_bar_create(parent, BOARD_LCD_HRES - bar_width, bar_width, 0x0000FF);
    lv_obj_t* green_bar = color_bar_create(parent, 0, bar_width, 0x00FF00);
    diagnostic_rectangle_create(green_bar, 0, BOARD_LCD_VRES / 3, bar_width, SCREEN_DIAGNOSTIC_STRIPE_HEIGHT, lv_color_black());
    diagnostic_rectangle_create(green_bar, 0, (BOARD_LCD_VRES * 2) / 3, bar_width, SCREEN_DIAGNOSTIC_STRIPE_HEIGHT, lv_color_black());

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

    diagnostic_grid_create(parent);

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

    s_diagnostic_tabview = lv_tabview_create(screen);
    lv_obj_set_size(s_diagnostic_tabview, BOARD_LCD_HRES, BOARD_LCD_VRES);
    lv_obj_set_style_border_width(s_diagnostic_tabview, 0, 0);
    lv_obj_set_style_pad_all(s_diagnostic_tabview, 0, 0);
    lv_tabview_set_tab_bar_position(s_diagnostic_tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(s_diagnostic_tabview, 0);

    lv_obj_t* content = lv_tabview_get_content(s_diagnostic_tabview);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_pad_column(content, 0, 0);

    lv_obj_t* color_bars_page = lv_tabview_add_tab(s_diagnostic_tabview, "RGB");
    lv_obj_t* gradient_page = lv_tabview_add_tab(s_diagnostic_tabview, "Hue");
    lv_obj_t* swipe_page = lv_tabview_add_tab(s_diagnostic_tabview, "Swipe");
    lv_obj_t* solid_swipe_page = lv_tabview_add_tab(s_diagnostic_tabview, "Solid");
    lv_obj_t* dashboard_page = lv_tabview_add_tab(s_diagnostic_tabview, "Dashboard");
    screen_page_style(color_bars_page);
    screen_page_style(gradient_page);
    screen_page_style(swipe_page);
    screen_page_style(solid_swipe_page);
    screen_page_style(dashboard_page);
    color_bars_create(color_bars_page);
    hue_gradient_create(gradient_page);
    timing_buttons_create(gradient_page);
    swipe_test_create(swipe_page);
    solid_swipe_test_create(solid_swipe_page);
    dashboard_create(dashboard_page);
#if CONFIG_UI_METRICS
    diagnostic_metrics_start(disp);
#endif
    lv_tabview_set_active(s_diagnostic_tabview, DIAGNOSTIC_TAB_DASHBOARD, LV_ANIM_OFF);

    lvgl_port_unlock();
}
