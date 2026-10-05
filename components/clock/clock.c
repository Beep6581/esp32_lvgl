/* Version: 2026-10-05a */

#include "clock.h"

#include "clock_layout.h"
#include "clock_renderer.h"
#include "particle_engine.h"

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CLOCK_METRICS_PERIOD_US (5ULL * 1000ULL * 1000ULL)
#define CLOCK_TASK_STACK_SIZE 8192U
#define CLOCK_TASK_PRIORITY 5U
#define CLOCK_VALID_YEAR 2020

typedef struct {
    uint32_t frame_count;
    uint64_t simulation_us;
    uint64_t wait_us;
    uint64_t sync_us;
    uint64_t dirty_us;
    uint64_t render_us;
    uint64_t total_us;
    uint64_t dirty_pixels;
    uint32_t worst_dirty_pixels;
    uint32_t worst_total_us;
    uint32_t discarded_notifications;
    uint32_t deferred_frames;
    uint32_t late_completions;
    uint32_t submission_races;
    uint32_t dirty_tiles;
} clock_metrics_t;

typedef struct {
    clock_layout_t layout;
    particle_engine_t* particles;
    clock_renderer_t* renderer;
    TaskHandle_t task;
    clock_metrics_t metrics;
    int64_t last_frame_us;
    int64_t metrics_start_us;
    time_t displayed_minute;
    int displayed_second;
    bool time_warning_logged;
} clock_context_t;

static const char* TAG = "clock";
static clock_context_t s_clock;

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

static void accumulate_metrics(clock_context_t* clock, uint32_t simulation_us, const clock_renderer_metrics_t* renderer_metrics, uint32_t total_us) {
    clock_metrics_t* metrics = &clock->metrics;
    metrics->frame_count++;
    metrics->simulation_us += simulation_us;
    metrics->wait_us += renderer_metrics->wait_us;
    metrics->sync_us += renderer_metrics->sync_us;
    metrics->dirty_us += renderer_metrics->dirty_us;
    metrics->render_us += renderer_metrics->render_us;
    metrics->total_us += total_us;
    metrics->dirty_pixels += renderer_metrics->dirty_pixels;
    metrics->dirty_tiles += renderer_metrics->dirty_tiles;
    metrics->discarded_notifications += renderer_metrics->discarded_notifications;
    metrics->deferred_frames += renderer_metrics->deferred_frames;
    metrics->late_completions += renderer_metrics->late_completions;
    metrics->submission_races += renderer_metrics->submission_races;
    if (renderer_metrics->dirty_pixels > metrics->worst_dirty_pixels) {
        metrics->worst_dirty_pixels = renderer_metrics->dirty_pixels;
    }
    if (total_us > metrics->worst_total_us) {
        metrics->worst_total_us = total_us;
    }
}

static void log_metrics(clock_context_t* clock, int64_t now_us) {
    const uint64_t elapsed_us = (uint64_t)(now_us - clock->metrics_start_us);
    clock_metrics_t* metrics = &clock->metrics;
    if (elapsed_us < CLOCK_METRICS_PERIOD_US || metrics->frame_count == 0U) {
        return;
    }

    const uint32_t frame_count = metrics->frame_count;
    const uint32_t fps = (uint32_t)((uint64_t)frame_count * 1000000ULL / elapsed_us);
    const uint32_t average_dirty_pixels = (uint32_t)(metrics->dirty_pixels / frame_count);
    const uint32_t screen_pixels = BOARD_LCD_HRES * BOARD_LCD_VRES;
    ESP_LOGI(TAG,
             "particles=%u sparks=%u FPS=%lu sim=%luus wait=%luus sync=%luus dirty=%luus render=%luus total=%luus/%luus max, tiles=%lu, dirty=%lu px (%lu%%)/%lu max, deferred=%lu late=%lu submit_race=%lu discarded=%lu, internal_free=%u, psram_free=%u",
             (unsigned)particle_engine_count(clock->particles), (unsigned)particle_engine_spark_count(clock->particles), (unsigned long)fps,
             (unsigned long)(metrics->simulation_us / frame_count), (unsigned long)(metrics->wait_us / frame_count),
             (unsigned long)(metrics->sync_us / frame_count), (unsigned long)(metrics->dirty_us / frame_count),
             (unsigned long)(metrics->render_us / frame_count), (unsigned long)(metrics->total_us / frame_count), (unsigned long)metrics->worst_total_us,
             (unsigned long)(metrics->dirty_tiles / frame_count), (unsigned long)average_dirty_pixels,
             (unsigned long)(average_dirty_pixels * 100U / screen_pixels), (unsigned long)metrics->worst_dirty_pixels,
             (unsigned long)metrics->deferred_frames, (unsigned long)metrics->late_completions, (unsigned long)metrics->submission_races,
             (unsigned long)metrics->discarded_notifications,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    clock->metrics = (clock_metrics_t){0};
    clock->metrics_start_us = now_us;
}

static esp_err_t update_clock_scene(clock_context_t* clock, uint32_t elapsed_ms) {
    const time_t now = time(NULL);
    struct tm local_time;
    localtime_r(&now, &local_time);

    if (!clock->time_warning_logged && local_time.tm_year + 1900 < CLOCK_VALID_YEAR) {
        ESP_LOGW(TAG, "system time is not set; clock will start from the system epoch");
        clock->time_warning_logged = true;
    }

    const time_t minute = now / 60;
    if (minute != clock->displayed_minute) {
        esp_err_t err = rebuild_time_mask(clock, &local_time, true);
        if (err != ESP_OK) {
            return err;
        }
        clock->displayed_minute = minute;
    }
    if (local_time.tm_sec != clock->displayed_second) {
        clock->displayed_second = local_time.tm_sec;
        particle_engine_pulse_colon(clock->particles);
    }

    particle_engine_update(clock->particles, elapsed_ms);
    return ESP_OK;
}

static void clock_task(void* argument) {
    clock_context_t* clock = argument;

    while (true) {
        const int64_t frame_start_us = esp_timer_get_time();
        uint32_t elapsed_ms = (uint32_t)((frame_start_us - clock->last_frame_us) / 1000);
        if (elapsed_ms == 0U) {
            elapsed_ms = 1U;
        }
        clock->last_frame_us = frame_start_us;

        const int64_t simulation_start_us = esp_timer_get_time();
        esp_err_t err = update_clock_scene(clock, elapsed_ms);
        const uint32_t simulation_us = (uint32_t)(esp_timer_get_time() - simulation_start_us);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "clock scene update failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        clock_scene_t scene;
        particle_engine_get_scene(clock->particles, &scene);
        clock_renderer_metrics_t renderer_metrics;
        err = clock_renderer_present(clock->renderer, &scene, &renderer_metrics);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "frame presentation failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        const int64_t frame_end_us = esp_timer_get_time();
        accumulate_metrics(clock, simulation_us, &renderer_metrics, (uint32_t)(frame_end_us - frame_start_us));
        log_metrics(clock, frame_end_us);
    }
}

static void clock_cleanup(clock_context_t* clock) {
    if (clock->task != NULL) {
        vTaskDelete(clock->task);
    }
    clock_renderer_destroy(clock->renderer);
    particle_engine_destroy(clock->particles);
    clock_layout_deinit(&clock->layout);
    *clock = (clock_context_t){0};
}

esp_err_t clock_start(esp_lcd_panel_handle_t panel) {
    if (panel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_clock.task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = clock_layout_init(&s_clock.layout, BOARD_LCD_HRES, BOARD_LCD_VRES);
    if (err != ESP_OK) {
        return err;
    }

    s_clock.particles = particle_engine_create(BOARD_LCD_HRES, BOARD_LCD_VRES);
    if (s_clock.particles == NULL) {
        clock_cleanup(&s_clock);
        return ESP_ERR_NO_MEM;
    }

    const time_t now = time(NULL);
    struct tm local_time;
    localtime_r(&now, &local_time);
    err = rebuild_time_mask(&s_clock, &local_time, false);
    if (err != ESP_OK) {
        clock_cleanup(&s_clock);
        return err;
    }

    particle_engine_update(s_clock.particles, 1U);
    clock_scene_t scene;
    particle_engine_get_scene(s_clock.particles, &scene);
    const clock_renderer_background_t background = {
        .color = 0x0000,
    };
    s_clock.renderer = clock_renderer_create(panel, BOARD_LCD_HRES, BOARD_LCD_VRES, &scene, &background);
    if (s_clock.renderer == NULL) {
        clock_cleanup(&s_clock);
        return ESP_ERR_NO_MEM;
    }

    clock_renderer_metrics_t initial_metrics;
    err = clock_renderer_present(s_clock.renderer, &scene, &initial_metrics);
    if (err != ESP_OK) {
        clock_cleanup(&s_clock);
        return err;
    }

    s_clock.displayed_minute = now / 60;
    s_clock.displayed_second = local_time.tm_sec;
    s_clock.last_frame_us = esp_timer_get_time();
    s_clock.metrics_start_us = s_clock.last_frame_us;

    if (xTaskCreate(clock_task, "clock", CLOCK_TASK_STACK_SIZE, &s_clock, CLOCK_TASK_PRIORITY, &s_clock.task) != pdPASS) {
        clock_cleanup(&s_clock);
        return ESP_ERR_NO_MEM;
    }

    const size_t persistent_bytes = particle_engine_memory_size(s_clock.particles) + clock_renderer_memory_size(s_clock.renderer) +
                                    (size_t)BOARD_LCD_HRES * BOARD_LCD_VRES + 4096U;
    ESP_LOGI(TAG, "direct particle clock ready: %u particles, %u-byte particle scene, %u-byte A8 mask, %u tracked bytes (RGB framebuffers owned by display)",
             (unsigned)particle_engine_count(s_clock.particles), (unsigned)particle_engine_memory_size(s_clock.particles),
             (unsigned)(BOARD_LCD_HRES * BOARD_LCD_VRES), (unsigned)persistent_bytes);
    return ESP_OK;
}
