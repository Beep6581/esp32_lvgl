/* Version: 2026-10-05a */

#include "clock_renderer.h"

#include <stdbool.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CLOCK_RENDERER_TILE_SIZE 16U
#define CLOCK_RENDERER_FRAME_WAIT_MS 200U

/*
 * The RGB driver scans front while this task updates back. Once frame completion
 * makes the old front reusable, only the tiles changed in the preceding frame
 * are copied from front to back. Both buffers are therefore coherent without a
 * full-screen copy, and the scanout buffer is never modified in place.
 */

struct clock_renderer {
    esp_lcd_panel_handle_t panel;
    uint16_t* frame_buffers[2];
    uint8_t* previous_cells;
    uint8_t* dirty_tiles;
    uint8_t* sync_tiles;
    clock_scene_sprite_t* previous_sprites;
    clock_renderer_background_t background;
    volatile TaskHandle_t waiting_task;
    volatile uint32_t frame_complete_count;
    uint32_t safe_frame_count;
    uint16_t width;
    uint16_t height;
    uint16_t tile_columns;
    uint16_t tile_rows;
    uint16_t cell_columns;
    uint16_t cell_rows;
    uint16_t cell_stride;
    uint8_t cell_size;
    uint8_t front_index;
    uint8_t back_index;
    size_t tile_count;
    size_t cell_count;
    size_t previous_sprite_count;
    size_t sprite_capacity;
    bool frame_pending;
};

static const char* TAG = "clock_renderer";

static bool IRAM_ATTR frame_buffer_complete_cb(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t* event_data, void* user_ctx) {
    (void)panel;
    (void)event_data;

    clock_renderer_t* renderer = user_ctx;
    renderer->frame_complete_count++;

    BaseType_t task_woken = pdFALSE;
    if (renderer->waiting_task != NULL) {
        vTaskNotifyGiveFromISR(renderer->waiting_task, &task_woken);
    }
    return task_woken == pdTRUE;
}

static bool scene_matches_renderer(const clock_renderer_t* renderer, const clock_scene_t* scene) {
    return scene != NULL && scene->cells != NULL && scene->palette != NULL && scene->cell_columns == renderer->cell_columns &&
           scene->cell_rows == renderer->cell_rows && scene->cell_stride == renderer->cell_stride && scene->cell_size == renderer->cell_size &&
           scene->sprite_count <= renderer->sprite_capacity && (scene->sprite_count == 0U || scene->sprites != NULL);
}

static bool frame_count_reached(uint32_t current, uint32_t target) {
    return (int32_t)(current - target) >= 0;
}

static esp_err_t wait_for_safe_buffer(clock_renderer_t* renderer) {
    renderer->waiting_task = xTaskGetCurrentTaskHandle();
    while (!frame_count_reached(renderer->frame_complete_count, renderer->safe_frame_count)) {
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CLOCK_RENDERER_FRAME_WAIT_MS)) == 0U) {
            ESP_LOGE(TAG, "timed out waiting for frame completion");
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

static void copy_tile(const clock_renderer_t* renderer, uint16_t* destination, const uint16_t* source, uint16_t tile_index) {
    const uint16_t tile_x = tile_index % renderer->tile_columns;
    const uint16_t tile_y = tile_index / renderer->tile_columns;
    const uint16_t x1 = tile_x * CLOCK_RENDERER_TILE_SIZE;
    const uint16_t y1 = tile_y * CLOCK_RENDERER_TILE_SIZE;
    const uint16_t x2 = x1 + CLOCK_RENDERER_TILE_SIZE < renderer->width ? x1 + CLOCK_RENDERER_TILE_SIZE : renderer->width;
    const uint16_t y2 = y1 + CLOCK_RENDERER_TILE_SIZE < renderer->height ? y1 + CLOCK_RENDERER_TILE_SIZE : renderer->height;

    for (uint16_t y = y1; y < y2; y++) {
        memcpy(&destination[(uint32_t)y * renderer->width + x1], &source[(uint32_t)y * renderer->width + x1], (size_t)(x2 - x1) * sizeof(uint16_t));
    }
}

static void synchronize_back_buffer(clock_renderer_t* renderer) {
    uint16_t* destination = renderer->frame_buffers[renderer->back_index];
    const uint16_t* source = renderer->frame_buffers[renderer->front_index];
    for (uint16_t tile = 0; tile < renderer->tile_count; tile++) {
        if (renderer->sync_tiles[tile] != 0U) {
            copy_tile(renderer, destination, source, tile);
        }
    }
}

static void mark_rectangle(clock_renderer_t* renderer, int32_t x, int32_t y, int32_t width, int32_t height) {
    int32_t x2 = x + width;
    int32_t y2 = y + height;
    if (width <= 0 || height <= 0 || x >= renderer->width || y >= renderer->height || x2 <= 0 || y2 <= 0) {
        return;
    }

    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (x2 > renderer->width) {
        x2 = renderer->width;
    }
    if (y2 > renderer->height) {
        y2 = renderer->height;
    }

    const uint16_t tile_x1 = (uint16_t)x / CLOCK_RENDERER_TILE_SIZE;
    const uint16_t tile_y1 = (uint16_t)y / CLOCK_RENDERER_TILE_SIZE;
    const uint16_t tile_x2 = (uint16_t)(x2 - 1) / CLOCK_RENDERER_TILE_SIZE;
    const uint16_t tile_y2 = (uint16_t)(y2 - 1) / CLOCK_RENDERER_TILE_SIZE;
    for (uint16_t tile_y = tile_y1; tile_y <= tile_y2; tile_y++) {
        for (uint16_t tile_x = tile_x1; tile_x <= tile_x2; tile_x++) {
            renderer->dirty_tiles[(uint32_t)tile_y * renderer->tile_columns + tile_x] = 1U;
        }
    }
}

static void determine_dirty_tiles(clock_renderer_t* renderer, const clock_scene_t* scene) {
    memset(renderer->dirty_tiles, 0, renderer->tile_count);

    for (uint16_t cell_y = 0; cell_y < renderer->cell_rows; cell_y++) {
        const uint8_t* cells = &scene->cells[(uint32_t)cell_y * scene->cell_stride];
        uint8_t* previous = &renderer->previous_cells[(uint32_t)cell_y * renderer->cell_columns];
        for (uint16_t cell_x = 0; cell_x < renderer->cell_columns; cell_x++) {
            if (cells[cell_x] != previous[cell_x]) {
                previous[cell_x] = cells[cell_x];
                mark_rectangle(renderer, cell_x * renderer->cell_size, cell_y * renderer->cell_size, renderer->cell_size, renderer->cell_size);
            }
        }
    }

    for (size_t index = 0; index < renderer->previous_sprite_count; index++) {
        const clock_scene_sprite_t* sprite = &renderer->previous_sprites[index];
        mark_rectangle(renderer, sprite->x, sprite->y, sprite->width, sprite->height);
    }
    for (size_t index = 0; index < scene->sprite_count; index++) {
        const clock_scene_sprite_t* sprite = &scene->sprites[index];
        mark_rectangle(renderer, sprite->x, sprite->y, sprite->width, sprite->height);
        renderer->previous_sprites[index] = *sprite;
    }
    renderer->previous_sprite_count = scene->sprite_count;
}

static void restore_background(const clock_renderer_t* renderer, uint16_t* frame_buffer, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    for (uint16_t y = y1; y < y2; y++) {
        uint16_t* destination = &frame_buffer[(uint32_t)y * renderer->width + x1];
        if (renderer->background.pixels != NULL) {
            const uint16_t* source = (const uint16_t*)((const uint8_t*)renderer->background.pixels + (uint32_t)y * renderer->background.stride_bytes) + x1;
            memcpy(destination, source, (size_t)(x2 - x1) * sizeof(uint16_t));
        } else {
            for (uint16_t x = x1; x < x2; x++) {
                *destination++ = renderer->background.color;
            }
        }
    }
}

static void draw_scene_tile(const clock_renderer_t* renderer, const clock_scene_t* scene, uint16_t* frame_buffer, uint16_t tile_index) {
    const uint16_t tile_x = tile_index % renderer->tile_columns;
    const uint16_t tile_y = tile_index / renderer->tile_columns;
    const uint16_t x1 = tile_x * CLOCK_RENDERER_TILE_SIZE;
    const uint16_t y1 = tile_y * CLOCK_RENDERER_TILE_SIZE;
    const uint16_t x2 = x1 + CLOCK_RENDERER_TILE_SIZE < renderer->width ? x1 + CLOCK_RENDERER_TILE_SIZE : renderer->width;
    const uint16_t y2 = y1 + CLOCK_RENDERER_TILE_SIZE < renderer->height ? y1 + CLOCK_RENDERER_TILE_SIZE : renderer->height;

    restore_background(renderer, frame_buffer, x1, y1, x2, y2);

    const uint16_t cell_x1 = x1 / renderer->cell_size;
    const uint16_t cell_y1 = y1 / renderer->cell_size;
    const uint16_t cell_x2 = (x2 + renderer->cell_size - 1U) / renderer->cell_size;
    const uint16_t cell_y2 = (y2 + renderer->cell_size - 1U) / renderer->cell_size;
    for (uint16_t cell_y = cell_y1; cell_y < cell_y2 && cell_y < renderer->cell_rows; cell_y++) {
        for (uint16_t cell_x = cell_x1; cell_x < cell_x2 && cell_x < renderer->cell_columns; cell_x++) {
            const uint8_t level = scene->cells[(uint32_t)cell_y * scene->cell_stride + cell_x];
            if (level == 0U) {
                continue;
            }

            const uint16_t color = scene->palette[level];
            const uint16_t draw_x1 = cell_x * renderer->cell_size > x1 ? cell_x * renderer->cell_size : x1;
            const uint16_t draw_y1 = cell_y * renderer->cell_size > y1 ? cell_y * renderer->cell_size : y1;
            const uint16_t cell_right = (cell_x + 1U) * renderer->cell_size;
            const uint16_t cell_bottom = (cell_y + 1U) * renderer->cell_size;
            const uint16_t draw_x2 = cell_right < x2 ? cell_right : x2;
            const uint16_t draw_y2 = cell_bottom < y2 ? cell_bottom : y2;
            for (uint16_t y = draw_y1; y < draw_y2; y++) {
                uint16_t* destination = &frame_buffer[(uint32_t)y * renderer->width + draw_x1];
                for (uint16_t x = draw_x1; x < draw_x2; x++) {
                    *destination++ = color;
                }
            }
        }
    }

    for (size_t index = 0; index < scene->sprite_count; index++) {
        const clock_scene_sprite_t* sprite = &scene->sprites[index];
        const int32_t sprite_x2 = sprite->x + sprite->width;
        const int32_t sprite_y2 = sprite->y + sprite->height;
        if (sprite->x >= x2 || sprite->y >= y2 || sprite_x2 <= x1 || sprite_y2 <= y1) {
            continue;
        }
        const uint16_t draw_x1 = sprite->x > x1 ? (uint16_t)sprite->x : x1;
        const uint16_t draw_y1 = sprite->y > y1 ? (uint16_t)sprite->y : y1;
        const uint16_t draw_x2 = sprite_x2 < x2 ? (uint16_t)sprite_x2 : x2;
        const uint16_t draw_y2 = sprite_y2 < y2 ? (uint16_t)sprite_y2 : y2;
        for (uint16_t y = draw_y1; y < draw_y2; y++) {
            uint16_t* destination = &frame_buffer[(uint32_t)y * renderer->width + draw_x1];
            for (uint16_t x = draw_x1; x < draw_x2; x++) {
                *destination++ = sprite->color;
            }
        }
    }
}

clock_renderer_t* clock_renderer_create(esp_lcd_panel_handle_t panel, uint16_t width, uint16_t height, const clock_scene_t* initial_scene,
                                        const clock_renderer_background_t* background) {
    if (panel == NULL || width == 0U || height == 0U || initial_scene == NULL || initial_scene->cells == NULL || initial_scene->palette == NULL ||
        initial_scene->cell_columns == 0U || initial_scene->cell_rows == 0U || initial_scene->cell_size == 0U) {
        return NULL;
    }

    clock_renderer_t* renderer = heap_caps_calloc(1, sizeof(*renderer), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (renderer == NULL) {
        return NULL;
    }

    renderer->panel = panel;
    renderer->width = width;
    renderer->height = height;
    renderer->tile_columns = (width + CLOCK_RENDERER_TILE_SIZE - 1U) / CLOCK_RENDERER_TILE_SIZE;
    renderer->tile_rows = (height + CLOCK_RENDERER_TILE_SIZE - 1U) / CLOCK_RENDERER_TILE_SIZE;
    renderer->tile_count = (size_t)renderer->tile_columns * renderer->tile_rows;
    renderer->cell_columns = initial_scene->cell_columns;
    renderer->cell_rows = initial_scene->cell_rows;
    renderer->cell_stride = initial_scene->cell_stride;
    renderer->cell_size = initial_scene->cell_size;
    renderer->cell_count = (size_t)renderer->cell_columns * renderer->cell_rows;
    renderer->sprite_capacity = initial_scene->sprite_capacity;
    if (renderer->sprite_capacity < initial_scene->sprite_count) {
        renderer->sprite_capacity = initial_scene->sprite_count;
    }
    if (renderer->sprite_capacity == 0U) {
        renderer->sprite_capacity = 1U;
    }
    renderer->front_index = 0U;
    renderer->back_index = 1U;
    if (background != NULL) {
        renderer->background = *background;
    }

    renderer->previous_cells = heap_caps_calloc(renderer->cell_count, sizeof(*renderer->previous_cells), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    renderer->dirty_tiles = heap_caps_calloc(renderer->tile_count, sizeof(*renderer->dirty_tiles), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    renderer->sync_tiles = heap_caps_calloc(renderer->tile_count, sizeof(*renderer->sync_tiles), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    renderer->previous_sprites = heap_caps_calloc(renderer->sprite_capacity, sizeof(*renderer->previous_sprites), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (renderer->previous_cells == NULL || renderer->dirty_tiles == NULL || renderer->sync_tiles == NULL || renderer->previous_sprites == NULL ||
        esp_lcd_rgb_panel_get_frame_buffer(panel, 2, (void**)&renderer->frame_buffers[0], (void**)&renderer->frame_buffers[1]) != ESP_OK) {
        clock_renderer_destroy(renderer);
        return NULL;
    }

    const size_t frame_buffer_bytes = (size_t)width * height * sizeof(uint16_t);
    memset(renderer->frame_buffers[0], 0, frame_buffer_bytes);
    memset(renderer->frame_buffers[1], 0, frame_buffer_bytes);

    const esp_lcd_rgb_panel_event_callbacks_t callbacks = {
        .on_frame_buf_complete = frame_buffer_complete_cb,
    };
    if (esp_lcd_rgb_panel_register_event_callbacks(panel, &callbacks, renderer) != ESP_OK) {
        clock_renderer_destroy(renderer);
        return NULL;
    }

    ESP_LOGI(TAG, "direct renderer ready: fb0=%p (%s), fb1=%p (%s), %ux%u tiles, %u-byte tile",
             renderer->frame_buffers[0], esp_ptr_external_ram(renderer->frame_buffers[0]) ? "PSRAM" : "internal", renderer->frame_buffers[1],
             esp_ptr_external_ram(renderer->frame_buffers[1]) ? "PSRAM" : "internal", renderer->tile_columns, renderer->tile_rows,
             (unsigned)(CLOCK_RENDERER_TILE_SIZE * CLOCK_RENDERER_TILE_SIZE * sizeof(uint16_t)));
    return renderer;
}

void clock_renderer_destroy(clock_renderer_t* renderer) {
    if (renderer == NULL) {
        return;
    }
    if (renderer->panel != NULL) {
        const esp_lcd_rgb_panel_event_callbacks_t callbacks = {0};
        esp_lcd_rgb_panel_register_event_callbacks(renderer->panel, &callbacks, NULL);
    }
    heap_caps_free(renderer->previous_cells);
    heap_caps_free(renderer->dirty_tiles);
    heap_caps_free(renderer->sync_tiles);
    heap_caps_free(renderer->previous_sprites);
    heap_caps_free(renderer);
}

esp_err_t clock_renderer_present(clock_renderer_t* renderer, const clock_scene_t* scene, clock_renderer_metrics_t* metrics) {
    if (renderer == NULL || !scene_matches_renderer(renderer, scene)) {
        return ESP_ERR_INVALID_ARG;
    }

    const int64_t total_start_us = esp_timer_get_time();
    clock_renderer_metrics_t result = {0};

    uint32_t completed_at_reuse = renderer->frame_complete_count;
    if (renderer->frame_pending) {
        const int64_t wait_start_us = esp_timer_get_time();
        esp_err_t err = wait_for_safe_buffer(renderer);
        result.wait_us = (uint32_t)(esp_timer_get_time() - wait_start_us);
        if (err != ESP_OK) {
            return err;
        }
        completed_at_reuse = renderer->frame_complete_count;
        result.late_completions = (uint16_t)(completed_at_reuse - renderer->safe_frame_count);

        const uint8_t old_front = renderer->front_index;
        renderer->front_index = renderer->back_index;
        renderer->back_index = old_front;

        const int64_t sync_start_us = esp_timer_get_time();
        synchronize_back_buffer(renderer);
        result.sync_us = (uint32_t)(esp_timer_get_time() - sync_start_us);
    }

    const int64_t dirty_start_us = esp_timer_get_time();
    determine_dirty_tiles(renderer, scene);
    result.dirty_us = (uint32_t)(esp_timer_get_time() - dirty_start_us);

    const int64_t render_start_us = esp_timer_get_time();
    uint16_t* back_buffer = renderer->frame_buffers[renderer->back_index];
    for (uint16_t tile = 0; tile < renderer->tile_count; tile++) {
        if (renderer->dirty_tiles[tile] == 0U) {
            continue;
        }
        draw_scene_tile(renderer, scene, back_buffer, tile);
        result.dirty_tiles++;

        const uint16_t tile_x = tile % renderer->tile_columns;
        const uint16_t tile_y = tile / renderer->tile_columns;
        const uint16_t tile_width = tile_x * CLOCK_RENDERER_TILE_SIZE + CLOCK_RENDERER_TILE_SIZE <= renderer->width
                                        ? CLOCK_RENDERER_TILE_SIZE
                                        : renderer->width - tile_x * CLOCK_RENDERER_TILE_SIZE;
        const uint16_t tile_height = tile_y * CLOCK_RENDERER_TILE_SIZE + CLOCK_RENDERER_TILE_SIZE <= renderer->height
                                         ? CLOCK_RENDERER_TILE_SIZE
                                         : renderer->height - tile_y * CLOCK_RENDERER_TILE_SIZE;
        result.dirty_pixels += (uint32_t)tile_width * tile_height;
    }
    result.render_us = (uint32_t)(esp_timer_get_time() - render_start_us);

    memcpy(renderer->sync_tiles, renderer->dirty_tiles, renderer->tile_count);
    const uint32_t frame_count_before_submit = renderer->frame_complete_count;
    result.deferred_frames = (uint16_t)(frame_count_before_submit - completed_at_reuse);
    renderer->waiting_task = xTaskGetCurrentTaskHandle();
    result.discarded_notifications = (uint16_t)ulTaskNotifyTake(pdTRUE, 0);
    esp_err_t err = esp_lcd_panel_draw_bitmap(renderer->panel, 0, 0, renderer->width, renderer->height, back_buffer);
    if (err != ESP_OK) {
        return err;
    }
    const uint32_t frame_count_after_submit = renderer->frame_complete_count;
    result.submission_races = (uint16_t)(frame_count_after_submit - frame_count_before_submit);
    /* A completion observed after this post-submit snapshot unambiguously makes the old front buffer reusable. */
    renderer->safe_frame_count = frame_count_after_submit + 1U;
    renderer->frame_pending = true;
    result.total_us = (uint32_t)(esp_timer_get_time() - total_start_us);

    if (metrics != NULL) {
        *metrics = result;
    }
    return ESP_OK;
}

size_t clock_renderer_memory_size(const clock_renderer_t* renderer) {
    if (renderer == NULL) {
        return 0U;
    }
    return sizeof(*renderer) + renderer->cell_count + renderer->tile_count * 2U + renderer->sprite_capacity * sizeof(clock_scene_sprite_t);
}
