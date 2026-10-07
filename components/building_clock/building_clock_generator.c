#include "building_clock_generator.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "building_clock_random.h"

#define DATE_BAND_Y 420
#define DIGIT_ENVELOPE_Y 120
#define DIGIT_ENVELOPE_HEIGHT 215
#define COLON_ENVELOPE_X 216
#define COLON_ENVELOPE_WIDTH 25
#define WORK_HALO_MARGIN 10
#define HAZARD_MARGIN 4

typedef struct {
    building_clock_world_t* world;
    building_clock_random_t random;
    uint8_t top_left_platform_id;
    uint8_t top_right_platform_id;
    uint8_t lower_left_platform_id;
    uint8_t lower_right_platform_id;
    int16_t lower_y;
    bool crane_left;
} candidate_builder_t;

static const int16_t s_digit_x[BUILDING_CLOCK_DIGIT_COUNT] = {60, 136, 252, 328};
static const uint16_t s_digit_width[BUILDING_CLOCK_DIGIT_COUNT] = {69, 69, 69, 69};

static uint32_t random_u32(candidate_builder_t* builder, uint32_t minimum, uint32_t maximum_exclusive) {
    return building_clock_random_range(&builder->random, minimum, maximum_exclusive);
}

static int16_t random_i16(candidate_builder_t* builder, int16_t minimum, int16_t maximum_exclusive) {
    if (maximum_exclusive <= minimum) {
        return minimum;
    }
    const uint32_t span = (uint32_t)((int32_t)maximum_exclusive - minimum);
    return (int16_t)(minimum + (int32_t)building_clock_random_bounded(&builder->random, span));
}

static bool random_chance(candidate_builder_t* builder, uint32_t numerator, uint32_t denominator) {
    return building_clock_random_bounded(&builder->random, denominator) < numerator;
}

static uint8_t add_platform(candidate_builder_t* builder, int16_t x_start, int16_t x_end, int16_t surface_y) {
    building_clock_world_t* world = builder->world;
    if (world->platform_count >= BUILDING_CLOCK_MAX_PLATFORM_SPANS) {
        return BUILDING_CLOCK_INVALID_ID;
    }

    const uint8_t id = world->platform_count++;
    world->platforms[id] = (building_clock_platform_t){
        .x_start = x_start,
        .x_end = x_end,
        .surface_y = surface_y,
        .support_mask = 0U,
    };
    return id;
}

static uint8_t add_support(candidate_builder_t* builder, building_clock_support_kind_t kind, int16_t x, int16_t y, uint16_t width, uint16_t height) {
    building_clock_world_t* world = builder->world;
    if (world->major_support_count >= BUILDING_CLOCK_MAX_MAJOR_SUPPORTS) {
        return BUILDING_CLOCK_INVALID_ID;
    }

    const uint8_t id = world->major_support_count++;
    world->major_supports[id] = (building_clock_major_support_t){
        .kind = kind,
        .bounds = {.x = x, .y = y, .width = width, .height = height},
        .platform_mask = 0U,
    };
    return id;
}

static uint8_t find_platform_at(const building_clock_world_t* world, int16_t x, bool upper) {
    uint8_t selected = BUILDING_CLOCK_INVALID_ID;
    int16_t selected_y = upper ? INT16_MIN : INT16_MAX;

    for (uint8_t index = 0; index < world->platform_count; index++) {
        const building_clock_platform_t* platform = &world->platforms[index];
        if (x < platform->x_start || x > platform->x_end) {
            continue;
        }
        if (upper && platform->surface_y < DIGIT_ENVELOPE_Y && platform->surface_y > selected_y) {
            selected = index;
            selected_y = platform->surface_y;
        } else if (!upper && platform->surface_y >= DIGIT_ENVELOPE_Y + DIGIT_ENVELOPE_HEIGHT && platform->surface_y < selected_y) {
            selected = index;
            selected_y = platform->surface_y;
        }
    }

    return selected;
}

static void add_ladder(candidate_builder_t* builder, uint8_t first_platform_id, uint8_t second_platform_id, int16_t x) {
    building_clock_world_t* world = builder->world;
    if (world->ladder_count >= BUILDING_CLOCK_MAX_LADDERS || first_platform_id >= world->platform_count || second_platform_id >= world->platform_count) {
        return;
    }

    const building_clock_platform_t* first = &world->platforms[first_platform_id];
    const building_clock_platform_t* second = &world->platforms[second_platform_id];
    const bool first_is_top = first->surface_y < second->surface_y;
    const uint8_t top_id = first_is_top ? first_platform_id : second_platform_id;
    const uint8_t bottom_id = first_is_top ? second_platform_id : first_platform_id;

    world->ladders[world->ladder_count++] = (building_clock_ladder_t){
        .x = x,
        .top_y = world->platforms[top_id].surface_y,
        .bottom_y = world->platforms[bottom_id].surface_y,
        .top_platform_id = top_id,
        .bottom_platform_id = bottom_id,
    };
}

static void add_side_ladders(candidate_builder_t* builder) {
    const int16_t left_x = random_i16(builder, 46, 55);
    const int16_t right_x = random_i16(builder, 426, 435);
    const uint8_t left_lower = find_platform_at(builder->world, left_x, false);
    const uint8_t right_lower = find_platform_at(builder->world, right_x, false);
    add_ladder(builder, builder->top_left_platform_id, left_lower, left_x);
    add_ladder(builder, builder->top_right_platform_id, right_lower, right_x);
}

static void generate_base_platforms(candidate_builder_t* builder) {
    const int16_t top_left_y = random_i16(builder, 58, 103);
    const int16_t top_right_y = random_i16(builder, 58, 103);
    const int16_t top_left_end = random_i16(builder, 205, 229);
    const int16_t top_right_start = random_i16(builder, 244, 269);

    builder->top_left_platform_id = add_platform(builder, random_i16(builder, 14, 27), top_left_end, top_left_y);
    builder->top_right_platform_id = add_platform(builder, top_right_start, random_i16(builder, 453, 468), top_right_y);
    builder->lower_y = random_i16(builder, 356, 374);
}

static void generate_continuous_lower(candidate_builder_t* builder) {
    builder->lower_left_platform_id = add_platform(builder, random_i16(builder, 15, 28), random_i16(builder, 452, 468), builder->lower_y);
    builder->lower_right_platform_id = builder->lower_left_platform_id;

    if (random_chance(builder, 2U, 3U)) {
        const bool left = random_chance(builder, 1U, 2U);
        const int16_t y = random_i16(builder, 98, 115);
        if (left) {
            (void)add_platform(builder, random_i16(builder, 18, 32), random_i16(builder, 105, 158), y);
        } else {
            (void)add_platform(builder, random_i16(builder, 322, 376), random_i16(builder, 449, 466), y);
        }
    }
}

static void generate_split_lower_bridge(candidate_builder_t* builder) {
    const int16_t left_end = random_i16(builder, 217, 235);
    const int16_t right_start = random_i16(builder, 246, 264);
    builder->lower_left_platform_id = add_platform(builder, random_i16(builder, 14, 27), left_end, builder->lower_y);
    builder->lower_right_platform_id = add_platform(builder, right_start, random_i16(builder, 453, 468), builder->lower_y);

    const int16_t bridge_y = (int16_t)(builder->lower_y - random_i16(builder, 14, 21));
    const uint8_t bridge_id = add_platform(builder, random_i16(builder, 184, 202), random_i16(builder, 281, 300), bridge_y);
    add_ladder(builder, builder->lower_left_platform_id, bridge_id, random_i16(builder, 204, 215));
    add_ladder(builder, builder->lower_right_platform_id, bridge_id, random_i16(builder, 268, 279));
}

static void generate_asymmetric_loop(candidate_builder_t* builder) {
    builder->lower_left_platform_id = add_platform(builder, random_i16(builder, 18, 31), random_i16(builder, 450, 466), builder->lower_y);
    builder->lower_right_platform_id = builder->lower_left_platform_id;

    const bool tall_left = random_chance(builder, 1U, 2U);
    const int16_t side_y = random_i16(builder, 174, 286);
    const uint8_t side_id = tall_left ? add_platform(builder, 18, random_i16(builder, 48, 58), side_y) : add_platform(builder, random_i16(builder, 422, 432), 462, side_y);
    const int16_t ladder_x = tall_left ? random_i16(builder, 28, 45) : random_i16(builder, 438, 454);
    const uint8_t top_id = tall_left ? builder->top_left_platform_id : builder->top_right_platform_id;
    add_ladder(builder, top_id, side_id, ladder_x);
    add_ladder(builder, side_id, builder->lower_left_platform_id, ladder_x);
}

static void generate_twin_towers(candidate_builder_t* builder) {
    builder->lower_left_platform_id = add_platform(builder, random_i16(builder, 16, 29), random_i16(builder, 451, 467), builder->lower_y);
    builder->lower_right_platform_id = builder->lower_left_platform_id;

    const int16_t left_y = random_i16(builder, 155, 235);
    const int16_t right_y = random_i16(builder, 235, 315);
    const uint8_t left_id = add_platform(builder, 18, random_i16(builder, 48, 58), left_y);
    const uint8_t right_id = add_platform(builder, random_i16(builder, 422, 432), 462, right_y);
    const int16_t left_x = random_i16(builder, 29, 45);
    const int16_t right_x = random_i16(builder, 438, 454);
    add_ladder(builder, builder->top_left_platform_id, left_id, left_x);
    add_ladder(builder, left_id, builder->lower_left_platform_id, left_x);
    add_ladder(builder, builder->top_right_platform_id, right_id, right_x);
    add_ladder(builder, right_id, builder->lower_right_platform_id, right_x);
}

static void generate_central_machinery_route(candidate_builder_t* builder) {
    builder->lower_left_platform_id = add_platform(builder, random_i16(builder, 34, 45), random_i16(builder, 436, 447), builder->lower_y);
    builder->lower_right_platform_id = builder->lower_left_platform_id;

    const bool left = builder->crane_left;
    const int16_t y = random_i16(builder, 91, 115);
    if (left) {
        (void)add_platform(builder, random_i16(builder, 16, 28), random_i16(builder, 128, 184), y);
    } else {
        (void)add_platform(builder, random_i16(builder, 296, 352), random_i16(builder, 452, 468), y);
    }
}

static void generate_topology(candidate_builder_t* builder) {
    generate_base_platforms(builder);

    switch (builder->world->topology_family) {
    case BUILDING_CLOCK_TOPOLOGY_CONTINUOUS_LOWER_BRANCHES:
        generate_continuous_lower(builder);
        break;
    case BUILDING_CLOCK_TOPOLOGY_SPLIT_LOWER_RAISED_BRIDGE:
        generate_split_lower_bridge(builder);
        break;
    case BUILDING_CLOCK_TOPOLOGY_ASYMMETRIC_LOOP:
        generate_asymmetric_loop(builder);
        break;
    case BUILDING_CLOCK_TOPOLOGY_TWIN_TOWERS:
        generate_twin_towers(builder);
        break;
    case BUILDING_CLOCK_TOPOLOGY_CENTRAL_ROUTE_MACHINERY:
        generate_central_machinery_route(builder);
        break;
    default:
        break;
    }

    add_side_ladders(builder);
}

static void generate_supports(candidate_builder_t* builder) {
    const building_clock_topology_family_t family = builder->world->topology_family;
    const int16_t left_top_y = builder->world->platforms[builder->top_left_platform_id].surface_y;
    const int16_t right_top_y = builder->world->platforms[builder->top_right_platform_id].surface_y;

    if (family == BUILDING_CLOCK_TOPOLOGY_TWIN_TOWERS || (family == BUILDING_CLOCK_TOPOLOGY_ASYMMETRIC_LOOP && builder->crane_left) || random_chance(builder, 1U, 2U)) {
        (void)add_support(builder, BUILDING_CLOCK_SUPPORT_TOWER, random_i16(builder, 20, 29), left_top_y, (uint16_t)random_i16(builder, 10, 15), (uint16_t)(builder->lower_y - left_top_y + 1));
    }

    if (family == BUILDING_CLOCK_TOPOLOGY_TWIN_TOWERS || (family == BUILDING_CLOCK_TOPOLOGY_ASYMMETRIC_LOOP && !builder->crane_left) || random_chance(builder, 1U, 2U)) {
        const uint16_t width = (uint16_t)random_i16(builder, 10, 15);
        (void)add_support(builder, BUILDING_CLOCK_SUPPORT_TOWER, (int16_t)(451 - width), right_top_y, width, (uint16_t)(builder->lower_y - right_top_y + 1));
    }

    const uint8_t lower_supports = family == BUILDING_CLOCK_TOPOLOGY_SPLIT_LOWER_RAISED_BRIDGE ? 2U : 1U;
    for (uint8_t index = 0; index < lower_supports; index++) {
        const int16_t x = index == 0U ? random_i16(builder, 104, 132) : random_i16(builder, 332, 360);
        (void)add_support(builder, random_chance(builder, 1U, 2U) ? BUILDING_CLOCK_SUPPORT_FRAME : BUILDING_CLOCK_SUPPORT_BRACE, x, builder->lower_y, (uint16_t)random_i16(builder, 8, 13),
                          (uint16_t)(411 - builder->lower_y));
    }

    for (uint8_t support_id = 0; support_id < builder->world->major_support_count; support_id++) {
        building_clock_major_support_t* support = &builder->world->major_supports[support_id];
        const int16_t center_x = (int16_t)(support->bounds.x + (int16_t)(support->bounds.width / 2U));
        const int16_t bottom_y = (int16_t)(support->bounds.y + (int16_t)support->bounds.height - 1);
        for (uint8_t platform_id = 0; platform_id < builder->world->platform_count; platform_id++) {
            building_clock_platform_t* platform = &builder->world->platforms[platform_id];
            if (center_x >= platform->x_start && center_x <= platform->x_end && platform->surface_y >= support->bounds.y && platform->surface_y <= bottom_y) {
                support->platform_mask |= UINT32_C(1) << platform_id;
                platform->support_mask |= UINT32_C(1) << support_id;
            }
        }
    }
}

static void generate_crane_and_lift(candidate_builder_t* builder) {
    building_clock_world_t* world = builder->world;
    building_clock_crane_layout_t* crane = &world->cranes[0];
    const int16_t crane_x = builder->crane_left ? 48 : 432;
    const int16_t crane_top_y = world->platforms[builder->crane_left ? builder->top_left_platform_id : builder->top_right_platform_id].surface_y;
    *crane = (building_clock_crane_layout_t){
        .base_position = {.x = crane_x, .y = builder->lower_y},
        .home_hook_position = {.x = crane_x, .y = crane_top_y},
        .working_area = {.x = 32, .y = 40, .width = 416, .height = (uint16_t)(builder->lower_y - 39)},
        .operator_node_id = BUILDING_CLOCK_INVALID_ID,
    };
    world->crane_count = 1U;

    if (!random_chance(builder, 1U, 2U)) {
        return;
    }

    const int16_t lift_x = builder->crane_left ? 432 : 48;
    const uint8_t top_platform_id = find_platform_at(world, lift_x, true);
    const uint8_t lower_platform_id = find_platform_at(world, lift_x, false);
    if (top_platform_id == BUILDING_CLOCK_INVALID_ID || lower_platform_id == BUILDING_CLOCK_INVALID_ID) {
        return;
    }

    building_clock_lift_layout_t* lift = &world->lifts[0];
    *lift = (building_clock_lift_layout_t){
        .x = lift_x,
        .operator_node_id = BUILDING_CLOCK_INVALID_ID,
        .stop_count = 2U,
    };
    lift->stops[0] = (building_clock_lift_stop_t){
        .position = {.x = lift_x, .y = world->platforms[top_platform_id].surface_y},
        .nav_node_id = BUILDING_CLOCK_INVALID_ID,
    };
    lift->stops[1] = (building_clock_lift_stop_t){
        .position = {.x = lift_x, .y = world->platforms[lower_platform_id].surface_y},
        .nav_node_id = BUILDING_CLOCK_INVALID_ID,
    };
    world->lift_count = 1U;
}

static void add_cart_route(candidate_builder_t* builder, uint8_t platform_id) {
    building_clock_world_t* world = builder->world;
    if (world->cart_route_count >= BUILDING_CLOCK_MAX_CART_ROUTES || platform_id >= world->platform_count) {
        return;
    }

    const building_clock_platform_t* platform = &world->platforms[platform_id];
    const int16_t route_start = (int16_t)(platform->x_start + 16);
    const int16_t route_end = (int16_t)(platform->x_end - 16);
    if (route_end - route_start < 64) {
        return;
    }

    building_clock_cart_route_t* route = &world->cart_routes[world->cart_route_count++];
    route->stop_count = (uint8_t)random_u32(builder, 2U, 5U);
    for (uint8_t index = 0; index < route->stop_count; index++) {
        const int32_t span = route_end - route_start;
        int16_t x = (int16_t)(route_start + (span * index) / (route->stop_count - 1U));
        if (index > 0U && index + 1U < route->stop_count) {
            x = (int16_t)(x + random_i16(builder, -5, 6));
        }
        route->stops[index] = (building_clock_route_stop_t){
            .position = {.x = x, .y = platform->surface_y},
            .nav_node_id = BUILDING_CLOCK_INVALID_ID,
        };
    }
}

static void generate_cart_routes(candidate_builder_t* builder) {
    building_clock_world_t* world = builder->world;
    uint8_t eligible[BUILDING_CLOCK_MAX_PLATFORM_SPANS];
    uint8_t eligible_count = 0U;
    for (uint8_t index = 0; index < world->platform_count; index++) {
        const building_clock_platform_t* platform = &world->platforms[index];
        if (platform->surface_y >= DIGIT_ENVELOPE_Y + DIGIT_ENVELOPE_HEIGHT && platform->x_end - platform->x_start >= 110) {
            eligible[eligible_count++] = index;
        }
    }

    if (eligible_count == 0U) {
        return;
    }
    const uint8_t first_index = (uint8_t)building_clock_random_bounded(&builder->random, eligible_count);
    add_cart_route(builder, eligible[first_index]);
    if (eligible_count > 1U && random_chance(builder, 1U, 3U)) {
        uint8_t second_index = (uint8_t)building_clock_random_bounded(&builder->random, eligible_count - 1U);
        if (second_index >= first_index) {
            second_index++;
        }
        add_cart_route(builder, eligible[second_index]);
    }
}

static void generate_locations(candidate_builder_t* builder) {
    building_clock_world_t* world = builder->world;
    const bool depot_left = random_chance(builder, 1U, 2U);
    world->depot_count = 1U;
    world->depots[0] = (building_clock_depot_t){
        .area = {.x = depot_left ? 16 : 428, .y = 382, .width = 36, .height = 28},
        .access_node_id = BUILDING_CLOCK_INVALID_ID,
    };

    world->debris_bay_count = BUILDING_CLOCK_DIGIT_COUNT;
    world->staging_area_count = BUILDING_CLOCK_DIGIT_COUNT;
    for (uint8_t digit = 0; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        const int16_t center_x = (int16_t)(s_digit_x[digit] + (int16_t)(s_digit_width[digit] / 2U));
        const int16_t debris_x = (int16_t)(center_x - 12 + random_i16(builder, -3, 4));
        const int16_t staging_x = (int16_t)(center_x - 9 + random_i16(builder, -3, 4));
        world->debris_bays[digit] = (building_clock_debris_bay_t){
            .area = {.x = debris_x, .y = 394, .width = 24, .height = 18},
            .access_node_id = BUILDING_CLOCK_INVALID_ID,
            .assigned_digit_mask = (uint8_t)(1U << digit),
        };
        world->staging_areas[digit] = (building_clock_staging_area_t){
            .area = {.x = staging_x, .y = 337, .width = 18, .height = 10},
            .access_node_id = BUILDING_CLOCK_INVALID_ID,
            .assigned_digit_mask = (uint8_t)(1U << digit),
        };
    }

    const uint8_t extra_staging_count = (uint8_t)random_u32(builder, 0U, 3U);
    for (uint8_t index = 0; index < extra_staging_count && world->staging_area_count < BUILDING_CLOCK_MAX_STAGING_AREAS; index++) {
        const bool left = (index == 0U) ? !depot_left : depot_left;
        world->staging_areas[world->staging_area_count++] = (building_clock_staging_area_t){
            .area = {.x = left ? 24 : 436, .y = 337, .width = 20, .height = 10},
            .access_node_id = BUILDING_CLOCK_INVALID_ID,
            .assigned_digit_mask = (uint8_t)(left ? 0x03U : 0x0cU),
        };
    }
}

static void generate_worker_spawns(candidate_builder_t* builder) {
    building_clock_world_t* world = builder->world;
    world->worker_spawn_count = (uint8_t)random_u32(builder, 5U, BUILDING_CLOCK_MAX_WORKER_SPAWNS);
    for (uint8_t index = 0; index < world->worker_spawn_count; index++) {
        const int16_t nominal_x = (int16_t)(54 + (372 * index) / (world->worker_spawn_count - 1U));
        const int16_t x = (int16_t)(nominal_x + random_i16(builder, -8, 9));
        const uint8_t platform_id = find_platform_at(world, x, false);
        const uint8_t fallback_id = index < world->worker_spawn_count / 2U ? builder->lower_left_platform_id : builder->lower_right_platform_id;
        const building_clock_platform_t* platform = &world->platforms[platform_id != BUILDING_CLOCK_INVALID_ID ? platform_id : fallback_id];
        const int16_t bounded_x = x < platform->x_start ? platform->x_start : (x > platform->x_end ? platform->x_end : x);
        world->worker_spawns[index] = (building_clock_worker_spawn_t){
            .position = {.x = bounded_x, .y = platform->surface_y},
            .nav_node_id = BUILDING_CLOCK_INVALID_ID,
        };
    }
}

static building_clock_rect_t expanded_rect(building_clock_rect_t rect, int16_t margin) {
    return (building_clock_rect_t){
        .x = (int16_t)(rect.x - margin),
        .y = (int16_t)(rect.y - margin),
        .width = (uint16_t)(rect.width + (uint16_t)(2 * margin)),
        .height = (uint16_t)(rect.height + (uint16_t)(2 * margin)),
    };
}

static void add_digit_access(building_clock_digit_work_area_t* area, building_clock_digit_access_kind_t kind, int16_t x, int16_t y) {
    if (area->access_count >= BUILDING_CLOCK_MAX_DIGIT_ACCESS_POINTS) {
        return;
    }
    area->access[area->access_count++] = (building_clock_digit_access_t){
        .kind = kind,
        .position = {.x = x, .y = y},
        .nav_node_id = BUILDING_CLOCK_INVALID_ID,
    };
}

static void generate_digit_work_areas(candidate_builder_t* builder) {
    building_clock_world_t* world = builder->world;
    for (uint8_t digit = 0; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        building_clock_digit_work_area_t* area = &world->digit_work_areas[digit];
        area->envelope = (building_clock_rect_t){
            .x = s_digit_x[digit],
            .y = DIGIT_ENVELOPE_Y,
            .width = s_digit_width[digit],
            .height = DIGIT_ENVELOPE_HEIGHT,
        };
        area->work_halo = expanded_rect(area->envelope, WORK_HALO_MARGIN);
        area->hazard_area = expanded_rect(area->envelope, HAZARD_MARGIN);
        area->debris_bay_id = digit;

        const int16_t center_x = (int16_t)(area->envelope.x + (int16_t)(area->envelope.width / 2U));
        const uint8_t upper_id = find_platform_at(world, center_x, true);
        const uint8_t lower_id = find_platform_at(world, center_x, false);
        const int16_t upper_y = world->platforms[upper_id].surface_y;
        const int16_t lower_y = world->platforms[lower_id].surface_y;
        add_digit_access(area, BUILDING_CLOCK_DIGIT_ACCESS_LOWER, center_x, lower_y);
        add_digit_access(area, BUILDING_CLOCK_DIGIT_ACCESS_UPPER, center_x, upper_y);
        add_digit_access(area, BUILDING_CLOCK_DIGIT_ACCESS_LEFT, (int16_t)(area->envelope.x - 6), 226);
        add_digit_access(area, BUILDING_CLOCK_DIGIT_ACCESS_RIGHT, (int16_t)(area->envelope.x + (int16_t)area->envelope.width + 5), 226);

        building_clock_temp_attachment_set_t* attachment = &world->temp_attachment_sets[world->temp_attachment_set_count++];
        attachment->kind = random_chance(builder, 1U, 2U) ? BUILDING_CLOCK_TEMP_STRUCTURE_LADDER : BUILDING_CLOCK_TEMP_STRUCTURE_SCAFFOLD;
        attachment->assigned_digit_mask = (uint8_t)(1U << digit);
        attachment->allowed_area = (building_clock_rect_t){
            .x = (int16_t)(center_x - 8),
            .y = upper_y,
            .width = 17,
            .height = (uint16_t)(lower_y - upper_y + 1),
        };
        attachment->anchor_count = 2U;
        attachment->anchors[0] = (building_clock_point_t){.x = center_x, .y = upper_y};
        attachment->anchors[1] = (building_clock_point_t){.x = center_x, .y = lower_y};
        attachment->connection_count = 0U;
    }

    const uint8_t extra_count = (uint8_t)random_u32(builder, 0U, 3U);
    for (uint8_t index = 0; index < extra_count && world->temp_attachment_set_count < BUILDING_CLOCK_MAX_TEMP_ATTACHMENT_SETS; index++) {
        const uint8_t digit = (uint8_t)building_clock_random_bounded(&builder->random, BUILDING_CLOCK_DIGIT_COUNT);
        const building_clock_digit_work_area_t* area = &world->digit_work_areas[digit];
        const building_clock_point_t upper = area->access[1].position;
        const building_clock_point_t lower = area->access[0].position;
        building_clock_temp_attachment_set_t* attachment = &world->temp_attachment_sets[world->temp_attachment_set_count++];
        *attachment = (building_clock_temp_attachment_set_t){
            .kind = BUILDING_CLOCK_TEMP_STRUCTURE_PLATFORM,
            .assigned_digit_mask = (uint8_t)(1U << digit),
            .allowed_area = {.x = (int16_t)(area->envelope.x - 4), .y = upper.y, .width = (uint16_t)(area->envelope.width + 8U), .height = (uint16_t)(lower.y - upper.y + 1)},
            .anchor_count = 2U,
            .anchors = {upper, lower},
            .connection_count = 0U,
        };
    }
}

uint64_t building_clock_candidate_seed(uint64_t root_seed, uint32_t generator_version, uint16_t attempt) {
    const uint64_t world_seed = building_clock_random_derive_seed(root_seed, BUILDING_CLOCK_RANDOM_WORLD_LAYOUT);
    const uint64_t version_key = world_seed + UINT64_C(0xd1b54a32d192ed03) * generator_version;
    const uint64_t attempt_key = version_key + UINT64_C(0x94d049bb133111eb) * attempt;
    return building_clock_random_derive_seed(attempt_key, BUILDING_CLOCK_RANDOM_WORLD_LAYOUT);
}

void building_clock_generate_candidate(building_clock_world_t* world, uint64_t root_seed, uint32_t generator_version, uint16_t attempt) {
    if (world == NULL) {
        return;
    }

    memset(world, 0, sizeof(*world));
    world->replay = (building_clock_replay_metadata_t){
        .root_seed = root_seed,
        .world_layout_seed = building_clock_random_derive_seed(root_seed, BUILDING_CLOCK_RANDOM_WORLD_LAYOUT),
        .candidate_seed = building_clock_candidate_seed(root_seed, generator_version, attempt),
        .construction_scheduling_seed = building_clock_random_derive_seed(root_seed, BUILDING_CLOCK_RANDOM_CONSTRUCTION_SCHEDULING),
        .worker_behavior_seed = building_clock_random_derive_seed(root_seed, BUILDING_CLOCK_RANDOM_WORKER_BEHAVIOR),
        .cosmetic_seed = building_clock_random_derive_seed(root_seed, BUILDING_CLOCK_RANDOM_COSMETIC),
        .generator_version = generator_version,
        .generation_attempt = attempt,
    };

    candidate_builder_t builder = {
        .world = world,
        .top_left_platform_id = BUILDING_CLOCK_INVALID_ID,
        .top_right_platform_id = BUILDING_CLOCK_INVALID_ID,
        .lower_left_platform_id = BUILDING_CLOCK_INVALID_ID,
        .lower_right_platform_id = BUILDING_CLOCK_INVALID_ID,
    };
    building_clock_random_init(&builder.random, world->replay.candidate_seed);
    world->topology_family = (building_clock_topology_family_t)building_clock_random_bounded(&builder.random, BUILDING_CLOCK_TOPOLOGY_COUNT);
    builder.crane_left = random_chance(&builder, 1U, 2U);

    world->colon_envelope = (building_clock_rect_t){
        .x = COLON_ENVELOPE_X,
        .y = DIGIT_ENVELOPE_Y,
        .width = COLON_ENVELOPE_WIDTH,
        .height = DIGIT_ENVELOPE_HEIGHT,
    };
    world->date_band = (building_clock_rect_t){.x = 0, .y = DATE_BAND_Y, .width = BUILDING_CLOCK_WORLD_WIDTH, .height = 60};

    generate_topology(&builder);
    generate_supports(&builder);
    generate_crane_and_lift(&builder);
    generate_cart_routes(&builder);
    generate_locations(&builder);
    generate_worker_spawns(&builder);
    generate_digit_work_areas(&builder);
}
