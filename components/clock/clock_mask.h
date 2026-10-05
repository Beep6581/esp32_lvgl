/* Version: 2026-10-05 */

#pragma once

#include <stdint.h>

typedef struct {
    const uint8_t* alpha;
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    uint16_t x1;
    uint16_t y1;
    uint16_t x2;
    uint16_t y2;
    uint16_t colon_x1;
    uint16_t colon_y1;
    uint16_t colon_x2;
    uint16_t colon_y2;
} clock_mask_t;
