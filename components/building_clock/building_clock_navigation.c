#include "building_clock_navigation.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    building_clock_world_t* world;
    building_clock_nav_derivation_result_t result;
} navigation_builder_t;

#define LADDER_WAIT_DISTANCE 8

static building_clock_nav_derivation_result_t derivation_result(building_clock_nav_derivation_reason_t reason, building_clock_nav_source_kind_t source_kind, uint16_t item_index) {
    return (building_clock_nav_derivation_result_t){
        .reason = reason,
        .source_kind = source_kind,
        .item_index = item_index,
    };
}

static bool point_in_rect(building_clock_point_t point, building_clock_rect_t rect) {
    return point.x >= rect.x && point.y >= rect.y && point.x < (int32_t)rect.x + rect.width && point.y < (int32_t)rect.y + rect.height;
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

static bool point_on_platform(const building_clock_platform_t* platform, building_clock_point_t point) {
    return point.y == platform->surface_y && point.x >= platform->x_start && point.x <= platform->x_end;
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

static void reset_derived_state(building_clock_world_t* world) {
    memset(&world->navigation, 0, sizeof(world->navigation));

    for (uint8_t route = 0; route < world->cart_route_count; route++) {
        for (uint8_t stop = 0; stop < world->cart_routes[route].stop_count; stop++) {
            world->cart_routes[route].stops[stop].nav_node_id = BUILDING_CLOCK_INVALID_ID;
        }
    }
    for (uint8_t lift = 0; lift < world->lift_count; lift++) {
        world->lifts[lift].operator_node_id = BUILDING_CLOCK_INVALID_ID;
        for (uint8_t stop = 0; stop < world->lifts[lift].stop_count; stop++) {
            world->lifts[lift].stops[stop].nav_node_id = BUILDING_CLOCK_INVALID_ID;
        }
    }
    for (uint8_t crane = 0; crane < world->crane_count; crane++) {
        world->cranes[crane].operator_node_id = BUILDING_CLOCK_INVALID_ID;
    }
    for (uint8_t depot = 0; depot < world->depot_count; depot++) {
        world->depots[depot].access_node_id = BUILDING_CLOCK_INVALID_ID;
    }
    for (uint8_t bay = 0; bay < world->debris_bay_count; bay++) {
        world->debris_bays[bay].access_node_id = BUILDING_CLOCK_INVALID_ID;
    }
    for (uint8_t staging = 0; staging < world->staging_area_count; staging++) {
        world->staging_areas[staging].access_node_id = BUILDING_CLOCK_INVALID_ID;
    }
    for (uint8_t spawn = 0; spawn < world->worker_spawn_count; spawn++) {
        world->worker_spawns[spawn].nav_node_id = BUILDING_CLOCK_INVALID_ID;
    }
    for (uint8_t digit = 0; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        building_clock_digit_work_area_t* area = &world->digit_work_areas[digit];
        for (uint8_t access = 0; access < area->access_count; access++) {
            area->access[access].nav_node_id = BUILDING_CLOCK_INVALID_ID;
        }
        area->safe_retreat_node_count = 0U;
        memset(area->safe_retreat_node_ids, BUILDING_CLOCK_INVALID_ID, sizeof(area->safe_retreat_node_ids));
    }
    for (uint8_t attachment = 0; attachment < world->temp_attachment_set_count; attachment++) {
        world->temp_attachment_sets[attachment].connection_count = 0U;
        memset(world->temp_attachment_sets[attachment].connections, 0, sizeof(world->temp_attachment_sets[attachment].connections));
    }
}

static bool add_node(navigation_builder_t* builder, building_clock_nav_node_kind_t kind, building_clock_point_t position, building_clock_nav_source_kind_t source_kind, uint8_t source_id,
                     uint8_t source_point_id, uint8_t* node_id) {
    building_clock_nav_graph_t* graph = &builder->world->navigation;
    if (graph->node_count >= BUILDING_CLOCK_MAX_NAV_NODES) {
        builder->result = derivation_result(BUILDING_CLOCK_NAV_DERIVATION_NODE_CAPACITY, source_kind, source_id);
        return false;
    }

    const uint8_t id = graph->node_count++;
    graph->nodes[id] = (building_clock_nav_node_t){
        .kind = kind,
        .position = position,
        .source_kind = source_kind,
        .source_id = source_id,
        .source_point_id = source_point_id,
    };
    if (node_id != NULL) {
        *node_id = id;
    }
    return true;
}

static bool add_edge(navigation_builder_t* builder, uint8_t from_node_id, uint8_t to_node_id, building_clock_nav_edge_kind_t kind, building_clock_nav_source_kind_t source_kind, uint8_t source_id,
                     uint16_t traversal_cost, uint8_t capacity, uint8_t flags, uint8_t temp_attachment_set_id) {
    building_clock_nav_graph_t* graph = &builder->world->navigation;
    if (graph->edge_count >= BUILDING_CLOCK_MAX_NAV_EDGES) {
        builder->result = derivation_result(BUILDING_CLOCK_NAV_DERIVATION_EDGE_CAPACITY, source_kind, source_id);
        return false;
    }

    graph->edges[graph->edge_count++] = (building_clock_nav_edge_t){
        .from_node_id = from_node_id,
        .to_node_id = to_node_id,
        .kind = kind,
        .source_kind = source_kind,
        .source_id = source_id,
        .traversal_cost = traversal_cost,
        .capacity = capacity,
        .flags = flags,
        .temp_attachment_set_id = temp_attachment_set_id,
    };
    return true;
}

static bool add_platform_nodes(navigation_builder_t* builder) {
    for (uint8_t platform_id = 0; platform_id < builder->world->platform_count; platform_id++) {
        const building_clock_platform_t* platform = &builder->world->platforms[platform_id];
        if (!add_node(builder, BUILDING_CLOCK_NAV_NODE_PLATFORM_ENDPOINT, (building_clock_point_t){.x = platform->x_start, .y = platform->surface_y}, BUILDING_CLOCK_NAV_SOURCE_PLATFORM, platform_id,
                      0U, NULL) ||
            !add_node(builder, BUILDING_CLOCK_NAV_NODE_PLATFORM_ENDPOINT, (building_clock_point_t){.x = platform->x_end, .y = platform->surface_y}, BUILDING_CLOCK_NAV_SOURCE_PLATFORM, platform_id, 1U,
                      NULL)) {
            return false;
        }
    }
    return true;
}

static bool add_ladder_nodes(navigation_builder_t* builder) {
    for (uint8_t ladder_id = 0; ladder_id < builder->world->ladder_count; ladder_id++) {
        const building_clock_ladder_t* ladder = &builder->world->ladders[ladder_id];
        const building_clock_platform_t* top_platform = &builder->world->platforms[ladder->top_platform_id];
        const building_clock_platform_t* bottom_platform = &builder->world->platforms[ladder->bottom_platform_id];
        if (!add_node(builder, BUILDING_CLOCK_NAV_NODE_LADDER_TOP, (building_clock_point_t){.x = ladder->x, .y = ladder->top_y}, BUILDING_CLOCK_NAV_SOURCE_LADDER, ladder_id, 0U, NULL) ||
            !add_node(builder, BUILDING_CLOCK_NAV_NODE_LADDER_BOTTOM, (building_clock_point_t){.x = ladder->x, .y = ladder->bottom_y}, BUILDING_CLOCK_NAV_SOURCE_LADDER, ladder_id, 1U, NULL) ||
            !add_node(builder, BUILDING_CLOCK_NAV_NODE_SAFE_WAITING, ladder_waiting_position(top_platform, ladder->x), BUILDING_CLOCK_NAV_SOURCE_LADDER, ladder_id, 2U, NULL) ||
            !add_node(builder, BUILDING_CLOCK_NAV_NODE_SAFE_WAITING, ladder_waiting_position(bottom_platform, ladder->x), BUILDING_CLOCK_NAV_SOURCE_LADDER, ladder_id, 3U, NULL)) {
            return false;
        }
    }
    return true;
}

static bool add_route_nodes(navigation_builder_t* builder) {
    building_clock_world_t* world = builder->world;
    for (uint8_t route_id = 0; route_id < world->cart_route_count; route_id++) {
        for (uint8_t stop = 0; stop < world->cart_routes[route_id].stop_count; stop++) {
            if (!add_node(builder, BUILDING_CLOCK_NAV_NODE_CART_STOP, world->cart_routes[route_id].stops[stop].position, BUILDING_CLOCK_NAV_SOURCE_CART_ROUTE, route_id, stop,
                          &world->cart_routes[route_id].stops[stop].nav_node_id)) {
                return false;
            }
        }
    }
    for (uint8_t lift_id = 0; lift_id < world->lift_count; lift_id++) {
        for (uint8_t stop = 0; stop < world->lifts[lift_id].stop_count; stop++) {
            if (!add_node(builder, BUILDING_CLOCK_NAV_NODE_LIFT_STOP, world->lifts[lift_id].stops[stop].position, BUILDING_CLOCK_NAV_SOURCE_LIFT, lift_id, stop,
                          &world->lifts[lift_id].stops[stop].nav_node_id)) {
                return false;
            }
        }
    }
    return true;
}

static bool add_digit_nodes(navigation_builder_t* builder) {
    for (uint8_t digit = 0; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        building_clock_digit_work_area_t* area = &builder->world->digit_work_areas[digit];
        for (uint8_t access = 0; access < area->access_count; access++) {
            if (!add_node(builder, BUILDING_CLOCK_NAV_NODE_DIGIT_WORK, area->access[access].position, BUILDING_CLOCK_NAV_SOURCE_DIGIT_ACCESS, digit, access, &area->access[access].nav_node_id)) {
                return false;
            }
        }
    }
    return true;
}

static bool nearest_platform_position(const building_clock_world_t* world, building_clock_rect_t area, building_clock_point_t* position) {
    const int16_t center_x = (int16_t)(area.x + (int16_t)(area.width / 2U));
    const int16_t center_y = (int16_t)(area.y + (int16_t)(area.height / 2U));
    const int16_t area_end_x = (int16_t)(area.x + (int16_t)area.width - 1);
    uint8_t selected = BUILDING_CLOCK_INVALID_ID;
    int32_t selected_distance = INT32_MAX;

    for (uint8_t platform_id = 0; platform_id < world->platform_count; platform_id++) {
        const building_clock_platform_t* platform = &world->platforms[platform_id];
        if (area.x > platform->x_end || area_end_x < platform->x_start) {
            continue;
        }
        int32_t distance = (int32_t)platform->surface_y - center_y;
        if (distance < 0) {
            distance = -distance;
        }
        if (distance < selected_distance) {
            selected = platform_id;
            selected_distance = distance;
        }
    }

    if (selected == BUILDING_CLOCK_INVALID_ID) {
        return false;
    }
    int16_t access_x = center_x;
    if (access_x < world->platforms[selected].x_start) {
        access_x = world->platforms[selected].x_start;
    } else if (access_x > world->platforms[selected].x_end) {
        access_x = world->platforms[selected].x_end;
    }
    *position = (building_clock_point_t){.x = access_x, .y = world->platforms[selected].surface_y};
    return true;
}

static bool add_location_nodes(navigation_builder_t* builder) {
    building_clock_world_t* world = builder->world;
    building_clock_point_t position;

    for (uint8_t depot = 0; depot < world->depot_count; depot++) {
        if (!nearest_platform_position(world, world->depots[depot].area, &position)) {
            builder->result = derivation_result(BUILDING_CLOCK_NAV_DERIVATION_NO_WALKABLE_SURFACE, BUILDING_CLOCK_NAV_SOURCE_DEPOT, depot);
            return false;
        }
        if (!add_node(builder, BUILDING_CLOCK_NAV_NODE_DEPOT, position, BUILDING_CLOCK_NAV_SOURCE_DEPOT, depot, 0U, &world->depots[depot].access_node_id)) {
            return false;
        }
    }
    for (uint8_t bay = 0; bay < world->debris_bay_count; bay++) {
        if (!nearest_platform_position(world, world->debris_bays[bay].area, &position)) {
            builder->result = derivation_result(BUILDING_CLOCK_NAV_DERIVATION_NO_WALKABLE_SURFACE, BUILDING_CLOCK_NAV_SOURCE_DEBRIS_BAY, bay);
            return false;
        }
        if (!add_node(builder, BUILDING_CLOCK_NAV_NODE_DEBRIS, position, BUILDING_CLOCK_NAV_SOURCE_DEBRIS_BAY, bay, 0U, &world->debris_bays[bay].access_node_id)) {
            return false;
        }
    }
    for (uint8_t staging = 0; staging < world->staging_area_count; staging++) {
        if (!nearest_platform_position(world, world->staging_areas[staging].area, &position)) {
            builder->result = derivation_result(BUILDING_CLOCK_NAV_DERIVATION_NO_WALKABLE_SURFACE, BUILDING_CLOCK_NAV_SOURCE_STAGING_AREA, staging);
            return false;
        }
        if (!add_node(builder, BUILDING_CLOCK_NAV_NODE_STAGING, position, BUILDING_CLOCK_NAV_SOURCE_STAGING_AREA, staging, 0U, &world->staging_areas[staging].access_node_id)) {
            return false;
        }
    }
    return true;
}

static bool add_machinery_nodes(navigation_builder_t* builder) {
    building_clock_world_t* world = builder->world;
    for (uint8_t crane = 0; crane < world->crane_count; crane++) {
        if (!add_node(builder, BUILDING_CLOCK_NAV_NODE_MACHINERY_CONTROL, world->cranes[crane].base_position, BUILDING_CLOCK_NAV_SOURCE_MACHINERY, crane, 0U, &world->cranes[crane].operator_node_id)) {
            return false;
        }
    }
    for (uint8_t lift = 0; lift < world->lift_count; lift++) {
        const building_clock_lift_layout_t* layout = &world->lifts[lift];
        uint8_t bottom_stop = 0U;
        for (uint8_t stop = 1U; stop < layout->stop_count; stop++) {
            if (layout->stops[stop].position.y > layout->stops[bottom_stop].position.y) {
                bottom_stop = stop;
            }
        }
        if (!add_node(builder, BUILDING_CLOCK_NAV_NODE_MACHINERY_CONTROL, layout->stops[bottom_stop].position, BUILDING_CLOCK_NAV_SOURCE_MACHINERY, lift, 1U, &world->lifts[lift].operator_node_id)) {
            return false;
        }
    }
    return true;
}

static bool add_spawn_nodes(navigation_builder_t* builder) {
    for (uint8_t spawn = 0; spawn < builder->world->worker_spawn_count; spawn++) {
        if (!add_node(builder, BUILDING_CLOCK_NAV_NODE_SPAWN, builder->world->worker_spawns[spawn].position, BUILDING_CLOCK_NAV_SOURCE_SPAWN, spawn, 0U,
                      &builder->world->worker_spawns[spawn].nav_node_id)) {
            return false;
        }
    }
    return true;
}

static bool add_attachment_nodes(navigation_builder_t* builder) {
    for (uint8_t attachment = 0; attachment < builder->world->temp_attachment_set_count; attachment++) {
        const building_clock_temp_attachment_set_t* set = &builder->world->temp_attachment_sets[attachment];
        for (uint8_t anchor = 0; anchor < set->anchor_count; anchor++) {
            if (!add_node(builder, BUILDING_CLOCK_NAV_NODE_TEMP_ATTACHMENT, set->anchors[anchor], BUILDING_CLOCK_NAV_SOURCE_TEMP_ATTACHMENT, attachment, anchor, NULL)) {
                return false;
            }
        }
    }
    return true;
}

static bool add_all_nodes(navigation_builder_t* builder) {
    return add_platform_nodes(builder) && add_ladder_nodes(builder) && add_route_nodes(builder) && add_digit_nodes(builder) && add_location_nodes(builder) && add_machinery_nodes(builder) &&
           add_spawn_nodes(builder) && add_attachment_nodes(builder);
}

static bool add_walk_edges(navigation_builder_t* builder) {
    building_clock_world_t* world = builder->world;
    for (uint8_t platform_id = 0; platform_id < world->platform_count; platform_id++) {
        const building_clock_platform_t* platform = &world->platforms[platform_id];
        uint8_t node_ids[BUILDING_CLOCK_MAX_NAV_NODES];
        uint8_t node_count = 0U;
        for (uint8_t node_id = 0; node_id < world->navigation.node_count; node_id++) {
            if (point_on_platform(platform, world->navigation.nodes[node_id].position)) {
                node_ids[node_count++] = node_id;
            }
        }

        for (uint8_t index = 1U; index < node_count; index++) {
            const uint8_t value = node_ids[index];
            uint8_t position = index;
            while (position > 0U) {
                const building_clock_point_t previous_point = world->navigation.nodes[node_ids[position - 1U]].position;
                const building_clock_point_t value_point = world->navigation.nodes[value].position;
                if (previous_point.x < value_point.x || (previous_point.x == value_point.x && node_ids[position - 1U] < value)) {
                    break;
                }
                node_ids[position] = node_ids[position - 1U];
                position--;
            }
            node_ids[position] = value;
        }

        const uint16_t platform_length = (uint16_t)(platform->x_end - platform->x_start + 1);
        const uint8_t capacity = platform_length < 64U ? 1U : 2U;
        uint8_t flags = BUILDING_CLOCK_NAV_EDGE_BIDIRECTIONAL;
        if (platform_length >= 64U) {
            flags |= BUILDING_CLOCK_NAV_EDGE_ALLOW_LARGE_LOAD;
        }
        for (uint8_t index = 1U; index < node_count; index++) {
            const uint8_t from = node_ids[index - 1U];
            const uint8_t to = node_ids[index];
            if (!add_edge(builder, from, to, BUILDING_CLOCK_NAV_EDGE_WALK, BUILDING_CLOCK_NAV_SOURCE_PLATFORM, platform_id,
                          point_distance(world->navigation.nodes[from].position, world->navigation.nodes[to].position), capacity, flags, BUILDING_CLOCK_INVALID_ID)) {
                return false;
            }
        }
    }
    return true;
}

static uint8_t find_source_node(const building_clock_nav_graph_t* graph, building_clock_nav_source_kind_t source_kind, uint8_t source_id, uint8_t source_point_id) {
    for (uint8_t node_id = 0; node_id < graph->node_count; node_id++) {
        const building_clock_nav_node_t* node = &graph->nodes[node_id];
        if (node->source_kind == source_kind && node->source_id == source_id && node->source_point_id == source_point_id) {
            return node_id;
        }
    }
    return BUILDING_CLOCK_INVALID_ID;
}

static bool add_ladder_edges(navigation_builder_t* builder) {
    for (uint8_t ladder = 0; ladder < builder->world->ladder_count; ladder++) {
        const uint8_t top = find_source_node(&builder->world->navigation, BUILDING_CLOCK_NAV_SOURCE_LADDER, ladder, 0U);
        const uint8_t bottom = find_source_node(&builder->world->navigation, BUILDING_CLOCK_NAV_SOURCE_LADDER, ladder, 1U);
        if (top == BUILDING_CLOCK_INVALID_ID || bottom == BUILDING_CLOCK_INVALID_ID) {
            builder->result = derivation_result(BUILDING_CLOCK_NAV_DERIVATION_MISSING_SOURCE_NODE, BUILDING_CLOCK_NAV_SOURCE_LADDER, ladder);
            return false;
        }
        if (!add_edge(builder, top, bottom, BUILDING_CLOCK_NAV_EDGE_CLIMB, BUILDING_CLOCK_NAV_SOURCE_LADDER, ladder,
                      point_distance(builder->world->navigation.nodes[top].position, builder->world->navigation.nodes[bottom].position), 1U, BUILDING_CLOCK_NAV_EDGE_BIDIRECTIONAL,
                      BUILDING_CLOCK_INVALID_ID)) {
            return false;
        }
    }
    return true;
}

static bool add_cart_edges(navigation_builder_t* builder) {
    for (uint8_t route = 0; route < builder->world->cart_route_count; route++) {
        const building_clock_cart_route_t* cart_route = &builder->world->cart_routes[route];
        for (uint8_t stop = 1U; stop < cart_route->stop_count; stop++) {
            const uint8_t from = cart_route->stops[stop - 1U].nav_node_id;
            const uint8_t to = cart_route->stops[stop].nav_node_id;
            if (!add_edge(builder, from, to, BUILDING_CLOCK_NAV_EDGE_RIDE_CART, BUILDING_CLOCK_NAV_SOURCE_CART_ROUTE, route,
                          point_distance(builder->world->navigation.nodes[from].position, builder->world->navigation.nodes[to].position), 2U,
                          BUILDING_CLOCK_NAV_EDGE_BIDIRECTIONAL | BUILDING_CLOCK_NAV_EDGE_ALLOW_LARGE_LOAD, BUILDING_CLOCK_INVALID_ID)) {
                return false;
            }
        }
    }
    return true;
}

static bool add_lift_edges(navigation_builder_t* builder) {
    for (uint8_t lift = 0; lift < builder->world->lift_count; lift++) {
        const building_clock_lift_layout_t* layout = &builder->world->lifts[lift];
        for (uint8_t stop = 1U; stop < layout->stop_count; stop++) {
            const uint8_t from = layout->stops[stop - 1U].nav_node_id;
            const uint8_t to = layout->stops[stop].nav_node_id;
            if (!add_edge(builder, from, to, BUILDING_CLOCK_NAV_EDGE_RIDE_LIFT, BUILDING_CLOCK_NAV_SOURCE_LIFT, lift,
                          point_distance(builder->world->navigation.nodes[from].position, builder->world->navigation.nodes[to].position), 3U,
                          BUILDING_CLOCK_NAV_EDGE_BIDIRECTIONAL | BUILDING_CLOCK_NAV_EDGE_ALLOW_LARGE_LOAD, BUILDING_CLOCK_INVALID_ID)) {
                return false;
            }
        }
    }
    return true;
}

static bool add_temp_edges(navigation_builder_t* builder) {
    building_clock_world_t* world = builder->world;
    for (uint8_t attachment = 0; attachment < world->temp_attachment_set_count; attachment++) {
        building_clock_temp_attachment_set_t* set = &world->temp_attachment_sets[attachment];
        if (set->anchor_count < 2U || set->connection_count >= BUILDING_CLOCK_MAX_ATTACHMENT_CONNECTIONS) {
            builder->result = derivation_result(BUILDING_CLOCK_NAV_DERIVATION_TEMP_CONNECTION_CAPACITY, BUILDING_CLOCK_NAV_SOURCE_TEMP_ATTACHMENT, attachment);
            return false;
        }
        const uint8_t from = find_source_node(&world->navigation, BUILDING_CLOCK_NAV_SOURCE_TEMP_ATTACHMENT, attachment, 0U);
        const uint8_t to = find_source_node(&world->navigation, BUILDING_CLOCK_NAV_SOURCE_TEMP_ATTACHMENT, attachment, (uint8_t)(set->anchor_count - 1U));
        if (from == BUILDING_CLOCK_INVALID_ID || to == BUILDING_CLOCK_INVALID_ID) {
            builder->result = derivation_result(BUILDING_CLOCK_NAV_DERIVATION_MISSING_SOURCE_NODE, BUILDING_CLOCK_NAV_SOURCE_TEMP_ATTACHMENT, attachment);
            return false;
        }

        const bool allow_large_load = set->kind != BUILDING_CLOCK_TEMP_STRUCTURE_LADDER;
        set->connections[0] = (building_clock_temp_connection_t){
            .from_node_id = from,
            .to_node_id = to,
            .edge_kind = BUILDING_CLOCK_NAV_EDGE_TEMP_CONNECTION,
            .traversal_cost = point_distance(world->navigation.nodes[from].position, world->navigation.nodes[to].position),
            .capacity = 1U,
            .allow_large_load = allow_large_load,
        };
        set->connection_count = 1U;

        uint8_t flags = BUILDING_CLOCK_NAV_EDGE_BIDIRECTIONAL | BUILDING_CLOCK_NAV_EDGE_REQUIRES_TEMP_STRUCTURE;
        if (allow_large_load) {
            flags |= BUILDING_CLOCK_NAV_EDGE_ALLOW_LARGE_LOAD;
        }
        if (!add_edge(builder, from, to, BUILDING_CLOCK_NAV_EDGE_TEMP_CONNECTION, BUILDING_CLOCK_NAV_SOURCE_TEMP_ATTACHMENT, attachment, set->connections[0].traversal_cost, 1U, flags, attachment)) {
            return false;
        }
    }
    return true;
}

static bool add_all_edges(navigation_builder_t* builder) {
    return add_walk_edges(builder) && add_ladder_edges(builder) && add_cart_edges(builder) && add_lift_edges(builder) && add_temp_edges(builder);
}

static void set_safe_retreat_nodes(building_clock_world_t* world) {
    for (uint8_t digit = 0; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        building_clock_digit_work_area_t* area = &world->digit_work_areas[digit];
        for (uint8_t access = 0; access < area->access_count && area->safe_retreat_node_count < BUILDING_CLOCK_MAX_SAFE_RETREAT_NODES; access++) {
            const uint8_t node_id = area->access[access].nav_node_id;
            if (node_id < world->navigation.node_count && !point_in_rect(world->navigation.nodes[node_id].position, area->hazard_area)) {
                area->safe_retreat_node_ids[area->safe_retreat_node_count++] = node_id;
            }
        }
    }
}

building_clock_nav_derivation_result_t building_clock_derive_navigation(building_clock_world_t* world) {
    if (world == NULL) {
        return derivation_result(BUILDING_CLOCK_NAV_DERIVATION_NULL_WORLD, BUILDING_CLOCK_NAV_SOURCE_PLATFORM, BUILDING_CLOCK_NAV_DERIVATION_NO_ITEM);
    }

    reset_derived_state(world);
    navigation_builder_t builder = {
        .world = world,
        .result = derivation_result(BUILDING_CLOCK_NAV_DERIVATION_OK, BUILDING_CLOCK_NAV_SOURCE_PLATFORM, BUILDING_CLOCK_NAV_DERIVATION_NO_ITEM),
    };

    if (!add_all_nodes(&builder) || !add_all_edges(&builder)) {
        const building_clock_nav_derivation_result_t failure = builder.result;
        reset_derived_state(world);
        return failure;
    }
    set_safe_retreat_nodes(world);
    return builder.result;
}

const char* building_clock_nav_derivation_reason_name(building_clock_nav_derivation_reason_t reason) {
    switch (reason) {
    case BUILDING_CLOCK_NAV_DERIVATION_OK:
        return "ok";
    case BUILDING_CLOCK_NAV_DERIVATION_NULL_WORLD:
        return "null world";
    case BUILDING_CLOCK_NAV_DERIVATION_NODE_CAPACITY:
        return "navigation node capacity exceeded";
    case BUILDING_CLOCK_NAV_DERIVATION_EDGE_CAPACITY:
        return "navigation edge capacity exceeded";
    case BUILDING_CLOCK_NAV_DERIVATION_NO_WALKABLE_SURFACE:
        return "semantic location has no walkable surface";
    case BUILDING_CLOCK_NAV_DERIVATION_MISSING_SOURCE_NODE:
        return "navigation source node missing";
    case BUILDING_CLOCK_NAV_DERIVATION_TEMP_CONNECTION_CAPACITY:
        return "temporary connection capacity exceeded";
    default:
        return "unknown navigation derivation result";
    }
}
