/* Version: 2026-10-05 */

#include "clock_layout.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "lvgl.h"

#define CLOCK_GLYPH_BUFFER_WIDTH 64U
#define CLOCK_GLYPH_BUFFER_HEIGHT 64U
#define CLOCK_GLYPH_BUFFER_SIZE (CLOCK_GLYPH_BUFFER_WIDTH * CLOCK_GLYPH_BUFFER_HEIGHT)
#define CLOCK_LAYOUT_MAX_WIDTH 440U
#define CLOCK_LAYOUT_SCALE_X_MAX 3U
#define CLOCK_LAYOUT_SCALE_Y_EXTRA 1U
#define CLOCK_LAYOUT_CENTER_Y 270

static void mask_include_point(clock_mask_t* mask, uint16_t x, uint16_t y, bool colon) {
    if (x < mask->x1) {
        mask->x1 = x;
    }
    if (x > mask->x2) {
        mask->x2 = x;
    }
    if (y < mask->y1) {
        mask->y1 = y;
    }
    if (y > mask->y2) {
        mask->y2 = y;
    }

    if (!colon) {
        return;
    }
    if (x < mask->colon_x1) {
        mask->colon_x1 = x;
    }
    if (x > mask->colon_x2) {
        mask->colon_x2 = x;
    }
    if (y < mask->colon_y1) {
        mask->colon_y1 = y;
    }
    if (y > mask->colon_y2) {
        mask->colon_y2 = y;
    }
}

static bool glyph_descriptor(uint32_t character, lv_font_glyph_dsc_t* glyph) {
    return lv_font_get_glyph_dsc(&lv_font_montserrat_48, glyph, character, 0) && glyph->box_w > 0 && glyph->box_h > 0;
}

static esp_err_t draw_scaled_glyph(clock_layout_t* layout, clock_mask_t* mask, uint32_t character, int32_t cell_x, uint16_t cell_width, int32_t top_y, uint16_t tallest_glyph,
                                   uint8_t scale_x, uint8_t scale_y, bool colon) {
    lv_font_glyph_dsc_t glyph;
    if (!glyph_descriptor(character, &glyph)) {
        return ESP_ERR_NOT_FOUND;
    }

    const uint32_t glyph_stride = lv_draw_buf_width_to_stride(glyph.box_w, LV_COLOR_FORMAT_A8);
    const uint32_t required_size = glyph_stride * glyph.box_h;
    if (required_size > CLOCK_GLYPH_BUFFER_SIZE) {
        lv_font_glyph_release_draw_data(&glyph);
        return ESP_ERR_INVALID_SIZE;
    }

    lv_draw_buf_t glyph_buffer;
    if (lv_draw_buf_init(&glyph_buffer, glyph.box_w, glyph.box_h, LV_COLOR_FORMAT_A8, glyph_stride, layout->glyph_pixels, CLOCK_GLYPH_BUFFER_SIZE) != LV_RESULT_OK) {
        lv_font_glyph_release_draw_data(&glyph);
        return ESP_FAIL;
    }

    const lv_draw_buf_t* rendered = lv_font_get_glyph_bitmap(&glyph, &glyph_buffer);
    if (rendered == NULL || rendered->data == NULL) {
        lv_font_glyph_release_draw_data(&glyph);
        return ESP_FAIL;
    }

    const int32_t scaled_width = glyph.box_w * scale_x;
    const int32_t draw_x = cell_x + ((int32_t)cell_width - scaled_width) / 2;
    const int32_t draw_y = top_y + ((int32_t)tallest_glyph - glyph.box_h) * scale_y / 2;

    for (uint16_t source_y = 0; source_y < glyph.box_h; source_y++) {
        const uint8_t* source_row = rendered->data + source_y * rendered->header.stride;
        for (uint16_t source_x = 0; source_x < glyph.box_w; source_x++) {
            const uint8_t alpha = source_row[source_x];
            if (alpha == 0) {
                continue;
            }

            for (uint8_t repeat_y = 0; repeat_y < scale_y; repeat_y++) {
                const int32_t y = draw_y + source_y * scale_y + repeat_y;
                if (y < 0 || y >= layout->height) {
                    continue;
                }
                uint8_t* destination_row = layout->mask_pixels + y * layout->width;

                for (uint8_t repeat_x = 0; repeat_x < scale_x; repeat_x++) {
                    const int32_t x = draw_x + source_x * scale_x + repeat_x;
                    if (x < 0 || x >= layout->width) {
                        continue;
                    }
                    if (alpha > destination_row[x]) {
                        destination_row[x] = alpha;
                    }
                    mask_include_point(mask, (uint16_t)x, (uint16_t)y, colon);
                }
            }
        }
    }

    lv_font_glyph_release_draw_data(&glyph);
    return ESP_OK;
}

esp_err_t clock_layout_init(clock_layout_t* layout, uint16_t width, uint16_t height) {
    if (layout == NULL || width == 0 || height == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * Direct clock mode does not start LVGL, but LVGL's font decoder still
     * needs the draw-buffer stride handler normally installed by lv_init().
     * Initialize only those public handlers; the animation remains outside
     * LVGL and no unused LVGL rendering threads are created.
     */
    if (!lv_is_initialized()) {
        lv_draw_buf_init_with_default_handlers(lv_draw_buf_get_handlers());
    }

    *layout = (clock_layout_t){
        .width = width,
        .height = height,
    };

    layout->mask_pixels = heap_caps_aligned_alloc(64, (size_t)width * height, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    layout->glyph_pixels = heap_caps_aligned_alloc(64, CLOCK_GLYPH_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (layout->mask_pixels == NULL || layout->glyph_pixels == NULL) {
        clock_layout_deinit(layout);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void clock_layout_deinit(clock_layout_t* layout) {
    if (layout == NULL) {
        return;
    }
    heap_caps_free(layout->mask_pixels);
    heap_caps_free(layout->glyph_pixels);
    *layout = (clock_layout_t){0};
}

esp_err_t clock_layout_build_time(clock_layout_t* layout, uint8_t hour, uint8_t minute, clock_mask_t* mask) {
    if (layout == NULL || layout->mask_pixels == NULL || mask == NULL || hour > 23 || minute > 59) {
        return ESP_ERR_INVALID_ARG;
    }

    char text[6] = {
        (char)('0' + hour / 10),
        (char)('0' + hour % 10),
        ':',
        (char)('0' + minute / 10),
        (char)('0' + minute % 10),
        '\0',
    };

    uint16_t digit_advance = 0;
    uint16_t tallest_glyph = 0;
    for (uint32_t character = '0'; character <= '9'; character++) {
        lv_font_glyph_dsc_t glyph;
        if (!glyph_descriptor(character, &glyph)) {
            return ESP_ERR_NOT_FOUND;
        }
        if (glyph.adv_w > digit_advance) {
            digit_advance = glyph.adv_w;
        }
        if (glyph.box_h > tallest_glyph) {
            tallest_glyph = glyph.box_h;
        }
        lv_font_glyph_release_draw_data(&glyph);
    }

    lv_font_glyph_dsc_t colon_glyph;
    if (!glyph_descriptor(':', &colon_glyph)) {
        return ESP_ERR_NOT_FOUND;
    }
    const uint16_t colon_advance = colon_glyph.adv_w;
    lv_font_glyph_release_draw_data(&colon_glyph);

    const uint16_t unscaled_width = digit_advance * 4U + colon_advance;
    uint8_t scale_x = CLOCK_LAYOUT_SCALE_X_MAX;
    while (scale_x > 1U && unscaled_width * scale_x > CLOCK_LAYOUT_MAX_WIDTH) {
        scale_x--;
    }
    const uint8_t scale_y = scale_x + CLOCK_LAYOUT_SCALE_Y_EXTRA;
    const uint16_t total_width = unscaled_width * scale_x;
    const uint16_t total_height = tallest_glyph * scale_y;
    int32_t cell_x = ((int32_t)layout->width - total_width) / 2;
    const int32_t top_y = CLOCK_LAYOUT_CENTER_Y - total_height / 2;

    memset(layout->mask_pixels, 0, (size_t)layout->width * layout->height);
    *mask = (clock_mask_t){
        .alpha = layout->mask_pixels,
        .width = layout->width,
        .height = layout->height,
        .stride = layout->width,
        .x1 = layout->width - 1U,
        .y1 = layout->height - 1U,
        .colon_x1 = layout->width - 1U,
        .colon_y1 = layout->height - 1U,
    };

    for (size_t index = 0; index < 5; index++) {
        const bool colon = text[index] == ':';
        const uint16_t cell_width = (colon ? colon_advance : digit_advance) * scale_x;
        esp_err_t err = draw_scaled_glyph(layout, mask, (uint32_t)text[index], cell_x, cell_width, top_y, tallest_glyph, scale_x, scale_y, colon);
        if (err != ESP_OK) {
            return err;
        }
        cell_x += cell_width;
    }

    return ESP_OK;
}
