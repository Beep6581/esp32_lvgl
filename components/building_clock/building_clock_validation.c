#include "building_clock_validation.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MIN_PLATFORM_LENGTH 24
#define DEBRIS_FALL_CLEARANCE 16
#define PLATFORM_AREA_HEIGHT 3U
#define LADDER_AREA_WIDTH 3U
#define MAX_PREVALIDATION_PLATFORM_COUNT 14U
#define MAX_PREVALIDATION_LADDER_COUNT 8U
#define MAX_OCCUPIED_PERCENT 30U

static building_clock_prevalidation_result_t result(building_clock_prevalidation_reason_t reason, uint16_t item_index) {
    return (building_clock_prevalidation_result_t){.reason = reason, .item_index = item_index};
}

static bool rect_in_world(building_clock_rect_t rect) {
    if (rect.x < 0 || rect.y < 0 || rect.width == 0U || rect.height == 0U) {
        return false;
    }
    return (int32_t)rect.x + rect.width <= BUILDING_CLOCK_WORLD_WIDTH && (int32_t)rect.y + rect.height <= BUILDING_CLOCK_WORLD_HEIGHT;
}

static bool point_in_world(building_clock_point_t point) {
    return point.x >= 0 && point.x < BUILDING_CLOCK_WORLD_WIDTH && point.y >= 0 && point.y < BUILDING_CLOCK_WORLD_HEIGHT;
}

static bool point_in_rect(building_clock_point_t point, building_clock_rect_t rect) {
    return point.x >= rect.x && point.y >= rect.y && point.x < (int32_t)rect.x + rect.width && point.y < (int32_t)rect.y + rect.height;
}

static bool rects_overlap(building_clock_rect_t first, building_clock_rect_t second) {
    return first.x < (int32_t)second.x + second.width && second.x < (int32_t)first.x + first.width && first.y < (int32_t)second.y + second.height && second.y < (int32_t)first.y + first.height;
}

static bool horizontal_span_hits_rect(int16_t x_start, int16_t x_end, int16_t y, building_clock_rect_t rect) {
    return y >= rect.y && y < (int32_t)rect.y + rect.height && x_start < (int32_t)rect.x + rect.width && x_end >= rect.x;
}

static bool vertical_span_hits_rect(int16_t x, int16_t y_start, int16_t y_end, building_clock_rect_t rect) {
    return x >= rect.x && x < (int32_t)rect.x + rect.width && y_start < (int32_t)rect.y + rect.height && y_end >= rect.y;
}

static bool point_hits_protected(const building_clock_world_t* world, building_clock_point_t point) {
    if (point_in_rect(point, world->colon_envelope) || point_in_rect(point, world->date_band)) {
        return true;
    }
    for (uint8_t digit = 0; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        if (point_in_rect(point, world->digit_work_areas[digit].envelope)) {
            return true;
        }
    }
    return false;
}

static bool rect_hits_protected(const building_clock_world_t* world, building_clock_rect_t rect) {
    if (rects_overlap(rect, world->colon_envelope) || rects_overlap(rect, world->date_band)) {
        return true;
    }
    for (uint8_t digit = 0; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        if (rects_overlap(rect, world->digit_work_areas[digit].envelope)) {
            return true;
        }
    }
    return false;
}

static bool horizontal_span_hits_protected(const building_clock_world_t* world, int16_t x_start, int16_t x_end, int16_t y) {
    if (horizontal_span_hits_rect(x_start, x_end, y, world->colon_envelope) || horizontal_span_hits_rect(x_start, x_end, y, world->date_band)) {
        return true;
    }
    for (uint8_t digit = 0; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        if (horizontal_span_hits_rect(x_start, x_end, y, world->digit_work_areas[digit].envelope)) {
            return true;
        }
    }
    return false;
}

static bool vertical_span_hits_protected(const building_clock_world_t* world, int16_t x, int16_t y_start, int16_t y_end) {
    if (vertical_span_hits_rect(x, y_start, y_end, world->colon_envelope) || vertical_span_hits_rect(x, y_start, y_end, world->date_band)) {
        return true;
    }
    for (uint8_t digit = 0; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        if (vertical_span_hits_rect(x, y_start, y_end, world->digit_work_areas[digit].envelope)) {
            return true;
        }
    }
    return false;
}

static bool point_on_platform(const building_clock_world_t* world, building_clock_point_t point) {
    for (uint8_t index = 0; index < world->platform_count; index++) {
        const building_clock_platform_t* platform = &world->platforms[index];
        if (point.y == platform->surface_y && point.x >= platform->x_start && point.x <= platform->x_end) {
            return true;
        }
    }
    return false;
}

static building_clock_prevalidation_result_t validate_counts(const building_clock_world_t* world) {
    if (world->platform_count > BUILDING_CLOCK_MAX_PLATFORM_SPANS || world->ladder_count > BUILDING_CLOCK_MAX_LADDERS || world->major_support_count > BUILDING_CLOCK_MAX_MAJOR_SUPPORTS ||
        world->crane_count > BUILDING_CLOCK_MAX_CRANES || world->lift_count > BUILDING_CLOCK_MAX_LIFTS || world->cart_route_count > BUILDING_CLOCK_MAX_CART_ROUTES ||
        world->depot_count > BUILDING_CLOCK_MAX_MATERIAL_DEPOTS || world->debris_bay_count > BUILDING_CLOCK_MAX_DEBRIS_BAYS || world->staging_area_count > BUILDING_CLOCK_MAX_STAGING_AREAS ||
        world->worker_spawn_count > BUILDING_CLOCK_MAX_WORKER_SPAWNS || world->temp_attachment_set_count > BUILDING_CLOCK_MAX_TEMP_ATTACHMENT_SETS ||
        world->topology_family >= BUILDING_CLOCK_TOPOLOGY_COUNT) {
        return result(BUILDING_CLOCK_PREVALIDATION_COUNT_EXCEEDS_CAPACITY, BUILDING_CLOCK_VALIDATION_NO_ITEM);
    }

    for (uint8_t index = 0; index < world->cart_route_count; index++) {
        if (world->cart_routes[index].stop_count > BUILDING_CLOCK_MAX_CART_ROUTE_STOPS) {
            return result(BUILDING_CLOCK_PREVALIDATION_COUNT_EXCEEDS_CAPACITY, index);
        }
    }
    for (uint8_t index = 0; index < world->lift_count; index++) {
        if (world->lifts[index].stop_count > BUILDING_CLOCK_MAX_LIFT_STOPS) {
            return result(BUILDING_CLOCK_PREVALIDATION_COUNT_EXCEEDS_CAPACITY, index);
        }
    }
    for (uint8_t index = 0; index < BUILDING_CLOCK_DIGIT_COUNT; index++) {
        if (world->digit_work_areas[index].access_count > BUILDING_CLOCK_MAX_DIGIT_ACCESS_POINTS || world->digit_work_areas[index].safe_retreat_node_count > BUILDING_CLOCK_MAX_SAFE_RETREAT_NODES) {
            return result(BUILDING_CLOCK_PREVALIDATION_COUNT_EXCEEDS_CAPACITY, index);
        }
    }
    for (uint8_t index = 0; index < world->temp_attachment_set_count; index++) {
        if (world->temp_attachment_sets[index].anchor_count > BUILDING_CLOCK_MAX_ATTACHMENT_ANCHORS ||
            world->temp_attachment_sets[index].connection_count > BUILDING_CLOCK_MAX_ATTACHMENT_CONNECTIONS) {
            return result(BUILDING_CLOCK_PREVALIDATION_COUNT_EXCEEDS_CAPACITY, index);
        }
    }
    return result(BUILDING_CLOCK_PREVALIDATION_OK, BUILDING_CLOCK_VALIDATION_NO_ITEM);
}

static building_clock_prevalidation_result_t validate_protected_regions(const building_clock_world_t* world) {
    if (!rect_in_world(world->colon_envelope) || !rect_in_world(world->date_band)) {
        return result(BUILDING_CLOCK_PREVALIDATION_PROTECTED_REGION_INVALID, BUILDING_CLOCK_VALIDATION_NO_ITEM);
    }
    if (rects_overlap(world->colon_envelope, world->date_band)) {
        return result(BUILDING_CLOCK_PREVALIDATION_PROTECTED_REGION_OVERLAP, BUILDING_CLOCK_VALIDATION_NO_ITEM);
    }

    for (uint8_t digit = 0; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        const building_clock_rect_t envelope = world->digit_work_areas[digit].envelope;
        if (!rect_in_world(envelope) || rects_overlap(envelope, world->colon_envelope) || rects_overlap(envelope, world->date_band)) {
            return result(BUILDING_CLOCK_PREVALIDATION_PROTECTED_REGION_INVALID, digit);
        }
        for (uint8_t other = (uint8_t)(digit + 1U); other < BUILDING_CLOCK_DIGIT_COUNT; other++) {
            if (rects_overlap(envelope, world->digit_work_areas[other].envelope)) {
                return result(BUILDING_CLOCK_PREVALIDATION_PROTECTED_REGION_OVERLAP, digit);
            }
        }
    }
    return result(BUILDING_CLOCK_PREVALIDATION_OK, BUILDING_CLOCK_VALIDATION_NO_ITEM);
}

static building_clock_prevalidation_result_t validate_platforms(const building_clock_world_t* world) {
    for (uint8_t index = 0; index < world->platform_count; index++) {
        const building_clock_platform_t* platform = &world->platforms[index];
        if (platform->x_start < 0 || platform->x_end >= BUILDING_CLOCK_WORLD_WIDTH || platform->surface_y < 0 || platform->surface_y >= BUILDING_CLOCK_WORLD_HEIGHT ||
            platform->x_end < platform->x_start) {
            return result(BUILDING_CLOCK_PREVALIDATION_PLATFORM_BOUNDS, index);
        }
        if (platform->x_end - platform->x_start + 1 < MIN_PLATFORM_LENGTH) {
            return result(BUILDING_CLOCK_PREVALIDATION_PLATFORM_TOO_SHORT, index);
        }
        if (horizontal_span_hits_protected(world, platform->x_start, platform->x_end, platform->surface_y)) {
            return result(BUILDING_CLOCK_PREVALIDATION_PLATFORM_PROTECTED_REGION, index);
        }
        for (uint8_t other = (uint8_t)(index + 1U); other < world->platform_count; other++) {
            const building_clock_platform_t* second = &world->platforms[other];
            if (platform->surface_y == second->surface_y && platform->x_start <= second->x_end && second->x_start <= platform->x_end) {
                return result(BUILDING_CLOCK_PREVALIDATION_PLATFORM_OVERLAP, index);
            }
        }
    }
    return result(BUILDING_CLOCK_PREVALIDATION_OK, BUILDING_CLOCK_VALIDATION_NO_ITEM);
}

static building_clock_prevalidation_result_t validate_supports(const building_clock_world_t* world) {
    const uint32_t valid_platform_mask = world->platform_count == 32U ? UINT32_MAX : (UINT32_C(1) << world->platform_count) - 1U;
    const uint32_t valid_support_mask = world->major_support_count == 32U ? UINT32_MAX : (UINT32_C(1) << world->major_support_count) - 1U;

    for (uint8_t index = 0; index < world->major_support_count; index++) {
        const building_clock_major_support_t* support = &world->major_supports[index];
        if (!rect_in_world(support->bounds)) {
            return result(BUILDING_CLOCK_PREVALIDATION_SUPPORT_BOUNDS, index);
        }
        if (rect_hits_protected(world, support->bounds)) {
            return result(BUILDING_CLOCK_PREVALIDATION_SUPPORT_PROTECTED_REGION, index);
        }
        if ((support->platform_mask & ~valid_platform_mask) != 0U) {
            return result(BUILDING_CLOCK_PREVALIDATION_SUPPORT_RELATIONSHIP, index);
        }
        for (uint8_t other = (uint8_t)(index + 1U); other < world->major_support_count; other++) {
            if (rects_overlap(support->bounds, world->major_supports[other].bounds)) {
                return result(BUILDING_CLOCK_PREVALIDATION_SUPPORT_OVERLAP, index);
            }
        }
    }

    for (uint8_t platform_id = 0; platform_id < world->platform_count; platform_id++) {
        const building_clock_platform_t* platform = &world->platforms[platform_id];
        if ((platform->support_mask & ~valid_support_mask) != 0U) {
            return result(BUILDING_CLOCK_PREVALIDATION_SUPPORT_RELATIONSHIP, platform_id);
        }
        for (uint8_t support_id = 0; support_id < world->major_support_count; support_id++) {
            const bool platform_links = (platform->support_mask & (UINT32_C(1) << support_id)) != 0U;
            const bool support_links = (world->major_supports[support_id].platform_mask & (UINT32_C(1) << platform_id)) != 0U;
            if (platform_links != support_links) {
                return result(BUILDING_CLOCK_PREVALIDATION_SUPPORT_RELATIONSHIP, platform_id);
            }
        }
    }
    return result(BUILDING_CLOCK_PREVALIDATION_OK, BUILDING_CLOCK_VALIDATION_NO_ITEM);
}

static building_clock_prevalidation_result_t validate_ladders(const building_clock_world_t* world) {
    for (uint8_t index = 0; index < world->ladder_count; index++) {
        const building_clock_ladder_t* ladder = &world->ladders[index];
        if (ladder->x < 0 || ladder->x >= BUILDING_CLOCK_WORLD_WIDTH || ladder->top_y < 0 || ladder->bottom_y >= BUILDING_CLOCK_WORLD_HEIGHT || ladder->top_y >= ladder->bottom_y) {
            return result(BUILDING_CLOCK_PREVALIDATION_LADDER_BOUNDS, index);
        }
        if (ladder->top_platform_id >= world->platform_count || ladder->bottom_platform_id >= world->platform_count) {
            return result(BUILDING_CLOCK_PREVALIDATION_LADDER_ENDPOINT, index);
        }
        const building_clock_platform_t* top = &world->platforms[ladder->top_platform_id];
        const building_clock_platform_t* bottom = &world->platforms[ladder->bottom_platform_id];
        if (ladder->top_y != top->surface_y || ladder->bottom_y != bottom->surface_y || ladder->x < top->x_start || ladder->x > top->x_end || ladder->x < bottom->x_start ||
            ladder->x > bottom->x_end) {
            return result(BUILDING_CLOCK_PREVALIDATION_LADDER_ENDPOINT, index);
        }
        if (vertical_span_hits_protected(world, ladder->x, ladder->top_y, ladder->bottom_y)) {
            return result(BUILDING_CLOCK_PREVALIDATION_LADDER_PROTECTED_REGION, index);
        }
    }
    return result(BUILDING_CLOCK_PREVALIDATION_OK, BUILDING_CLOCK_VALIDATION_NO_ITEM);
}

static building_clock_prevalidation_result_t validate_cranes(const building_clock_world_t* world) {
    for (uint8_t index = 0; index < world->crane_count; index++) {
        const building_clock_crane_layout_t* crane = &world->cranes[index];
        if (!point_in_world(crane->base_position) || !point_in_world(crane->home_hook_position) || !rect_in_world(crane->working_area) || crane->working_area.width < 64U ||
            crane->working_area.height < 64U || !point_in_rect(crane->base_position, crane->working_area) || !point_in_rect(crane->home_hook_position, crane->working_area) ||
            point_hits_protected(world, crane->base_position) || !point_on_platform(world, crane->base_position) || crane->operator_node_id != BUILDING_CLOCK_INVALID_ID) {
            return result(BUILDING_CLOCK_PREVALIDATION_CRANE_GEOMETRY, index);
        }
        for (uint8_t support = 0; support < world->major_support_count; support++) {
            if (point_in_rect(crane->base_position, world->major_supports[support].bounds)) {
                return result(BUILDING_CLOCK_PREVALIDATION_CRANE_GEOMETRY, index);
            }
        }
    }
    return result(BUILDING_CLOCK_PREVALIDATION_OK, BUILDING_CLOCK_VALIDATION_NO_ITEM);
}

static building_clock_prevalidation_result_t validate_lifts(const building_clock_world_t* world) {
    for (uint8_t index = 0; index < world->lift_count; index++) {
        const building_clock_lift_layout_t* lift = &world->lifts[index];
        if (lift->x < 0 || lift->x >= BUILDING_CLOCK_WORLD_WIDTH || lift->stop_count < 2U || lift->operator_node_id != BUILDING_CLOCK_INVALID_ID) {
            return result(BUILDING_CLOCK_PREVALIDATION_LIFT_GEOMETRY, index);
        }
        int16_t top_y = INT16_MAX;
        int16_t bottom_y = INT16_MIN;
        for (uint8_t stop = 0; stop < lift->stop_count; stop++) {
            const building_clock_lift_stop_t* lift_stop = &lift->stops[stop];
            if (!point_in_world(lift_stop->position) || lift_stop->position.x != lift->x || !point_on_platform(world, lift_stop->position) || lift_stop->nav_node_id != BUILDING_CLOCK_INVALID_ID) {
                return result(BUILDING_CLOCK_PREVALIDATION_LIFT_GEOMETRY, index);
            }
            if (lift_stop->position.y < top_y) {
                top_y = lift_stop->position.y;
            }
            if (lift_stop->position.y > bottom_y) {
                bottom_y = lift_stop->position.y;
            }
        }
        if (vertical_span_hits_protected(world, lift->x, top_y, bottom_y)) {
            return result(BUILDING_CLOCK_PREVALIDATION_LIFT_GEOMETRY, index);
        }
    }
    return result(BUILDING_CLOCK_PREVALIDATION_OK, BUILDING_CLOCK_VALIDATION_NO_ITEM);
}

static building_clock_prevalidation_result_t validate_cart_routes(const building_clock_world_t* world) {
    for (uint8_t index = 0; index < world->cart_route_count; index++) {
        const building_clock_cart_route_t* route = &world->cart_routes[index];
        if (route->stop_count < 2U) {
            return result(BUILDING_CLOCK_PREVALIDATION_CART_ROUTE_GEOMETRY, index);
        }
        uint8_t common_platform = BUILDING_CLOCK_INVALID_ID;
        for (uint8_t platform_id = 0; platform_id < world->platform_count; platform_id++) {
            bool contains_all = true;
            for (uint8_t stop = 0; stop < route->stop_count; stop++) {
                const building_clock_platform_t* platform = &world->platforms[platform_id];
                const building_clock_point_t point = route->stops[stop].position;
                if (point.y != platform->surface_y || point.x < platform->x_start || point.x > platform->x_end) {
                    contains_all = false;
                    break;
                }
            }
            if (contains_all) {
                common_platform = platform_id;
                break;
            }
        }
        if (common_platform == BUILDING_CLOCK_INVALID_ID) {
            return result(BUILDING_CLOCK_PREVALIDATION_CART_ROUTE_GEOMETRY, index);
        }
        for (uint8_t stop = 0; stop < route->stop_count; stop++) {
            if (!point_in_world(route->stops[stop].position) || route->stops[stop].nav_node_id != BUILDING_CLOCK_INVALID_ID ||
                (stop > 0U && route->stops[stop].position.x <= route->stops[stop - 1U].position.x)) {
                return result(BUILDING_CLOCK_PREVALIDATION_CART_ROUTE_GEOMETRY, index);
            }
        }
        const building_clock_platform_t* platform = &world->platforms[common_platform];
        if (horizontal_span_hits_protected(world, route->stops[0].position.x, route->stops[route->stop_count - 1U].position.x, platform->surface_y)) {
            return result(BUILDING_CLOCK_PREVALIDATION_CART_ROUTE_GEOMETRY, index);
        }
    }
    return result(BUILDING_CLOCK_PREVALIDATION_OK, BUILDING_CLOCK_VALIDATION_NO_ITEM);
}

static building_clock_prevalidation_result_t validate_locations(const building_clock_world_t* world) {
    building_clock_rect_t locations[BUILDING_CLOCK_MAX_MATERIAL_DEPOTS + BUILDING_CLOCK_MAX_DEBRIS_BAYS + BUILDING_CLOCK_MAX_STAGING_AREAS];
    uint8_t location_count = 0U;

    for (uint8_t index = 0; index < world->depot_count; index++) {
        if (!rect_in_world(world->depots[index].area)) {
            return result(BUILDING_CLOCK_PREVALIDATION_LOCATION_BOUNDS, index);
        }
        if (rect_hits_protected(world, world->depots[index].area) || world->depots[index].access_node_id != BUILDING_CLOCK_INVALID_ID) {
            return result(BUILDING_CLOCK_PREVALIDATION_LOCATION_PROTECTED_REGION, index);
        }
        locations[location_count++] = world->depots[index].area;
    }
    for (uint8_t index = 0; index < world->debris_bay_count; index++) {
        const building_clock_debris_bay_t* bay = &world->debris_bays[index];
        if (!rect_in_world(bay->area)) {
            return result(BUILDING_CLOCK_PREVALIDATION_LOCATION_BOUNDS, index);
        }
        if (rect_hits_protected(world, bay->area) || bay->access_node_id != BUILDING_CLOCK_INVALID_ID || bay->assigned_digit_mask == 0U || (bay->assigned_digit_mask & 0xf0U) != 0U) {
            return result(BUILDING_CLOCK_PREVALIDATION_LOCATION_PROTECTED_REGION, index);
        }
        locations[location_count++] = bay->area;
    }
    for (uint8_t index = 0; index < world->staging_area_count; index++) {
        const building_clock_staging_area_t* staging = &world->staging_areas[index];
        if (!rect_in_world(staging->area)) {
            return result(BUILDING_CLOCK_PREVALIDATION_LOCATION_BOUNDS, index);
        }
        if (rect_hits_protected(world, staging->area) || staging->access_node_id != BUILDING_CLOCK_INVALID_ID || staging->assigned_digit_mask == 0U || (staging->assigned_digit_mask & 0xf0U) != 0U) {
            return result(BUILDING_CLOCK_PREVALIDATION_LOCATION_PROTECTED_REGION, index);
        }
        locations[location_count++] = staging->area;
    }

    for (uint8_t index = 0; index < location_count; index++) {
        for (uint8_t other = (uint8_t)(index + 1U); other < location_count; other++) {
            if (rects_overlap(locations[index], locations[other])) {
                return result(BUILDING_CLOCK_PREVALIDATION_LOCATION_OVERLAP, index);
            }
        }
    }

    for (uint8_t index = 0; index < world->debris_bay_count; index++) {
        const building_clock_rect_t bay = world->debris_bays[index].area;
        const building_clock_rect_t fall_space = {
            .x = bay.x,
            .y = (int16_t)(bay.y - DEBRIS_FALL_CLEARANCE),
            .width = bay.width,
            .height = DEBRIS_FALL_CLEARANCE,
        };
        for (uint8_t platform = 0; platform < world->platform_count; platform++) {
            const building_clock_platform_t* span = &world->platforms[platform];
            if (horizontal_span_hits_rect(span->x_start, span->x_end, span->surface_y, fall_space)) {
                return result(BUILDING_CLOCK_PREVALIDATION_DEBRIS_FALL_SPACE, index);
            }
        }
        for (uint8_t support = 0; support < world->major_support_count; support++) {
            if (rects_overlap(fall_space, world->major_supports[support].bounds)) {
                return result(BUILDING_CLOCK_PREVALIDATION_DEBRIS_FALL_SPACE, index);
            }
        }
    }
    return result(BUILDING_CLOCK_PREVALIDATION_OK, BUILDING_CLOCK_VALIDATION_NO_ITEM);
}

static building_clock_prevalidation_result_t validate_spawns(const building_clock_world_t* world) {
    for (uint8_t index = 0; index < world->worker_spawn_count; index++) {
        const building_clock_worker_spawn_t* spawn = &world->worker_spawns[index];
        if (!point_in_world(spawn->position) || point_hits_protected(world, spawn->position) || !point_on_platform(world, spawn->position) || spawn->nav_node_id != BUILDING_CLOCK_INVALID_ID) {
            return result(BUILDING_CLOCK_PREVALIDATION_SPAWN_GEOMETRY, index);
        }
    }
    return result(BUILDING_CLOCK_PREVALIDATION_OK, BUILDING_CLOCK_VALIDATION_NO_ITEM);
}

static building_clock_prevalidation_result_t validate_digit_access(const building_clock_world_t* world) {
    for (uint8_t digit = 0; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        const building_clock_digit_work_area_t* area = &world->digit_work_areas[digit];
        if (!rect_in_world(area->work_halo) || !rect_in_world(area->hazard_area) || area->debris_bay_id >= world->debris_bay_count || area->access_count < 4U || area->safe_retreat_node_count != 0U) {
            return result(BUILDING_CLOCK_PREVALIDATION_DIGIT_ACCESS_GEOMETRY, digit);
        }
        for (uint8_t access = 0; access < area->access_count; access++) {
            if (!point_in_world(area->access[access].position) || area->access[access].nav_node_id != BUILDING_CLOCK_INVALID_ID) {
                return result(BUILDING_CLOCK_PREVALIDATION_DIGIT_ACCESS_GEOMETRY, digit);
            }
            if ((area->access[access].kind == BUILDING_CLOCK_DIGIT_ACCESS_LOWER || area->access[access].kind == BUILDING_CLOCK_DIGIT_ACCESS_UPPER) &&
                !point_on_platform(world, area->access[access].position)) {
                return result(BUILDING_CLOCK_PREVALIDATION_DIGIT_ACCESS_GEOMETRY, digit);
            }
        }
    }
    return result(BUILDING_CLOCK_PREVALIDATION_OK, BUILDING_CLOCK_VALIDATION_NO_ITEM);
}

static building_clock_prevalidation_result_t validate_attachments(const building_clock_world_t* world) {
    for (uint8_t index = 0; index < world->temp_attachment_set_count; index++) {
        const building_clock_temp_attachment_set_t* attachment = &world->temp_attachment_sets[index];
        if (!rect_in_world(attachment->allowed_area) || attachment->anchor_count == 0U || attachment->assigned_digit_mask == 0U || (attachment->assigned_digit_mask & 0xf0U) != 0U ||
            attachment->connection_count != 0U) {
            return result(BUILDING_CLOCK_PREVALIDATION_TEMP_ATTACHMENT_GEOMETRY, index);
        }
        for (uint8_t anchor = 0; anchor < attachment->anchor_count; anchor++) {
            if (!point_in_world(attachment->anchors[anchor]) || !point_in_rect(attachment->anchors[anchor], attachment->allowed_area) || !point_on_platform(world, attachment->anchors[anchor])) {
                return result(BUILDING_CLOCK_PREVALIDATION_TEMP_ATTACHMENT_GEOMETRY, index);
            }
        }
    }
    return result(BUILDING_CLOCK_PREVALIDATION_OK, BUILDING_CLOCK_VALIDATION_NO_ITEM);
}

static building_clock_prevalidation_result_t validate_composition(const building_clock_world_t* world) {
    if (world->platform_count > MAX_PREVALIDATION_PLATFORM_COUNT || world->ladder_count > MAX_PREVALIDATION_LADDER_COUNT) {
        return result(BUILDING_CLOCK_PREVALIDATION_COMPOSITION_TOO_DENSE, BUILDING_CLOCK_VALIDATION_NO_ITEM);
    }

    uint64_t occupied_area = 0U;
    for (uint8_t index = 0; index < world->platform_count; index++) {
        occupied_area += (uint64_t)(world->platforms[index].x_end - world->platforms[index].x_start + 1) * PLATFORM_AREA_HEIGHT;
    }
    for (uint8_t index = 0; index < world->ladder_count; index++) {
        occupied_area += (uint64_t)(world->ladders[index].bottom_y - world->ladders[index].top_y + 1) * LADDER_AREA_WIDTH;
    }
    for (uint8_t index = 0; index < world->major_support_count; index++) {
        occupied_area += (uint64_t)world->major_supports[index].bounds.width * world->major_supports[index].bounds.height;
    }
    for (uint8_t index = 0; index < world->depot_count; index++) {
        occupied_area += (uint64_t)world->depots[index].area.width * world->depots[index].area.height;
    }
    for (uint8_t index = 0; index < world->debris_bay_count; index++) {
        occupied_area += (uint64_t)world->debris_bays[index].area.width * world->debris_bays[index].area.height;
    }
    for (uint8_t index = 0; index < world->staging_area_count; index++) {
        occupied_area += (uint64_t)world->staging_areas[index].area.width * world->staging_areas[index].area.height;
    }

    uint64_t protected_area = (uint64_t)world->colon_envelope.width * world->colon_envelope.height;
    for (uint8_t digit = 0; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        protected_area += (uint64_t)world->digit_work_areas[digit].envelope.width * world->digit_work_areas[digit].envelope.height;
    }
    const uint64_t construction_area = (uint64_t)BUILDING_CLOCK_WORLD_WIDTH * world->date_band.y - protected_area;
    if (construction_area == 0U || occupied_area * 100U > construction_area * MAX_OCCUPIED_PERCENT) {
        return result(BUILDING_CLOCK_PREVALIDATION_NEGATIVE_SPACE, BUILDING_CLOCK_VALIDATION_NO_ITEM);
    }
    return result(BUILDING_CLOCK_PREVALIDATION_OK, BUILDING_CLOCK_VALIDATION_NO_ITEM);
}

building_clock_prevalidation_result_t building_clock_prevalidate_candidate(const building_clock_world_t* world) {
    if (world == NULL) {
        return result(BUILDING_CLOCK_PREVALIDATION_NULL_WORLD, BUILDING_CLOCK_VALIDATION_NO_ITEM);
    }

    building_clock_prevalidation_result_t check = validate_counts(world);
    if (check.reason != BUILDING_CLOCK_PREVALIDATION_OK) {
        return check;
    }
    check = validate_protected_regions(world);
    if (check.reason != BUILDING_CLOCK_PREVALIDATION_OK) {
        return check;
    }
    if (world->navigation.node_count != 0U || world->navigation.edge_count != 0U) {
        return result(BUILDING_CLOCK_PREVALIDATION_NAVIGATION_NOT_EMPTY, BUILDING_CLOCK_VALIDATION_NO_ITEM);
    }

    check = validate_platforms(world);
    if (check.reason != BUILDING_CLOCK_PREVALIDATION_OK) {
        return check;
    }
    check = validate_supports(world);
    if (check.reason != BUILDING_CLOCK_PREVALIDATION_OK) {
        return check;
    }
    check = validate_ladders(world);
    if (check.reason != BUILDING_CLOCK_PREVALIDATION_OK) {
        return check;
    }
    check = validate_cranes(world);
    if (check.reason != BUILDING_CLOCK_PREVALIDATION_OK) {
        return check;
    }
    check = validate_lifts(world);
    if (check.reason != BUILDING_CLOCK_PREVALIDATION_OK) {
        return check;
    }
    check = validate_cart_routes(world);
    if (check.reason != BUILDING_CLOCK_PREVALIDATION_OK) {
        return check;
    }
    check = validate_locations(world);
    if (check.reason != BUILDING_CLOCK_PREVALIDATION_OK) {
        return check;
    }
    check = validate_spawns(world);
    if (check.reason != BUILDING_CLOCK_PREVALIDATION_OK) {
        return check;
    }
    check = validate_digit_access(world);
    if (check.reason != BUILDING_CLOCK_PREVALIDATION_OK) {
        return check;
    }
    check = validate_attachments(world);
    if (check.reason != BUILDING_CLOCK_PREVALIDATION_OK) {
        return check;
    }
    return validate_composition(world);
}

const char* building_clock_prevalidation_reason_name(building_clock_prevalidation_reason_t reason) {
    switch (reason) {
    case BUILDING_CLOCK_PREVALIDATION_OK:
        return "ok";
    case BUILDING_CLOCK_PREVALIDATION_NULL_WORLD:
        return "null world";
    case BUILDING_CLOCK_PREVALIDATION_COUNT_EXCEEDS_CAPACITY:
        return "count exceeds capacity";
    case BUILDING_CLOCK_PREVALIDATION_PROTECTED_REGION_INVALID:
        return "protected region invalid";
    case BUILDING_CLOCK_PREVALIDATION_PROTECTED_REGION_OVERLAP:
        return "protected regions overlap";
    case BUILDING_CLOCK_PREVALIDATION_NAVIGATION_NOT_EMPTY:
        return "navigation is not empty";
    case BUILDING_CLOCK_PREVALIDATION_PLATFORM_BOUNDS:
        return "platform out of bounds";
    case BUILDING_CLOCK_PREVALIDATION_PLATFORM_TOO_SHORT:
        return "platform too short";
    case BUILDING_CLOCK_PREVALIDATION_PLATFORM_PROTECTED_REGION:
        return "platform crosses protected region";
    case BUILDING_CLOCK_PREVALIDATION_PLATFORM_OVERLAP:
        return "platform spans overlap";
    case BUILDING_CLOCK_PREVALIDATION_SUPPORT_BOUNDS:
        return "support out of bounds";
    case BUILDING_CLOCK_PREVALIDATION_SUPPORT_PROTECTED_REGION:
        return "support crosses protected region";
    case BUILDING_CLOCK_PREVALIDATION_SUPPORT_OVERLAP:
        return "supports overlap";
    case BUILDING_CLOCK_PREVALIDATION_SUPPORT_RELATIONSHIP:
        return "support relationship invalid";
    case BUILDING_CLOCK_PREVALIDATION_LADDER_BOUNDS:
        return "ladder out of bounds";
    case BUILDING_CLOCK_PREVALIDATION_LADDER_ENDPOINT:
        return "ladder endpoint invalid";
    case BUILDING_CLOCK_PREVALIDATION_LADDER_PROTECTED_REGION:
        return "ladder crosses protected region";
    case BUILDING_CLOCK_PREVALIDATION_CRANE_GEOMETRY:
        return "crane geometry invalid";
    case BUILDING_CLOCK_PREVALIDATION_LIFT_GEOMETRY:
        return "lift geometry invalid";
    case BUILDING_CLOCK_PREVALIDATION_CART_ROUTE_GEOMETRY:
        return "cart route geometry invalid";
    case BUILDING_CLOCK_PREVALIDATION_LOCATION_BOUNDS:
        return "semantic location out of bounds";
    case BUILDING_CLOCK_PREVALIDATION_LOCATION_PROTECTED_REGION:
        return "semantic location crosses protected region";
    case BUILDING_CLOCK_PREVALIDATION_LOCATION_OVERLAP:
        return "semantic locations overlap";
    case BUILDING_CLOCK_PREVALIDATION_DEBRIS_FALL_SPACE:
        return "debris fall space obstructed";
    case BUILDING_CLOCK_PREVALIDATION_SPAWN_GEOMETRY:
        return "worker spawn geometry invalid";
    case BUILDING_CLOCK_PREVALIDATION_DIGIT_ACCESS_GEOMETRY:
        return "digit access geometry invalid";
    case BUILDING_CLOCK_PREVALIDATION_TEMP_ATTACHMENT_GEOMETRY:
        return "temporary attachment geometry invalid";
    case BUILDING_CLOCK_PREVALIDATION_COMPOSITION_TOO_DENSE:
        return "composition too dense";
    case BUILDING_CLOCK_PREVALIDATION_NEGATIVE_SPACE:
        return "insufficient negative space";
    default:
        return "unknown preliminary validation result";
    }
}
