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
