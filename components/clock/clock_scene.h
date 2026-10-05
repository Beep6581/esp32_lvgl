/* Version: 2026-10-05a */

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int16_t x;
    int16_t y;
    uint8_t width;
    uint8_t height;
    uint16_t color;
} clock_scene_sprite_t;

typedef struct {
    const uint8_t* cells;
    const uint16_t* palette;
    const clock_scene_sprite_t* sprites;
    size_t sprite_count;
    size_t sprite_capacity;
    uint16_t cell_columns;
    uint16_t cell_rows;
    uint16_t cell_stride;
    uint8_t cell_size;
} clock_scene_t;
