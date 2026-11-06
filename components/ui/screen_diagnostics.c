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
#define DASHBOARD_CHART_UPDATE_MS 100
#define DASHBOARD_CHART_POINT_COUNT 64
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
/* One extra point supports interpolated intersections at both chart edges. */
static int32_t s_dashboard_chart_x[DASHBOARD_CHART_POINT_COUNT + 1];
static int32_t s_dashboard_chart_y[DASHBOARD_CHART_POINT_COUNT + 1];

enum {
    DIAGNOSTIC_TAB_RGB,
    DIAGNOSTIC_TAB_HUE,
    DIAGNOSTIC_TAB_DASHBOARD,
    DIAGNOSTIC_TAB_SWIPE,
    DIAGNOSTIC_TAB_SOLID,
};

static const char* const s_diagnostic_tab_name[] = {"RGB", "Hue", "Dashboard", "Swipe", "Solid"};

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
static const lv_point_precise_t s_swipe_diagonal_points[] = {
    {60, BOARD_LCD_VRES - 60},
    {BOARD_LCD_HRES - 60, 60},
};

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

static lv_obj_t* diagnostic_label_create(lv_obj_t* parent, const char* text, lv_color_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

static void diagnostic_animation_start(lv_obj_t* object, lv_anim_exec_xcb_t callback, int32_t start, int32_t end, uint32_t duration, bool reverse) {
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_exec_cb(&animation, callback);
    lv_anim_set_values(&animation, start, end);
    lv_anim_set_duration(&animation, duration);
    if (reverse) {
        lv_anim_set_reverse_duration(&animation, duration);
    }
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&animation, lv_anim_path_linear);
    lv_anim_start(&animation);
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

static lv_obj_t* color_bar_create(lv_obj_t* parent, int32_t x, int32_t width, uint32_t color) {
    lv_obj_t* bar = diagnostic_rectangle_create(parent, x, 0, width, BOARD_LCD_VRES, lv_color_hex(color));
    lv_obj_set_clickable(bar, true);
    return bar;
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
    lv_obj_t* page = diagnostic_rectangle_create(parent, x, 0, BOARD_LCD_HRES, BOARD_LCD_VRES, background_color);

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

    lv_obj_t* label = diagnostic_label_create(page, text, foreground_color);
    lv_obj_set_style_bg_color(label, background_color, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
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
    const uint32_t active_tab = lv_tabview_get_tab_active(s_diagnostic_tabview);
    if (active_tab < sizeof(s_diagnostic_tab_name) / sizeof(s_diagnostic_tab_name[0])) {
        return s_diagnostic_tab_name[active_tab];
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
        render_ms = (s_diagnostic_display_metrics.render_elapsed_sum - s_diagnostic_display_metrics.flush_in_render_elapsed_sum) / s_diagnostic_display_metrics.render_count;
        flush_ms = (s_diagnostic_display_metrics.flush_in_render_elapsed_sum + s_diagnostic_display_metrics.flush_outside_render_elapsed_sum) / s_diagnostic_display_metrics.render_count;
    }

    const uint32_t refresh_ms = render_ms + flush_ms;

    (void)timer;
    s_diagnostic_animation_exec_count = 0;
    s_diagnostic_display_metrics = (diagnostic_display_metrics_t){0};
    s_diagnostic_metrics_last_tick = lv_tick_get();
    ESP_LOGI(TAG, "%s: %lu animation callbacks/s | LVGL: %lu FPS, %lu ms (%lu render | %lu flush)", diagnostic_active_page_name(), (unsigned long)callbacks_per_second, (unsigned long)fps,
             (unsigned long)refresh_ms, (unsigned long)render_ms, (unsigned long)flush_ms);
}

static void diagnostic_metrics_start(lv_display_t* display) {
    s_diagnostic_animation_exec_count = 0;
    s_diagnostic_metrics_last_tick = lv_tick_get();
    s_diagnostic_display_metrics = (diagnostic_display_metrics_t){0};
    lv_display_add_event_cb(display, diagnostic_display_event_cb, LV_EVENT_ALL, NULL);
    lv_timer_create(diagnostic_metrics_timer_cb, 1000, NULL);
}
#endif

static lv_obj_t* swipe_track_create(lv_obj_t* parent) {
    lv_obj_t* track = lv_obj_create(parent);
    lv_obj_set_pos(track, 0, 0);
    lv_obj_set_size(track, BOARD_LCD_HRES * 2, BOARD_LCD_VRES);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(track, 0, 0);
    lv_obj_set_style_radius(track, 0, 0);
    lv_obj_set_style_pad_all(track, 0, 0);
    return track;
}

static void swipe_test_create(lv_obj_t* parent) {
    lv_obj_t* track = swipe_track_create(parent);
    const display_rgb_timing_t* active_timing = display_get_rgb_timing();
    swipe_test_page_create(track, 0, lv_color_hex(0x101820), lv_color_hex(0x00FFFF), active_timing->name);
    swipe_test_page_create(track, BOARD_LCD_HRES, lv_color_hex(0xF0E8D8), lv_color_hex(0xC00030), active_timing->name);

    diagnostic_animation_start(track, swipe_track_set_x, 0, -BOARD_LCD_HRES, SWIPE_DIAGNOSTIC_ANIMATION_MS, true);
}

static void solid_swipe_test_create(lv_obj_t* parent) {
    lv_obj_t* track = swipe_track_create(parent);

    diagnostic_rectangle_create(track, 0, 0, BOARD_LCD_HRES, BOARD_LCD_VRES, lv_color_hex(0xFF0000));
    diagnostic_rectangle_create(track, BOARD_LCD_HRES, 0, BOARD_LCD_HRES, BOARD_LCD_VRES, lv_color_hex(0x00FF00));

#if CONFIG_UI_METRICS
    diagnostic_animation_start(track, monitored_swipe_track_set_x, 0, -BOARD_LCD_HRES, SWIPE_DIAGNOSTIC_ANIMATION_MS, true);
#else
    diagnostic_animation_start(track, swipe_track_set_x, 0, -BOARD_LCD_HRES, SWIPE_DIAGNOSTIC_ANIMATION_MS, true);
#endif
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

static void dashboard_chart_scroll(void* chart, int32_t elapsed_ms) {
    /* Pre-smoothed irregular wave; fixed samples keep benchmark runs repeatable. */
    static const int32_t values[DASHBOARD_CHART_POINT_COUNT] = {
        24, 18, 28, 25, 35, 27, 33, 45, 40, 50, 47, 57, 54, 65, 62, 73, 69, 80, 73, 73, 66, 82, 77, 88, 79, 79, 70, 79, 70, 72, 61, 65,
        54, 57, 46, 50, 39, 40, 30, 36, 27, 29, 20, 24, 15, 20, 11, 17, 18, 28, 25, 36, 33, 43, 37, 40, 30, 34, 24, 28, 18, 22, 14, 22,
    };

    const uint32_t step = (uint32_t)elapsed_ms / DASHBOARD_CHART_UPDATE_MS;
    const int32_t offset = elapsed_ms % DASHBOARD_CHART_UPDATE_MS;
    for (uint32_t i = 0; i <= DASHBOARD_CHART_POINT_COUNT; i++) {
        s_dashboard_chart_x[i] = (int32_t)(i * DASHBOARD_CHART_UPDATE_MS) - offset;
        s_dashboard_chart_y[i] = values[(step + i) % DASHBOARD_CHART_POINT_COUNT];
    }
    /* LVGL clamps out-of-range X values. Interpolate their Y values too so
     * entering/leaving segments meet the edges without vertical jumps. */
    s_dashboard_chart_x[0] = 0;
    s_dashboard_chart_y[0] += (s_dashboard_chart_y[1] - s_dashboard_chart_y[0]) * offset / DASHBOARD_CHART_UPDATE_MS;
    const uint32_t last = DASHBOARD_CHART_POINT_COUNT;
    s_dashboard_chart_x[last] = (DASHBOARD_CHART_POINT_COUNT - 1) * DASHBOARD_CHART_UPDATE_MS;
    s_dashboard_chart_y[last] = s_dashboard_chart_y[last - 1] + (s_dashboard_chart_y[last] - s_dashboard_chart_y[last - 1]) * offset / DASHBOARD_CHART_UPDATE_MS;
    lv_chart_refresh(chart);
#if CONFIG_UI_METRICS
    diagnostic_animation_callback_record(DIAGNOSTIC_TAB_DASHBOARD);
#endif
}

static void dashboard_button_create(lv_obj_t* parent, int32_t x, const char* text) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_pos(button, x, 70);
    lv_obj_set_size(button, 136, 40);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x2463A8), 0);
    lv_obj_set_style_radius(button, 7, 0);

    lv_obj_t* label = diagnostic_label_create(button, text, lv_color_white());
    lv_obj_center(label);
}

static void dashboard_create(lv_obj_t* parent) {
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0C141D), 0);

    lv_obj_t* header = dashboard_card_create(parent, 12, 10, 456, 48);
    lv_obj_t* title = diagnostic_label_create(header, "DEVICE DASHBOARD", lv_color_white());
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t* status = diagnostic_label_create(header, "ONLINE", lv_color_hex(0x55D68B));
    lv_obj_align(status, LV_ALIGN_RIGHT_MID, -14, 0);

    dashboard_button_create(parent, 12, "Overview");
    dashboard_button_create(parent, 172, "Control");
    dashboard_button_create(parent, 332, "History");

    lv_obj_t* gauge_card = dashboard_card_create(parent, 12, 122, 210, 184);
    lv_obj_t* gauge_title = diagnostic_label_create(gauge_card, "SYSTEM LOAD", lv_color_hex(0xAFC1D2));
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

    s_dashboard_value_label = diagnostic_label_create(gauge_card, "15%", lv_color_white());
    lv_obj_set_style_text_font(s_dashboard_value_label, &lv_font_montserrat_16, 0);
    lv_obj_align(s_dashboard_value_label, LV_ALIGN_CENTER, 0, 20);

    lv_obj_t* control_card = dashboard_card_create(parent, 234, 122, 234, 184);
    lv_obj_t* output_label = diagnostic_label_create(control_card, "OUTPUT", lv_color_hex(0xAFC1D2));
    lv_obj_set_pos(output_label, 14, 12);

    lv_obj_t* bar = lv_bar_create(control_card);
    lv_obj_set_pos(bar, 14, 40);
    lv_obj_set_size(bar, 206, 20);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x263747), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x55D68B), LV_PART_INDICATOR);

    lv_obj_t* target_label = diagnostic_label_create(control_card, "TARGET", lv_color_hex(0xAFC1D2));
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
    lv_obj_t* chart_title = diagnostic_label_create(chart_card, "ACTIVITY - LAST 6 SECONDS", lv_color_hex(0xAFC1D2));
    lv_obj_set_pos(chart_title, 12, 8);

    lv_obj_t* chart = lv_chart_create(chart_card);
    lv_obj_set_pos(chart, 10, 31);
    lv_obj_set_size(chart, 436, 108);
    /* SCATTER connects explicit X/Y points, allowing sub-sample horizontal motion. */
    lv_chart_set_type(chart, LV_CHART_TYPE_SCATTER);
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_X, 0, (DASHBOARD_CHART_POINT_COUNT - 1) * DASHBOARD_CHART_UPDATE_MS);
    lv_chart_set_point_count(chart, DASHBOARD_CHART_POINT_COUNT + 1);
    lv_chart_set_div_line_count(chart, 4, 8);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_bg_color(chart, lv_color_hex(0x111C27), 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_line_color(chart, lv_color_hex(0x2A3B4C), LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    lv_chart_series_t* series = lv_chart_add_series(chart, lv_color_hex(0xB679FF), LV_CHART_AXIS_PRIMARY_Y);

    lv_chart_set_series_ext_x_array(chart, series, s_dashboard_chart_x);
    lv_chart_set_series_ext_y_array(chart, series, s_dashboard_chart_y);
    dashboard_chart_scroll(chart, 0);

    const uint32_t chart_cycle_ms = DASHBOARD_CHART_POINT_COUNT * DASHBOARD_CHART_UPDATE_MS;
    diagnostic_animation_start(chart, dashboard_chart_scroll, 0, chart_cycle_ms, chart_cycle_ms, false);

    diagnostic_animation_start(arc, dashboard_arc_set_value, 15, 92, DASHBOARD_ARC_ANIMATION_MS, true);
    diagnostic_animation_start(bar, dashboard_bar_set_value, 20, 95, DASHBOARD_BAR_ANIMATION_MS, true);
    diagnostic_animation_start(slider, dashboard_slider_set_value, 5, 95, DASHBOARD_SLIDER_ANIMATION_MS, true);
}

static void color_bars_create(lv_obj_t* parent) {
    const int32_t bar_width = BOARD_LCD_HRES / SCREEN_DIAGNOSTIC_BAR_COUNT;

    color_bar_create(parent, 0, bar_width, 0xFF0000);
    color_bar_create(parent, BOARD_LCD_HRES - bar_width, bar_width, 0x0000FF);
    lv_obj_t* green_bar = color_bar_create(parent, 0, bar_width, 0x00FF00);
    diagnostic_rectangle_create(green_bar, 0, BOARD_LCD_VRES / 3, bar_width, SCREEN_DIAGNOSTIC_STRIPE_HEIGHT, lv_color_black());
    diagnostic_rectangle_create(green_bar, 0, (BOARD_LCD_VRES * 2) / 3, bar_width, SCREEN_DIAGNOSTIC_STRIPE_HEIGHT, lv_color_black());

    diagnostic_animation_start(green_bar, swipe_track_set_x, 0, BOARD_LCD_HRES - bar_width, SCREEN_DIAGNOSTIC_ANIMATION_MS, true);

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
            lv_color_t color;

            if (y <= middle_y) {
                const uint32_t remaining = middle_y - y;
                const uint32_t position = middle_y - quadratic_scale(remaining, middle_y);
                color = lv_color_mix(saturated, lv_color_white(), position * 255U / middle_y);
            } else {
                const uint32_t distance = bottom_y - middle_y;
                const uint32_t position = quadratic_scale(y - middle_y, distance);
                color = lv_color_mix(lv_color_black(), saturated, position * 255U / distance);
            }

            uint16_t* row = (uint16_t*)(s_gradient_buffer + y * stride);
            row[x] = lv_color_to_u16(color);
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
    const display_timing_mode_t mode = (display_timing_mode_t)(uintptr_t)lv_event_get_user_data(e);
    const esp_err_t err = display_set_timing_mode_and_restart(mode);
    ESP_LOGE(TAG, "failed to save timing mode: %s", esp_err_to_name(err));
}

static void timing_button_create(lv_obj_t* parent, display_timing_mode_t mode, int32_t x, int32_t y, int32_t width) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_size(button, width, TIMING_BUTTON_HEIGHT);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0xC0C0C0), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_add_event_cb(button, timing_button_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)mode);

    lv_obj_t* label = diagnostic_label_create(button, display_get_timing_name(mode), lv_color_hex(0xC0C0C0));
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_center(label);
}

static void timing_buttons_create(lv_obj_t* parent) {
    // BS and WT keep their bottom row; all remaining modes populate the grid.
    const display_timing_mode_t standard_modes[] = {DISPLAY_TIMING_BS, DISPLAY_TIMING_WT};
    const size_t standard_count = sizeof(standard_modes) / sizeof(standard_modes[0]);
    const int32_t standard_width = (TIMING_BUTTON_WIDTH * (int32_t)standard_count) + (TIMING_BUTTON_GAP * ((int32_t)standard_count - 1));
    const int32_t standard_start_x = (BOARD_LCD_HRES - standard_width) / 2;
    const int32_t standard_y = BOARD_LCD_VRES - TIMING_BUTTON_HEIGHT - 14;

    for (size_t i = 0; i < standard_count; i++) {
        timing_button_create(parent, standard_modes[i], standard_start_x + (int32_t)i * (TIMING_BUTTON_WIDTH + TIMING_BUTTON_GAP), standard_y, TIMING_BUTTON_WIDTH);
    }

    const int32_t diagnostic_count = DISPLAY_TIMING_COUNT - DISPLAY_TIMING_MIN_A22;
    const int32_t grid_width = (TIMING_GRID_BUTTON_WIDTH * TIMING_GRID_COLUMNS) + (TIMING_GRID_COLUMN_GAP * (TIMING_GRID_COLUMNS - 1));
    const int32_t grid_rows = (diagnostic_count + TIMING_GRID_COLUMNS - 1) / TIMING_GRID_COLUMNS;
    const int32_t grid_height = (TIMING_BUTTON_HEIGHT * grid_rows) + (TIMING_GRID_ROW_GAP * (grid_rows - 1));
    const int32_t grid_start_x = (BOARD_LCD_HRES - grid_width) / 2;
    const int32_t grid_start_y = standard_y - TIMING_GRID_TO_BUTTON_GAP - grid_height;

    for (int32_t i = 0; i < diagnostic_count; i++) {
        const display_timing_mode_t mode = (display_timing_mode_t)(DISPLAY_TIMING_MIN_A22 + i);
        const int32_t column = i % TIMING_GRID_COLUMNS;
        const int32_t row = i / TIMING_GRID_COLUMNS;
        timing_button_create(parent, mode, grid_start_x + column * (TIMING_GRID_BUTTON_WIDTH + TIMING_GRID_COLUMN_GAP),
                             grid_start_y + row * (TIMING_BUTTON_HEIGHT + TIMING_GRID_ROW_GAP), TIMING_GRID_BUTTON_WIDTH);
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

    lv_obj_t* pages[sizeof(s_diagnostic_tab_name) / sizeof(s_diagnostic_tab_name[0])];
    for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
        pages[i] = lv_tabview_add_tab(s_diagnostic_tabview, s_diagnostic_tab_name[i]);
        screen_page_style(pages[i]);
    }
    color_bars_create(pages[DIAGNOSTIC_TAB_RGB]);
    hue_gradient_create(pages[DIAGNOSTIC_TAB_HUE]);
    timing_buttons_create(pages[DIAGNOSTIC_TAB_HUE]);
    swipe_test_create(pages[DIAGNOSTIC_TAB_SWIPE]);
    solid_swipe_test_create(pages[DIAGNOSTIC_TAB_SOLID]);
    dashboard_create(pages[DIAGNOSTIC_TAB_DASHBOARD]);
#if CONFIG_UI_METRICS
    diagnostic_metrics_start(disp);
#endif
    lv_tabview_set_active(s_diagnostic_tabview, DIAGNOSTIC_TAB_DASHBOARD, LV_ANIM_OFF);

    lvgl_port_unlock();
}
