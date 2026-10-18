#include "building_clock_scene.h"

#include <stddef.h>
#include <string.h>

static bool world_counts_are_valid(const building_clock_world_t* world) {
    return world->platform_count <= BUILDING_CLOCK_MAX_PLATFORM_SPANS && world->ladder_count <= BUILDING_CLOCK_MAX_LADDERS && world->major_support_count <= BUILDING_CLOCK_MAX_MAJOR_SUPPORTS &&
           world->crane_count <= BUILDING_CLOCK_MAX_CRANES && world->lift_count <= BUILDING_CLOCK_MAX_LIFTS && world->cart_route_count <= BUILDING_CLOCK_MAX_CART_ROUTES &&
           world->depot_count <= BUILDING_CLOCK_MAX_MATERIAL_DEPOTS && world->debris_bay_count <= BUILDING_CLOCK_MAX_DEBRIS_BAYS && world->staging_area_count <= BUILDING_CLOCK_MAX_STAGING_AREAS &&
           world->worker_spawn_count <= BUILDING_CLOCK_MAX_WORKER_SPAWNS && world->temp_attachment_set_count <= BUILDING_CLOCK_MAX_TEMP_ATTACHMENT_SETS &&
           world->navigation.node_count <= BUILDING_CLOCK_MAX_NAV_NODES && world->navigation.edge_count <= BUILDING_CLOCK_MAX_NAV_EDGES;
}

static bool state_counts_are_valid(const building_clock_simulation_state_t* state) {
    if (state->worker_count > BUILDING_CLOCK_MAX_WORKERS || state->used_job_slot_count > BUILDING_CLOCK_MAX_JOBS || state->used_temp_structure_slot_count > BUILDING_CLOCK_MAX_TEMP_STRUCTURES ||
        state->used_load_slot_count > BUILDING_CLOCK_MAX_LOADS || state->used_debris_slot_count > BUILDING_CLOCK_MAX_DEBRIS_PIECES ||
        state->falling_section_count > BUILDING_CLOCK_MAX_FALLING_SECTIONS || state->cart_count > BUILDING_CLOCK_MAX_CARTS || state->crane_count > BUILDING_CLOCK_MAX_CRANES ||
        state->lift_count > BUILDING_CLOCK_MAX_LIFTS || state->hazard_count > BUILDING_CLOCK_MAX_HAZARDS) {
        return false;
    }
    for (uint8_t digit = 0U; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        if (state->digits[digit].outgoing.section_count > BUILDING_CLOCK_MAX_DIGIT_SECTIONS || state->digits[digit].incoming.section_count > BUILDING_CLOCK_MAX_DIGIT_SECTIONS) {
            return false;
        }
    }
    return true;
}

static bool worker_can_use_presentation_spacing(const building_clock_world_t* world, const building_clock_worker_t* worker) {
    if (worker->state == BUILDING_CLOCK_WORKER_CLIMB || worker->rope.phase != BUILDING_CLOCK_ROPE_NONE) {
        return false;
    }
    if (worker->reserved_edge_id < world->navigation.edge_count) {
        const building_clock_nav_edge_t* edge = &world->navigation.edges[worker->reserved_edge_id];
        return edge->kind == BUILDING_CLOCK_NAV_EDGE_WALK && edge->capacity > 1U;
    }
    return worker->current_node_id < world->navigation.node_count && building_clock_navigation_node_capacity(world, worker->current_node_id) > 1U;
}

static void set_worker_presentation_positions(building_clock_scene_snapshot_t* snapshot) {
    const building_clock_world_t* world = snapshot->world;
    for (uint8_t worker_id = 0U; worker_id < snapshot->state.worker_count; worker_id++) {
        const building_clock_worker_t* worker = &snapshot->state.workers[worker_id];
        building_clock_point_t* position = &snapshot->worker_presentation_positions[worker_id];
        *position = worker->position;
        if (!worker_can_use_presentation_spacing(world, worker)) {
            continue;
        }

        uint8_t group_count = 0U;
        uint8_t rank = 0U;
        for (uint8_t other_id = 0U; other_id < snapshot->state.worker_count; other_id++) {
            const building_clock_worker_t* other = &snapshot->state.workers[other_id];
            if (other->position.x == worker->position.x && other->position.y == worker->position.y && worker_can_use_presentation_spacing(world, other)) {
                if (other_id < worker_id) {
                    rank++;
                }
                group_count++;
            }
        }
        if (group_count > 1U) {
            const int16_t offset = (int16_t)(((int16_t)(2U * rank) - (int16_t)group_count + 1) * 3);
            position->x = (int16_t)(position->x + offset);
            if (position->x < 4) {
                position->x = 4;
            } else if (position->x >= BUILDING_CLOCK_WORLD_WIDTH - 4) {
                position->x = BUILDING_CLOCK_WORLD_WIDTH - 5;
            }
        }
    }
}

building_clock_scene_status_t building_clock_scene_capture(const building_clock_world_t* world, const building_clock_simulation_t* simulation, building_clock_scene_snapshot_t* snapshot) {
    if (world == NULL || simulation == NULL || snapshot == NULL) {
        return BUILDING_CLOCK_SCENE_INVALID_ARGUMENT;
    }
    if (!world_counts_are_valid(world)) {
        memset(snapshot, 0, sizeof(*snapshot));
        return BUILDING_CLOCK_SCENE_INVALID_WORLD;
    }
    if (!state_counts_are_valid(&simulation->state)) {
        memset(snapshot, 0, sizeof(*snapshot));
        return BUILDING_CLOCK_SCENE_INVALID_STATE;
    }

    *snapshot = (building_clock_scene_snapshot_t){
        .world = world,
        .state = simulation->state,
        .navigation = simulation->navigation,
        .metrics = simulation->metrics,
    };
    set_worker_presentation_positions(snapshot);
    return BUILDING_CLOCK_SCENE_OK;
}

const char* building_clock_scene_status_name(building_clock_scene_status_t status) {
    switch (status) {
    case BUILDING_CLOCK_SCENE_OK:
        return "ok";
    case BUILDING_CLOCK_SCENE_INVALID_ARGUMENT:
        return "invalid argument";
    case BUILDING_CLOCK_SCENE_INVALID_WORLD:
        return "invalid world";
    case BUILDING_CLOCK_SCENE_INVALID_STATE:
        return "invalid simulation state";
    default:
        return "unknown";
    }
}
