#include "building_clock_geometry.h"

#include <stddef.h>

#define SECTION_THICKNESS 14
#define SECTION_BEAM_INSET 4
#define BRICK_COLUMNS 7U
#define BRICK_ROWS 11U
#define BRICK_WIDTH 9
#define BRICK_HEIGHT 10
#define BRICK_X_STEP 10
#define BRICK_Y_STEP 12

/* Coarse masonry silhouettes, stored top course first. */
static const uint8_t s_numeral_courses[10][BRICK_ROWS] = {
    {0x3e, 0x7f, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x7f, 0x3e}, {0x0c, 0x1c, 0x3c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x3f, 0x3f},
    {0x3e, 0x7f, 0x03, 0x03, 0x06, 0x1c, 0x30, 0x60, 0x60, 0x7f, 0x7f}, {0x3e, 0x7f, 0x03, 0x03, 0x06, 0x1e, 0x07, 0x03, 0x03, 0x7f, 0x3e},
    {0x06, 0x0e, 0x1e, 0x36, 0x66, 0x7f, 0x7f, 0x06, 0x06, 0x06, 0x06}, {0x7f, 0x7f, 0x60, 0x60, 0x7e, 0x7f, 0x03, 0x03, 0x03, 0x7f, 0x3e},
    {0x1e, 0x3f, 0x60, 0x60, 0x7e, 0x7f, 0x63, 0x63, 0x63, 0x7f, 0x3e}, {0x7f, 0x7f, 0x03, 0x06, 0x06, 0x0c, 0x0c, 0x18, 0x18, 0x30, 0x30},
    {0x3e, 0x7f, 0x63, 0x63, 0x36, 0x3e, 0x6f, 0x63, 0x63, 0x7f, 0x3e}, {0x3e, 0x7f, 0x63, 0x63, 0x63, 0x7f, 0x3f, 0x03, 0x03, 0x7e, 0x3c},
};

static bool numeral_has_section(uint8_t numeral, building_clock_section_kind_t kind) {
    static const uint8_t masks[10] = {
        0x7b, 0x50, 0x37, 0x57, 0x5c, 0x4f, 0x6f, 0x52, 0x7f, 0x5f,
    };
    if (numeral >= 10U || kind > BUILDING_CLOCK_SECTION_LOWER_RIGHT_PIER) {
        return false;
    }
    return (masks[numeral] & (uint8_t)(1U << kind)) != 0U;
}

static building_clock_section_kind_t brick_section(uint8_t numeral, uint8_t row, uint8_t column) {
    if (row <= 1U && numeral_has_section(numeral, BUILDING_CLOCK_SECTION_TOP_BEAM)) {
        return BUILDING_CLOCK_SECTION_TOP_BEAM;
    }
    if (row >= 9U && numeral_has_section(numeral, BUILDING_CLOCK_SECTION_FOUNDATION)) {
        /* Each side pier owns its own footing, allowing independent fronts.
         * The center footing remains a separate bounded masonry section. */
        if (column <= 1U && numeral_has_section(numeral, BUILDING_CLOCK_SECTION_LOWER_LEFT_PIER)) {
            return BUILDING_CLOCK_SECTION_LOWER_LEFT_PIER;
        }
        if (column >= 5U && numeral_has_section(numeral, BUILDING_CLOCK_SECTION_LOWER_RIGHT_PIER)) {
            return BUILDING_CLOCK_SECTION_LOWER_RIGHT_PIER;
        }
        return BUILDING_CLOCK_SECTION_FOUNDATION;
    }
    if ((row == 5U || row == 6U) && numeral_has_section(numeral, BUILDING_CLOCK_SECTION_MIDDLE_BEAM)) {
        if (column <= 1U && numeral_has_section(numeral, BUILDING_CLOCK_SECTION_LOWER_LEFT_PIER)) {
            return BUILDING_CLOCK_SECTION_LOWER_LEFT_PIER;
        }
        if (column >= 5U && numeral_has_section(numeral, BUILDING_CLOCK_SECTION_LOWER_RIGHT_PIER)) {
            return BUILDING_CLOCK_SECTION_LOWER_RIGHT_PIER;
        }
        return BUILDING_CLOCK_SECTION_MIDDLE_BEAM;
    }

    const bool left = column < BRICK_COLUMNS / 2U;
    const bool upper = row < BRICK_ROWS / 2U;
    building_clock_section_kind_t candidate;
    if (upper) {
        candidate = left ? BUILDING_CLOCK_SECTION_UPPER_LEFT_PIER : BUILDING_CLOCK_SECTION_UPPER_RIGHT_PIER;
    } else {
        candidate = left ? BUILDING_CLOCK_SECTION_LOWER_LEFT_PIER : BUILDING_CLOCK_SECTION_LOWER_RIGHT_PIER;
    }
    if (numeral_has_section(numeral, candidate)) {
        return candidate;
    }
    if (upper && numeral_has_section(numeral, BUILDING_CLOCK_SECTION_UPPER_RIGHT_PIER)) {
        return BUILDING_CLOCK_SECTION_UPPER_RIGHT_PIER;
    }
    if (!upper && numeral_has_section(numeral, BUILDING_CLOCK_SECTION_LOWER_RIGHT_PIER)) {
        return BUILDING_CLOCK_SECTION_LOWER_RIGHT_PIER;
    }
    return numeral_has_section(numeral, BUILDING_CLOCK_SECTION_MIDDLE_BEAM) ? BUILDING_CLOCK_SECTION_MIDDLE_BEAM : BUILDING_CLOCK_SECTION_FINISHING_OR_INFILL;
}

building_clock_rect_t building_clock_section_bounds(building_clock_rect_t envelope, building_clock_section_kind_t kind) {
    const int16_t thickness = SECTION_THICKNESS;
    const int16_t middle_y = (int16_t)(envelope.y + (int16_t)(envelope.height / 2U) - thickness / 2);
    const int16_t right_x = (int16_t)(envelope.x + (int16_t)envelope.width - thickness);
    const int16_t lower_y = (int16_t)(middle_y + thickness);
    const uint16_t vertical_height = (uint16_t)(envelope.height / 2U - SECTION_THICKNESS - 2U);

    switch (kind) {
    case BUILDING_CLOCK_SECTION_FOUNDATION:
        return (building_clock_rect_t){
            .x = (int16_t)(envelope.x + SECTION_BEAM_INSET),
            .y = (int16_t)(envelope.y + (int16_t)envelope.height - thickness),
            .width = (uint16_t)(envelope.width - 2U * SECTION_BEAM_INSET),
            .height = SECTION_THICKNESS,
        };
    case BUILDING_CLOCK_SECTION_TOP_BEAM:
        return (building_clock_rect_t){
            .x = (int16_t)(envelope.x + SECTION_BEAM_INSET),
            .y = envelope.y,
            .width = (uint16_t)(envelope.width - 2U * SECTION_BEAM_INSET),
            .height = SECTION_THICKNESS,
        };
    case BUILDING_CLOCK_SECTION_MIDDLE_BEAM:
        return (building_clock_rect_t){
            .x = (int16_t)(envelope.x + SECTION_BEAM_INSET),
            .y = middle_y,
            .width = (uint16_t)(envelope.width - 2U * SECTION_BEAM_INSET),
            .height = SECTION_THICKNESS,
        };
    case BUILDING_CLOCK_SECTION_UPPER_LEFT_PIER:
        return (building_clock_rect_t){.x = envelope.x, .y = (int16_t)(envelope.y + thickness - 2), .width = SECTION_THICKNESS, .height = vertical_height};
    case BUILDING_CLOCK_SECTION_UPPER_RIGHT_PIER:
        return (building_clock_rect_t){.x = right_x, .y = (int16_t)(envelope.y + thickness - 2), .width = SECTION_THICKNESS, .height = vertical_height};
    case BUILDING_CLOCK_SECTION_LOWER_LEFT_PIER:
        return (building_clock_rect_t){.x = envelope.x, .y = lower_y, .width = SECTION_THICKNESS, .height = vertical_height};
    case BUILDING_CLOCK_SECTION_LOWER_RIGHT_PIER:
        return (building_clock_rect_t){.x = right_x, .y = lower_y, .width = SECTION_THICKNESS, .height = vertical_height};
    case BUILDING_CLOCK_SECTION_CORNER_OR_DIAGONAL:
    case BUILDING_CLOCK_SECTION_FINISHING_OR_INFILL:
    default:
        return (building_clock_rect_t){
            .x = (int16_t)(envelope.x + (int16_t)(envelope.width / 2U) - thickness / 2),
            .y = middle_y,
            .width = SECTION_THICKNESS,
            .height = SECTION_THICKNESS,
        };
    }
}

building_clock_point_t building_clock_rect_center(building_clock_rect_t rect) {
    return (building_clock_point_t){
        .x = (int16_t)(rect.x + (int16_t)(rect.width / 2U)),
        .y = (int16_t)(rect.y + (int16_t)(rect.height / 2U)),
    };
}

uint8_t building_clock_section_visible_bricks(building_clock_rect_t envelope, uint8_t numeral, building_clock_section_kind_t kind,
                                              building_clock_rect_t bricks[BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION]) {
    if (numeral >= 10U || bricks == NULL) {
        return 0U;
    }

    const int16_t origin_x = (int16_t)(envelope.x + ((int16_t)envelope.width - (int16_t)(BRICK_COLUMNS * BRICK_X_STEP - 1U)) / 2);
    const int16_t origin_y = (int16_t)(envelope.y + ((int16_t)envelope.height - (int16_t)(BRICK_ROWS * BRICK_Y_STEP - 2U)) / 2);
    uint8_t count = 0U;
    /* Work one narrow vertical strip at a time, bottom course first. This
     * shared order is also the prefix retained when construction is interrupted. */
    for (uint8_t strip = 0U; strip < 3U; strip++) {
        for (uint8_t course = 0U; course < BRICK_ROWS; course++) {
            const uint8_t row = (uint8_t)(BRICK_ROWS - 1U - course);
            for (uint8_t column = (uint8_t)(strip * 3U); column < BRICK_COLUMNS && column < (strip + 1U) * 3U; column++) {
                if ((s_numeral_courses[numeral][row] & (uint8_t)(1U << (BRICK_COLUMNS - 1U - column))) == 0U || brick_section(numeral, row, column) != kind) {
                    continue;
                }
                if (count >= BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION) {
                    return count;
                }
                const int16_t joint_offset = (row & 1U) != 0U ? 1 : 0;
                bricks[count++] = (building_clock_rect_t){
                    .x = (int16_t)(origin_x + column * BRICK_X_STEP + joint_offset),
                    .y = (int16_t)(origin_y + row * BRICK_Y_STEP),
                    .width = BRICK_WIDTH,
                    .height = BRICK_HEIGHT,
                };
            }
        }
    }
    return count;
}

static building_clock_point_t brick_landing_position(building_clock_rect_t original, building_clock_rect_t catch_area, int32_t drift, uint8_t brick_id) {
    int32_t x = original.x + (int32_t)(original.width / 2U) + drift;
    const int32_t minimum_x = catch_area.x + (int32_t)(original.width / 2U);
    const int32_t maximum_x = catch_area.x + (int32_t)catch_area.width - (int32_t)original.width + (int32_t)(original.width / 2U);
    if (x < minimum_x) {
        x = minimum_x;
    } else if (x > maximum_x) {
        x = maximum_x;
    }
    const uint32_t pile_depth = catch_area.height - original.height;
    const uint32_t course_offset = (uint32_t)(brick_id % 3U) * 2U;
    const int32_t y = catch_area.y + (int32_t)catch_area.height - (int32_t)original.height + (int32_t)(original.height / 2U) - (int32_t)(course_offset < pile_depth ? course_offset : pile_depth);
    return (building_clock_point_t){.x = (int16_t)x, .y = (int16_t)y};
}

static uint32_t brick_failure_distance(building_clock_point_t start, building_clock_point_t origin) {
    const int32_t dx = (int32_t)start.x - origin.x;
    const int32_t dy = (int32_t)start.y - origin.y;
    return (uint32_t)(dx < 0 ? -dx : dx) + (uint32_t)(dy < 0 ? -dy : dy);
}

uint8_t building_clock_falling_bricks(building_clock_rect_t envelope, building_clock_rect_t catch_area, const building_clock_falling_section_t* section,
                                      building_clock_falling_brick_t bricks[BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION]) {
    if (section == NULL || bricks == NULL || section->duration_ms == 0U || section->masonry_progress_per_mille > 1000U) {
        return 0U;
    }
    building_clock_rect_t original[BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION];
    const uint8_t total = building_clock_section_visible_bricks(envelope, section->source_numeral, section->section_kind, original);
    const uint8_t count = (uint8_t)(((uint32_t)total * section->masonry_progress_per_mille + 999U) / 1000U);
    if (count == 0U || catch_area.width < original[0].width || catch_area.height < original[0].height) {
        return 0U;
    }

    const building_clock_point_t origin = building_clock_rect_center(original[section->failure_brick_id % count]);
    uint32_t maximum_distance = 0U;
    int32_t maximum_drop = 0;
    for (uint8_t brick = 0U; brick < count; brick++) {
        const building_clock_point_t start = building_clock_rect_center(original[brick]);
        /* Course/column distance gives neighboring bricks neighboring failure
         * times, independent of the construction-order array indices. */
        const uint32_t distance = brick_failure_distance(start, origin);
        if (distance > maximum_distance) {
            maximum_distance = distance;
        }
        bricks[brick] = (building_clock_falling_brick_t){
            .bounds = original[brick],
            .landing_position = brick_landing_position(original[brick], catch_area, (int32_t)section->landing_position.x - section->start_position.x, brick),
        };
        const int32_t drop = (int32_t)bricks[brick].landing_position.y - start.y;
        if (drop > maximum_drop) {
            maximum_drop = drop;
        }
    }

    /* Spread consumes only the first third of the existing fall window. The
     * remaining window gives all bricks in this group the same acceleration;
     * lower bricks hit the ground earlier. Collapse pacing is unchanged. */
    /* One wide division per section, not three per brick. Q8 acceleration
     * keeps the per-brick products in 32 bits even for 16-bit coordinates.
     * Millisecond input still accepts the full uint32_t duration range. */
    const uint32_t phase = section->elapsed_ms < section->duration_ms ? (uint32_t)(((uint64_t)section->elapsed_ms * 1000U) / section->duration_ms) : 1000U;
    const uint32_t spread = maximum_distance > 0U ? 333U : 0U;
    for (uint8_t brick = 0U; brick < count; brick++) {
        building_clock_falling_brick_t* state = &bricks[brick];
        const building_clock_point_t start = building_clock_rect_center(original[brick]);
        state->release_per_mille = maximum_distance > 0U ? (uint16_t)(brick_failure_distance(start, origin) * spread / maximum_distance) : 0U;
        state->detached = phase >= state->release_per_mille;
        if (!state->detached) {
            continue;
        }
        const uint32_t progress = (phase - state->release_per_mille) * 256U / (1000U - spread);
        const uint32_t bounded_progress = progress < 256U ? progress : 256U;
        const int32_t drop = (int32_t)(((uint32_t)maximum_drop * bounded_progress * bounded_progress) >> 16U);
        const int32_t distance = (int32_t)state->landing_position.y - start.y;
        state->settled = state->detached && drop >= distance;
        state->progress_per_mille = distance > 0 ? (uint16_t)(drop < distance ? (drop * 1000) / distance : 1000) : 1000U;
        state->bounds.x = (int16_t)(original[brick].x + ((int32_t)state->landing_position.x - start.x) * state->progress_per_mille / 1000);
        state->bounds.y = (int16_t)(original[brick].y + (drop < distance ? drop : distance));
        /* Small alternating tumbles, derived from geometry rather than stored
         * angular state. Bricks return flat as they enter the shallow pile. */
        const int16_t angle = (int16_t)(((uint32_t)state->progress_per_mille * (1000U - state->progress_per_mille)) >> 13U);
        state->angle_degrees = (((uint16_t)start.x ^ (uint16_t)start.y ^ section->failure_brick_id) & 1U) != 0U ? angle : (int16_t)-angle;
    }
    return count;
}

uint8_t building_clock_debris_bricks(building_clock_rect_t envelope, building_clock_rect_t catch_area, const building_clock_debris_t* debris,
                                     building_clock_rect_t bricks[BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION]) {
    if (debris == NULL || bricks == NULL || debris->masonry_progress_per_mille > 1000U) {
        return 0U;
    }
    const building_clock_section_kind_t kind = (building_clock_section_kind_t)debris->section_kind;
    const uint8_t total = building_clock_section_visible_bricks(envelope, debris->source_numeral, kind, bricks);
    const uint8_t count = (uint8_t)(((uint32_t)total * debris->masonry_progress_per_mille + 999U) / 1000U);
    if (count == 0U || catch_area.width < bricks[0].width || catch_area.height < bricks[0].height) {
        return 0U;
    }
    const building_clock_point_t start = building_clock_rect_center(building_clock_section_bounds(envelope, kind));
    for (uint8_t brick = 0U; brick < count; brick++) {
        const building_clock_point_t landing = brick_landing_position(bricks[brick], catch_area, (int32_t)debris->position.x - start.x, brick);
        bricks[brick].x = (int16_t)(landing.x - (int16_t)(bricks[brick].width / 2U));
        bricks[brick].y = (int16_t)(landing.y - (int16_t)(bricks[brick].height / 2U));
    }
    return count;
}
