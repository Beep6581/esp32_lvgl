#pragma once

#include "building_clock_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shared semantic geometry used by simulation and presentation. */
#define BUILDING_CLOCK_DIGIT_ENVELOPE_Y 170
#define BUILDING_CLOCK_DIGIT_ENVELOPE_WIDTH 78
#define BUILDING_CLOCK_DIGIT_ENVELOPE_HEIGHT 144
#define BUILDING_CLOCK_COLON_ENVELOPE_X 222
#define BUILDING_CLOCK_COLON_ENVELOPE_WIDTH 36
#define BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION 24

building_clock_rect_t building_clock_section_bounds(building_clock_rect_t envelope, building_clock_section_kind_t kind);
building_clock_point_t building_clock_rect_center(building_clock_rect_t rect);
uint8_t building_clock_section_visible_bricks(building_clock_rect_t envelope, uint8_t numeral, building_clock_section_kind_t kind,
                                              building_clock_rect_t bricks[BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION]);

#ifdef __cplusplus
}
#endif
