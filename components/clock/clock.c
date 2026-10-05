/* Version: 2026-10-05 */

#include "clock.h"

#include "clock_layout.h"
#include "particle_engine.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"

#define CLOCK_FRAME_PERIOD_MS 33U
#define CLOCK_METRICS_PERIOD_MS 5000U
#define CLOCK_VALID_YEAR 2020

typedef struct {
    clock_layout_t layout;
    particle_engine_t* particles;
    lv_display_t* display;
    lv_obj_t* canvas;
    lv_timer_t* timer;
    uint8_t* canvas_pixels;
    uint32_t canvas_stride;
    uint32_t last_frame_tick;
    uint32_t metrics_tick;
    uint32_t callback_count;
    uint64_t callback_time_us;
    uint32_t callback_max_us;
    uint32_t refresh_count;
    uint32_t render_count;
    uint64_t render_time_us;
    int64_t render_start_us;
    time_t displayed_minute;
    int displayed_second;
    bool time_warning_logged;
    bool display_event_registered;
} clock_context_t;

static const char* TAG = "clock";
static clock_context_t s_clock;

static void clock_display_event_cb(lv_event_t* event) {
    clock_context_t* clock = lv_event_get_user_data(event);

    switch (lv_event_get_code(event)) {
        case LV_EVENT_REFR_READY:
            clock->refresh_count++;
            break;
        case LV_EVENT_RENDER_START:
            clock->render_start_us = esp_timer_get_time();
            break;
        case LV_EVENT_RENDER_READY:
            clock->render_time_us += esp_timer_get_time() - clock->render_start_us;
            clock->render_count++;
            break;
        default:
            break;
    }
}

static esp_err_t rebuild_time_mask(clock_context_t* clock, const struct tm* local_time, bool transition) {
    clock_mask_t mask;
    esp_err_t err = clock_layout_build_time(&clock->layout, (uint8_t)local_time->tm_hour, (uint8_t)local_time->tm_min, &mask);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to build time mask: %s", esp_err_to_name(err));
        return err;
    }
    particle_engine_set_mask(clock->particles, &mask, transition);
    return ESP_OK;
}

static void log_metrics(clock_context_t* clock, uint32_t now_tick) {
    const uint32_t elapsed_ms = lv_tick_elaps(clock->metrics_tick);
    if (elapsed_ms < CLOCK_METRICS_PERIOD_MS) {
        return;
    }

    const uint32_t callbacks_per_second = elapsed_ms > 0 ? clock->callback_count * 1000U / elapsed_ms : 0;
    const uint32_t frames_per_second = elapsed_ms > 0 ? clock->refresh_count * 1000U / elapsed_ms : 0;
    const uint32_t average_us = clock->callback_count > 0 ? (uint32_t)(clock->callback_time_us / clock->callback_count) : 0;
    const uint32_t render_average_us = clock->render_count > 0 ? (uint32_t)(clock->render_time_us / clock->render_count) : 0;
    ESP_LOGI(TAG,
             "particles=%u sparks=%u max_rise=%ldpx callbacks=%lu/s LVGL=%lu FPS, render=%lu us avg, simulation+canvas=%lu us avg/%lu us max, internal_free=%u, psram_free=%u",
             (unsigned)particle_engine_count(clock->particles), (unsigned)particle_engine_spark_count(clock->particles),
             (long)particle_engine_spark_max_rise(clock->particles), (unsigned long)callbacks_per_second, (unsigned long)frames_per_second,
             (unsigned long)render_average_us, (unsigned long)average_us, (unsigned long)clock->callback_max_us,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    clock->metrics_tick = now_tick;
    clock->callback_count = 0;
    clock->callback_time_us = 0;
    clock->callback_max_us = 0;
    clock->refresh_count = 0;
    clock->render_count = 0;
    clock->render_time_us = 0;
}

static void clock_timer_cb(lv_timer_t* timer) {
    clock_context_t* clock = lv_timer_get_user_data(timer);
    const int64_t callback_start_us = esp_timer_get_time();
    const uint32_t now_tick = lv_tick_get();
    uint32_t elapsed_ms = lv_tick_elaps(clock->last_frame_tick);
    if (elapsed_ms == 0) {
        elapsed_ms = 1;
    }
    clock->last_frame_tick = now_tick;

    const time_t now = time(NULL);
    struct tm local_time;
    localtime_r(&now, &local_time);

    if (!clock->time_warning_logged && local_time.tm_year + 1900 < CLOCK_VALID_YEAR) {
        ESP_LOGW(TAG, "system time is not set; clock will start from the system epoch");
        clock->time_warning_logged = true;
    }

    const time_t minute = now / 60;
    if (minute != clock->displayed_minute) {
        if (rebuild_time_mask(clock, &local_time, true) == ESP_OK) {
            clock->displayed_minute = minute;
        }
    }
    if (local_time.tm_sec != clock->displayed_second) {
        clock->displayed_second = local_time.tm_sec;
        particle_engine_pulse_colon(clock->particles);
    }

    particle_engine_update(clock->particles, elapsed_ms);
    particle_engine_render_rgb565(clock->particles, clock->canvas_pixels, clock->canvas_stride);
    lv_obj_invalidate(clock->canvas);

    const uint32_t callback_us = (uint32_t)(esp_timer_get_time() - callback_start_us);
    clock->callback_count++;
    clock->callback_time_us += callback_us;
    if (callback_us > clock->callback_max_us) {
        clock->callback_max_us = callback_us;
    }
    log_metrics(clock, now_tick);
}

static void clock_cleanup(clock_context_t* clock) {
    if (clock->timer != NULL) {
        lv_timer_delete(clock->timer);
    }
    if (clock->canvas != NULL) {
        lv_obj_delete(clock->canvas);
    }
    if (clock->display_event_registered) {
        lv_display_remove_event_cb_with_user_data(clock->display, clock_display_event_cb, clock);
    }
    particle_engine_destroy(clock->particles);
    clock_layout_deinit(&clock->layout);
    heap_caps_free(clock->canvas_pixels);
    *clock = (clock_context_t){0};
}

esp_err_t clock_start(lv_display_t* display) {
    if (display == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_clock.timer != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_clock.display = display;

    esp_err_t err = clock_layout_init(&s_clock.layout, BOARD_LCD_HRES, BOARD_LCD_VRES);
    if (err != ESP_OK) {
        return err;
    }

    s_clock.particles = particle_engine_create(BOARD_LCD_HRES, BOARD_LCD_VRES);
    s_clock.canvas_stride = lv_draw_buf_width_to_stride(BOARD_LCD_HRES, LV_COLOR_FORMAT_RGB565);
    const size_t canvas_size = (size_t)s_clock.canvas_stride * BOARD_LCD_VRES;
    s_clock.canvas_pixels = heap_caps_aligned_alloc(64, canvas_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_clock.particles == NULL || s_clock.canvas_pixels == NULL) {
        clock_layout_deinit(&s_clock.layout);
        particle_engine_destroy(s_clock.particles);
        heap_caps_free(s_clock.canvas_pixels);
        s_clock = (clock_context_t){0};
        return ESP_ERR_NO_MEM;
    }
    memset(s_clock.canvas_pixels, 0, canvas_size);

    const time_t now = time(NULL);
    struct tm local_time;
    localtime_r(&now, &local_time);

    lvgl_port_lock(0);
    lv_obj_t* screen = lv_display_get_screen_active(display);
    lv_obj_clean(screen);
    lv_obj_set_scrollable(screen, false);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    s_clock.canvas = lv_canvas_create(screen);
    if (s_clock.canvas != NULL) {
        lv_canvas_set_buffer(s_clock.canvas, s_clock.canvas_pixels, BOARD_LCD_HRES, BOARD_LCD_VRES, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(s_clock.canvas, 0, 0);
        lv_obj_set_scrollable(s_clock.canvas, false);
    }

    if (s_clock.canvas == NULL) {
        lvgl_port_unlock();
        clock_cleanup(&s_clock);
        return ESP_ERR_NO_MEM;
    }

    err = rebuild_time_mask(&s_clock, &local_time, false);
    if (err == ESP_OK) {
        particle_engine_render_rgb565(s_clock.particles, s_clock.canvas_pixels, s_clock.canvas_stride);
        s_clock.displayed_minute = now / 60;
        s_clock.displayed_second = local_time.tm_sec;
        s_clock.last_frame_tick = lv_tick_get();
        s_clock.metrics_tick = s_clock.last_frame_tick;
        s_clock.display_event_registered = lv_display_add_event_cb(display, clock_display_event_cb, LV_EVENT_ALL, &s_clock) != NULL;
        s_clock.timer = lv_timer_create(clock_timer_cb, CLOCK_FRAME_PERIOD_MS, &s_clock);
    }
    lvgl_port_unlock();

    if (err != ESP_OK || s_clock.timer == NULL) {
        lvgl_port_lock(0);
        clock_cleanup(&s_clock);
        lvgl_port_unlock();
        return err != ESP_OK ? err : ESP_ERR_NO_MEM;
    }

    const size_t persistent_bytes = particle_engine_memory_size(s_clock.particles) + (size_t)BOARD_LCD_HRES * BOARD_LCD_VRES + 4096U + canvas_size;
    ESP_LOGI(TAG, "particle clock ready: %u particles, %u-byte pool, %u-byte A8 mask, %u-byte RGB565 canvas, %u persistent bytes total",
             (unsigned)particle_engine_count(s_clock.particles), (unsigned)particle_engine_memory_size(s_clock.particles), (unsigned)(BOARD_LCD_HRES * BOARD_LCD_VRES),
             (unsigned)canvas_size, (unsigned)persistent_bytes);
    return ESP_OK;
}
