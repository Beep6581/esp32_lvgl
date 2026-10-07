#include "building_clock_validation.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define LADDER_WAIT_DISTANCE 8

static building_clock_complete_validation_result_t complete_result(building_clock_complete_validation_reason_t reason, uint16_t item_index) {
    return (building_clock_complete_validation_result_t){.reason = reason, .item_index = item_index};
}

static bool points_equal(building_clock_point_t first, building_clock_point_t second) {
    return first.x == second.x && first.y == second.y;
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

static bool point_on_platform(const building_clock_platform_t* platform, building_clock_point_t point) {
    return point.y == platform->surface_y && point.x >= platform->x_start && point.x <= platform->x_end;
}

static bool point_on_any_platform(const building_clock_world_t* world, building_clock_point_t point) {
    for (uint8_t platform = 0; platform < world->platform_count; platform++) {
        if (point_on_platform(&world->platforms[platform], point)) {
            return true;
        }
    }
    return false;
}

static bool point_horizontally_in_rect(building_clock_point_t point, building_clock_rect_t rect) {
    return point.x >= rect.x && point.x < (int32_t)rect.x + rect.width;
}

static building_clock_point_t ladder_waiting_position(const building_clock_platform_t* platform, int16_t ladder_x) {
    int16_t waiting_x = (int16_t)(ladder_x + LADDER_WAIT_DISTANCE);
    if (waiting_x > platform->x_end) {
        waiting_x = (int16_t)(ladder_x - LADDER_WAIT_DISTANCE);
    }
    if (waiting_x < platform->x_start) {
        waiting_x = platform->x_start;
    }
    return (building_clock_point_t){.x = waiting_x, .y = platform->surface_y};
}

static uint16_t point_distance(building_clock_point_t first, building_clock_point_t second) {
    int32_t x_distance = (int32_t)first.x - second.x;
    int32_t y_distance = (int32_t)first.y - second.y;
    if (x_distance < 0) {
        x_distance = -x_distance;
    }
    if (y_distance < 0) {
        y_distance = -y_distance;
    }
    const int32_t distance = x_distance + y_distance;
    return (uint16_t)(distance == 0 ? 1 : distance);
}

static bool node_matches(const building_clock_world_t* world, uint8_t node_id, building_clock_nav_node_kind_t kind, building_clock_nav_source_kind_t source_kind, uint8_t source_id,
                         uint8_t source_point_id) {
    if (node_id >= world->navigation.node_count) {
        return false;
    }
    const building_clock_nav_node_t* node = &world->navigation.nodes[node_id];
    return node->kind == kind && node->source_kind == source_kind && node->source_id == source_id && node->source_point_id == source_point_id;
}

static bool valid_node_source(const building_clock_world_t* world, const building_clock_nav_node_t* node) {
    switch (node->source_kind) {
    case BUILDING_CLOCK_NAV_SOURCE_PLATFORM:
        if (node->source_id >= world->platform_count || node->source_point_id > 1U || node->kind != BUILDING_CLOCK_NAV_NODE_PLATFORM_ENDPOINT) {
            return false;
        }
        return points_equal(node->position, (building_clock_point_t){
                                                .x = node->source_point_id == 0U ? world->platforms[node->source_id].x_start : world->platforms[node->source_id].x_end,
                                                .y = world->platforms[node->source_id].surface_y,
                                            });
    case BUILDING_CLOCK_NAV_SOURCE_LADDER:
        if (node->source_id >= world->ladder_count || node->source_point_id > 3U) {
            return false;
        }
        const building_clock_ladder_t* ladder = &world->ladders[node->source_id];
        if (node->source_point_id == 0U) {
            return node->kind == BUILDING_CLOCK_NAV_NODE_LADDER_TOP && points_equal(node->position, (building_clock_point_t){.x = ladder->x, .y = ladder->top_y});
        }
        if (node->source_point_id == 1U) {
            return node->kind == BUILDING_CLOCK_NAV_NODE_LADDER_BOTTOM && points_equal(node->position, (building_clock_point_t){.x = ladder->x, .y = ladder->bottom_y});
        }
        const uint8_t platform_id = node->source_point_id == 2U ? ladder->top_platform_id : ladder->bottom_platform_id;
        if (platform_id >= world->platform_count) {
            return false;
        }
        return node->kind == BUILDING_CLOCK_NAV_NODE_SAFE_WAITING && points_equal(node->position, ladder_waiting_position(&world->platforms[platform_id], ladder->x));
    case BUILDING_CLOCK_NAV_SOURCE_CART_ROUTE:
        return node->source_id < world->cart_route_count && node->source_point_id < world->cart_routes[node->source_id].stop_count && node->kind == BUILDING_CLOCK_NAV_NODE_CART_STOP &&
               points_equal(node->position, world->cart_routes[node->source_id].stops[node->source_point_id].position);
    case BUILDING_CLOCK_NAV_SOURCE_LIFT:
        return node->source_id < world->lift_count && node->source_point_id < world->lifts[node->source_id].stop_count && node->kind == BUILDING_CLOCK_NAV_NODE_LIFT_STOP &&
               points_equal(node->position, world->lifts[node->source_id].stops[node->source_point_id].position);
    case BUILDING_CLOCK_NAV_SOURCE_DIGIT_ACCESS:
        return node->source_id < BUILDING_CLOCK_DIGIT_COUNT && node->source_point_id < world->digit_work_areas[node->source_id].access_count && node->kind == BUILDING_CLOCK_NAV_NODE_DIGIT_WORK &&
               points_equal(node->position, world->digit_work_areas[node->source_id].access[node->source_point_id].position);
    case BUILDING_CLOCK_NAV_SOURCE_STAGING_AREA:
        return node->source_id < world->staging_area_count && node->source_point_id == 0U && node->kind == BUILDING_CLOCK_NAV_NODE_STAGING &&
               point_horizontally_in_rect(node->position, world->staging_areas[node->source_id].area) && point_on_any_platform(world, node->position);
    case BUILDING_CLOCK_NAV_SOURCE_DEPOT:
        return node->source_id < world->depot_count && node->source_point_id == 0U && node->kind == BUILDING_CLOCK_NAV_NODE_DEPOT &&
               point_horizontally_in_rect(node->position, world->depots[node->source_id].area) && point_on_any_platform(world, node->position);
    case BUILDING_CLOCK_NAV_SOURCE_DEBRIS_BAY:
        return node->source_id < world->debris_bay_count && node->source_point_id == 0U && node->kind == BUILDING_CLOCK_NAV_NODE_DEBRIS &&
               point_horizontally_in_rect(node->position, world->debris_bays[node->source_id].area) && point_on_any_platform(world, node->position);
    case BUILDING_CLOCK_NAV_SOURCE_MACHINERY:
        if (node->kind != BUILDING_CLOCK_NAV_NODE_MACHINERY_CONTROL) {
            return false;
        }
        if (node->source_point_id == 0U) {
            return node->source_id < world->crane_count && points_equal(node->position, world->cranes[node->source_id].base_position);
        }
        if (node->source_point_id == 1U && node->source_id < world->lift_count) {
            for (uint8_t stop = 0; stop < world->lifts[node->source_id].stop_count; stop++) {
                if (points_equal(node->position, world->lifts[node->source_id].stops[stop].position)) {
                    return true;
                }
            }
        }
        return false;
    case BUILDING_CLOCK_NAV_SOURCE_SPAWN:
        return node->source_id < world->worker_spawn_count && node->source_point_id == 0U && node->kind == BUILDING_CLOCK_NAV_NODE_SPAWN &&
               points_equal(node->position, world->worker_spawns[node->source_id].position);
    case BUILDING_CLOCK_NAV_SOURCE_TEMP_ATTACHMENT:
        return node->source_id < world->temp_attachment_set_count && node->source_point_id < world->temp_attachment_sets[node->source_id].anchor_count &&
               node->kind == BUILDING_CLOCK_NAV_NODE_TEMP_ATTACHMENT && points_equal(node->position, world->temp_attachment_sets[node->source_id].anchors[node->source_point_id]);
    default:
        return false;
    }
}

static bool edge_points_match(building_clock_point_t first, building_clock_point_t second, building_clock_point_t expected_first, building_clock_point_t expected_second) {
    return (points_equal(first, expected_first) && points_equal(second, expected_second)) || (points_equal(first, expected_second) && points_equal(second, expected_first));
}

static bool valid_edge_geometry(const building_clock_world_t* world, const building_clock_nav_edge_t* edge) {
    const building_clock_nav_node_t* from = &world->navigation.nodes[edge->from_node_id];
    const building_clock_nav_node_t* to = &world->navigation.nodes[edge->to_node_id];
    if (edge->traversal_cost != point_distance(from->position, to->position)) {
        return false;
    }

    switch (edge->kind) {
    case BUILDING_CLOCK_NAV_EDGE_WALK:
        if (edge->source_kind != BUILDING_CLOCK_NAV_SOURCE_PLATFORM || edge->source_id >= world->platform_count || !point_on_platform(&world->platforms[edge->source_id], from->position) ||
            !point_on_platform(&world->platforms[edge->source_id], to->position)) {
            return false;
        }
        const uint16_t platform_length = (uint16_t)(world->platforms[edge->source_id].x_end - world->platforms[edge->source_id].x_start + 1);
        return edge->capacity == (platform_length < 64U ? 1U : 2U) && ((edge->flags & BUILDING_CLOCK_NAV_EDGE_ALLOW_LARGE_LOAD) != 0U) == (platform_length >= 64U);
    case BUILDING_CLOCK_NAV_EDGE_CLIMB:
        if (edge->source_kind != BUILDING_CLOCK_NAV_SOURCE_LADDER || edge->source_id >= world->ladder_count || edge->capacity != 1U || (edge->flags & BUILDING_CLOCK_NAV_EDGE_ALLOW_LARGE_LOAD) != 0U) {
            return false;
        }
        return edge_points_match(from->position, to->position, (building_clock_point_t){.x = world->ladders[edge->source_id].x, .y = world->ladders[edge->source_id].top_y},
                                 (building_clock_point_t){.x = world->ladders[edge->source_id].x, .y = world->ladders[edge->source_id].bottom_y});
    case BUILDING_CLOCK_NAV_EDGE_RIDE_CART:
        return edge->source_kind == BUILDING_CLOCK_NAV_SOURCE_CART_ROUTE && edge->source_id < world->cart_route_count && from->source_kind == BUILDING_CLOCK_NAV_SOURCE_CART_ROUTE &&
               to->source_kind == BUILDING_CLOCK_NAV_SOURCE_CART_ROUTE && from->source_id == edge->source_id && to->source_id == edge->source_id &&
               (from->source_point_id + 1U == to->source_point_id || to->source_point_id + 1U == from->source_point_id) && edge->capacity == 2U &&
               (edge->flags & BUILDING_CLOCK_NAV_EDGE_ALLOW_LARGE_LOAD) != 0U;
    case BUILDING_CLOCK_NAV_EDGE_RIDE_LIFT:
        return edge->source_kind == BUILDING_CLOCK_NAV_SOURCE_LIFT && edge->source_id < world->lift_count && from->source_kind == BUILDING_CLOCK_NAV_SOURCE_LIFT &&
               to->source_kind == BUILDING_CLOCK_NAV_SOURCE_LIFT && from->source_id == edge->source_id && to->source_id == edge->source_id &&
               (from->source_point_id + 1U == to->source_point_id || to->source_point_id + 1U == from->source_point_id) && edge->capacity == 3U &&
               (edge->flags & BUILDING_CLOCK_NAV_EDGE_ALLOW_LARGE_LOAD) != 0U;
    case BUILDING_CLOCK_NAV_EDGE_TEMP_CONNECTION:
        return edge->source_kind == BUILDING_CLOCK_NAV_SOURCE_TEMP_ATTACHMENT && edge->source_id < world->temp_attachment_set_count && edge->temp_attachment_set_id == edge->source_id &&
               from->source_kind == BUILDING_CLOCK_NAV_SOURCE_TEMP_ATTACHMENT && to->source_kind == BUILDING_CLOCK_NAV_SOURCE_TEMP_ATTACHMENT && from->source_id == edge->source_id &&
               to->source_id == edge->source_id && point_in_rect(from->position, world->temp_attachment_sets[edge->source_id].allowed_area) &&
               point_in_rect(to->position, world->temp_attachment_sets[edge->source_id].allowed_area);
    default:
        return false;
    }
}

static building_clock_complete_validation_result_t validate_graph(const building_clock_world_t* world) {
    if (world->navigation.node_count > BUILDING_CLOCK_MAX_NAV_NODES || world->navigation.edge_count > BUILDING_CLOCK_MAX_NAV_EDGES) {
        return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_GRAPH_CAPACITY, BUILDING_CLOCK_VALIDATION_NO_ITEM);
    }
    if (world->navigation.node_count == 0U || world->navigation.edge_count == 0U) {
        return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_GRAPH_EMPTY, BUILDING_CLOCK_VALIDATION_NO_ITEM);
    }

    for (uint8_t node_id = 0; node_id < world->navigation.node_count; node_id++) {
        const building_clock_nav_node_t* node = &world->navigation.nodes[node_id];
        if (!point_in_world(node->position)) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_NODE_REFERENCE, node_id);
        }
        if (!valid_node_source(world, node)) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_NODE_SOURCE, node_id);
        }
    }

    const uint8_t valid_flags = BUILDING_CLOCK_NAV_EDGE_BIDIRECTIONAL | BUILDING_CLOCK_NAV_EDGE_ALLOW_LARGE_LOAD | BUILDING_CLOCK_NAV_EDGE_REQUIRES_TEMP_STRUCTURE;
    for (uint16_t edge_id = 0; edge_id < world->navigation.edge_count; edge_id++) {
        const building_clock_nav_edge_t* edge = &world->navigation.edges[edge_id];
        if (edge->from_node_id >= world->navigation.node_count || edge->to_node_id >= world->navigation.node_count || edge->from_node_id == edge->to_node_id || edge->traversal_cost == 0U ||
            edge->capacity == 0U || (edge->flags & ~valid_flags) != 0U) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_EDGE_REFERENCE, edge_id);
        }
        if ((edge->kind == BUILDING_CLOCK_NAV_EDGE_TEMP_CONNECTION) != ((edge->flags & BUILDING_CLOCK_NAV_EDGE_REQUIRES_TEMP_STRUCTURE) != 0U)) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_EDGE_SOURCE, edge_id);
        }
        if (!valid_edge_geometry(world, edge)) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_EDGE_GEOMETRY, edge_id);
        }
    }
    return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_OK, BUILDING_CLOCK_VALIDATION_NO_ITEM);
}

static building_clock_complete_validation_result_t validate_semantic_node_references(const building_clock_world_t* world) {
    for (uint8_t route = 0; route < world->cart_route_count; route++) {
        for (uint8_t stop = 0; stop < world->cart_routes[route].stop_count; stop++) {
            if (!node_matches(world, world->cart_routes[route].stops[stop].nav_node_id, BUILDING_CLOCK_NAV_NODE_CART_STOP, BUILDING_CLOCK_NAV_SOURCE_CART_ROUTE, route, stop)) {
                return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_NODE_REFERENCE, route);
            }
        }
    }
    for (uint8_t lift = 0; lift < world->lift_count; lift++) {
        for (uint8_t stop = 0; stop < world->lifts[lift].stop_count; stop++) {
            if (!node_matches(world, world->lifts[lift].stops[stop].nav_node_id, BUILDING_CLOCK_NAV_NODE_LIFT_STOP, BUILDING_CLOCK_NAV_SOURCE_LIFT, lift, stop)) {
                return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_NODE_REFERENCE, lift);
            }
        }
        if (!node_matches(world, world->lifts[lift].operator_node_id, BUILDING_CLOCK_NAV_NODE_MACHINERY_CONTROL, BUILDING_CLOCK_NAV_SOURCE_MACHINERY, lift, 1U)) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_NODE_REFERENCE, lift);
        }
    }
    for (uint8_t crane = 0; crane < world->crane_count; crane++) {
        if (!node_matches(world, world->cranes[crane].operator_node_id, BUILDING_CLOCK_NAV_NODE_MACHINERY_CONTROL, BUILDING_CLOCK_NAV_SOURCE_MACHINERY, crane, 0U)) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_NODE_REFERENCE, crane);
        }
    }
    for (uint8_t depot = 0; depot < world->depot_count; depot++) {
        if (!node_matches(world, world->depots[depot].access_node_id, BUILDING_CLOCK_NAV_NODE_DEPOT, BUILDING_CLOCK_NAV_SOURCE_DEPOT, depot, 0U)) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_NODE_REFERENCE, depot);
        }
    }
    for (uint8_t bay = 0; bay < world->debris_bay_count; bay++) {
        if (!node_matches(world, world->debris_bays[bay].access_node_id, BUILDING_CLOCK_NAV_NODE_DEBRIS, BUILDING_CLOCK_NAV_SOURCE_DEBRIS_BAY, bay, 0U)) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_NODE_REFERENCE, bay);
        }
    }
    for (uint8_t staging = 0; staging < world->staging_area_count; staging++) {
        if (!node_matches(world, world->staging_areas[staging].access_node_id, BUILDING_CLOCK_NAV_NODE_STAGING, BUILDING_CLOCK_NAV_SOURCE_STAGING_AREA, staging, 0U)) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_NODE_REFERENCE, staging);
        }
    }
    for (uint8_t spawn = 0; spawn < world->worker_spawn_count; spawn++) {
        if (!node_matches(world, world->worker_spawns[spawn].nav_node_id, BUILDING_CLOCK_NAV_NODE_SPAWN, BUILDING_CLOCK_NAV_SOURCE_SPAWN, spawn, 0U)) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_NODE_REFERENCE, spawn);
        }
    }
    for (uint8_t digit = 0; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        for (uint8_t access = 0; access < world->digit_work_areas[digit].access_count; access++) {
            if (!node_matches(world, world->digit_work_areas[digit].access[access].nav_node_id, BUILDING_CLOCK_NAV_NODE_DIGIT_WORK, BUILDING_CLOCK_NAV_SOURCE_DIGIT_ACCESS, digit, access)) {
                return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_NODE_REFERENCE, digit);
            }
        }
    }
    return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_OK, BUILDING_CLOCK_VALIDATION_NO_ITEM);
}

static void mark_reachable(const building_clock_world_t* world, uint8_t start_node_id, bool include_temporary, bool reachable[BUILDING_CLOCK_MAX_NAV_NODES]) {
    uint8_t queue[BUILDING_CLOCK_MAX_NAV_NODES];
    uint8_t head = 0U;
    uint8_t tail = 0U;
    memset(reachable, 0, BUILDING_CLOCK_MAX_NAV_NODES * sizeof(reachable[0]));

    if (start_node_id >= world->navigation.node_count) {
        return;
    }
    reachable[start_node_id] = true;
    queue[tail++] = start_node_id;

    while (head < tail) {
        const uint8_t current = queue[head++];
        for (uint16_t edge_id = 0; edge_id < world->navigation.edge_count; edge_id++) {
            const building_clock_nav_edge_t* edge = &world->navigation.edges[edge_id];
            if (!include_temporary && (edge->flags & BUILDING_CLOCK_NAV_EDGE_REQUIRES_TEMP_STRUCTURE) != 0U) {
                continue;
            }
            uint8_t next = BUILDING_CLOCK_INVALID_ID;
            if (edge->from_node_id == current) {
                next = edge->to_node_id;
            } else if (edge->to_node_id == current && (edge->flags & BUILDING_CLOCK_NAV_EDGE_BIDIRECTIONAL) != 0U) {
                next = edge->from_node_id;
            }
            if (next < world->navigation.node_count && !reachable[next]) {
                reachable[next] = true;
                queue[tail++] = next;
            }
        }
    }
}

static building_clock_complete_validation_result_t validate_main_component(const building_clock_world_t* world, bool reachable[BUILDING_CLOCK_MAX_NAV_NODES]) {
    if (world->worker_spawn_count == 0U) {
        return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_SPAWN_UNREACHABLE, BUILDING_CLOCK_VALIDATION_NO_ITEM);
    }
    mark_reachable(world, world->worker_spawns[0].nav_node_id, false, reachable);

    for (uint8_t spawn = 0; spawn < world->worker_spawn_count; spawn++) {
        if (!reachable[world->worker_spawns[spawn].nav_node_id]) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_SPAWN_UNREACHABLE, spawn);
        }
    }
    for (uint8_t depot = 0; depot < world->depot_count; depot++) {
        if (!reachable[world->depots[depot].access_node_id]) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_DEPOT_UNREACHABLE, depot);
        }
    }
    for (uint8_t staging = 0; staging < world->staging_area_count; staging++) {
        if (!reachable[world->staging_areas[staging].access_node_id]) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_STAGING_UNREACHABLE, staging);
        }
    }
    for (uint8_t bay = 0; bay < world->debris_bay_count; bay++) {
        if (!reachable[world->debris_bays[bay].access_node_id]) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_DEBRIS_UNREACHABLE, bay);
        }
    }
    for (uint8_t crane = 0; crane < world->crane_count; crane++) {
        if (!reachable[world->cranes[crane].operator_node_id]) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_MACHINERY_UNREACHABLE, crane);
        }
    }
    for (uint8_t lift = 0; lift < world->lift_count; lift++) {
        if (!reachable[world->lifts[lift].operator_node_id]) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_MACHINERY_UNREACHABLE, lift);
        }
    }
    for (uint8_t route = 0; route < world->cart_route_count; route++) {
        for (uint8_t stop = 0; stop < world->cart_routes[route].stop_count; stop++) {
            if (!reachable[world->cart_routes[route].stops[stop].nav_node_id]) {
                return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_CART_STOP_UNREACHABLE, route);
            }
        }
    }
    return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_OK, BUILDING_CLOCK_VALIDATION_NO_ITEM);
}

static bool crane_serves_point(const building_clock_world_t* world, const bool reachable[BUILDING_CLOCK_MAX_NAV_NODES], building_clock_point_t point) {
    for (uint8_t crane = 0; crane < world->crane_count; crane++) {
        if (reachable[world->cranes[crane].operator_node_id] && point_in_rect(point, world->cranes[crane].working_area)) {
            return true;
        }
    }
    return false;
}

static building_clock_complete_validation_result_t validate_digit_access(const building_clock_world_t* world, const bool reachable[BUILDING_CLOCK_MAX_NAV_NODES],
                                                                         const bool reachable_with_temporary[BUILDING_CLOCK_MAX_NAV_NODES]) {
    for (uint8_t digit = 0; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        bool lower = false;
        bool left = false;
        bool right = false;
        bool upper = false;
        const building_clock_digit_work_area_t* area = &world->digit_work_areas[digit];

        for (uint8_t access = 0; access < area->access_count; access++) {
            const building_clock_digit_access_t* point = &area->access[access];
            const bool permanently_reachable = reachable[point->nav_node_id];
            const bool machinery_reachable = crane_serves_point(world, reachable, point->position);
            switch (point->kind) {
            case BUILDING_CLOCK_DIGIT_ACCESS_LOWER:
                lower |= permanently_reachable;
                break;
            case BUILDING_CLOCK_DIGIT_ACCESS_LEFT:
                left |= permanently_reachable || machinery_reachable;
                break;
            case BUILDING_CLOCK_DIGIT_ACCESS_RIGHT:
                right |= permanently_reachable || machinery_reachable;
                break;
            case BUILDING_CLOCK_DIGIT_ACCESS_UPPER:
                upper |= reachable_with_temporary[point->nav_node_id] || machinery_reachable;
                break;
            case BUILDING_CLOCK_DIGIT_ACCESS_MACHINERY:
            case BUILDING_CLOCK_DIGIT_ACCESS_STAGING:
                break;
            default:
                return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_DIGIT_ACCESS_UNREACHABLE, digit);
            }
        }
        if (!lower) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_DIGIT_ACCESS_UNREACHABLE, digit);
        }
        if (!upper) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_UPPER_CONSTRUCTION_ACCESS, digit);
        }
        if (!left || !right) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_CRANE_RANGE, digit);
        }
    }
    return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_OK, BUILDING_CLOCK_VALIDATION_NO_ITEM);
}

static bool node_has_walk_edge(const building_clock_world_t* world, uint8_t node_id) {
    for (uint16_t edge = 0; edge < world->navigation.edge_count; edge++) {
        if (world->navigation.edges[edge].kind == BUILDING_CLOCK_NAV_EDGE_WALK && (world->navigation.edges[edge].from_node_id == node_id || world->navigation.edges[edge].to_node_id == node_id)) {
            return true;
        }
    }
    return false;
}

static building_clock_complete_validation_result_t validate_waiting_positions(const building_clock_world_t* world) {
    for (uint8_t ladder = 0; ladder < world->ladder_count; ladder++) {
        uint8_t top_waiting = BUILDING_CLOCK_INVALID_ID;
        uint8_t bottom_waiting = BUILDING_CLOCK_INVALID_ID;
        for (uint8_t node = 0; node < world->navigation.node_count; node++) {
            const building_clock_nav_node_t* candidate = &world->navigation.nodes[node];
            if (candidate->source_kind == BUILDING_CLOCK_NAV_SOURCE_LADDER && candidate->source_id == ladder) {
                if (candidate->source_point_id == 2U) {
                    top_waiting = node;
                } else if (candidate->source_point_id == 3U) {
                    bottom_waiting = node;
                }
            }
        }
        if (top_waiting == BUILDING_CLOCK_INVALID_ID || bottom_waiting == BUILDING_CLOCK_INVALID_ID || !node_has_walk_edge(world, top_waiting) || !node_has_walk_edge(world, bottom_waiting)) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_WAITING_POSITION, ladder);
        }
    }
    return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_OK, BUILDING_CLOCK_VALIDATION_NO_ITEM);
}

static building_clock_complete_validation_result_t validate_hazard_retreat(const building_clock_world_t* world, const bool reachable[BUILDING_CLOCK_MAX_NAV_NODES],
                                                                           const bool reachable_with_temporary[BUILDING_CLOCK_MAX_NAV_NODES]) {
    for (uint8_t digit = 0; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        const building_clock_digit_work_area_t* area = &world->digit_work_areas[digit];
        if (area->safe_retreat_node_count == 0U || area->safe_retreat_node_count > BUILDING_CLOCK_MAX_SAFE_RETREAT_NODES) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_HAZARD_RETREAT, digit);
        }
        bool has_safe_node = false;
        for (uint8_t safe = 0; safe < area->safe_retreat_node_count; safe++) {
            const uint8_t node_id = area->safe_retreat_node_ids[safe];
            if (node_id < world->navigation.node_count && reachable[node_id] && !point_in_rect(world->navigation.nodes[node_id].position, area->hazard_area)) {
                has_safe_node = true;
            }
        }
        if (!has_safe_node) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_HAZARD_RETREAT, digit);
        }

        bool temporary_exit = false;
        for (uint8_t attachment = 0; attachment < world->temp_attachment_set_count; attachment++) {
            const building_clock_temp_attachment_set_t* set = &world->temp_attachment_sets[attachment];
            if ((set->assigned_digit_mask & (uint8_t)(1U << digit)) == 0U) {
                continue;
            }
            for (uint8_t connection = 0; connection < set->connection_count; connection++) {
                const building_clock_temp_connection_t* link = &set->connections[connection];
                if (link->from_node_id >= world->navigation.node_count || link->to_node_id >= world->navigation.node_count || !reachable_with_temporary[link->from_node_id] ||
                    !reachable_with_temporary[link->to_node_id]) {
                    continue;
                }
                if ((reachable[link->from_node_id] && !point_in_rect(world->navigation.nodes[link->from_node_id].position, area->hazard_area)) ||
                    (reachable[link->to_node_id] && !point_in_rect(world->navigation.nodes[link->to_node_id].position, area->hazard_area))) {
                    temporary_exit = true;
                }
            }
        }
        if (!temporary_exit) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_HAZARD_RETREAT, digit);
        }
    }
    return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_OK, BUILDING_CLOCK_VALIDATION_NO_ITEM);
}

static building_clock_complete_validation_result_t validate_material_delivery(const building_clock_world_t* world, const bool reachable[BUILDING_CLOCK_MAX_NAV_NODES]) {
    for (uint8_t digit = 0; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        bool staging_available = false;
        for (uint8_t staging = 0; staging < world->staging_area_count; staging++) {
            if ((world->staging_areas[staging].assigned_digit_mask & (uint8_t)(1U << digit)) != 0U && reachable[world->staging_areas[staging].access_node_id]) {
                staging_available = true;
                break;
            }
        }
        if (!staging_available) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_MATERIAL_DELIVERY, digit);
        }
    }
    return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_OK, BUILDING_CLOCK_VALIDATION_NO_ITEM);
}

static bool graph_has_temp_edge(const building_clock_world_t* world, uint8_t attachment, const building_clock_temp_connection_t* connection) {
    for (uint16_t edge = 0; edge < world->navigation.edge_count; edge++) {
        const building_clock_nav_edge_t* candidate = &world->navigation.edges[edge];
        if (candidate->kind == BUILDING_CLOCK_NAV_EDGE_TEMP_CONNECTION && candidate->source_id == attachment && candidate->temp_attachment_set_id == attachment &&
            candidate->from_node_id == connection->from_node_id && candidate->to_node_id == connection->to_node_id && candidate->traversal_cost == connection->traversal_cost &&
            candidate->capacity == connection->capacity && ((candidate->flags & BUILDING_CLOCK_NAV_EDGE_ALLOW_LARGE_LOAD) != 0U) == connection->allow_large_load) {
            return true;
        }
    }
    return false;
}

static building_clock_complete_validation_result_t validate_temporary_routes(const building_clock_world_t* world, const bool reachable[BUILDING_CLOCK_MAX_NAV_NODES],
                                                                             const bool reachable_with_temporary[BUILDING_CLOCK_MAX_NAV_NODES]) {
    for (uint8_t attachment = 0; attachment < world->temp_attachment_set_count; attachment++) {
        const building_clock_temp_attachment_set_t* set = &world->temp_attachment_sets[attachment];
        if (set->connection_count == 0U || set->connection_count > BUILDING_CLOCK_MAX_ATTACHMENT_CONNECTIONS) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_TEMP_STRUCTURE_ROUTE, attachment);
        }
        for (uint8_t connection = 0; connection < set->connection_count; connection++) {
            const building_clock_temp_connection_t* link = &set->connections[connection];
            if (link->from_node_id >= world->navigation.node_count || link->to_node_id >= world->navigation.node_count || (!reachable[link->from_node_id] && !reachable[link->to_node_id]) ||
                !reachable_with_temporary[link->from_node_id] || !reachable_with_temporary[link->to_node_id] || link->edge_kind != BUILDING_CLOCK_NAV_EDGE_TEMP_CONNECTION || link->capacity == 0U ||
                link->traversal_cost == 0U || link->allow_large_load != (set->kind != BUILDING_CLOCK_TEMP_STRUCTURE_LADDER) || !graph_has_temp_edge(world, attachment, link)) {
                return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_TEMP_STRUCTURE_ROUTE, attachment);
            }
        }
    }
    return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_OK, BUILDING_CLOCK_VALIDATION_NO_ITEM);
}

static building_clock_complete_validation_result_t validate_debris_relationships(const building_clock_world_t* world, const bool reachable[BUILDING_CLOCK_MAX_NAV_NODES]) {
    for (uint8_t bay = 0; bay < world->debris_bay_count; bay++) {
        bool serves_digit = false;
        for (uint8_t digit = 0; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
            if ((world->debris_bays[bay].assigned_digit_mask & (uint8_t)(1U << digit)) == 0U) {
                continue;
            }
            serves_digit = true;
            for (uint8_t other = 0; other < BUILDING_CLOCK_DIGIT_COUNT; other++) {
                if (other != digit && rects_overlap(world->debris_bays[bay].area, world->digit_work_areas[other].work_halo)) {
                    return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_DEBRIS_FOUNDATION_CONFLICT, bay);
                }
            }
        }
        if (!serves_digit || !reachable[world->debris_bays[bay].access_node_id]) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_DEBRIS_SERVICE_ROUTE, bay);
        }
        bool depot_route = false;
        for (uint8_t depot = 0; depot < world->depot_count; depot++) {
            depot_route |= reachable[world->depots[depot].access_node_id];
        }
        if (!depot_route) {
            return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_DEBRIS_SERVICE_ROUTE, bay);
        }
    }
    return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_OK, BUILDING_CLOCK_VALIDATION_NO_ITEM);
}

building_clock_complete_validation_result_t building_clock_validate_complete(const building_clock_world_t* world) {
    if (world == NULL) {
        return complete_result(BUILDING_CLOCK_COMPLETE_VALIDATION_NULL_WORLD, BUILDING_CLOCK_VALIDATION_NO_ITEM);
    }

    building_clock_complete_validation_result_t check = validate_graph(world);
    if (check.reason != BUILDING_CLOCK_COMPLETE_VALIDATION_OK) {
        return check;
    }
    check = validate_semantic_node_references(world);
    if (check.reason != BUILDING_CLOCK_COMPLETE_VALIDATION_OK) {
        return check;
    }

    bool reachable[BUILDING_CLOCK_MAX_NAV_NODES];
    check = validate_main_component(world, reachable);
    if (check.reason != BUILDING_CLOCK_COMPLETE_VALIDATION_OK) {
        return check;
    }
    bool reachable_with_temporary[BUILDING_CLOCK_MAX_NAV_NODES];
    mark_reachable(world, world->worker_spawns[0].nav_node_id, true, reachable_with_temporary);

    check = validate_digit_access(world, reachable, reachable_with_temporary);
    if (check.reason != BUILDING_CLOCK_COMPLETE_VALIDATION_OK) {
        return check;
    }
    check = validate_waiting_positions(world);
    if (check.reason != BUILDING_CLOCK_COMPLETE_VALIDATION_OK) {
        return check;
    }
    check = validate_temporary_routes(world, reachable, reachable_with_temporary);
    if (check.reason != BUILDING_CLOCK_COMPLETE_VALIDATION_OK) {
        return check;
    }
    check = validate_hazard_retreat(world, reachable, reachable_with_temporary);
    if (check.reason != BUILDING_CLOCK_COMPLETE_VALIDATION_OK) {
        return check;
    }
    check = validate_material_delivery(world, reachable);
    if (check.reason != BUILDING_CLOCK_COMPLETE_VALIDATION_OK) {
        return check;
    }
    return validate_debris_relationships(world, reachable);
}

const char* building_clock_complete_validation_reason_name(building_clock_complete_validation_reason_t reason) {
    switch (reason) {
    case BUILDING_CLOCK_COMPLETE_VALIDATION_OK:
        return "ok";
    case BUILDING_CLOCK_COMPLETE_VALIDATION_NULL_WORLD:
        return "null world";
    case BUILDING_CLOCK_COMPLETE_VALIDATION_GRAPH_CAPACITY:
        return "navigation graph exceeds capacity";
    case BUILDING_CLOCK_COMPLETE_VALIDATION_GRAPH_EMPTY:
        return "navigation graph is empty";
    case BUILDING_CLOCK_COMPLETE_VALIDATION_NODE_REFERENCE:
        return "navigation node reference invalid";
    case BUILDING_CLOCK_COMPLETE_VALIDATION_NODE_SOURCE:
        return "navigation node source invalid";
    case BUILDING_CLOCK_COMPLETE_VALIDATION_EDGE_REFERENCE:
        return "navigation edge reference invalid";
    case BUILDING_CLOCK_COMPLETE_VALIDATION_EDGE_SOURCE:
        return "navigation edge source invalid";
    case BUILDING_CLOCK_COMPLETE_VALIDATION_EDGE_GEOMETRY:
        return "navigation edge does not match geometry";
    case BUILDING_CLOCK_COMPLETE_VALIDATION_SPAWN_UNREACHABLE:
        return "worker spawn unreachable";
    case BUILDING_CLOCK_COMPLETE_VALIDATION_DEPOT_UNREACHABLE:
        return "material depot unreachable";
    case BUILDING_CLOCK_COMPLETE_VALIDATION_STAGING_UNREACHABLE:
        return "staging area unreachable";
    case BUILDING_CLOCK_COMPLETE_VALIDATION_DEBRIS_UNREACHABLE:
        return "debris bay unreachable";
    case BUILDING_CLOCK_COMPLETE_VALIDATION_MACHINERY_UNREACHABLE:
        return "machinery control unreachable";
    case BUILDING_CLOCK_COMPLETE_VALIDATION_DIGIT_ACCESS_UNREACHABLE:
        return "digit work access unreachable";
    case BUILDING_CLOCK_COMPLETE_VALIDATION_DEBRIS_SERVICE_ROUTE:
        return "debris service route invalid";
    case BUILDING_CLOCK_COMPLETE_VALIDATION_CART_STOP_UNREACHABLE:
        return "cart stop unreachable";
    case BUILDING_CLOCK_COMPLETE_VALIDATION_WAITING_POSITION:
        return "ladder waiting position invalid";
    case BUILDING_CLOCK_COMPLETE_VALIDATION_HAZARD_RETREAT:
        return "hazard retreat route invalid";
    case BUILDING_CLOCK_COMPLETE_VALIDATION_MATERIAL_DELIVERY:
        return "digit material delivery unavailable";
    case BUILDING_CLOCK_COMPLETE_VALIDATION_UPPER_CONSTRUCTION_ACCESS:
        return "upper digit construction access unavailable";
    case BUILDING_CLOCK_COMPLETE_VALIDATION_CRANE_RANGE:
        return "crane work outside operating range";
    case BUILDING_CLOCK_COMPLETE_VALIDATION_TEMP_STRUCTURE_ROUTE:
        return "temporary structure route invalid";
    case BUILDING_CLOCK_COMPLETE_VALIDATION_DEBRIS_FOUNDATION_CONFLICT:
        return "debris conflicts with another digit foundation";
    default:
        return "unknown complete validation result";
    }
}
