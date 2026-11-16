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

typedef struct {
    building_clock_rect_t bounds;
    building_clock_point_t landing_position;
    /* Release phase is independent of the section's chosen millisecond duration. */
    uint16_t release_per_mille;
    int16_t angle_degrees;
    uint16_t progress_per_mille;
    bool detached;
    bool settled;
} building_clock_falling_brick_t;

/* Read-only, integer trajectories: failure spreads outward from one brick.
 * Sections remain bounded cleanup groups, not rigid moving objects. Sampling
 * at duration_ms also reconstructs the final pile without per-brick entities.
 * Bounds describe the current upright brick footprint; angle is a transient
 * presentation tilt, not a collision shape or persistent rotation state. */
uint8_t building_clock_falling_bricks(building_clock_rect_t envelope, building_clock_rect_t catch_area, const building_clock_falling_section_t* section,
                                      building_clock_falling_brick_t bricks[BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION]);
uint8_t building_clock_debris_bricks(building_clock_rect_t envelope, building_clock_rect_t catch_area, const building_clock_debris_t* debris,
                                     building_clock_rect_t bricks[BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION]);

#ifdef __cplusplus
}
#endif
