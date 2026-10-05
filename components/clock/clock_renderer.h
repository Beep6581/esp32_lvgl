/* Version: 2026-10-05a */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "clock_scene.h"
#include "esp_err.h"
#include "esp_lcd_types.h"

typedef struct clock_renderer clock_renderer_t;

typedef struct {
    const uint16_t* pixels;
    uint32_t stride_bytes;
    uint16_t color;
} clock_renderer_background_t;

typedef struct {
    uint32_t wait_us;
    uint32_t sync_us;
    uint32_t dirty_us;
    uint32_t render_us;
    uint32_t total_us;
    uint32_t dirty_pixels;
    uint16_t discarded_notifications;
    uint16_t deferred_frames;
    uint16_t late_completions;
    uint16_t submission_races;
    uint16_t dirty_tiles;
} clock_renderer_metrics_t;

clock_renderer_t* clock_renderer_create(esp_lcd_panel_handle_t panel, uint16_t width, uint16_t height, const clock_scene_t* initial_scene,
                                        const clock_renderer_background_t* background);
void clock_renderer_destroy(clock_renderer_t* renderer);
esp_err_t clock_renderer_present(clock_renderer_t* renderer, const clock_scene_t* scene, clock_renderer_metrics_t* metrics);
size_t clock_renderer_memory_size(const clock_renderer_t* renderer);
