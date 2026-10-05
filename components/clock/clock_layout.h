/* Version: 2026-10-05 */

#pragma once

#include <stdint.h>

#include "clock_mask.h"
#include "esp_err.h"

typedef struct {
    uint8_t* mask_pixels;
    uint8_t* glyph_pixels;
    uint16_t width;
    uint16_t height;
} clock_layout_t;

esp_err_t clock_layout_init(clock_layout_t* layout, uint16_t width, uint16_t height);
void clock_layout_deinit(clock_layout_t* layout);
esp_err_t clock_layout_build_time(clock_layout_t* layout, uint8_t hour, uint8_t minute, clock_mask_t* mask);
