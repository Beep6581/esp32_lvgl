#include "building_clock_simulation.h"

#include <stddef.h>
#include <string.h>

#include "building_clock_geometry.h"

#define WALK_MS_PER_COST_UNIT 12U
#define CLIMB_MS_PER_COST_UNIT 20U
#define TEMP_MS_PER_COST_UNIT 16U
#define MINIMUM_TRAVERSAL_MS 100U
#define DEFAULT_PATROL_ACTION_MS 200U
#define DEFAULT_INSPECT_ACTION_MS 800U
#define DEFAULT_HAMMER_ACTION_MS 1000U
#define MAX_WORKER_TRANSITIONS_PER_QUANTUM 8U
#define FALLING_VARIATION_MS 200U
#define ROPE_MS_PER_PIXEL 20U
#define ROPE_REACH 12
#define CART_SACK_CAPACITY 4U

static const building_clock_transition_pacing_t s_default_transition_pacing = {
    .preparation_ms = 500U,
    .minimum_evacuation_ms = 250U,
    .evacuation_margin_ms = 500U,
    .evacuation_contention_ms_per_worker = 250U,
    .section_failure_interval_ms = 200U,
    .falling_section_ms = 700U,
    .settling_ms = 500U,
    .changed_digit_stagger_ms = 300U,
    .clearing_minimum_ms = 5000U,
    .foundation_minimum_ms = 5000U,
    .structure_minimum_ms = 10000U,
    .recognizable_minimum_ms = 10000U,
    .finishing_minimum_ms = 4000U,
    .debris_clear_action_ms = 700U,
    .section_place_action_ms = 900U,
    .section_secure_action_ms = 700U,
    .finishing_action_ms = 900U,
    .temporary_install_action_ms = 1200U,
    .staging_action_ms = 4000U,
    .relief_entry_interval_ms = 1500U,
    .complete_hold_ms = 500U,
};

_Static_assert(BUILDING_CLOCK_MAX_NAV_EDGES <= UINT8_MAX, "worker route edge IDs must fit uint8_t");
_Static_assert(BUILDING_CLOCK_MAX_WORKER_ROUTE_NODES == BUILDING_CLOCK_MAX_ROUTE_NODES, "worker and route node capacities must match");
_Static_assert(BUILDING_CLOCK_MAX_WORKER_ROUTE_EDGES == BUILDING_CLOCK_MAX_ROUTE_EDGES, "worker and route edge capacities must match");

static void copy_route_to_worker(building_clock_worker_t* worker, const building_clock_route_t* route);
static uint32_t traversal_duration(const building_clock_world_t* world, const building_clock_nav_edge_t* edge);
static uint8_t section_collapse_priority(building_clock_section_kind_t kind);
static building_clock_point_t section_start_position(const building_clock_digit_work_area_t* area, building_clock_section_kind_t kind);
static bool activate_digit_hazard(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t digit_id);
static void complete_current_job(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id);
static uint8_t section_target_node(const building_clock_world_t* world, uint8_t digit_id, uint8_t numeral, building_clock_section_kind_t kind);
static bool section_dependencies_secured(const building_clock_digit_structure_t* structure, const building_clock_section_state_t* section);
static bool cache_nodes_share_platform(const building_clock_world_t* world, uint8_t first, uint8_t second);
static bool worker_can_do_job(const building_clock_worker_t* worker, const building_clock_job_t* job, uint64_t elapsed_ms);
static uint8_t job_route_destination(const building_clock_job_t* job);
static bool job_is_live(const building_clock_job_t* job);
static void update_carried_load_positions(building_clock_simulation_t* simulation, uint8_t worker_id);

static void park_worker_cart(building_clock_simulation_t* simulation, uint8_t worker_id) {
    for (uint8_t id = 0U; id < simulation->state.cart_count; id++) {
        building_clock_cart_t* cart = &simulation->state.carts[id];
        if (cart->operator_worker_id == worker_id) {
            cart->operator_worker_id = BUILDING_CLOCK_INVALID_ID;
            cart->status = BUILDING_CLOCK_CART_WAITING;
        }
    }
}

static bool simple_job_kind_supported(building_clock_job_kind_t kind) {
    return kind == BUILDING_CLOCK_JOB_PATROL || kind == BUILDING_CLOCK_JOB_INSPECT || kind == BUILDING_CLOCK_JOB_SECURE_SECTION;
}

static bool world_is_usable(const building_clock_world_t* world) {
    if (world == NULL || world->navigation.node_count == 0U || world->navigation.node_count > BUILDING_CLOCK_MAX_NAV_NODES || world->navigation.edge_count > BUILDING_CLOCK_MAX_NAV_EDGES ||
        world->worker_spawn_count == 0U || world->worker_spawn_count > BUILDING_CLOCK_MAX_WORKER_SPAWNS) {
        return false;
    }
    for (uint8_t spawn = 0U; spawn < world->worker_spawn_count; spawn++) {
        if (world->worker_spawns[spawn].nav_node_id >= world->navigation.node_count) {
            return false;
        }
    }
    return true;
}

static bool pacing_is_usable(const building_clock_transition_pacing_t* pacing) {
    return pacing->section_failure_interval_ms > 0U && pacing->falling_section_ms > 0U && pacing->debris_clear_action_ms > 0U && pacing->section_place_action_ms > 0U &&
           pacing->section_secure_action_ms > 0U && pacing->finishing_action_ms > 0U;
}

static void record_site_event(building_clock_simulation_t* simulation, building_clock_site_event_kind_t kind, building_clock_point_t position, uint8_t digit_id) {
    building_clock_site_event_t* event = &simulation->state.recent_site_events[simulation->state.next_site_event];
    *event = (building_clock_site_event_t){.elapsed_ms = simulation->state.elapsed_ms, .position = position, .kind = (uint8_t)kind, .digit_id = digit_id};
    simulation->state.next_site_event = (uint8_t)((simulation->state.next_site_event + 1U) % BUILDING_CLOCK_RECENT_SITE_EVENTS);
}

static bool point_in_rect(building_clock_point_t point, building_clock_rect_t rect) {
    return point.x >= rect.x && point.y >= rect.y && point.x < (int32_t)rect.x + rect.width && point.y < (int32_t)rect.y + rect.height;
}

static bool segment_may_cross_rect(building_clock_point_t from, building_clock_point_t to, building_clock_rect_t rect) {
    const int16_t minimum_x = from.x < to.x ? from.x : to.x;
    const int16_t maximum_x = from.x > to.x ? from.x : to.x;
    const int16_t minimum_y = from.y < to.y ? from.y : to.y;
    const int16_t maximum_y = from.y > to.y ? from.y : to.y;
    return minimum_x < (int32_t)rect.x + rect.width && maximum_x >= rect.x && minimum_y < (int32_t)rect.y + rect.height && maximum_y >= rect.y;
}

static bool point_in_active_hazard(const building_clock_simulation_t* simulation, building_clock_point_t point) {
    for (uint8_t hazard = 0U; hazard < simulation->state.hazard_count; hazard++) {
        if (point_in_rect(point, simulation->state.hazards[hazard].area)) {
            return true;
        }
    }
    return false;
}

static bool route_intersects_active_hazard(const building_clock_simulation_t* simulation, const building_clock_world_t* world, const building_clock_route_t* route) {
    for (uint8_t node = 0U; node < route->node_count; node++) {
        if (point_in_active_hazard(simulation, world->navigation.nodes[route->node_ids[node]].position)) {
            return true;
        }
    }
    for (uint8_t edge = 0U; edge < route->edge_count; edge++) {
        const building_clock_point_t from = world->navigation.nodes[route->node_ids[edge]].position;
        const building_clock_point_t to = world->navigation.nodes[route->node_ids[edge + 1U]].position;
        for (uint8_t hazard = 0U; hazard < simulation->state.hazard_count; hazard++) {
            if (segment_may_cross_rect(from, to, simulation->state.hazards[hazard].area)) {
                return true;
            }
        }
    }
    return false;
}

static void clear_worker_route(building_clock_worker_t* worker) {
    worker->destination_node_id = BUILDING_CLOCK_INVALID_ID;
    worker->route_node_count = 0U;
    worker->route_edge_count = 0U;
    worker->route_node_index = 0U;
    worker->route_purpose = BUILDING_CLOCK_WORKER_ROUTE_NONE;
    worker->evacuation_digit_id = BUILDING_CLOCK_INVALID_ID;
    memset(worker->route_node_ids, BUILDING_CLOCK_INVALID_ID, sizeof(worker->route_node_ids));
    memset(worker->route_edge_ids, BUILDING_CLOCK_INVALID_ID, sizeof(worker->route_edge_ids));
}

static void initialize_worker(building_clock_worker_t* worker, uint8_t worker_id, const building_clock_world_t* world) {
    memset(worker, 0, sizeof(*worker));
    const uint8_t spawn_id = (uint8_t)(worker_id % world->worker_spawn_count);
    const uint8_t node_id = world->worker_spawns[spawn_id].nav_node_id;
    worker->preferred_role = worker_id % 2U == 0U ? BUILDING_CLOCK_WORKER_ROLE_INSPECTOR : BUILDING_CLOCK_WORKER_ROLE_BUILDER;
    worker->capabilities = BUILDING_CLOCK_WORKER_CAN_BUILD | BUILDING_CLOCK_WORKER_CAN_RIG | BUILDING_CLOCK_WORKER_CAN_CLEAR | BUILDING_CLOCK_WORKER_CAN_INSPECT;
    worker->state = BUILDING_CLOCK_WORKER_IDLE;
    worker->position = world->navigation.nodes[node_id].position;
    worker->facing = BUILDING_CLOCK_FACING_RIGHT;
    worker->current_job_id = BUILDING_CLOCK_INVALID_ID;
    worker->carried_load_id = BUILDING_CLOCK_INVALID_ID;
    worker->carried_debris_id = BUILDING_CLOCK_INVALID_ID;
    worker->tended_builder_id = BUILDING_CLOCK_INVALID_ID;
    worker->current_node_id = node_id;
    worker->reserved_edge_id = BUILDING_CLOCK_INVALID_EDGE_ID;
    worker->reserved_destination_node_id = BUILDING_CLOCK_INVALID_ID;
    clear_worker_route(worker);
}

building_clock_simulation_status_t building_clock_simulation_init(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_count) {
    if (simulation == NULL || world == NULL) {
        return BUILDING_CLOCK_SIMULATION_INVALID_ARGUMENT;
    }
    if (!world_is_usable(world)) {
        return BUILDING_CLOCK_SIMULATION_INVALID_WORLD;
    }
    if (worker_count == 0U || worker_count > BUILDING_CLOCK_MAX_WORKERS) {
        return BUILDING_CLOCK_SIMULATION_INVALID_STATE;
    }

    memset(simulation, 0, sizeof(*simulation));
    building_clock_navigation_runtime_init(&simulation->navigation);
    building_clock_random_init(&simulation->worker_random, world->replay.worker_behavior_seed);
    building_clock_random_init(&simulation->transition_random, world->replay.construction_scheduling_seed);
    simulation->pacing = s_default_transition_pacing;
    simulation->state.worker_count = worker_count;
    simulation->crew_limit = worker_count >= 6U ? BUILDING_CLOCK_MAX_WORKERS : worker_count;
    simulation->state.cart_count = world->cart_route_count;
    simulation->state.crane_count = world->crane_count;
    simulation->state.lift_count = world->lift_count;
    for (uint8_t cart = 0U; cart < simulation->state.cart_count; cart++) {
        simulation->state.carts[cart].route_id = cart;
        simulation->state.carts[cart].operator_worker_id = BUILDING_CLOCK_INVALID_ID;
        simulation->state.carts[cart].pickup_bay_id = BUILDING_CLOCK_INVALID_ID;
        simulation->state.carts[cart].target_stop_id = world->cart_routes[cart].stop_count > 0U ? (uint8_t)(world->cart_routes[cart].stop_count - 1U) : 0U;
    }
    for (uint8_t depot = 0U; depot < world->depot_count; depot++) {
        simulation->state.depot_inventory[depot].quantity[BUILDING_CLOCK_MATERIAL_STRUCTURAL_SECTION] = 64U;
    }

    for (uint8_t worker_id = 0U; worker_id < worker_count; worker_id++) {
        building_clock_worker_t* worker = &simulation->state.workers[worker_id];
        initialize_worker(worker, worker_id, world);
        const building_clock_reservation_status_t reservation = building_clock_navigation_reserve_node(world, &simulation->navigation, worker->current_node_id, worker_id);
        if (reservation != BUILDING_CLOCK_RESERVATION_ACQUIRED && reservation != BUILDING_CLOCK_RESERVATION_ALREADY_HELD) {
            memset(simulation, 0, sizeof(*simulation));
            return BUILDING_CLOCK_SIMULATION_INVALID_STATE;
        }
    }
    return BUILDING_CLOCK_SIMULATION_OK;
}

building_clock_simulation_status_t building_clock_simulation_add_job(building_clock_simulation_t* simulation, const building_clock_world_t* world, const building_clock_simple_job_spec_t* spec,
                                                                     uint8_t* job_id) {
    if (simulation == NULL || world == NULL || spec == NULL) {
        return BUILDING_CLOCK_SIMULATION_INVALID_ARGUMENT;
    }
    if (!simple_job_kind_supported(spec->kind)) {
        return BUILDING_CLOCK_SIMULATION_UNSUPPORTED_JOB;
    }
    if (spec->target_node_id >= world->navigation.node_count) {
        return BUILDING_CLOCK_SIMULATION_INVALID_STATE;
    }
    if (point_in_active_hazard(simulation, world->navigation.nodes[spec->target_node_id].position)) {
        return BUILDING_CLOCK_SIMULATION_UNSAFE_TARGET;
    }

    for (uint8_t slot = 0U; slot < BUILDING_CLOCK_MAX_JOBS; slot++) {
        if (simulation->state.jobs[slot].status != BUILDING_CLOCK_JOB_UNUSED) {
            continue;
        }
        simulation->state.jobs[slot] = (building_clock_job_t){
            .kind = spec->kind,
            .status = BUILDING_CLOCK_JOB_PENDING,
            .subject = {.kind = BUILDING_CLOCK_JOB_SUBJECT_NONE},
            .priority = spec->priority,
            .source_node_id = BUILDING_CLOCK_INVALID_ID,
            .target_node_id = spec->target_node_id,
            .source_action_complete = true,
            .required_material = BUILDING_CLOCK_MATERIAL_NONE,
            .required_machinery_kind = BUILDING_CLOCK_MACHINERY_NONE,
            .required_machinery_id = BUILDING_CLOCK_INVALID_ID,
            .required_worker_capabilities = spec->required_worker_capabilities,
            .action_duration_ms = spec->action_duration_ms,
            .earliest_start_elapsed_ms = spec->earliest_start_elapsed_ms,
            .desired_completion_elapsed_ms = spec->desired_completion_elapsed_ms,
            .reserved_worker_id = BUILDING_CLOCK_INVALID_ID,
        };
        simulation->state.used_job_slot_count++;
        if (simulation->state.used_job_slot_count > simulation->metrics.peak_job_slots) {
            simulation->metrics.peak_job_slots = simulation->state.used_job_slot_count;
        }
        if (job_id != NULL) {
            *job_id = slot;
        }
        return BUILDING_CLOCK_SIMULATION_OK;
    }
    return BUILDING_CLOCK_SIMULATION_CAPACITY_EXCEEDED;
}

static void release_transit_reservations(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    if (worker->reserved_edge_id != BUILDING_CLOCK_INVALID_EDGE_ID) {
        (void)building_clock_navigation_release_edge(world, &simulation->navigation, worker->reserved_edge_id, worker_id);
        worker->reserved_edge_id = BUILDING_CLOCK_INVALID_EDGE_ID;
    }
    if (worker->reserved_destination_node_id != BUILDING_CLOCK_INVALID_ID) {
        (void)building_clock_navigation_release_node(world, &simulation->navigation, worker->reserved_destination_node_id, worker_id);
        worker->reserved_destination_node_id = BUILDING_CLOCK_INVALID_ID;
    }
    worker->edge_progress_per_mille = 0U;
    worker->state_elapsed_ms = 0U;
}

static void return_worker_to_current_node(building_clock_worker_t* worker, const building_clock_world_t* world) {
    if (worker->current_node_id < world->navigation.node_count) {
        worker->position = world->navigation.nodes[worker->current_node_id].position;
    }
}

static void clear_worker_job(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    worker->recovery_recalled = false;
    release_transit_reservations(simulation, world, worker_id);
    if (worker->platform_activity_phase != BUILDING_CLOCK_PLATFORM_NONE) {
        park_worker_cart(simulation, worker_id);
        worker->platform_activity_phase = BUILDING_CLOCK_PLATFORM_RETURN;
        worker->current_job_id = BUILDING_CLOCK_INVALID_ID;
        worker->carried_load_id = BUILDING_CLOCK_INVALID_ID;
        worker->state = BUILDING_CLOCK_WORKER_WALK;
        clear_worker_route(worker);
        return;
    }
    if (worker->rope.phase != BUILDING_CLOCK_ROPE_NONE) {
        worker->rope.returning = true;
        worker->rope.phase = BUILDING_CLOCK_ROPE_ASCEND;
        worker->current_job_id = BUILDING_CLOCK_INVALID_ID;
        worker->carried_load_id = BUILDING_CLOCK_INVALID_ID;
        worker->state = BUILDING_CLOCK_WORKER_CLIMB;
        clear_worker_route(worker);
        return;
    }
    return_worker_to_current_node(worker, world);
    (void)building_clock_navigation_reserve_node(world, &simulation->navigation, worker->current_node_id, worker_id);
    worker->current_job_id = BUILDING_CLOCK_INVALID_ID;
    worker->carried_load_id = BUILDING_CLOCK_INVALID_ID;
    worker->state = BUILDING_CLOCK_WORKER_IDLE;
    worker->action_phase = 0U;
    clear_worker_route(worker);
}

static void plan_idle_egress(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    if (worker->rope.phase != BUILDING_CLOCK_ROPE_NONE) {
        return;
    }
    if (building_clock_navigation_node_capacity(world, worker->current_node_id) >= BUILDING_CLOCK_MAX_WORKERS) {
        return;
    }
    /* Keep clearing/working at the current real station when another useful
     * local job is ready. Leaving after every rubble piece dispatched distant
     * roof workers down the same ladder while its local clearer walked away. */
    if (!point_in_active_hazard(simulation, worker->position)) {
        for (uint8_t job_id = 0U; job_id < BUILDING_CLOCK_MAX_JOBS; job_id++) {
            const building_clock_job_t* job = &simulation->state.jobs[job_id];
            if (worker_can_do_job(worker, job, simulation->state.elapsed_ms) && job_route_destination(job) == worker->current_node_id) {
                return;
            }
        }
    }

    building_clock_route_t best_route;
    memset(&best_route, 0, sizeof(best_route));
    bool found_route = false;
    for (uint8_t node_id = 0U; node_id < world->navigation.node_count; node_id++) {
        if (building_clock_navigation_node_capacity(world, node_id) < BUILDING_CLOCK_MAX_WORKERS) {
            continue;
        }
        const building_clock_route_request_t request = {
            .start_node_id = worker->current_node_id,
            .destination_node_id = node_id,
            .allow_temporary_connections = true,
            .carries_large_load = false,
            .allow_machinery_edges = false,
        };
        building_clock_route_t route;
        if (building_clock_find_route(world, &simulation->navigation, &request, &route) != BUILDING_CLOCK_ROUTE_OK) {
            continue;
        }
        if (!found_route || route.total_cost < best_route.total_cost || (route.total_cost == best_route.total_cost && node_id < best_route.node_ids[best_route.node_count - 1U])) {
            best_route = route;
            found_route = true;
        }
    }
    if (found_route) {
        copy_route_to_worker(worker, &best_route);
        worker->route_purpose = BUILDING_CLOCK_WORKER_ROUTE_IDLE_EGRESS;
        worker->state = BUILDING_CLOCK_WORKER_YIELD_OR_TURN;
    }
}

static bool choose_platform_activity_position(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t id) {
    building_clock_worker_t* worker = &simulation->state.workers[id];
    if (building_clock_navigation_node_capacity(world, worker->current_node_id) < BUILDING_CLOCK_MAX_WORKERS) {
        return false;
    }
    bool crowded = false;
    for (uint8_t other = 0U; other < simulation->state.worker_count; other++) {
        const building_clock_point_t p = simulation->state.workers[other].position;
        crowded |= other != id && p.y == worker->position.y && p.x - worker->position.x < 22 && worker->position.x - p.x < 22;
    }
    if (!crowded) {
        return false;
    }
    int32_t best_distance = INT32_MAX;
    building_clock_point_t best = worker->position;
    for (uint8_t platform = 0U; platform < world->platform_count; platform++) {
        const building_clock_platform_t* surface = &world->platforms[platform];
        if (surface->surface_y != worker->position.y || surface->x_end - surface->x_start < 120 || worker->position.x < surface->x_start || worker->position.x > surface->x_end) {
            continue;
        }
        for (int16_t x = (int16_t)(surface->x_start + 14); x <= surface->x_end - 14; x = (int16_t)(x + 13)) {
            int32_t distance = (int32_t)x - worker->position.x;
            distance = distance < 0 ? -distance : distance;
            bool clear = distance >= 24 && distance <= 100;
            for (uint8_t other = 0U; clear && other < simulation->state.worker_count; other++) {
                if (other == id) {
                    continue;
                }
                const building_clock_worker_t* colleague = &simulation->state.workers[other];
                const building_clock_point_t p = colleague->platform_activity_phase != BUILDING_CLOCK_PLATFORM_NONE ? colleague->platform_activity_position : colleague->position;
                clear = p.y != surface->surface_y || p.x - x >= 22 || x - p.x >= 22;
            }
            for (uint8_t ladder = 0U; clear && ladder < world->ladder_count; ladder++) {
                const building_clock_ladder_t* l = &world->ladders[ladder];
                if (l->top_y == surface->surface_y || l->bottom_y == surface->surface_y) {
                    clear = l->x - x >= 13 || x - l->x >= 13;
                }
            }
            for (uint8_t hazard = 0U; clear && hazard < simulation->state.hazard_count; hazard++) {
                const building_clock_rect_t area = simulation->state.hazards[hazard].area;
                clear = !segment_may_cross_rect(worker->position, (building_clock_point_t){x, surface->surface_y}, area);
            }
            if (clear && distance < best_distance) {
                best_distance = distance;
                best = (building_clock_point_t){x, surface->surface_y};
            }
        }
    }
    if (best_distance == INT32_MAX) {
        return false;
    }
    worker->platform_activity_position = best;
    worker->platform_activity_phase = BUILDING_CLOCK_PLATFORM_WALK_OUT;
    worker->platform_motion_remainder_ms = 0U;
    return true;
}

static building_clock_point_t cart_position(const building_clock_world_t* world, uint8_t id, const building_clock_cart_t* cart) {
    const building_clock_cart_route_t* route = &world->cart_routes[id];
    const building_clock_point_t from = route->stops[cart->current_stop_id].position;
    const building_clock_point_t to = route->stops[cart->target_stop_id].position;
    return (building_clock_point_t){(int16_t)(from.x + ((int32_t)to.x - from.x) * cart->route_progress_per_mille / 1000), from.y};
}

static bool same_platform(const building_clock_world_t* world, building_clock_point_t from, building_clock_point_t to) {
    for (uint8_t id = 0U; id < world->platform_count; id++) {
        const building_clock_platform_t* p = &world->platforms[id];
        if (from.y == p->surface_y && to.y == p->surface_y && from.x >= p->x_start && from.x <= p->x_end && to.x >= p->x_start && to.x <= p->x_end) {
            return true;
        }
    }
    return false;
}

/* Recovery uses real service bays and a real flat trolley route. The depot-side
 * endpoint is the route's recovery chute. No cargo is invented for animation. */
static bool choose_cart_task(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    for (uint8_t id = 0U; id < simulation->state.cart_count; id++) {
        building_clock_cart_t* cart = &simulation->state.carts[id];
        const building_clock_cart_route_t* route = &world->cart_routes[id];
        if (route->stop_count < 2U || cart->operator_worker_id != BUILDING_CLOCK_INVALID_ID) {
            continue;
        }
        bool reserved_for_other = false;
        for (uint8_t job_id = 0U; job_id < BUILDING_CLOCK_MAX_JOBS; job_id++) {
            const building_clock_job_t* job = &simulation->state.jobs[job_id];
            if (job_is_live(job) && job->kind == BUILDING_CLOCK_JOB_PUSH_CART && job->subject.object_id == id && worker->current_job_id != job_id) {
                reserved_for_other = true;
            }
        }
        if (reserved_for_other || (worker->current_job_id < BUILDING_CLOCK_MAX_JOBS && simulation->state.jobs[worker->current_job_id].subject.object_id != id)) {
            continue;
        }
        const building_clock_point_t p = cart_position(world, id, cart);
        const int16_t left = route->stops[0].position.x;
        const int16_t right = route->stops[route->stop_count - 1U].position.x;
        const building_clock_point_t depot = world->depot_count > 0U ? building_clock_rect_center(world->depots[0].area) : route->stops[0].position;
        const int16_t recovery_x = depot.x < (left + right) / 2 ? left : right;
        cart->pickup_bay_id = BUILDING_CLOCK_INVALID_ID;
        cart->destination_x = recovery_x;
        if (cart->rubble_sacks == 0U) {
            int32_t nearest = INT32_MAX;
            for (uint8_t bay = 0U; bay < world->debris_bay_count; bay++) {
                if (simulation->state.recovery_sacks[bay] == 0U || world->debris_bays[bay].access_node_id >= world->navigation.node_count) {
                    continue;
                }
                building_clock_point_t service = world->navigation.nodes[world->debris_bays[bay].access_node_id].position;
                service.x = building_clock_rect_center(world->debris_bays[bay].area).x;
                const int32_t distance = service.x > p.x ? service.x - p.x : p.x - service.x;
                if (service.x >= left && service.x <= right && same_platform(world, p, service) && distance < nearest) {
                    nearest = distance;
                    cart->pickup_bay_id = bay;
                    cart->destination_x = service.x;
                }
            }
            if (cart->pickup_bay_id == BUILDING_CLOCK_INVALID_ID) {
                continue;
            }
        }
        if (same_platform(world, worker->position, p) && building_clock_navigation_node_capacity(world, worker->current_node_id) == BUILDING_CLOCK_MAX_WORKERS) {
            cart->operator_worker_id = worker_id;
            cart->status = BUILDING_CLOCK_CART_LOADING;
            cart->transfer_elapsed_ms = 0U;
            worker->platform_activity_phase = BUILDING_CLOCK_PLATFORM_CART_APPROACH;
            worker->platform_motion_remainder_ms = 0U;
            worker->platform_activity_position = p;
            return true;
        }
    }
    return false;
}

static bool advance_platform_activity(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id, uint32_t elapsed_ms) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    building_clock_cart_t* cart = NULL;
    uint8_t cart_id = 0U;
    for (; cart_id < simulation->state.cart_count; cart_id++) {
        if (simulation->state.carts[cart_id].operator_worker_id == worker_id) {
            cart = &simulation->state.carts[cart_id];
            break;
        }
    }
    if (cart != NULL && worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_CART_TRANSFER) {
        worker->state = BUILDING_CLOCK_WORKER_FINISH_JOB;
        cart->transfer_elapsed_ms = (uint16_t)(cart->transfer_elapsed_ms + elapsed_ms);
        worker->action_phase = (uint8_t)(cart->transfer_elapsed_ms * 4U / BUILDING_CLOCK_CART_TRANSFER_MS);
        if (cart->transfer_elapsed_ms < BUILDING_CLOCK_CART_TRANSFER_MS) {
            return true;
        }
        cart->transfer_elapsed_ms = 0U;
        if (cart->pickup_bay_id < world->debris_bay_count) {
            uint16_t* stock = &simulation->state.recovery_sacks[cart->pickup_bay_id];
            if (*stock > 0U && cart->rubble_sacks < CART_SACK_CAPACITY) {
                (*stock)--;
                cart->rubble_sacks++;
            }
            if (*stock > 0U && cart->rubble_sacks < CART_SACK_CAPACITY) {
                return true;
            }
            cart->pickup_bay_id = BUILDING_CLOCK_INVALID_ID;
            const building_clock_cart_route_t* route = &world->cart_routes[cart_id];
            const building_clock_rect_t depot = world->depots[0].area;
            cart->destination_x =
                depot.x < (route->stops[0].position.x + route->stops[route->stop_count - 1U].position.x) / 2 ? route->stops[0].position.x : route->stops[route->stop_count - 1U].position.x;
            worker->platform_activity_phase = BUILDING_CLOCK_PLATFORM_CART_APPROACH;
            cart->status = BUILDING_CLOCK_CART_LOADING;
        } else {
            if (cart->rubble_sacks > 0U) {
                cart->rubble_sacks--;
                if (simulation->state.depot_inventory[0].quantity[BUILDING_CLOCK_MATERIAL_LOOSE_MASONRY] < UINT16_MAX) {
                    simulation->state.depot_inventory[0].quantity[BUILDING_CLOCK_MATERIAL_LOOSE_MASONRY]++;
                }
                simulation->metrics.rubble_sacks_recovered++;
            }
            if (cart->rubble_sacks == 0U) {
                simulation->metrics.trolley_loads_delivered++;
                if (worker->current_job_id < BUILDING_CLOCK_MAX_JOBS && simulation->state.jobs[worker->current_job_id].kind == BUILDING_CLOCK_JOB_PUSH_CART) {
                    complete_current_job(simulation, world, worker_id);
                } else {
                    park_worker_cart(simulation, worker_id);
                    worker->platform_activity_phase = BUILDING_CLOCK_PLATFORM_RETURN;
                }
                cart->status = BUILDING_CLOCK_CART_PARKED;
            }
        }
        return true;
    }
    if (worker->platform_activity_phase != BUILDING_CLOCK_PLATFORM_WALK_OUT && worker->platform_activity_phase != BUILDING_CLOCK_PLATFORM_RETURN &&
        worker->platform_activity_phase != BUILDING_CLOCK_PLATFORM_CART_APPROACH && worker->platform_activity_phase != BUILDING_CLOCK_PLATFORM_CART_PUSH &&
        worker->platform_activity_phase != BUILDING_CLOCK_PLATFORM_WORK_APPROACH && worker->platform_activity_phase != BUILDING_CLOCK_PLATFORM_PICKUP_APPROACH) {
        return false;
    }
    building_clock_point_t destination =
        worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_RETURN ? world->navigation.nodes[worker->current_node_id].position : worker->platform_activity_position;
    int push_direction = 1;
    if (cart != NULL) {
        const building_clock_point_t p = cart_position(world, cart_id, cart);
        /* Keep a push's handle side fixed. Recomputing it from rounded route
         * progress can alternate sides forever near an interior bay stop. */
        if (worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_CART_APPROACH) {
            cart->push_direction = cart->destination_x >= p.x ? 1 : -1;
        }
        push_direction = cart->push_direction;
        destination = worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_CART_APPROACH ? p : (building_clock_point_t){cart->destination_x, p.y};
        destination.x = (int16_t)(destination.x - push_direction * 12);
    }
    const uint32_t total = elapsed_ms + worker->platform_motion_remainder_ms;
    uint32_t distance = total / WALK_MS_PER_COST_UNIT;
    worker->platform_motion_remainder_ms = (uint8_t)(total % WALK_MS_PER_COST_UNIT);
    const int32_t dx = (int32_t)destination.x - worker->position.x;
    const uint32_t remaining = (uint32_t)(dx < 0 ? -dx : dx);
    if (distance > remaining) {
        distance = remaining;
    }
    worker->facing = dx < 0 ? BUILDING_CLOCK_FACING_LEFT : BUILDING_CLOCK_FACING_RIGHT;
    worker->position.x = (int16_t)(worker->position.x + (dx < 0 ? -(int32_t)distance : (int32_t)distance));
    worker->state = worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_CART_PUSH ? BUILDING_CLOCK_WORKER_PUSH : BUILDING_CLOCK_WORKER_WALK;
    if (cart != NULL && worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_CART_PUSH) {
        const building_clock_cart_route_t* route = &world->cart_routes[cart_id];
        const int32_t span = (int32_t)route->stops[cart->target_stop_id].position.x - route->stops[cart->current_stop_id].position.x;
        cart->route_progress_per_mille = (uint16_t)(((int32_t)worker->position.x + push_direction * 12 - route->stops[cart->current_stop_id].position.x) * 1000 / span);
    }
    if (worker->position.x == destination.x) {
        if (cart != NULL && worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_CART_APPROACH) {
            worker->platform_activity_phase = BUILDING_CLOCK_PLATFORM_CART_PUSH;
            cart->status = BUILDING_CLOCK_CART_MOVING;
            return true;
        }
        if (cart != NULL) {
            worker->platform_activity_phase = BUILDING_CLOCK_PLATFORM_CART_TRANSFER;
            cart->status = cart->pickup_bay_id < world->debris_bay_count ? BUILDING_CLOCK_CART_LOADING : BUILDING_CLOCK_CART_UNLOADING;
            cart->transfer_elapsed_ms = 0U;
            return true;
        }
        if (worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_WORK_APPROACH || worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_PICKUP_APPROACH) {
            worker->platform_activity_phase = worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_WORK_APPROACH ? BUILDING_CLOCK_PLATFORM_WORK : BUILDING_CLOCK_PLATFORM_PICKUP;
            worker->state = BUILDING_CLOCK_WORKER_CHOOSE_JOB;
            return true;
        }
        worker->platform_activity_phase = worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_RETURN ? BUILDING_CLOCK_PLATFORM_NONE : BUILDING_CLOCK_PLATFORM_REST;
        worker->state = worker->current_job_id < BUILDING_CLOCK_MAX_JOBS ? BUILDING_CLOCK_WORKER_CHOOSE_JOB : BUILDING_CLOCK_WORKER_IDLE;
        worker->behavior_timer_ms = 0U;
    }
    return true;
}

static void clear_job_owner_references(building_clock_simulation_t* simulation, uint8_t job_id) {
    for (uint8_t digit = 0U; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        if (simulation->state.digits[digit].finishing_job_id == job_id) {
            simulation->state.digits[digit].finishing_job_id = BUILDING_CLOCK_INVALID_ID;
        }
        building_clock_digit_structure_t* structures[] = {
            &simulation->state.digits[digit].outgoing,
            &simulation->state.digits[digit].incoming,
        };
        for (uint8_t structure = 0U; structure < 2U; structure++) {
            for (uint8_t section = 0U; section < structures[structure]->section_count; section++) {
                if (structures[structure]->sections[section].assigned_job_id == job_id) {
                    structures[structure]->sections[section].assigned_job_id = BUILDING_CLOCK_INVALID_ID;
                }
            }
        }
    }
    for (uint8_t structure = 0U; structure < BUILDING_CLOCK_MAX_TEMP_STRUCTURES; structure++) {
        if (simulation->state.temp_structures[structure].assigned_job_id == job_id) {
            simulation->state.temp_structures[structure].assigned_job_id = BUILDING_CLOCK_INVALID_ID;
        }
    }
}

static building_clock_load_t* find_section_load(building_clock_simulation_t* simulation, uint8_t digit_id, uint8_t section_id) {
    for (uint8_t load_id = 0U; load_id < BUILDING_CLOCK_MAX_LOADS; load_id++) {
        building_clock_load_t* load = &simulation->state.loads[load_id];
        if (load->status != BUILDING_CLOCK_LOAD_UNUSED && load->digit_id == digit_id && load->section_id == section_id) {
            return load;
        }
    }
    return NULL;
}

static void restage_carried_loads(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id) {
    if (worker_id >= simulation->state.worker_count) {
        return;
    }
    const uint8_t first = simulation->state.workers[worker_id].carried_load_id;
    if (first >= BUILDING_CLOCK_MAX_LOADS) {
        return;
    }
    const uint8_t ids[2] = {first, simulation->state.loads[first].companion_load_id};
    for (uint8_t index = 0U; index < 2U; index++) {
        if (ids[index] >= BUILDING_CLOCK_MAX_LOADS) {
            continue;
        }
        building_clock_load_t* load = &simulation->state.loads[ids[index]];
        if (load->status != BUILDING_CLOCK_LOAD_CARRIED || load->carrier_worker_id != worker_id) {
            continue;
        }
        load->status = BUILDING_CLOCK_LOAD_STAGED;
        load->companion_load_id = BUILDING_CLOCK_INVALID_ID;
        load->carrier_worker_id = BUILDING_CLOCK_INVALID_ID;
        load->placement_progress_per_mille = 0U;
        simulation->state.digits[load->digit_id].incoming.sections[load->section_id].status = BUILDING_CLOCK_SECTION_STAGED;
        simulation->state.staging_inventory[load->location_id].quantity[BUILDING_CLOCK_MATERIAL_STRUCTURAL_SECTION]++;
        load->position = world->navigation.nodes[load->access_node_id].position;
        load->position.x = (int16_t)(load->position.x + (load->section_id % 2U == 0U ? -8 : 8));
        load->position.y -= 7;
    }
    simulation->state.workers[worker_id].carried_load_id = BUILDING_CLOCK_INVALID_ID;
}

static void release_job_subject(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t job_id) {
    const building_clock_job_t* job = &simulation->state.jobs[job_id];
    if (job->subject.kind == BUILDING_CLOCK_JOB_SUBJECT_DEBRIS && job->subject.object_id < BUILDING_CLOCK_MAX_DEBRIS_PIECES) {
        building_clock_debris_t* debris = &simulation->state.debris[job->subject.object_id];
        if (job->reserved_worker_id < simulation->state.worker_count) {
            building_clock_worker_t* worker = &simulation->state.workers[job->reserved_worker_id];
            if (worker->carried_debris_id == job->subject.object_id) {
                /* Cancellation puts the sack down here, not back at its old
                 * foundation or magically into the remote recovery bay. */
                debris->position = worker->position;
                debris->position.y -= 3;
                debris->status = BUILDING_CLOCK_DEBRIS_RECOVERABLE;
                worker->carried_debris_id = BUILDING_CLOCK_INVALID_ID;
            }
        }
        debris->reserved_worker_id = BUILDING_CLOCK_INVALID_ID;
        return;
    }
    if (job->kind == BUILDING_CLOCK_JOB_STAGE_MATERIAL && job->subject.kind == BUILDING_CLOCK_JOB_SUBJECT_LOAD && job->subject.object_id < BUILDING_CLOCK_MAX_LOADS) {
        building_clock_load_t* load = &simulation->state.loads[job->subject.object_id];
        if (load->status != BUILDING_CLOCK_LOAD_UNUSED) {
            if (load->companion_load_id < BUILDING_CLOCK_MAX_LOADS) {
                building_clock_load_t* companion = &simulation->state.loads[load->companion_load_id];
                memset(companion, 0, sizeof(*companion));
                simulation->state.used_load_slot_count--;
            }
            memset(load, 0, sizeof(*load));
            simulation->state.used_load_slot_count--;
        }
        return;
    }
    if ((job->kind == BUILDING_CLOCK_JOB_INSTALL_TEMP_LADDER || job->kind == BUILDING_CLOCK_JOB_INSTALL_TEMP_PLATFORM) && job->subject.kind == BUILDING_CLOCK_JOB_SUBJECT_TEMP_STRUCTURE &&
        job->subject.object_id < BUILDING_CLOCK_MAX_TEMP_STRUCTURES) {
        building_clock_temp_structure_t* structure = &simulation->state.temp_structures[job->subject.object_id];
        if (structure->status != BUILDING_CLOCK_TEMP_STRUCTURE_UNUSED && structure->status != BUILDING_CLOCK_TEMP_STRUCTURE_ACTIVE) {
            memset(structure, 0, sizeof(*structure));
            if (simulation->state.used_temp_structure_slot_count > 0U) {
                simulation->state.used_temp_structure_slot_count--;
            }
        }
        return;
    }
    if (job->subject.kind != BUILDING_CLOCK_JOB_SUBJECT_DIGIT_SECTION || job->subject.digit_id >= BUILDING_CLOCK_DIGIT_COUNT) {
        return;
    }
    building_clock_digit_structure_t* structure = &simulation->state.digits[job->subject.digit_id].incoming;
    if (job->subject.section_id >= structure->section_count) {
        return;
    }
    if (job->kind == BUILDING_CLOCK_JOB_PLACE_SECTION || job->kind == BUILDING_CLOCK_JOB_SECURE_SECTION) {
        restage_carried_loads(simulation, world, job->reserved_worker_id);
    }
}

static void cancel_job_internal(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t job_id, bool plan_egress, bool interrupted_by_transition) {
    building_clock_job_t* job = &simulation->state.jobs[job_id];
    const uint8_t worker_id = job->reserved_worker_id;
    release_job_subject(simulation, world, job_id);
    if (worker_id < simulation->state.worker_count && simulation->state.workers[worker_id].current_job_id == job_id) {
        clear_worker_job(simulation, world, worker_id);
        if (plan_egress) {
            plan_idle_egress(simulation, world, worker_id);
        }
    }
    clear_job_owner_references(simulation, job_id);
    job->status = BUILDING_CLOCK_JOB_CANCELLED;
    job->reserved_worker_id = BUILDING_CLOCK_INVALID_ID;
    simulation->metrics.jobs_cancelled++;
    if (interrupted_by_transition) {
        simulation->metrics.jobs_interrupted++;
    }
}

building_clock_simulation_status_t building_clock_simulation_cancel_job(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t job_id) {
    if (simulation == NULL || world == NULL) {
        return BUILDING_CLOCK_SIMULATION_INVALID_ARGUMENT;
    }
    if (job_id >= BUILDING_CLOCK_MAX_JOBS || simulation->state.jobs[job_id].status == BUILDING_CLOCK_JOB_UNUSED) {
        return BUILDING_CLOCK_SIMULATION_INVALID_STATE;
    }
    building_clock_job_t* job = &simulation->state.jobs[job_id];
    if (job->status == BUILDING_CLOCK_JOB_COMPLETE || job->status == BUILDING_CLOCK_JOB_CANCELLED) {
        return BUILDING_CLOCK_SIMULATION_OK;
    }

    cancel_job_internal(simulation, world, job_id, true, false);
    return BUILDING_CLOCK_SIMULATION_OK;
}

building_clock_simulation_status_t building_clock_simulation_recycle_job(building_clock_simulation_t* simulation, uint8_t job_id) {
    if (simulation == NULL) {
        return BUILDING_CLOCK_SIMULATION_INVALID_ARGUMENT;
    }
    if (job_id >= BUILDING_CLOCK_MAX_JOBS) {
        return BUILDING_CLOCK_SIMULATION_INVALID_STATE;
    }
    building_clock_job_t* job = &simulation->state.jobs[job_id];
    if (job->status != BUILDING_CLOCK_JOB_COMPLETE && job->status != BUILDING_CLOCK_JOB_CANCELLED) {
        return BUILDING_CLOCK_SIMULATION_INVALID_STATE;
    }
    for (uint8_t worker_id = 0U; worker_id < simulation->state.worker_count; worker_id++) {
        if (simulation->state.workers[worker_id].current_job_id == job_id) {
            return BUILDING_CLOCK_SIMULATION_INVALID_STATE;
        }
    }
    clear_job_owner_references(simulation, job_id);
    memset(job, 0, sizeof(*job));
    if (simulation->state.used_job_slot_count > 0U) {
        simulation->state.used_job_slot_count--;
    }
    simulation->metrics.jobs_recycled++;
    return BUILDING_CLOCK_SIMULATION_OK;
}

static bool worker_can_do_job(const building_clock_worker_t* worker, const building_clock_job_t* job, uint64_t elapsed_ms) {
    return job->status == BUILDING_CLOCK_JOB_PENDING && job->earliest_start_elapsed_ms <= elapsed_ms && (worker->capabilities & job->required_worker_capabilities) == job->required_worker_capabilities;
}

static uint8_t job_route_destination(const building_clock_job_t* job) {
    if (job->source_node_id != BUILDING_CLOCK_INVALID_ID && !job->source_action_complete) {
        return job->source_node_id;
    }
    return job->target_node_id;
}

static building_clock_simulation_status_t add_transition_job(building_clock_simulation_t* simulation, const building_clock_world_t* world, building_clock_job_kind_t kind,
                                                             building_clock_job_subject_t subject, uint8_t source_node_id, uint8_t target_node_id, uint8_t priority, uint16_t capabilities,
                                                             uint32_t action_duration_ms, uint8_t* job_id) {
    if (target_node_id >= world->navigation.node_count || (source_node_id != BUILDING_CLOCK_INVALID_ID && source_node_id >= world->navigation.node_count)) {
        return BUILDING_CLOCK_SIMULATION_INVALID_STATE;
    }
    for (uint8_t slot = 0U; slot < BUILDING_CLOCK_MAX_JOBS; slot++) {
        if (simulation->state.jobs[slot].status != BUILDING_CLOCK_JOB_UNUSED) {
            continue;
        }
        building_clock_job_t* job = &simulation->state.jobs[slot];
        memset(job, 0, sizeof(*job));
        job->kind = kind;
        job->status = BUILDING_CLOCK_JOB_PENDING;
        /* Do not copy unspecified padding from a caller's local subject. */
        job->subject.kind = subject.kind;
        job->subject.object_id = subject.object_id;
        job->subject.digit_id = subject.digit_id;
        job->subject.section_id = subject.section_id;
        job->priority = priority;
        job->source_node_id = source_node_id;
        job->target_node_id = target_node_id;
        job->source_action_complete = source_node_id == BUILDING_CLOCK_INVALID_ID;
        job->required_material = BUILDING_CLOCK_MATERIAL_NONE;
        job->required_machinery_kind = BUILDING_CLOCK_MACHINERY_NONE;
        job->required_machinery_id = BUILDING_CLOCK_INVALID_ID;
        job->required_worker_capabilities = capabilities;
        job->action_duration_ms = action_duration_ms;
        job->reserved_worker_id = BUILDING_CLOCK_INVALID_ID;
        simulation->state.used_job_slot_count++;
        if (simulation->state.used_job_slot_count > simulation->metrics.peak_job_slots) {
            simulation->metrics.peak_job_slots = simulation->state.used_job_slot_count;
        }
        if (job_id != NULL) {
            *job_id = slot;
        }
        return BUILDING_CLOCK_SIMULATION_OK;
    }
    return BUILDING_CLOCK_SIMULATION_CAPACITY_EXCEEDED;
}

static bool job_is_better(const building_clock_job_t* candidate_job, uint32_t candidate_cost, uint8_t candidate_id, const building_clock_job_t* best_job, uint32_t best_cost, uint8_t best_id) {
    if (best_job == NULL || candidate_job->priority != best_job->priority) {
        return best_job == NULL || candidate_job->priority > best_job->priority;
    }
    if (candidate_cost != best_cost) {
        return candidate_cost < best_cost;
    }
    return candidate_id < best_id;
}

static void copy_route_to_worker(building_clock_worker_t* worker, const building_clock_route_t* route) {
    clear_worker_route(worker);
    worker->route_node_count = route->node_count;
    worker->route_edge_count = route->edge_count;
    worker->destination_node_id = route->node_ids[route->node_count - 1U];
    memcpy(worker->route_node_ids, route->node_ids, route->node_count * sizeof(route->node_ids[0]));
    for (uint8_t edge = 0U; edge < route->edge_count; edge++) {
        worker->route_edge_ids[edge] = (uint8_t)route->edge_ids[edge];
    }
}

static uint32_t route_duration_ms(const building_clock_world_t* world, const building_clock_route_t* route) {
    uint32_t total = 0U;
    for (uint8_t edge = 0U; edge < route->edge_count; edge++) {
        total += traversal_duration(world, &world->navigation.edges[route->edge_ids[edge]]);
    }
    return total;
}

static uint32_t rope_return_ms(const building_clock_world_t* world, const building_clock_worker_t* worker) {
    const building_clock_point_t home = world->navigation.nodes[worker->current_node_id].position;
    int32_t dx = (int32_t)home.x - worker->position.x;
    int32_t dy = (int32_t)worker->rope.anchor.y - worker->position.y;
    return (uint32_t)((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy)) * ROPE_MS_PER_PIXEL + 100U;
}

/* Only predict short, already-committed local releases. Do not reserve an
 * inspector across a long roof detour while a local crew is returning. */
static uint32_t local_release_ms(const building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id) {
    const building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    if (worker->rope.returning && worker->rope.phase != BUILDING_CLOCK_ROPE_NONE) {
        return rope_return_ms(world, worker);
    }
    if (worker->current_job_id >= BUILDING_CLOCK_MAX_JOBS) {
        return UINT32_MAX;
    }
    const building_clock_job_t* job = &simulation->state.jobs[worker->current_job_id];
    if ((job->kind == BUILDING_CLOCK_JOB_STAGE_MATERIAL && job->source_action_complete && worker->state == BUILDING_CLOCK_WORKER_FINISH_JOB) ||
        (job->kind == BUILDING_CLOCK_JOB_CLEAR_DEBRIS && worker->state == BUILDING_CLOCK_WORKER_CLEAR_DEBRIS)) {
        return job->action_duration_ms - worker->state_elapsed_ms + 100U;
    }
    return UINT32_MAX;
}

static bool assign_job(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    uint8_t best_job_id = BUILDING_CLOCK_INVALID_ID;
    uint32_t best_cost = UINT32_MAX;
    building_clock_route_t best_route;
    memset(&best_route, 0, sizeof(best_route));

    for (uint8_t job_id = 0U; job_id < BUILDING_CLOCK_MAX_JOBS; job_id++) {
        const building_clock_job_t* job = &simulation->state.jobs[job_id];
        if (!worker_can_do_job(worker, job, simulation->state.elapsed_ms)) {
            continue;
        }
        /* Independent rope builders do not reserve half the crew for tenders.
         * Actual work-front occupancy and rope lanes still constrain access. */
        /* Do not dispatch a queue of workers to one exclusive work front. */
        uint8_t working_here = 0U;
        for (uint8_t other = 0U; other < BUILDING_CLOCK_MAX_JOBS; other++) {
            const building_clock_job_t* active = &simulation->state.jobs[other];
            if (active->status == BUILDING_CLOCK_JOB_ACTIVE && active->target_node_id == job->target_node_id &&
                (job->kind != BUILDING_CLOCK_JOB_STAGE_MATERIAL || active->kind == BUILDING_CLOCK_JOB_STAGE_MATERIAL)) {
                working_here++;
            }
        }
        const uint8_t node_capacity = building_clock_navigation_node_capacity(world, job->target_node_id);
        const uint8_t dispatch_capacity = job->kind == BUILDING_CLOCK_JOB_STAGE_MATERIAL && node_capacity > 4U ? 4U : node_capacity;
        if (working_here >= dispatch_capacity) {
            continue;
        }
        const uint8_t destination_node_id = job_route_destination(job);
        /* Let a nearby free crew member take the pickup/control job instead
         * of sending the first worker ID across the level past them. This is
         * a dispatch preference only; actual travel still uses graph routes. */
        const building_clock_route_request_t request = {
            .start_node_id = worker->current_node_id,
            .destination_node_id = destination_node_id,
            .allow_temporary_connections = true,
        };
        building_clock_route_t route;
        const building_clock_route_status_t route_status = building_clock_find_route(world, &simulation->navigation, &request, &route);
        if (route_status != BUILDING_CLOCK_ROUTE_OK) {
            simulation->metrics.route_failures++;
            continue;
        }
        if (route_intersects_active_hazard(simulation, world, &route)) {
            continue;
        }
        const uint32_t travel_ms = route_duration_ms(world, &route);
        bool nearer_available = false;
        for (uint8_t other_id = 0U; other_id < simulation->state.worker_count; other_id++) {
            const building_clock_worker_t* other = &simulation->state.workers[other_id];
            if (other_id == worker_id || !worker_can_do_job(other, job, simulation->state.elapsed_ms)) {
                continue;
            }
            uint32_t release_ms = 0U;
            if (other->current_job_id != BUILDING_CLOCK_INVALID_ID || other->rope.phase != BUILDING_CLOCK_ROPE_NONE || other->route_purpose != BUILDING_CLOCK_WORKER_ROUTE_NONE) {
                if ((job->kind != BUILDING_CLOCK_JOB_INSPECT && job->kind != BUILDING_CLOCK_JOB_STAGE_MATERIAL && job->kind != BUILDING_CLOCK_JOB_CLEAR_DEBRIS) ||
                    !cache_nodes_share_platform(world, other->current_node_id, destination_node_id)) {
                    continue;
                }
                release_ms = local_release_ms(simulation, world, other_id);
                if (release_ms >= travel_ms) {
                    continue;
                }
            }
            const building_clock_route_request_t other_request = {
                .start_node_id = other->current_node_id,
                .destination_node_id = destination_node_id,
                .allow_temporary_connections = true,
            };
            building_clock_route_t other_route;
            if (building_clock_find_route(world, &simulation->navigation, &other_request, &other_route) == BUILDING_CLOCK_ROUTE_OK &&
                !route_intersects_active_hazard(simulation, world, &other_route)) {
                const uint32_t other_ms = release_ms + route_duration_ms(world, &other_route);
                if (other_ms < travel_ms || (other_ms == travel_ms && other_id < worker_id)) {
                    nearer_available = true;
                    break;
                }
            }
        }
        if (nearer_available) {
            continue;
        }
        const building_clock_job_t* best_job = best_job_id == BUILDING_CLOCK_INVALID_ID ? NULL : &simulation->state.jobs[best_job_id];
        if (job_is_better(job, travel_ms, job_id, best_job, best_cost, best_job_id)) {
            best_job_id = job_id;
            best_cost = travel_ms;
            best_route = route;
        }
    }

    if (best_job_id == BUILDING_CLOCK_INVALID_ID) {
        return false;
    }
    building_clock_job_t* job = &simulation->state.jobs[best_job_id];
    job->status = BUILDING_CLOCK_JOB_ACTIVE;
    job->reserved_worker_id = worker_id;
    worker->current_job_id = best_job_id;
    copy_route_to_worker(worker, &best_route);
    worker->route_purpose = BUILDING_CLOCK_WORKER_ROUTE_JOB;
    worker->state = BUILDING_CLOCK_WORKER_CHOOSE_JOB;
    worker->state_elapsed_ms = 0U;
    simulation->metrics.jobs_started++;
    return true;
}

static uint32_t action_duration(const building_clock_job_t* job) {
    if (job->action_duration_ms > 0U) {
        return job->action_duration_ms;
    }
    switch (job->kind) {
    case BUILDING_CLOCK_JOB_CLEAR_DEBRIS:
        return s_default_transition_pacing.debris_clear_action_ms;
    case BUILDING_CLOCK_JOB_PLACE_SECTION:
        return s_default_transition_pacing.section_place_action_ms;
    case BUILDING_CLOCK_JOB_INSPECT:
        return DEFAULT_INSPECT_ACTION_MS;
    case BUILDING_CLOCK_JOB_SECURE_SECTION:
        return DEFAULT_HAMMER_ACTION_MS;
    case BUILDING_CLOCK_JOB_PATROL:
    default:
        return DEFAULT_PATROL_ACTION_MS;
    }
}

static void free_section_load(building_clock_simulation_t* simulation, uint8_t digit_id, uint8_t section_id) {
    for (uint8_t load_id = 0U; load_id < BUILDING_CLOCK_MAX_LOADS; load_id++) {
        building_clock_load_t* load = &simulation->state.loads[load_id];
        if (load->status == BUILDING_CLOCK_LOAD_UNUSED || load->digit_id != digit_id || load->section_id != section_id) {
            continue;
        }
        if (load->carrier_worker_id < simulation->state.worker_count && simulation->state.workers[load->carrier_worker_id].carried_load_id == load_id) {
            simulation->state.workers[load->carrier_worker_id].carried_load_id = load->companion_load_id;
        }
        memset(load, 0, sizeof(*load));
        if (simulation->state.used_load_slot_count > 0U) {
            simulation->state.used_load_slot_count--;
        }
        return;
    }
}

static void apply_job_completion(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t job_id) {
    building_clock_job_t* job = &simulation->state.jobs[job_id];
    if (job->kind == BUILDING_CLOCK_JOB_STAGE_MATERIAL && job->subject.kind == BUILDING_CLOCK_JOB_SUBJECT_LOAD && job->subject.object_id < BUILDING_CLOCK_MAX_LOADS) {
        const uint8_t load_ids[2] = {job->subject.object_id, simulation->state.loads[job->subject.object_id].companion_load_id};
        for (uint8_t index = 0U; index < 2U; index++) {
            if (load_ids[index] >= BUILDING_CLOCK_MAX_LOADS) {
                continue;
            }
            building_clock_load_t* load = &simulation->state.loads[load_ids[index]];
            load->status = BUILDING_CLOCK_LOAD_STAGED;
            load->carrier_worker_id = BUILDING_CLOCK_INVALID_ID;
            load->companion_load_id = BUILDING_CLOCK_INVALID_ID;
            load->position = simulation->state.workers[job->reserved_worker_id].position;
            load->position.x = (int16_t)(load->position.x + (index == 0U ? -3 : 4));
            load->position.y -= 7;
            simulation->state.digits[load->digit_id].incoming.sections[load->section_id].status = BUILDING_CLOCK_SECTION_STAGED;
            simulation->state.staging_inventory[load->location_id].quantity[BUILDING_CLOCK_MATERIAL_STRUCTURAL_SECTION]++;
            simulation->metrics.sections_staged++;
        }
        return;
    }
    if ((job->kind == BUILDING_CLOCK_JOB_INSTALL_TEMP_LADDER || job->kind == BUILDING_CLOCK_JOB_INSTALL_TEMP_PLATFORM) && job->subject.kind == BUILDING_CLOCK_JOB_SUBJECT_TEMP_STRUCTURE &&
        job->subject.object_id < BUILDING_CLOCK_MAX_TEMP_STRUCTURES) {
        building_clock_temp_structure_t* structure = &simulation->state.temp_structures[job->subject.object_id];
        structure->status = BUILDING_CLOCK_TEMP_STRUCTURE_ACTIVE;
        structure->progress_per_mille = 1000U;
        (void)building_clock_navigation_set_temp_active(world, &simulation->navigation, structure->attachment_set_id, true);
        return;
    }
    if (job->kind == BUILDING_CLOCK_JOB_CLEAR_DEBRIS && job->subject.kind == BUILDING_CLOCK_JOB_SUBJECT_DEBRIS && job->subject.object_id < BUILDING_CLOCK_MAX_DEBRIS_PIECES) {
        building_clock_debris_t* debris = &simulation->state.debris[job->subject.object_id];
        if (debris->status != BUILDING_CLOCK_DEBRIS_UNUSED) {
            record_site_event(simulation, BUILDING_CLOCK_SITE_RUBBLE_CLEARED, debris->position, debris->source_digit_id);
            if (debris->debris_bay_id < world->debris_bay_count && simulation->state.recovery_sacks[debris->debris_bay_id] < UINT16_MAX) {
                simulation->state.recovery_sacks[debris->debris_bay_id]++;
            }
            simulation->state.workers[job->reserved_worker_id].carried_debris_id = BUILDING_CLOCK_INVALID_ID;
            memset(debris, 0, sizeof(*debris));
            if (simulation->state.used_debris_slot_count > 0U) {
                simulation->state.used_debris_slot_count--;
            }
            simulation->metrics.debris_cleared++;
        }
        return;
    }
    if (job->subject.kind != BUILDING_CLOCK_JOB_SUBJECT_DIGIT_SECTION || job->subject.digit_id >= BUILDING_CLOCK_DIGIT_COUNT) {
        return;
    }
    building_clock_digit_state_t* digit = &simulation->state.digits[job->subject.digit_id];
    if (job->kind == BUILDING_CLOCK_JOB_INSPECT && digit->finishing_job_id == job_id) {
        digit->finishing_complete = true;
        return;
    }
    if (job->subject.section_id >= digit->incoming.section_count) {
        return;
    }
    building_clock_section_state_t* section = &digit->incoming.sections[job->subject.section_id];
    if (job->kind == BUILDING_CLOCK_JOB_PLACE_SECTION) {
        section->status = BUILDING_CLOCK_SECTION_PLACED;
        section->progress_per_mille = 1000U;
        free_section_load(simulation, job->subject.digit_id, job->subject.section_id);
        simulation->metrics.sections_placed++;
    } else if (job->kind == BUILDING_CLOCK_JOB_SECURE_SECTION && section->status != BUILDING_CLOCK_SECTION_SECURED) {
        section->status = BUILDING_CLOCK_SECTION_SECURED;
        section->progress_per_mille = 1000U;
        simulation->metrics.sections_secured++;
    }
}

static uint8_t rope_platform(const building_clock_world_t* world, uint8_t node_id, const building_clock_rect_t* bricks, uint8_t count) {
    if (node_id >= world->navigation.node_count || count == 0U) {
        return BUILDING_CLOCK_INVALID_ID;
    }
    const building_clock_point_t node = world->navigation.nodes[node_id].position;
    for (uint8_t id = 0U; id < world->platform_count; id++) {
        const building_clock_platform_t* platform = &world->platforms[id];
        if (node.y != platform->surface_y || node.x < platform->x_start || node.x > platform->x_end) {
            continue;
        }
        bool usable = true;
        for (uint8_t brick = 0U; brick < count; brick++) {
            const building_clock_point_t center = building_clock_rect_center(bricks[brick]);
            if (center.x < platform->x_start + 2 || center.x > platform->x_end - 2 || center.y <= node.y + 14) {
                usable = false;
            }
            for (uint8_t other = 0U; other < world->platform_count; other++) {
                const building_clock_platform_t* obstacle = &world->platforms[other];
                if (obstacle->surface_y > node.y && obstacle->surface_y < center.y + 8 && center.x >= obstacle->x_start && center.x <= obstacle->x_end) {
                    usable = false;
                }
            }
        }
        if (usable) {
            return id;
        }
    }
    return BUILDING_CLOCK_INVALID_ID;
}

static uint8_t job_bricks(const building_clock_simulation_t* simulation, const building_clock_world_t* world, const building_clock_job_t* job,
                          building_clock_rect_t bricks[BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION]) {
    if (job->subject.kind != BUILDING_CLOCK_JOB_SUBJECT_DIGIT_SECTION || job->subject.digit_id >= BUILDING_CLOCK_DIGIT_COUNT) {
        return 0U;
    }
    const building_clock_digit_structure_t* structure = &simulation->state.digits[job->subject.digit_id].incoming;
    if (job->subject.section_id >= structure->section_count) {
        return 0U;
    }
    return building_clock_section_visible_bricks(world->digit_work_areas[job->subject.digit_id].envelope, structure->numeral, structure->sections[job->subject.section_id].kind, bricks);
}

static bool begin_rope_work(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    const building_clock_job_t* job = &simulation->state.jobs[worker->current_job_id];
    if (job->kind != BUILDING_CLOCK_JOB_PLACE_SECTION && job->kind != BUILDING_CLOCK_JOB_SECURE_SECTION) {
        return false;
    }
    building_clock_rect_t bricks[BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION];
    const uint8_t count = job_bricks(simulation, world, job, bricks);
    const uint8_t platform = rope_platform(world, worker->current_node_id, bricks, count);
    if (platform == BUILDING_CLOCK_INVALID_ID) {
        return false;
    }
    memset(&worker->rope, 0, sizeof(worker->rope));
    worker->rope.phase = BUILDING_CLOCK_ROPE_ALIGN;
    worker->rope.anchor = worker->position;
    worker->rope.work_position = building_clock_rect_center(bricks[0]);
    worker->rope.platform_id = platform;
    worker->rope.work_position.y += 8;
    (void)building_clock_navigation_release_node(world, &simulation->navigation, worker->current_node_id, worker_id);
    worker->state = BUILDING_CLOCK_WORKER_WALK;
    worker->state_elapsed_ms = 0U;
    return true;
}

/* A second small kit can stay with a builder on the same rope front. Reserve
 * it now, but do not place it until its actual supports have been secured. */
static void pack_next_rope_kit(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id, uint8_t load_id) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    const building_clock_job_t* job = &simulation->state.jobs[worker->current_job_id];
    building_clock_load_t* first = &simulation->state.loads[load_id];
    building_clock_rect_t bricks[BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION];
    const uint8_t brick_count = job_bricks(simulation, world, job, bricks);
    const uint8_t platform = rope_platform(world, job->target_node_id, bricks, brick_count);
    if (platform == BUILDING_CLOCK_INVALID_ID) {
        return;
    }
    building_clock_digit_structure_t* structure = &simulation->state.digits[first->digit_id].incoming;
    for (uint8_t id = 0U; id < BUILDING_CLOCK_MAX_LOADS; id++) {
        building_clock_load_t* next = &simulation->state.loads[id];
        if (next->status != BUILDING_CLOCK_LOAD_STAGED || next->digit_id != first->digit_id || next->access_node_id != first->access_node_id) {
            continue;
        }
        const int dx = next->position.x - worker->position.x;
        if (dx < -12 || dx > 12 || next->position.y != first->position.y) {
            continue;
        }
        building_clock_section_state_t* section = &structure->sections[next->section_id];
        if (section->assigned_job_id != BUILDING_CLOCK_INVALID_ID || (section->dependency_mask & (uint16_t)(1U << first->section_id)) == 0U) {
            continue;
        }
        bool ready_after_this = true;
        for (uint8_t dependency = 0U; dependency < structure->section_count; dependency++) {
            if (dependency != first->section_id && (section->dependency_mask & (uint16_t)(1U << dependency)) != 0U && structure->sections[dependency].status != BUILDING_CLOCK_SECTION_SECURED) {
                ready_after_this = false;
            }
        }
        const uint8_t count = building_clock_section_visible_bricks(world->digit_work_areas[first->digit_id].envelope, structure->numeral, section->kind, bricks);
        if (!ready_after_this || rope_platform(world, job->target_node_id, bricks, count) != platform) {
            continue;
        }
        first->companion_load_id = id;
        next->status = BUILDING_CLOCK_LOAD_CARRIED;
        next->carrier_worker_id = worker_id;
        section->assigned_job_id = worker->current_job_id;
        simulation->state.staging_inventory[next->location_id].quantity[BUILDING_CLOCK_MATERIAL_STRUCTURAL_SECTION]--;
        return;
    }
}

/* A work location is a small area on a real platform, not a pile of workers
 * at its graph anchor. Occupied work spots are considered in stable ID order. */
static bool approach_work_spot(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id, int16_t preferred_x, bool pickup) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    if (worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_WORK || worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_PICKUP) {
        return false;
    }
    static const int16_t offsets[] = {0, -16, 16, -32, 32, -48, 48, -64, 64};
    bool has_surface = false;
    for (uint8_t slot = 0U; slot < (pickup ? 1U : sizeof(offsets) / sizeof(offsets[0])); slot++) {
        const building_clock_point_t point = {(int16_t)(preferred_x + offsets[slot]), worker->position.y};
        if (!same_platform(world, worker->position, point) || point_in_active_hazard(simulation, point)) {
            continue;
        }
        has_surface = true;
        bool free = true;
        for (uint8_t id = 0U; !pickup && id < world->ladder_count; id++) {
            const building_clock_ladder_t* ladder = &world->ladders[id];
            if ((ladder->top_y == point.y || ladder->bottom_y == point.y) && point.x - ladder->x < 13 && ladder->x - point.x < 13) {
                free = false;
            }
        }
        for (uint8_t hazard = 0U; hazard < simulation->state.hazard_count; hazard++) {
            if (segment_may_cross_rect(worker->position, point, simulation->state.hazards[hazard].area)) {
                free = false;
            }
        }
        for (uint8_t id = 0U; !pickup && id < simulation->state.worker_count; id++) {
            const building_clock_worker_t* colleague = &simulation->state.workers[id];
            if (id != worker_id && (colleague->platform_activity_phase == BUILDING_CLOCK_PLATFORM_WORK_APPROACH || colleague->platform_activity_phase == BUILDING_CLOCK_PLATFORM_WORK)) {
                const building_clock_point_t p = colleague->platform_activity_position;
                if (point.y == p.y && point.x - p.x < 15 && p.x - point.x < 15) {
                    free = false;
                }
            }
        }
        if (free) {
            worker->platform_activity_position = point;
            worker->platform_activity_phase = pickup ? BUILDING_CLOCK_PLATFORM_PICKUP_APPROACH : BUILDING_CLOCK_PLATFORM_WORK_APPROACH;
            worker->platform_motion_remainder_ms = 0U;
            worker->state = BUILDING_CLOCK_WORKER_WALK;
            return true;
        }
    }
    if (!pickup && has_surface) {
        worker->state = BUILDING_CLOCK_WORKER_WAIT;
        return true;
    }
    return false;
}

static bool begin_job_action(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    building_clock_job_t* job = &simulation->state.jobs[worker->current_job_id];
    if (job->kind == BUILDING_CLOCK_JOB_PUSH_CART) {
        if (!choose_cart_task(simulation, world, worker_id)) {
            /* Another completed recovery may have consumed the final sacks. */
            complete_current_job(simulation, world, worker_id);
        }
        return true;
    }
    if (job->kind == BUILDING_CLOCK_JOB_CLEAR_DEBRIS) {
        const building_clock_debris_t* debris = &simulation->state.debris[job->subject.object_id];
        if (!job->source_action_complete && approach_work_spot(simulation, world, worker_id, debris->position.x, false)) {
            return true;
        }
    } else if (job->kind == BUILDING_CLOCK_JOB_STAGE_MATERIAL && job->source_action_complete && approach_work_spot(simulation, world, worker_id, worker->position.x, false)) {
        return true;
    }
    if (job->kind != BUILDING_CLOCK_JOB_CLEAR_DEBRIS && job->source_node_id != BUILDING_CLOCK_INVALID_ID && !job->source_action_complete) {
        const bool delivery = job->kind == BUILDING_CLOCK_JOB_STAGE_MATERIAL && job->subject.kind == BUILDING_CLOCK_JOB_SUBJECT_LOAD;
        if (!delivery && (job->kind != BUILDING_CLOCK_JOB_PLACE_SECTION || job->subject.kind != BUILDING_CLOCK_JOB_SUBJECT_DIGIT_SECTION)) {
            return false;
        }
        uint8_t load_id = BUILDING_CLOCK_INVALID_ID;
        for (uint8_t candidate = 0U; candidate < BUILDING_CLOCK_MAX_LOADS; candidate++) {
            const building_clock_load_t* load = &simulation->state.loads[candidate];
            if ((delivery ? load->status == BUILDING_CLOCK_LOAD_AT_DEPOT && candidate == job->subject.object_id : load->status == BUILDING_CLOCK_LOAD_STAGED) &&
                load->digit_id == job->subject.digit_id && load->section_id == job->subject.section_id) {
                load_id = candidate;
                break;
            }
        }
        if (load_id == BUILDING_CLOCK_INVALID_ID) {
            return false;
        }
        building_clock_load_t* load = &simulation->state.loads[load_id];
        if (!delivery && approach_work_spot(simulation, world, worker_id, load->position.x, true)) {
            return true;
        }
        load->status = BUILDING_CLOCK_LOAD_CARRIED;
        load->placement_progress_per_mille = 0U;
        load->carrier_worker_id = worker_id;
        if (delivery && load->companion_load_id < BUILDING_CLOCK_MAX_LOADS) {
            building_clock_load_t* companion = &simulation->state.loads[load->companion_load_id];
            companion->status = BUILDING_CLOCK_LOAD_CARRIED;
            companion->carrier_worker_id = worker_id;
        }
        if (!delivery && load->location_id < BUILDING_CLOCK_MAX_STAGING_AREAS && simulation->state.staging_inventory[load->location_id].quantity[BUILDING_CLOCK_MATERIAL_STRUCTURAL_SECTION] > 0U) {
            simulation->state.staging_inventory[load->location_id].quantity[BUILDING_CLOCK_MATERIAL_STRUCTURAL_SECTION]--;
        }
        if (!delivery) {
            pack_next_rope_kit(simulation, world, worker_id, load_id);
        }
        worker->carried_load_id = load_id;
        building_clock_section_state_t* section = &simulation->state.digits[job->subject.digit_id].incoming.sections[job->subject.section_id];
        if (!delivery) {
            section->status = BUILDING_CLOCK_SECTION_IN_TRANSIT;
        }
        job->source_action_complete = true;

        const building_clock_route_request_t request = {
            .start_node_id = worker->current_node_id,
            .destination_node_id = job->target_node_id,
            .allow_temporary_connections = true,
            .carries_large_load = false,
            .allow_machinery_edges = false,
        };
        building_clock_route_t route;
        if (building_clock_find_route(world, &simulation->navigation, &request, &route) != BUILDING_CLOCK_ROUTE_OK || route_intersects_active_hazard(simulation, world, &route)) {
            return false;
        }
        copy_route_to_worker(worker, &route);
        worker->route_purpose = BUILDING_CLOCK_WORKER_ROUTE_JOB;
        worker->state = BUILDING_CLOCK_WORKER_CARRY;
        worker->state_elapsed_ms = 0U;
        /* A roof cache is an area, not a mandatory return-to-anchor detour.
         * After physical pickup, rig directly on this same real platform;
         * ROPE_ALIGN still walks the builder to the installation column. */
        if (!delivery && cache_nodes_share_platform(world, worker->current_node_id, job->target_node_id) && begin_rope_work(simulation, world, worker_id)) {
            worker->platform_activity_phase = BUILDING_CLOCK_PLATFORM_NONE;
            return true;
        }
        if (worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_PICKUP) {
            worker->platform_activity_phase = BUILDING_CLOCK_PLATFORM_RETURN;
        }
        return true;
    }
    worker->state_elapsed_ms = 0U;
    worker->action_phase = 0U;
    if (begin_rope_work(simulation, world, worker_id)) {
        return true;
    }
    switch (job->kind) {
    case BUILDING_CLOCK_JOB_INSPECT:
        worker->state = BUILDING_CLOCK_WORKER_INSPECT;
        break;
    case BUILDING_CLOCK_JOB_SECURE_SECTION:
    case BUILDING_CLOCK_JOB_PLACE_SECTION:
    case BUILDING_CLOCK_JOB_INSTALL_TEMP_LADDER:
    case BUILDING_CLOCK_JOB_INSTALL_TEMP_PLATFORM:
        worker->state = BUILDING_CLOCK_WORKER_HAMMER;
        break;
    case BUILDING_CLOCK_JOB_CLEAR_DEBRIS:
        worker->state = job->source_action_complete ? BUILDING_CLOCK_WORKER_FINISH_JOB : BUILDING_CLOCK_WORKER_CLEAR_DEBRIS;
        break;
    case BUILDING_CLOCK_JOB_PATROL:
    case BUILDING_CLOCK_JOB_STAGE_MATERIAL:
    default:
        worker->state = BUILDING_CLOCK_WORKER_FINISH_JOB;
        break;
    }
    return true;
}

static uint32_t traversal_duration(const building_clock_world_t* world, const building_clock_nav_edge_t* edge) {
    uint32_t milliseconds_per_unit = WALK_MS_PER_COST_UNIT;
    if (edge->kind == BUILDING_CLOCK_NAV_EDGE_CLIMB) {
        milliseconds_per_unit = CLIMB_MS_PER_COST_UNIT;
    } else if (edge->kind == BUILDING_CLOCK_NAV_EDGE_TEMP_CONNECTION && edge->temp_attachment_set_id < world->temp_attachment_set_count &&
               world->temp_attachment_sets[edge->temp_attachment_set_id].kind == BUILDING_CLOCK_TEMP_STRUCTURE_LADDER) {
        milliseconds_per_unit = CLIMB_MS_PER_COST_UNIT;
    } else if (edge->kind == BUILDING_CLOCK_NAV_EDGE_TEMP_CONNECTION) {
        milliseconds_per_unit = TEMP_MS_PER_COST_UNIT;
    }
    const uint32_t duration = (uint32_t)edge->traversal_cost * milliseconds_per_unit;
    return duration < MINIMUM_TRAVERSAL_MS ? MINIMUM_TRAVERSAL_MS : duration;
}

static void note_wait(building_clock_simulation_t* simulation, building_clock_worker_t* worker) {
    if (worker->state != BUILDING_CLOCK_WORKER_WAIT) {
        simulation->metrics.wait_events++;
        worker->state_elapsed_ms = 0U;
    }
    simulation->metrics.reservation_conflicts++;
    worker->state = BUILDING_CLOCK_WORKER_WAIT;
}

static bool reservation_acquired(building_clock_reservation_status_t status) {
    return status == BUILDING_CLOCK_RESERVATION_ACQUIRED || status == BUILDING_CLOCK_RESERVATION_ALREADY_HELD;
}

static void release_current_node_for_departure(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id) {
    const building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    const uint16_t worker_bit = (uint16_t)(1U << worker_id);
    if ((building_clock_navigation_node_owner_mask(world, &simulation->navigation, worker->current_node_id) & worker_bit) != 0U) {
        (void)building_clock_navigation_release_node(world, &simulation->navigation, worker->current_node_id, worker_id);
    }
}

static bool is_following_ladder(const building_clock_world_t* world, const building_clock_nav_edge_t* edge) {
    return edge->capacity > 1U && edge->kind == BUILDING_CLOCK_NAV_EDGE_CLIMB && world->navigation.nodes[edge->from_node_id].position.x == world->navigation.nodes[edge->to_node_id].position.x;
}

static bool ladder_entry_clear(const building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id, uint16_t edge_id) {
    const building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    if (!is_following_ladder(world, &world->navigation.edges[edge_id])) {
        return true;
    }
    const int16_t start_y = world->navigation.nodes[worker->current_node_id].position.y;
    for (uint8_t other = 0U; other < simulation->state.worker_count; other++) {
        const building_clock_worker_t* leader = &simulation->state.workers[other];
        if (other == worker_id || leader->reserved_edge_id != edge_id) {
            continue;
        }
        int32_t distance = (int32_t)leader->position.y - start_y;
        distance = distance < 0 ? -distance : distance;
        if (leader->current_node_id != worker->current_node_id || distance < (int32_t)BUILDING_CLOCK_LADDER_SPACING) {
            return false;
        }
    }
    return true;
}

static bool begin_next_traversal(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    if (worker->route_node_index >= worker->route_edge_count) {
        if (worker->route_purpose == BUILDING_CLOCK_WORKER_ROUTE_JOB && worker->current_job_id < BUILDING_CLOCK_MAX_JOBS) {
            if (!begin_job_action(simulation, world, worker_id)) {
                return false;
            }
        } else {
            const bool completed_evacuation = worker->route_purpose == BUILDING_CLOCK_WORKER_ROUTE_EVACUATION;
            worker->state = BUILDING_CLOCK_WORKER_IDLE;
            clear_worker_route(worker);
            if (completed_evacuation) {
                simulation->metrics.workers_evacuated++;
                plan_idle_egress(simulation, world, worker_id);
            }
        }
        return true;
    }

    const uint8_t edge_id = worker->route_edge_ids[worker->route_node_index];
    const uint8_t next_node_id = worker->route_node_ids[worker->route_node_index + 1U];
    if (edge_id >= world->navigation.edge_count || next_node_id >= world->navigation.node_count) {
        return false;
    }
    release_current_node_for_departure(simulation, world, worker_id);
    const building_clock_nav_edge_t* edge = &world->navigation.edges[edge_id];
    if (!ladder_entry_clear(simulation, world, worker_id, edge_id)) {
        note_wait(simulation, worker);
        return true;
    }
    const building_clock_reservation_status_t edge_reservation = building_clock_navigation_reserve_edge(world, &simulation->navigation, edge_id, worker_id);
    if (!reservation_acquired(edge_reservation)) {
        note_wait(simulation, worker);
        return true;
    }
    if (edge_reservation == BUILDING_CLOCK_RESERVATION_ACQUIRED) {
        simulation->metrics.edge_reservations_acquired++;
    }

    const building_clock_reservation_status_t node_reservation = building_clock_navigation_reserve_node(world, &simulation->navigation, next_node_id, worker_id);
    if (!reservation_acquired(node_reservation)) {
        (void)building_clock_navigation_release_edge(world, &simulation->navigation, edge_id, worker_id);
        note_wait(simulation, worker);
        return true;
    }
    if (node_reservation == BUILDING_CLOCK_RESERVATION_ACQUIRED) {
        simulation->metrics.node_reservations_acquired++;
    }

    worker->reserved_edge_id = edge_id;
    worker->reserved_destination_node_id = next_node_id;
    worker->state_elapsed_ms = 0U;
    worker->edge_progress_per_mille = 0U;
    worker->state = BUILDING_CLOCK_WORKER_WALK;
    const building_clock_point_t edge_from = world->navigation.nodes[edge->from_node_id].position;
    const building_clock_point_t edge_to = world->navigation.nodes[edge->to_node_id].position;
    if (edge->kind == BUILDING_CLOCK_NAV_EDGE_CLIMB || (edge->kind == BUILDING_CLOCK_NAV_EDGE_TEMP_CONNECTION && edge_from.y != edge_to.y)) {
        worker->state = BUILDING_CLOCK_WORKER_CLIMB;
    }
    return true;
}

static void update_worker_position(building_clock_worker_t* worker, const building_clock_world_t* world, uint8_t next_node_id) {
    const building_clock_point_t from = world->navigation.nodes[worker->current_node_id].position;
    const building_clock_point_t to = world->navigation.nodes[next_node_id].position;
    const int32_t progress = worker->edge_progress_per_mille;
    worker->position.x = (int16_t)(from.x + ((int32_t)to.x - from.x) * progress / 1000);
    worker->position.y = (int16_t)(from.y + ((int32_t)to.y - from.y) * progress / 1000);
    if (to.x < from.x) {
        worker->facing = BUILDING_CLOCK_FACING_LEFT;
    } else if (to.x > from.x) {
        worker->facing = BUILDING_CLOCK_FACING_RIGHT;
    }
}

static uint32_t advance_traversal(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id, uint32_t available_ms) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    const uint8_t edge_id = (uint8_t)worker->reserved_edge_id;
    const uint8_t next_node_id = worker->reserved_destination_node_id;
    const building_clock_nav_edge_t* edge = &world->navigation.edges[edge_id];
    const uint32_t duration = traversal_duration(world, edge);
    const uint32_t remaining = duration - worker->state_elapsed_ms;
    uint32_t consumed = available_ms < remaining ? available_ms : remaining;
    bool following_paused = false;
    if (is_following_ladder(world, edge)) {
        const int32_t start_y = world->navigation.nodes[worker->current_node_id].position.y;
        int32_t length = (int32_t)world->navigation.nodes[next_node_id].position.y - start_y;
        length = length < 0 ? -length : length;
        uint32_t maximum_elapsed = duration;
        for (uint8_t other = 0U; other < simulation->state.worker_count; other++) {
            const building_clock_worker_t* leader = &simulation->state.workers[other];
            if (other == worker_id || leader->reserved_edge_id != edge_id || leader->current_node_id != worker->current_node_id || leader->state_elapsed_ms <= worker->state_elapsed_ms) {
                continue;
            }
            int32_t ahead = (int32_t)leader->position.y - start_y;
            ahead = (ahead < 0 ? -ahead : ahead) - (int32_t)BUILDING_CLOCK_LADDER_SPACING;
            const uint32_t limit = ahead > 0 ? (uint32_t)(((uint64_t)(uint32_t)ahead * 1000U / (uint32_t)length) * duration / 1000U) : 0U;
            if (limit < maximum_elapsed) {
                maximum_elapsed = limit;
            }
        }
        if (worker->state_elapsed_ms + consumed > maximum_elapsed) {
            consumed = maximum_elapsed > worker->state_elapsed_ms ? maximum_elapsed - worker->state_elapsed_ms : 0U;
            following_paused = true;
        }
    }
    worker->state_elapsed_ms += consumed;
    worker->edge_progress_per_mille = (uint16_t)(((uint64_t)worker->state_elapsed_ms * 1000U) / duration);
    update_worker_position(worker, world, next_node_id);

    if (worker->state_elapsed_ms >= duration) {
        (void)building_clock_navigation_release_edge(world, &simulation->navigation, edge_id, worker_id);
        worker->current_node_id = next_node_id;
        worker->position = world->navigation.nodes[next_node_id].position;
        worker->reserved_edge_id = BUILDING_CLOCK_INVALID_EDGE_ID;
        worker->reserved_destination_node_id = BUILDING_CLOCK_INVALID_ID;
        worker->route_node_index++;
        worker->state_elapsed_ms = 0U;
        worker->edge_progress_per_mille = 0U;
        switch (worker->route_purpose) {
        case BUILDING_CLOCK_WORKER_ROUTE_JOB:
            worker->state = BUILDING_CLOCK_WORKER_CHOOSE_JOB;
            break;
        case BUILDING_CLOCK_WORKER_ROUTE_EVACUATION:
            worker->state = BUILDING_CLOCK_WORKER_AVOID_DANGER;
            break;
        case BUILDING_CLOCK_WORKER_ROUTE_IDLE_EGRESS:
        case BUILDING_CLOCK_WORKER_ROUTE_NONE:
        default:
            worker->state = BUILDING_CLOCK_WORKER_YIELD_OR_TURN;
            break;
        }
    }
    /* A follower consumes the update while holding its actual ladder position,
     * rather than replaying unused time or trying to enter the edge again. */
    return following_paused ? available_ms : consumed;
}

static void complete_current_job(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    if (worker->current_job_id < BUILDING_CLOCK_MAX_JOBS) {
        building_clock_job_t* job = &simulation->state.jobs[worker->current_job_id];
        apply_job_completion(simulation, world, worker->current_job_id);
        job->status = BUILDING_CLOCK_JOB_COMPLETE;
        job->reserved_worker_id = BUILDING_CLOCK_INVALID_ID;
        clear_job_owner_references(simulation, worker->current_job_id);
        simulation->metrics.jobs_completed++;
    }
    restage_carried_loads(simulation, world, worker_id);
    clear_worker_job(simulation, world, worker_id);
    plan_idle_egress(simulation, world, worker_id);
}

static uint32_t advance_action(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id, uint32_t available_ms) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    building_clock_job_t* job = &simulation->state.jobs[worker->current_job_id];
    const uint32_t duration = action_duration(job);
    const uint32_t remaining = duration - worker->state_elapsed_ms;
    const uint32_t consumed = available_ms < remaining ? available_ms : remaining;
    worker->state_elapsed_ms += consumed;
    worker->action_phase = (uint8_t)(((uint64_t)worker->state_elapsed_ms * 4U) / duration);
    if ((job->kind == BUILDING_CLOCK_JOB_INSTALL_TEMP_LADDER || job->kind == BUILDING_CLOCK_JOB_INSTALL_TEMP_PLATFORM) && job->subject.object_id < BUILDING_CLOCK_MAX_TEMP_STRUCTURES) {
        simulation->state.temp_structures[job->subject.object_id].progress_per_mille = (uint16_t)(((uint64_t)worker->state_elapsed_ms * 1000U) / duration);
    }
    if (job->kind == BUILDING_CLOCK_JOB_PLACE_SECTION && job->subject.kind == BUILDING_CLOCK_JOB_SUBJECT_DIGIT_SECTION && job->subject.digit_id < BUILDING_CLOCK_DIGIT_COUNT &&
        job->subject.section_id < simulation->state.digits[job->subject.digit_id].incoming.section_count) {
        building_clock_load_t* load = find_section_load(simulation, job->subject.digit_id, job->subject.section_id);
        if (load != NULL && job->target_node_id < world->navigation.node_count) {
            const uint32_t progress = (uint32_t)(((uint64_t)worker->state_elapsed_ms * 1000U) / duration);
            load->placement_progress_per_mille = (uint16_t)progress;
            load->position = worker->position;
            load->position.y = (int16_t)(load->position.y - 9);
            simulation->state.digits[job->subject.digit_id].incoming.sections[job->subject.section_id].progress_per_mille = (uint16_t)progress;
        }
    }
    if (worker->state_elapsed_ms >= duration) {
        if (job->kind == BUILDING_CLOCK_JOB_CLEAR_DEBRIS && !job->source_action_complete) {
            building_clock_debris_t* debris = &simulation->state.debris[job->subject.object_id];
            const building_clock_route_request_t request = {
                .start_node_id = worker->current_node_id,
                .destination_node_id = job->target_node_id,
                .allow_temporary_connections = true,
                .allow_machinery_edges = false,
            };
            building_clock_route_t route;
            if (building_clock_find_route(world, &simulation->navigation, &request, &route) != BUILDING_CLOCK_ROUTE_OK || route_intersects_active_hazard(simulation, world, &route)) {
                /* Keep the rubble on the ground and retry; never lose it. */
                worker->state_elapsed_ms = 0U;
                return consumed;
            }
            debris->status = BUILDING_CLOCK_DEBRIS_CARRIED;
            debris->reserved_worker_id = worker_id;
            worker->carried_debris_id = job->subject.object_id;
            job->source_action_complete = true;
            copy_route_to_worker(worker, &route);
            worker->route_purpose = BUILDING_CLOCK_WORKER_ROUTE_JOB;
            worker->state = BUILDING_CLOCK_WORKER_CARRY;
            worker->state_elapsed_ms = 0U;
            if (worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_WORK) {
                worker->platform_activity_phase = BUILDING_CLOCK_PLATFORM_RETURN;
            }
            return consumed;
        }
        if (job->kind == BUILDING_CLOCK_JOB_PLACE_SECTION) {
            apply_job_completion(simulation, world, worker->current_job_id);
            job->kind = BUILDING_CLOCK_JOB_SECURE_SECTION;
            job->source_node_id = BUILDING_CLOCK_INVALID_ID;
            job->source_action_complete = true;
            job->action_duration_ms = simulation->pacing.section_secure_action_ms;
            worker->state = BUILDING_CLOCK_WORKER_HAMMER;
            worker->state_elapsed_ms = 0U;
            worker->action_phase = 0U;
            return consumed;
        }
        complete_current_job(simulation, world, worker_id);
    }
    return consumed;
}

static void fail_current_job(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id) {
    const uint8_t job_id = simulation->state.workers[worker_id].current_job_id;
    if (job_id < BUILDING_CLOCK_MAX_JOBS) {
        cancel_job_internal(simulation, world, job_id, true, false);
        return;
    }
    clear_worker_job(simulation, world, worker_id);
    plan_idle_egress(simulation, world, worker_id);
}

static bool rope_lane_available(const building_clock_simulation_t* simulation, uint8_t worker_id, int16_t x) {
    for (uint8_t other_id = 0U; other_id < simulation->state.worker_count; other_id++) {
        const building_clock_worker_t* other = &simulation->state.workers[other_id];
        if (other_id != worker_id && other->rope.phase != BUILDING_CLOCK_ROPE_NONE && (other->position.y > other->rope.anchor.y || other->rope.phase == BUILDING_CLOCK_ROPE_DESCEND) &&
            other->rope.anchor.x - x < 9 && x - other->rope.anchor.x < 9) {
            return false;
        }
    }
    return true;
}

static void update_carried_load_positions(building_clock_simulation_t* simulation, uint8_t worker_id) {
    const building_clock_worker_t* carrier = &simulation->state.workers[worker_id];
    if (carrier->carried_debris_id < BUILDING_CLOCK_MAX_DEBRIS_PIECES) {
        building_clock_debris_t* debris = &simulation->state.debris[carrier->carried_debris_id];
        debris->position = carrier->position;
        debris->position.y -= 7;
    }
    const building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    if (worker->carried_load_id >= BUILDING_CLOCK_MAX_LOADS) {
        return;
    }
    const uint8_t ids[2] = {worker->carried_load_id, simulation->state.loads[worker->carried_load_id].companion_load_id};
    for (uint8_t index = 0U; index < 2U; index++) {
        if (ids[index] >= BUILDING_CLOCK_MAX_LOADS) {
            continue;
        }
        building_clock_load_t* load = &simulation->state.loads[ids[index]];
        if (load->status != BUILDING_CLOCK_LOAD_CARRIED || load->carrier_worker_id != worker_id) {
            continue;
        }
        load->position = worker->position;
        load->position.y -= 9;
        if (ids[1] < BUILDING_CLOCK_MAX_LOADS) {
            load->position.x = (int16_t)(load->position.x + (index == 0U ? -6 : 6));
        }
    }
}

static bool continue_rope_front(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    if (worker->carried_load_id >= BUILDING_CLOCK_MAX_LOADS) {
        return false;
    }
    building_clock_load_t* load = &simulation->state.loads[worker->carried_load_id];
    building_clock_digit_structure_t* structure = &simulation->state.digits[load->digit_id].incoming;
    building_clock_section_state_t* section = &structure->sections[load->section_id];
    if (load->status != BUILDING_CLOCK_LOAD_CARRIED || load->carrier_worker_id != worker_id || section->assigned_job_id != worker->current_job_id ||
        !section_dependencies_secured(structure, section)) {
        return false;
    }
    building_clock_rect_t bricks[BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION];
    const uint8_t count = building_clock_section_visible_bricks(world->digit_work_areas[load->digit_id].envelope, structure->numeral, section->kind, bricks);
    if (rope_platform(world, worker->current_node_id, bricks, count) != worker->rope.platform_id) {
        return false;
    }
    const building_clock_job_subject_t subject = {.kind = BUILDING_CLOCK_JOB_SUBJECT_DIGIT_SECTION, .object_id = BUILDING_CLOCK_INVALID_ID, .digit_id = load->digit_id, .section_id = load->section_id};
    uint8_t next_job = BUILDING_CLOCK_INVALID_ID;
    if (add_transition_job(simulation, world, BUILDING_CLOCK_JOB_PLACE_SECTION, subject, BUILDING_CLOCK_INVALID_ID, worker->current_node_id, 40U, BUILDING_CLOCK_WORKER_CAN_BUILD,
                           simulation->pacing.section_place_action_ms, &next_job) != BUILDING_CLOCK_SIMULATION_OK) {
        return false;
    }
    building_clock_job_t* previous = &simulation->state.jobs[worker->current_job_id];
    previous->status = BUILDING_CLOCK_JOB_COMPLETE;
    previous->reserved_worker_id = BUILDING_CLOCK_INVALID_ID;
    clear_job_owner_references(simulation, worker->current_job_id);
    simulation->metrics.jobs_completed++;
    building_clock_job_t* job = &simulation->state.jobs[next_job];
    job->status = BUILDING_CLOCK_JOB_ACTIVE;
    job->reserved_worker_id = worker_id;
    job->source_action_complete = true;
    worker->current_job_id = next_job;
    section->assigned_job_id = next_job;
    section->status = BUILDING_CLOCK_SECTION_IN_TRANSIT;
    worker->rope.brick_index = 0U;
    worker->rope.work_position = building_clock_rect_center(bricks[0]);
    worker->rope.work_position.y += 8;
    const int32_t shift = (int32_t)worker->rope.work_position.x - worker->rope.anchor.x;
    worker->rope.phase = shift < -ROPE_REACH || shift > ROPE_REACH ? BUILDING_CLOCK_ROPE_SHIFT : BUILDING_CLOCK_ROPE_DESCEND;
    worker->state_elapsed_ms = 0U;
    worker->action_phase = 0U;
    simulation->metrics.jobs_started++;
    simulation->metrics.chained_rope_sections++;
    return true;
}

static void advance_rope(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id, uint32_t elapsed_ms) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    building_clock_rope_t* rope = &worker->rope;
    building_clock_job_t* job = worker->current_job_id < BUILDING_CLOCK_MAX_JOBS ? &simulation->state.jobs[worker->current_job_id] : NULL;
    if (rope->phase == BUILDING_CLOCK_ROPE_DESCEND) {
        const int32_t dx = (int32_t)rope->work_position.x - worker->position.x;
        const int32_t dy = (int32_t)rope->work_position.y - worker->position.y;
        if (dx >= -ROPE_REACH && dx <= ROPE_REACH && dy >= -ROPE_REACH && dy <= ROPE_REACH) {
            /* Reach adjacent masonry with the arms, rather than commuting the
             * entire suspended body to each brick center. Motion speed and
             * placement/hammer durations are unchanged. */
            rope->phase = BUILDING_CLOCK_ROPE_WORK;
            worker->state_elapsed_ms = 0U;
            worker->facing = dx < 0 ? BUILDING_CLOCK_FACING_LEFT : BUILDING_CLOCK_FACING_RIGHT;
        }
    }
    if (rope->phase == BUILDING_CLOCK_ROPE_WORK && job != NULL) {
        worker->state = BUILDING_CLOCK_WORKER_HAMMER;
        building_clock_rect_t bricks[BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION];
        const uint8_t count = job_bricks(simulation, world, job, bricks);
        const bool placing = job->kind == BUILDING_CLOCK_JOB_PLACE_SECTION;
        uint32_t duration = placing && count > 0U ? job->action_duration_ms / count : job->action_duration_ms;
        if (duration < 100U) {
            duration = 100U;
        }
        worker->state_elapsed_ms += elapsed_ms;
        worker->action_phase = (uint8_t)((worker->state_elapsed_ms / 100U) % 4U);
        if (worker->state_elapsed_ms >= duration) {
            worker->state_elapsed_ms = 0U;
            if (placing && count > 0U) {
                record_site_event(simulation, BUILDING_CLOCK_SITE_BRICK_SEATED, building_clock_rect_center(bricks[rope->brick_index]), job->subject.digit_id);
                rope->brick_index++;
                building_clock_section_state_t* section = &simulation->state.digits[job->subject.digit_id].incoming.sections[job->subject.section_id];
                section->progress_per_mille = (uint16_t)(((uint32_t)rope->brick_index * 1000U + count - 1U) / count);
                building_clock_load_t* load = find_section_load(simulation, job->subject.digit_id, job->subject.section_id);
                if (load != NULL) {
                    load->placement_progress_per_mille = section->progress_per_mille;
                }
                if (rope->brick_index < count) {
                    rope->work_position = building_clock_rect_center(bricks[rope->brick_index]);
                    rope->work_position.y += 8;
                    int32_t shift = (int32_t)rope->work_position.x - rope->anchor.x;
                    if (shift < -ROPE_REACH || shift > ROPE_REACH) {
                        rope->phase = BUILDING_CLOCK_ROPE_SHIFT;
                    } else {
                        rope->phase = BUILDING_CLOCK_ROPE_DESCEND;
                    }
                } else {
                    apply_job_completion(simulation, world, worker->current_job_id);
                    job->kind = BUILDING_CLOCK_JOB_SECURE_SECTION;
                    job->action_duration_ms = simulation->pacing.section_secure_action_ms;
                }
            } else {
                /* Dependencies become usable when secured, not after the
                 * builder's subsequent trip back to the roof. */
                apply_job_completion(simulation, world, worker->current_job_id);
                if (!continue_rope_front(simulation, world, worker_id)) {
                    rope->returning = true;
                    rope->phase = BUILDING_CLOCK_ROPE_ASCEND;
                }
            }
        }
    } else {
        building_clock_point_t destination = rope->work_position;
        if (rope->phase == BUILDING_CLOCK_ROPE_DESCEND) {
            destination.x = rope->anchor.x;
        }
        if (rope->phase == BUILDING_CLOCK_ROPE_ASCEND) {
            destination = (building_clock_point_t){.x = worker->position.x, .y = rope->anchor.y};
        } else if (rope->phase == BUILDING_CLOCK_ROPE_ALIGN || rope->phase == BUILDING_CLOCK_ROPE_SHIFT) {
            /* Place the anchor at the center of the current three-brick strip. */
            if (job != NULL) {
                const building_clock_rect_t envelope = world->digit_work_areas[job->subject.digit_id].envelope;
                const int16_t origin = (int16_t)(envelope.x + ((int16_t)envelope.width - 69) / 2);
                const int16_t strip = (int16_t)((rope->work_position.x - origin) / 30);
                destination.x = (int16_t)(origin + strip * 30 + (strip == 2 ? 4 : 14));
            }
            destination.y = rope->phase == BUILDING_CLOCK_ROPE_SHIFT ? worker->position.y : rope->anchor.y;
            const building_clock_platform_t* roof = &world->platforms[rope->platform_id];
            if (destination.x < roof->x_start + 2) {
                destination.x = (int16_t)(roof->x_start + 2);
            } else if (destination.x > roof->x_end - 2) {
                destination.x = (int16_t)(roof->x_end - 2);
            }
            if (!rope_lane_available(simulation, worker_id, destination.x)) {
                worker->state = BUILDING_CLOCK_WORKER_WAIT;
                simulation->metrics.reservation_conflicts++;
                for (uint8_t other = 0U; other < worker_id; other++) {
                    const building_clock_worker_t* blocker = &simulation->state.workers[other];
                    if (rope->phase == BUILDING_CLOCK_ROPE_SHIFT && blocker->rope.phase == BUILDING_CLOCK_ROPE_SHIFT && blocker->state == BUILDING_CLOCK_WORKER_WAIT &&
                        blocker->position.y > blocker->rope.anchor.y && blocker->rope.anchor.x - destination.x < 9 && destination.x - blocker->rope.anchor.x < 9) {
                        /* Stable-ID yielding breaks suspended lane cycles.
                         * Retrieve first; repositioning then happens on roof. */
                        rope->phase = BUILDING_CLOCK_ROPE_ASCEND;
                        break;
                    }
                }
                return;
            }
            if (rope->phase == BUILDING_CLOCK_ROPE_SHIFT) {
                const int16_t low = worker->position.x < destination.x ? worker->position.x : destination.x;
                const int16_t high = worker->position.x > destination.x ? worker->position.x : destination.x;
                for (uint8_t other = 0U; other < simulation->state.worker_count; other++) {
                    const building_clock_worker_t* adjacent = &simulation->state.workers[other];
                    if (other != worker_id && adjacent->rope.phase != BUILDING_CLOCK_ROPE_NONE && adjacent->position.y > adjacent->rope.anchor.y && adjacent->rope.anchor.x > low &&
                        adjacent->rope.anchor.x < high) {
                        /* Do not sweep a suspended builder through another
                         * rope. Retrieve and reposition from the roof. */
                        rope->phase = BUILDING_CLOCK_ROPE_ASCEND;
                        return;
                    }
                }
            }
        } else if (rope->phase == BUILDING_CLOCK_ROPE_RETURN) {
            destination = world->navigation.nodes[worker->current_node_id].position;
        }
        worker->state = destination.y == worker->position.y ? BUILDING_CLOCK_WORKER_WALK : BUILDING_CLOCK_WORKER_CLIMB;
        const int16_t previous_y = worker->position.y;
        rope->motion_remainder_ms += elapsed_ms;
        uint32_t distance = rope->motion_remainder_ms / ROPE_MS_PER_PIXEL;
        const uint32_t anchor_distance = distance;
        rope->motion_remainder_ms %= ROPE_MS_PER_PIXEL;
        while (distance-- > 0U && (worker->position.x != destination.x || worker->position.y != destination.y)) {
            if (worker->position.y != destination.y) {
                worker->position.y = (int16_t)(worker->position.y + (worker->position.y < destination.y ? 1 : -1));
            } else {
                worker->facing = worker->position.x < destination.x ? BUILDING_CLOCK_FACING_RIGHT : BUILDING_CLOCK_FACING_LEFT;
                worker->position.x = (int16_t)(worker->position.x + (worker->position.x < destination.x ? 1 : -1));
            }
        }
        rope->moved_vertically = worker->position.y != previous_y && (rope->phase == BUILDING_CLOCK_ROPE_DESCEND || rope->phase == BUILDING_CLOCK_ROPE_ASCEND);
        if (rope->phase == BUILDING_CLOCK_ROPE_SHIFT) {
            for (uint32_t moved = 0U; moved < anchor_distance && rope->anchor.x != destination.x; moved++) {
                rope->anchor.x = (int16_t)(rope->anchor.x + (rope->anchor.x < destination.x ? 1 : -1));
            }
        }
        if (worker->position.x == destination.x && worker->position.y == destination.y && (rope->phase != BUILDING_CLOCK_ROPE_SHIFT || rope->anchor.x == destination.x)) {
            switch (rope->phase) {
            case BUILDING_CLOCK_ROPE_ALIGN:
                rope->anchor = worker->position;
                rope->phase = BUILDING_CLOCK_ROPE_DESCEND;
                break;
            case BUILDING_CLOCK_ROPE_DESCEND:
                rope->phase = BUILDING_CLOCK_ROPE_WORK;
                worker->state_elapsed_ms = 0U;
                break;
            case BUILDING_CLOCK_ROPE_SHIFT:
                rope->phase = BUILDING_CLOCK_ROPE_DESCEND;
                break;
            case BUILDING_CLOCK_ROPE_ASCEND:
                rope->phase = rope->returning ? BUILDING_CLOCK_ROPE_RETURN : BUILDING_CLOCK_ROPE_ALIGN;
                break;
            case BUILDING_CLOCK_ROPE_RETURN:
                memset(rope, 0, sizeof(*rope));
                (void)building_clock_navigation_reserve_node(world, &simulation->navigation, worker->current_node_id, worker_id);
                if (job != NULL) {
                    complete_current_job(simulation, world, worker_id);
                } else {
                    worker->state = BUILDING_CLOCK_WORKER_IDLE;
                    plan_idle_egress(simulation, world, worker_id);
                }
                break;
            default:
                break;
            }
        }
    }
    update_carried_load_positions(simulation, worker_id);
}

static void advance_worker(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id, uint32_t elapsed_ms) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    uint32_t remaining_ms = elapsed_ms;

    for (uint8_t transition = 0U; transition < MAX_WORKER_TRANSITIONS_PER_QUANTUM && remaining_ms > 0U; transition++) {
        if (worker->recovery_recalled && worker->reserved_edge_id == BUILDING_CLOCK_INVALID_EDGE_ID) {
            bool transferring = false;
            for (uint8_t id = 0U; id < simulation->state.cart_count; id++) {
                transferring |= simulation->state.carts[id].operator_worker_id == worker_id && worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_CART_TRANSFER &&
                                simulation->state.carts[id].transfer_elapsed_ms > 0U;
            }
            if (!transferring) {
                cancel_job_internal(simulation, world, worker->current_job_id, true, false);
                return;
            }
        }
        if (worker->rope.phase != BUILDING_CLOCK_ROPE_NONE) {
            advance_rope(simulation, world, worker_id, remaining_ms);
            return;
        }
        if (advance_platform_activity(simulation, world, worker_id, remaining_ms)) {
            update_carried_load_positions(simulation, worker_id);
            return;
        }
        if (worker->current_job_id == BUILDING_CLOCK_INVALID_ID && worker->route_purpose == BUILDING_CLOCK_WORKER_ROUTE_NONE) {
            worker->behavior_timer_ms += remaining_ms;
            if (simulation->state.clock_initialized && worker->behavior_timer_ms >= 1000U) {
                if (((worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_NONE || worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_REST) &&
                     choose_cart_task(simulation, world, worker_id)) ||
                    (worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_NONE && choose_platform_activity_position(simulation, world, worker_id))) {
                    (void)advance_platform_activity(simulation, world, worker_id, remaining_ms);
                    return;
                }
                static const building_clock_worker_state_kind_t activities[] = {
                    BUILDING_CLOCK_WORKER_WATCH,
                    BUILDING_CLOCK_WORKER_DRINK,
                    BUILDING_CLOCK_WORKER_SMOKE,
                    BUILDING_CLOCK_WORKER_REST,
                };
                worker->state = activities[(worker_id + worker->behavior_timer_ms / 4000U) % 4U];
                worker->action_phase = (uint8_t)((worker->behavior_timer_ms / 300U) % 4U);
            }
            return;
        }

        switch (worker->state) {
        case BUILDING_CLOCK_WORKER_CHOOSE_JOB:
        case BUILDING_CLOCK_WORKER_CARRY:
        case BUILDING_CLOCK_WORKER_YIELD_OR_TURN:
        case BUILDING_CLOCK_WORKER_AVOID_DANGER:
        case BUILDING_CLOCK_WORKER_WAIT:
            if (!begin_next_traversal(simulation, world, worker_id)) {
                fail_current_job(simulation, world, worker_id);
                return;
            }
            if (worker->state == BUILDING_CLOCK_WORKER_WAIT) {
                worker->state_elapsed_ms += remaining_ms;
                return;
            }
            break;
        case BUILDING_CLOCK_WORKER_WALK:
        case BUILDING_CLOCK_WORKER_CLIMB: {
            const uint32_t consumed = advance_traversal(simulation, world, worker_id, remaining_ms);
            remaining_ms -= consumed;
            break;
        }
        case BUILDING_CLOCK_WORKER_INSPECT:
        case BUILDING_CLOCK_WORKER_OPERATE_MACHINERY:
        case BUILDING_CLOCK_WORKER_HAMMER:
        case BUILDING_CLOCK_WORKER_CLEAR_DEBRIS:
        case BUILDING_CLOCK_WORKER_FINISH_JOB: {
            const uint32_t consumed = advance_action(simulation, world, worker_id, remaining_ms);
            remaining_ms -= consumed;
            break;
        }
        default:
            fail_current_job(simulation, world, worker_id);
            return;
        }
    }
    update_carried_load_positions(simulation, worker_id);
}

static void append_digit_section(building_clock_digit_structure_t* structure, building_clock_section_kind_t kind, building_clock_section_status_t status) {
    if (structure->section_count >= BUILDING_CLOCK_MAX_DIGIT_SECTIONS) {
        return;
    }
    structure->sections[structure->section_count++] = (building_clock_section_state_t){
        .kind = kind,
        .status = status,
        .assigned_job_id = BUILDING_CLOCK_INVALID_ID,
        .progress_per_mille = status == BUILDING_CLOCK_SECTION_SECURED ? 1000U : 0U,
    };
}

static void initialize_digit_structure(building_clock_digit_structure_t* structure, uint8_t numeral, building_clock_section_status_t status) {
    memset(structure, 0, sizeof(*structure));
    structure->numeral = numeral;

    bool top = false;
    bool upper_left = false;
    bool upper_right = false;
    bool middle = false;
    bool lower_left = false;
    bool lower_right = false;
    bool foundation = false;
    switch (numeral) {
    case 0U:
        top = upper_left = upper_right = lower_left = lower_right = foundation = true;
        break;
    case 1U:
        upper_right = lower_right = true;
        break;
    case 2U:
        top = upper_right = middle = lower_left = foundation = true;
        break;
    case 3U:
        top = upper_right = middle = lower_right = foundation = true;
        break;
    case 4U:
        upper_left = upper_right = middle = lower_right = true;
        break;
    case 5U:
        top = upper_left = middle = lower_right = foundation = true;
        break;
    case 6U:
        top = upper_left = middle = lower_left = lower_right = foundation = true;
        break;
    case 7U:
        top = upper_right = lower_right = true;
        break;
    case 8U:
        top = upper_left = upper_right = middle = lower_left = lower_right = foundation = true;
        break;
    case 9U:
        top = upper_left = upper_right = middle = lower_right = foundation = true;
        break;
    default:
        return;
    }

    if (foundation) {
        append_digit_section(structure, BUILDING_CLOCK_SECTION_FOUNDATION, status);
    }
    if (lower_left) {
        append_digit_section(structure, BUILDING_CLOCK_SECTION_LOWER_LEFT_PIER, status);
    }
    if (lower_right) {
        append_digit_section(structure, BUILDING_CLOCK_SECTION_LOWER_RIGHT_PIER, status);
    }
    if (middle) {
        append_digit_section(structure, BUILDING_CLOCK_SECTION_MIDDLE_BEAM, status);
    }
    if (upper_left) {
        append_digit_section(structure, BUILDING_CLOCK_SECTION_UPPER_LEFT_PIER, status);
    }
    if (upper_right) {
        append_digit_section(structure, BUILDING_CLOCK_SECTION_UPPER_RIGHT_PIER, status);
    }
    if (top) {
        append_digit_section(structure, BUILDING_CLOCK_SECTION_TOP_BEAM, status);
    }

    for (uint8_t section = 0U; section < structure->section_count; section++) {
        const building_clock_section_kind_t kind = structure->sections[section].kind;
        uint16_t dependencies = 0U;
        for (uint8_t previous = 0U; previous < structure->section_count; previous++) {
            const building_clock_section_kind_t support = structure->sections[previous].kind;
            bool needed = false;
            switch (kind) {
            case BUILDING_CLOCK_SECTION_MIDDLE_BEAM:
                needed = support == BUILDING_CLOCK_SECTION_LOWER_LEFT_PIER || support == BUILDING_CLOCK_SECTION_LOWER_RIGHT_PIER;
                break;
            case BUILDING_CLOCK_SECTION_UPPER_LEFT_PIER:
                needed = lower_left ? support == BUILDING_CLOCK_SECTION_LOWER_LEFT_PIER : support == BUILDING_CLOCK_SECTION_MIDDLE_BEAM;
                break;
            case BUILDING_CLOCK_SECTION_UPPER_RIGHT_PIER:
                needed = lower_right ? support == BUILDING_CLOCK_SECTION_LOWER_RIGHT_PIER : support == BUILDING_CLOCK_SECTION_MIDDLE_BEAM;
                break;
            case BUILDING_CLOCK_SECTION_TOP_BEAM:
                needed = support == BUILDING_CLOCK_SECTION_UPPER_LEFT_PIER || support == BUILDING_CLOCK_SECTION_UPPER_RIGHT_PIER;
                break;
            default:
                break;
            }
            if (needed) {
                dependencies |= (uint16_t)(1U << previous);
            }
        }
        structure->sections[section].dependency_mask = dependencies;
    }
}

bool building_clock_simulation_transition_active(const building_clock_simulation_t* simulation) {
    if (simulation == NULL) {
        return false;
    }
    for (uint8_t digit = 0U; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        if (simulation->state.digits[digit].lifecycle != BUILDING_CLOCK_DIGIT_STABLE) {
            return true;
        }
    }
    return false;
}

static bool evacuation_route_duration(const building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id, uint8_t digit_id, uint32_t* duration_ms) {
    const building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    const building_clock_digit_work_area_t* area = &world->digit_work_areas[digit_id];
    bool found = false;
    uint32_t best_duration = UINT32_MAX;
    uint8_t best_node = BUILDING_CLOCK_INVALID_ID;
    for (uint8_t safe = 0U; safe < area->safe_retreat_node_count; safe++) {
        const uint8_t safe_node = area->safe_retreat_node_ids[safe];
        if (safe_node >= world->navigation.node_count || point_in_rect(world->navigation.nodes[safe_node].position, area->hazard_area)) {
            continue;
        }
        const building_clock_route_request_t request = {
            .start_node_id = worker->current_node_id,
            .destination_node_id = safe_node,
            .allow_temporary_connections = true,
            .carries_large_load = false,
            .allow_machinery_edges = false,
        };
        building_clock_route_t route;
        if (building_clock_find_route(world, &simulation->navigation, &request, &route) != BUILDING_CLOCK_ROUTE_OK) {
            continue;
        }
        uint32_t candidate_duration = 0U;
        for (uint8_t edge = 0U; edge < route.edge_count; edge++) {
            candidate_duration += traversal_duration(world, &world->navigation.edges[route.edge_ids[edge]]);
        }
        if (!found || candidate_duration < best_duration || (candidate_duration == best_duration && safe_node < best_node)) {
            found = true;
            best_duration = candidate_duration;
            best_node = safe_node;
        }
    }
    if (found) {
        if (worker->rope.phase != BUILDING_CLOCK_ROPE_NONE) {
            const building_clock_point_t node = world->navigation.nodes[worker->current_node_id].position;
            int32_t dx = (int32_t)node.x - worker->position.x;
            best_duration += (uint32_t)((dx < 0 ? -dx : dx) + worker->position.y - worker->rope.anchor.y) * ROPE_MS_PER_PIXEL;
        } else if (worker->platform_activity_phase != BUILDING_CLOCK_PLATFORM_NONE) {
            const int32_t dx = (int32_t)world->navigation.nodes[worker->current_node_id].position.x - worker->position.x;
            best_duration += (uint32_t)(dx < 0 ? -dx : dx) * WALK_MS_PER_COST_UNIT;
        }
        *duration_ms = best_duration;
    }
    return found;
}

static building_clock_simulation_status_t digit_evacuation_lead(const building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t digit_id, uint32_t* lead_ms) {
    uint32_t longest_route_ms = 0U;
    uint8_t affected_workers = 0U;
    const building_clock_rect_t hazard = world->digit_work_areas[digit_id].hazard_area;
    for (uint8_t worker_id = 0U; worker_id < simulation->state.worker_count; worker_id++) {
        if (!point_in_rect(simulation->state.workers[worker_id].position, hazard)) {
            continue;
        }
        uint32_t route_ms = 0U;
        if (!evacuation_route_duration(simulation, world, worker_id, digit_id, &route_ms)) {
            return BUILDING_CLOCK_SIMULATION_INVALID_STATE;
        }
        affected_workers++;
        if (route_ms > longest_route_ms) {
            longest_route_ms = route_ms;
        }
    }
    uint32_t result = longest_route_ms + simulation->pacing.evacuation_margin_ms + (uint32_t)affected_workers * simulation->pacing.evacuation_contention_ms_per_worker;
    if (result < simulation->pacing.minimum_evacuation_ms) {
        result = simulation->pacing.minimum_evacuation_ms;
    }
    *lead_ms = result;
    return BUILDING_CLOCK_SIMULATION_OK;
}

building_clock_simulation_status_t building_clock_simulation_required_transition_lead(const building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t hour, uint8_t minute,
                                                                                      uint32_t* required_ms) {
    if (simulation == NULL || world == NULL || required_ms == NULL) {
        return BUILDING_CLOCK_SIMULATION_INVALID_ARGUMENT;
    }
    if (!world_is_usable(world) || !pacing_is_usable(&simulation->pacing)) {
        return BUILDING_CLOCK_SIMULATION_INVALID_STATE;
    }
    if (hour > 23U || minute > 59U) {
        return BUILDING_CLOCK_SIMULATION_INVALID_TIME;
    }
    if (!simulation->state.clock_initialized) {
        *required_ms = 0U;
        return BUILDING_CLOCK_SIMULATION_OK;
    }
    const uint8_t numerals[BUILDING_CLOCK_DIGIT_COUNT] = {
        (uint8_t)(hour / 10U),
        (uint8_t)(hour % 10U),
        (uint8_t)(minute / 10U),
        (uint8_t)(minute % 10U),
    };
    uint8_t changed_count = 0U;
    uint32_t maximum = 0U;
    for (uint8_t digit = 0U; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        if (simulation->state.digits[digit].target_digit == numerals[digit]) {
            continue;
        }
        uint32_t evacuation_ms = 0U;
        const building_clock_simulation_status_t status = digit_evacuation_lead(simulation, world, digit, &evacuation_ms);
        if (status != BUILDING_CLOCK_SIMULATION_OK) {
            return status;
        }
        const uint32_t candidate = simulation->pacing.preparation_ms + evacuation_ms + (uint32_t)changed_count * simulation->pacing.changed_digit_stagger_ms;
        if (candidate > maximum) {
            maximum = candidate;
        }
        changed_count++;
    }
    *required_ms = maximum;
    return BUILDING_CLOCK_SIMULATION_OK;
}

building_clock_simulation_status_t building_clock_simulation_set_time(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t hour, uint8_t minute) {
    if (simulation == NULL || world == NULL) {
        return BUILDING_CLOCK_SIMULATION_INVALID_ARGUMENT;
    }
    if (!world_is_usable(world)) {
        return BUILDING_CLOCK_SIMULATION_INVALID_WORLD;
    }
    if (!pacing_is_usable(&simulation->pacing)) {
        return BUILDING_CLOCK_SIMULATION_INVALID_STATE;
    }
    if (hour > 23U || minute > 59U) {
        return BUILDING_CLOCK_SIMULATION_INVALID_TIME;
    }

    const uint8_t numerals[BUILDING_CLOCK_DIGIT_COUNT] = {
        (uint8_t)(hour / 10U),
        (uint8_t)(hour % 10U),
        (uint8_t)(minute / 10U),
        (uint8_t)(minute % 10U),
    };
    if (!simulation->state.clock_initialized) {
        simulation->state.clock_initialized = true;
        simulation->state.current_hour = hour;
        simulation->state.current_minute = minute;
        simulation->state.target_hour = hour;
        simulation->state.target_minute = minute;
        for (uint8_t digit = 0U; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
            building_clock_digit_state_t* state = &simulation->state.digits[digit];
            state->current_digit = numerals[digit];
            state->target_digit = numerals[digit];
            state->lifecycle = BUILDING_CLOCK_DIGIT_STABLE;
            state->finishing_job_id = BUILDING_CLOCK_INVALID_ID;
            initialize_digit_structure(&state->outgoing, numerals[digit], BUILDING_CLOCK_SECTION_SECURED);
            initialize_digit_structure(&state->incoming, numerals[digit], BUILDING_CLOCK_SECTION_ABSENT);
        }
        return BUILDING_CLOCK_SIMULATION_OK;
    }

    uint8_t changed_count = 0U;
    if (simulation->state.target_hour != hour || simulation->state.target_minute != minute) {
        for (uint8_t job_id = 0U; job_id < BUILDING_CLOCK_MAX_JOBS; job_id++) {
            building_clock_job_t* job = &simulation->state.jobs[job_id];
            if (job->kind == BUILDING_CLOCK_JOB_PUSH_CART && job_is_live(job)) {
                if (job->reserved_worker_id < simulation->state.worker_count) {
                    /* Finish the current ladder/edge or sack transfer first;
                     * recalling a roof worker must not teleport them. */
                    simulation->state.workers[job->reserved_worker_id].recovery_recalled = true;
                } else {
                    cancel_job_internal(simulation, world, job_id, false, false);
                }
            }
        }
    }
    simulation->state.target_hour = hour;
    simulation->state.target_minute = minute;
    uint8_t changed_mask = 0U;
    for (uint8_t digit = 0U; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        building_clock_digit_state_t* state = &simulation->state.digits[digit];
        if (state->target_digit == numerals[digit]) {
            continue;
        }
        changed_mask |= (uint8_t)(1U << digit);
        /* Snapshot before cancellation returns in-transit loads to staging. */
        building_clock_digit_structure_t physical = state->incoming;
        const bool rebuilding = state->lifecycle >= BUILDING_CLOCK_DIGIT_CLEARING;
        for (uint8_t job_id = 0U; job_id < BUILDING_CLOCK_MAX_JOBS; job_id++) {
            building_clock_job_t* job = &simulation->state.jobs[job_id];
            if (job->subject.kind != BUILDING_CLOCK_JOB_SUBJECT_NONE && job->subject.digit_id == digit &&
                (job->status == BUILDING_CLOCK_JOB_ACTIVE || job->status == BUILDING_CLOCK_JOB_PENDING || job->status == BUILDING_CLOCK_JOB_RESERVED || job->status == BUILDING_CLOCK_JOB_BLOCKED)) {
                cancel_job_internal(simulation, world, job_id, false, true);
            }
        }
        for (uint8_t section = 0U; section < state->incoming.section_count; section++) {
            building_clock_load_t* load = find_section_load(simulation, digit, section);
            if (load != NULL && load->status == BUILDING_CLOCK_LOAD_STAGED && load->location_id < world->staging_area_count &&
                simulation->state.staging_inventory[load->location_id].quantity[BUILDING_CLOCK_MATERIAL_STRUCTURAL_SECTION] > 0U) {
                simulation->state.staging_inventory[load->location_id].quantity[BUILDING_CLOCK_MATERIAL_STRUCTURAL_SECTION]--;
            }
            free_section_load(simulation, digit, section);
        }
        if (rebuilding) {
            for (uint8_t section = 0U; section < physical.section_count; section++) {
                building_clock_section_state_t* piece = &physical.sections[section];
                piece->assigned_job_id = BUILDING_CLOCK_INVALID_ID;
                const bool present = piece->status == BUILDING_CLOCK_SECTION_PLACED || piece->status == BUILDING_CLOCK_SECTION_SECURED ||
                                     (piece->status == BUILDING_CLOCK_SECTION_IN_TRANSIT && piece->progress_per_mille > 0U);
                piece->status = present ? BUILDING_CLOCK_SECTION_SECURED : BUILDING_CLOCK_SECTION_DEBRIS;
            }
            state->outgoing = physical;
        }
        state->target_digit = numerals[digit];
        /* An already falling old structure keeps its identity and trajectory. */
        const bool already_collapsing = state->lifecycle == BUILDING_CLOCK_DIGIT_COLLAPSING || state->lifecycle == BUILDING_CLOCK_DIGIT_SETTLING;
        if (!already_collapsing) {
            state->lifecycle = BUILDING_CLOCK_DIGIT_PREPARING;
            state->lifecycle_elapsed_ms = 0U;
            state->transition_delay_ms = (uint32_t)changed_count * simulation->pacing.changed_digit_stagger_ms;
        }
        state->finishing_job_id = BUILDING_CLOCK_INVALID_ID;
        state->finishing_complete = false;
        initialize_digit_structure(&state->incoming, numerals[digit], BUILDING_CLOCK_SECTION_ABSENT);
        state->reconstruction_elapsed_ms = 0U;
        changed_count++;
    }
    for (uint8_t digit = 0U; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        building_clock_digit_state_t* state = &simulation->state.digits[digit];
        if ((changed_mask & (uint8_t)(1U << digit)) == 0U || state->lifecycle != BUILDING_CLOCK_DIGIT_PREPARING) {
            continue;
        }
        if (!activate_digit_hazard(simulation, world, digit)) {
            return BUILDING_CLOCK_SIMULATION_CAPACITY_EXCEEDED;
        }
        const building_clock_simulation_status_t lead_status = digit_evacuation_lead(simulation, world, digit, &state->evacuation_lead_ms);
        if (lead_status != BUILDING_CLOCK_SIMULATION_OK) {
            return lead_status;
        }
        if (state->evacuation_lead_ms > simulation->metrics.maximum_evacuation_lead_ms) {
            simulation->metrics.maximum_evacuation_lead_ms = state->evacuation_lead_ms;
        }
    }
    if (changed_count > 0U) {
        simulation->metrics.clock_changes_started++;
        simulation->metrics.digits_changed += changed_count;
    }
    return BUILDING_CLOCK_SIMULATION_OK;
}

static bool digit_hazard_is_active(const building_clock_simulation_t* simulation, uint8_t digit_id) {
    for (uint8_t hazard = 0U; hazard < simulation->state.hazard_count; hazard++) {
        if (simulation->state.hazards[hazard].kind == BUILDING_CLOCK_HAZARD_DIGIT_COLLAPSE && simulation->state.hazards[hazard].source_digit_id == digit_id) {
            return true;
        }
    }
    return false;
}

static bool activate_digit_hazard(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t digit_id) {
    if (digit_hazard_is_active(simulation, digit_id)) {
        return true;
    }
    if (simulation->state.hazard_count >= BUILDING_CLOCK_MAX_HAZARDS) {
        return false;
    }
    const building_clock_digit_work_area_t* area = &world->digit_work_areas[digit_id];
    building_clock_hazard_t* hazard = &simulation->state.hazards[simulation->state.hazard_count++];
    memset(hazard, 0, sizeof(*hazard));
    hazard->kind = BUILDING_CLOCK_HAZARD_DIGIT_COLLAPSE;
    hazard->area = area->hazard_area;
    hazard->source_digit_id = digit_id;
    hazard->safe_node_count = area->safe_retreat_node_count;
    memcpy(hazard->safe_node_ids, area->safe_retreat_node_ids, sizeof(hazard->safe_node_ids));
    return true;
}

static void deactivate_digit_hazard(building_clock_simulation_t* simulation, uint8_t digit_id) {
    for (uint8_t hazard = 0U; hazard < simulation->state.hazard_count; hazard++) {
        if (simulation->state.hazards[hazard].kind != BUILDING_CLOCK_HAZARD_DIGIT_COLLAPSE || simulation->state.hazards[hazard].source_digit_id != digit_id) {
            continue;
        }
        for (uint8_t next = (uint8_t)(hazard + 1U); next < simulation->state.hazard_count; next++) {
            simulation->state.hazards[next - 1U] = simulation->state.hazards[next];
        }
        simulation->state.hazard_count--;
        memset(&simulation->state.hazards[simulation->state.hazard_count], 0, sizeof(simulation->state.hazards[0]));
        return;
    }
}

static uint8_t section_collapse_priority(building_clock_section_kind_t kind) {
    switch (kind) {
    case BUILDING_CLOCK_SECTION_FOUNDATION:
        return 0U;
    case BUILDING_CLOCK_SECTION_LOWER_LEFT_PIER:
    case BUILDING_CLOCK_SECTION_LOWER_RIGHT_PIER:
        return 1U;
    case BUILDING_CLOCK_SECTION_MIDDLE_BEAM:
        return 2U;
    case BUILDING_CLOCK_SECTION_UPPER_LEFT_PIER:
    case BUILDING_CLOCK_SECTION_UPPER_RIGHT_PIER:
        return 3U;
    case BUILDING_CLOCK_SECTION_TOP_BEAM:
        return 4U;
    case BUILDING_CLOCK_SECTION_CORNER_OR_DIAGONAL:
    case BUILDING_CLOCK_SECTION_FINISHING_OR_INFILL:
    default:
        return 5U;
    }
}

static uint8_t find_next_secured_section(const building_clock_digit_structure_t* structure) {
    uint8_t selected = BUILDING_CLOCK_INVALID_ID;
    for (uint8_t section = 0U; section < structure->section_count; section++) {
        if (structure->sections[section].status != BUILDING_CLOCK_SECTION_SECURED) {
            continue;
        }
        if (selected == BUILDING_CLOCK_INVALID_ID || section_collapse_priority(structure->sections[section].kind) < section_collapse_priority(structure->sections[selected].kind) ||
            (section_collapse_priority(structure->sections[section].kind) == section_collapse_priority(structure->sections[selected].kind) && section < selected)) {
            selected = section;
        }
    }
    return selected;
}

static void mark_next_section_failed(building_clock_digit_structure_t* structure) {
    const uint8_t section = find_next_secured_section(structure);
    if (section != BUILDING_CLOCK_INVALID_ID) {
        structure->sections[section].status = BUILDING_CLOCK_SECTION_FAILED;
    }
}

static building_clock_point_t section_start_position(const building_clock_digit_work_area_t* area, building_clock_section_kind_t kind) {
    return building_clock_rect_center(building_clock_section_bounds(area->envelope, kind));
}

static bool landing_position_is_used(const building_clock_simulation_t* simulation, uint8_t digit_id, building_clock_point_t position) {
    for (uint8_t index = 0U; index < simulation->state.falling_section_count; index++) {
        const building_clock_falling_section_t* falling = &simulation->state.falling_sections[index];
        if (falling->source_digit_id == digit_id && falling->landing_position.x == position.x && falling->landing_position.y == position.y) {
            return true;
        }
    }
    for (uint8_t index = 0U; index < BUILDING_CLOCK_MAX_DEBRIS_PIECES; index++) {
        const building_clock_debris_t* debris = &simulation->state.debris[index];
        if (debris->status != BUILDING_CLOCK_DEBRIS_UNUSED && debris->status != BUILDING_CLOCK_DEBRIS_CLEARED && debris->source_digit_id == digit_id && debris->position.x == position.x &&
            debris->position.y == position.y) {
            return true;
        }
    }
    return false;
}

static building_clock_point_t choose_landing_position(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t digit_id, building_clock_point_t start_position) {
    static const int8_t horizontal_offsets[] = {-6, -4, -2, 0, 2, 4, 6};
    const building_clock_rect_t catch_area = world->digit_work_areas[digit_id].collapse_catch_area;
    const uint8_t first_offset = (uint8_t)building_clock_random_bounded(&simulation->transition_random, sizeof(horizontal_offsets) / sizeof(horizontal_offsets[0]));
    for (uint8_t row = 0U; row < 2U; row++) {
        for (uint8_t candidate = 0U; candidate < sizeof(horizontal_offsets) / sizeof(horizontal_offsets[0]); candidate++) {
            const uint8_t offset_id = (uint8_t)((first_offset + candidate) % (sizeof(horizontal_offsets) / sizeof(horizontal_offsets[0])));
            int16_t x = (int16_t)(start_position.x + horizontal_offsets[offset_id]);
            const int16_t minimum_x = (int16_t)(catch_area.x + 3);
            const int16_t maximum_x = (int16_t)(catch_area.x + (int16_t)catch_area.width - 4);
            if (x < minimum_x) {
                x = minimum_x;
            } else if (x > maximum_x) {
                x = maximum_x;
            }
            const building_clock_point_t position = {
                .x = x,
                .y = (int16_t)(catch_area.y + (int16_t)catch_area.height - 4 - (int16_t)row * 6),
            };
            if (!landing_position_is_used(simulation, digit_id, position)) {
                return position;
            }
        }
    }
    building_clock_point_t position = start_position;
    position.y = (int16_t)(catch_area.y + (int16_t)catch_area.height - 4);
    if (position.x < catch_area.x + 3) {
        position.x = (int16_t)(catch_area.x + 3);
    } else if (position.x > catch_area.x + (int16_t)catch_area.width - 4) {
        position.x = (int16_t)(catch_area.x + (int16_t)catch_area.width - 4);
    }
    return position;
}

static bool launch_failed_section(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t digit_id) {
    building_clock_digit_structure_t* structure = &simulation->state.digits[digit_id].outgoing;
    uint8_t section_id = BUILDING_CLOCK_INVALID_ID;
    for (uint8_t section = 0U; section < structure->section_count; section++) {
        if (structure->sections[section].status == BUILDING_CLOCK_SECTION_FAILED) {
            section_id = section;
            break;
        }
    }
    if (section_id == BUILDING_CLOCK_INVALID_ID) {
        return false;
    }
    if (simulation->state.falling_section_count >= BUILDING_CLOCK_MAX_FALLING_SECTIONS) {
        simulation->metrics.transition_capacity_delays++;
        return false;
    }

    building_clock_section_state_t* section = &structure->sections[section_id];
    const uint8_t debris_bay_id = world->digit_work_areas[digit_id].debris_bay_id;
    const building_clock_point_t start_position = section_start_position(&world->digit_work_areas[digit_id], section->kind);
    const building_clock_point_t landing_position = choose_landing_position(simulation, world, digit_id, start_position);
    building_clock_falling_section_t* falling = &simulation->state.falling_sections[simulation->state.falling_section_count++];
    *falling = (building_clock_falling_section_t){
        .section_kind = section->kind,
        .start_position = start_position,
        .landing_position = landing_position,
        .position = start_position,
        .source_digit_id = digit_id,
        .source_section_id = section_id,
        .source_numeral = structure->numeral,
        .failure_brick_id = (uint8_t)building_clock_random_bounded(&simulation->transition_random, BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION),
        .masonry_progress_per_mille = section->progress_per_mille,
        .target_debris_bay_id = debris_bay_id,
        .duration_ms = simulation->pacing.falling_section_ms + building_clock_random_bounded(&simulation->transition_random, FALLING_VARIATION_MS + 1U),
    };
    section->status = BUILDING_CLOCK_SECTION_FALLING;
    if (simulation->state.falling_section_count > simulation->metrics.peak_falling_sections) {
        simulation->metrics.peak_falling_sections = simulation->state.falling_section_count;
    }
    return true;
}

static bool create_settled_debris(building_clock_simulation_t* simulation, const building_clock_world_t* world, const building_clock_falling_section_t* falling) {
    uint8_t slot = BUILDING_CLOCK_INVALID_ID;
    for (uint8_t debris = 0U; debris < BUILDING_CLOCK_MAX_DEBRIS_PIECES; debris++) {
        if (simulation->state.debris[debris].status == BUILDING_CLOCK_DEBRIS_UNUSED) {
            slot = debris;
            break;
        }
    }
    if (slot == BUILDING_CLOCK_INVALID_ID || falling->target_debris_bay_id >= world->debris_bay_count) {
        simulation->metrics.transition_capacity_delays++;
        return false;
    }
    simulation->state.debris[slot] = (building_clock_debris_t){
        .status = BUILDING_CLOCK_DEBRIS_SETTLED,
        .material_kind = BUILDING_CLOCK_MATERIAL_LOOSE_MASONRY,
        .position = falling->landing_position,
        .source_digit_id = falling->source_digit_id,
        .source_section_id = falling->source_section_id,
        .debris_bay_id = falling->target_debris_bay_id,
        .reserved_worker_id = BUILDING_CLOCK_INVALID_ID,
        .section_kind = falling->section_kind,
        .source_numeral = falling->source_numeral,
        .failure_brick_id = falling->failure_brick_id,
        .masonry_progress_per_mille = falling->masonry_progress_per_mille,
        .horizontal_drift = (int8_t)(falling->landing_position.x - falling->start_position.x),
    };
    simulation->state.used_debris_slot_count++;
    simulation->metrics.debris_created++;
    record_site_event(simulation, BUILDING_CLOCK_SITE_RUBBLE_LANDED, falling->landing_position, falling->source_digit_id);
    if (simulation->state.used_debris_slot_count > simulation->metrics.peak_debris_pieces) {
        simulation->metrics.peak_debris_pieces = simulation->state.used_debris_slot_count;
    }
    return true;
}

static void advance_falling_sections(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint32_t elapsed_ms) {
    uint8_t falling_id = 0U;
    while (falling_id < simulation->state.falling_section_count) {
        building_clock_falling_section_t* falling = &simulation->state.falling_sections[falling_id];
        const uint32_t remaining = falling->duration_ms - falling->elapsed_ms;
        const uint32_t consumed = elapsed_ms < remaining ? elapsed_ms : remaining;
        falling->elapsed_ms += consumed;
        falling->progress_per_mille = (uint16_t)(((uint64_t)falling->elapsed_ms * 1000U) / falling->duration_ms);
        const int32_t horizontal_progress = falling->progress_per_mille;
        const int32_t vertical_progress = (horizontal_progress * horizontal_progress) / 1000;
        falling->position.x = (int16_t)(falling->start_position.x + ((int32_t)(falling->landing_position.x - falling->start_position.x) * horizontal_progress) / 1000);
        falling->position.y = (int16_t)(falling->start_position.y + ((int32_t)(falling->landing_position.y - falling->start_position.y) * vertical_progress) / 1000);
        if (falling->elapsed_ms < falling->duration_ms || !create_settled_debris(simulation, world, falling)) {
            falling_id++;
            continue;
        }
        if (falling->source_digit_id < BUILDING_CLOCK_DIGIT_COUNT) {
            building_clock_digit_structure_t* structure = &simulation->state.digits[falling->source_digit_id].outgoing;
            if (falling->source_section_id < structure->section_count) {
                structure->sections[falling->source_section_id].status = BUILDING_CLOCK_SECTION_DEBRIS;
                structure->sections[falling->source_section_id].progress_per_mille = 1000U;
            }
        }
        for (uint8_t next = (uint8_t)(falling_id + 1U); next < simulation->state.falling_section_count; next++) {
            simulation->state.falling_sections[next - 1U] = simulation->state.falling_sections[next];
        }
        simulation->state.falling_section_count--;
        memset(&simulation->state.falling_sections[simulation->state.falling_section_count], 0, sizeof(simulation->state.falling_sections[0]));
    }
}

static bool digit_collapse_complete(const building_clock_digit_state_t* digit) {
    for (uint8_t section = 0U; section < digit->outgoing.section_count; section++) {
        if (digit->outgoing.sections[section].status != BUILDING_CLOCK_SECTION_DEBRIS) {
            return false;
        }
    }
    return true;
}

static bool workers_are_safe_for_digit(const building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t digit_id) {
    const building_clock_rect_t hazard = world->digit_work_areas[digit_id].hazard_area;
    for (uint8_t worker = 0U; worker < simulation->state.worker_count; worker++) {
        const building_clock_worker_t* state = &simulation->state.workers[worker];
        if (point_in_rect(state->position, hazard) || (state->route_purpose == BUILDING_CLOCK_WORKER_ROUTE_EVACUATION && state->evacuation_digit_id == digit_id)) {
            return false;
        }
    }
    return true;
}

static bool job_is_live(const building_clock_job_t* job) {
    return job->status == BUILDING_CLOCK_JOB_PENDING || job->status == BUILDING_CLOCK_JOB_RESERVED || job->status == BUILDING_CLOCK_JOB_ACTIVE || job->status == BUILDING_CLOCK_JOB_BLOCKED;
}

static bool debris_has_job(const building_clock_simulation_t* simulation, uint8_t debris_id) {
    for (uint8_t job_id = 0U; job_id < BUILDING_CLOCK_MAX_JOBS; job_id++) {
        const building_clock_job_t* job = &simulation->state.jobs[job_id];
        if (job_is_live(job) && job->kind == BUILDING_CLOCK_JOB_CLEAR_DEBRIS && job->subject.kind == BUILDING_CLOCK_JOB_SUBJECT_DEBRIS && job->subject.object_id == debris_id) {
            return true;
        }
    }
    return false;
}

static uint8_t digit_access_node(const building_clock_world_t* world, uint8_t digit_id, building_clock_digit_access_kind_t preferred) {
    const building_clock_digit_work_area_t* area = &world->digit_work_areas[digit_id];
    for (uint8_t access = 0U; access < area->access_count; access++) {
        if (area->access[access].kind == preferred && area->access[access].nav_node_id < world->navigation.node_count) {
            return area->access[access].nav_node_id;
        }
    }
    return area->access_count > 0U ? area->access[0].nav_node_id : BUILDING_CLOCK_INVALID_ID;
}

static uint8_t digit_temp_access_node(const building_clock_world_t* world, uint8_t digit_id, uint8_t anchor_id) {
    const uint8_t digit_mask = (uint8_t)(1U << digit_id);
    for (uint8_t attachment = 0U; attachment < world->temp_attachment_set_count; attachment++) {
        const building_clock_temp_attachment_set_t* set = &world->temp_attachment_sets[attachment];
        if ((set->assigned_digit_mask & digit_mask) == 0U || anchor_id >= set->anchor_count) {
            continue;
        }
        for (uint8_t node_id = 0U; node_id < world->navigation.node_count; node_id++) {
            const building_clock_nav_node_t* node = &world->navigation.nodes[node_id];
            if (node->source_kind == BUILDING_CLOCK_NAV_SOURCE_TEMP_ATTACHMENT && node->source_id == attachment && node->source_point_id == anchor_id) {
                return node_id;
            }
        }
    }
    return BUILDING_CLOCK_INVALID_ID;
}

static uint8_t section_target_node(const building_clock_world_t* world, uint8_t digit_id, uint8_t numeral, building_clock_section_kind_t kind) {
    /* Rope access starts at an ordinary node on a roof spanning the numeral.
     * Horizontal positioning stays on that same real continuous platform. */
    const building_clock_rect_t envelope = world->digit_work_areas[digit_id].envelope;
    building_clock_rect_t limits[BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION];
    const uint8_t limit_count = building_clock_section_visible_bricks(envelope, numeral, kind, limits);
    const building_clock_point_t center = section_start_position(&world->digit_work_areas[digit_id], kind);
    uint8_t overhead = BUILDING_CLOCK_INVALID_ID;
    int32_t best_distance = INT32_MAX;
    for (uint8_t node = 0U; node < world->navigation.node_count; node++) {
        if (building_clock_navigation_node_capacity(world, node) < BUILDING_CLOCK_MAX_WORKERS || rope_platform(world, node, limits, limit_count) == BUILDING_CLOCK_INVALID_ID) {
            continue;
        }
        const building_clock_point_t position = world->navigation.nodes[node].position;
        int32_t dx = (int32_t)position.x - center.x;
        const int32_t distance = (dx < 0 ? -dx : dx) + center.y - position.y;
        if (distance < best_distance) {
            best_distance = distance;
            overhead = node;
        }
    }
    if (overhead != BUILDING_CLOCK_INVALID_ID) {
        return overhead;
    }
    uint8_t anchor_id;
    switch (kind) {
    case BUILDING_CLOCK_SECTION_FOUNDATION:
        return digit_access_node(world, digit_id, BUILDING_CLOCK_DIGIT_ACCESS_LOWER);
    case BUILDING_CLOCK_SECTION_LOWER_LEFT_PIER:
    case BUILDING_CLOCK_SECTION_LOWER_RIGHT_PIER:
        anchor_id = 2U;
        break;
    case BUILDING_CLOCK_SECTION_UPPER_LEFT_PIER:
    case BUILDING_CLOCK_SECTION_UPPER_RIGHT_PIER:
        anchor_id = 1U;
        break;
    case BUILDING_CLOCK_SECTION_TOP_BEAM:
        return digit_access_node(world, digit_id, BUILDING_CLOCK_DIGIT_ACCESS_UPPER);
    case BUILDING_CLOCK_SECTION_MIDDLE_BEAM:
    case BUILDING_CLOCK_SECTION_CORNER_OR_DIAGONAL:
    case BUILDING_CLOCK_SECTION_FINISHING_OR_INFILL:
    default:
        anchor_id = 2U;
        break;
    }
    const uint8_t temp_node = digit_temp_access_node(world, digit_id, anchor_id);
    return temp_node != BUILDING_CLOCK_INVALID_ID ? temp_node : digit_access_node(world, digit_id, BUILDING_CLOCK_DIGIT_ACCESS_LOWER);
}

static uint8_t digit_staging_area(const building_clock_world_t* world, uint8_t digit_id) {
    const uint8_t digit_mask = (uint8_t)(1U << digit_id);
    for (uint8_t staging = 0U; staging < world->staging_area_count; staging++) {
        if ((world->staging_areas[staging].assigned_digit_mask & digit_mask) != 0U && world->staging_areas[staging].access_node_id < world->navigation.node_count) {
            return staging;
        }
    }
    return BUILDING_CLOCK_INVALID_ID;
}

static uint8_t attachment_node(const building_clock_world_t* world, uint8_t attachment_id, uint8_t source_point_id) {
    for (uint8_t node_id = 0U; node_id < world->navigation.node_count; node_id++) {
        const building_clock_nav_node_t* node = &world->navigation.nodes[node_id];
        if (node->source_kind == BUILDING_CLOCK_NAV_SOURCE_TEMP_ATTACHMENT && node->source_id == attachment_id && node->source_point_id == source_point_id) {
            return node_id;
        }
    }
    return BUILDING_CLOCK_INVALID_ID;
}

static bool ensure_digit_temporary_access(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t digit_id) {
    const building_clock_digit_structure_t* incoming = &simulation->state.digits[digit_id].incoming;
    bool rope_access_sufficient = incoming->section_count > 0U;
    for (uint8_t section = 0U; section < incoming->section_count; section++) {
        building_clock_rect_t bricks[BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION];
        const building_clock_section_kind_t kind = incoming->sections[section].kind;
        const uint8_t count = building_clock_section_visible_bricks(world->digit_work_areas[digit_id].envelope, incoming->numeral, kind, bricks);
        const uint8_t node = section_target_node(world, digit_id, incoming->numeral, kind);
        rope_access_sufficient &= rope_platform(world, node, bricks, count) != BUILDING_CLOCK_INVALID_ID;
        const uint8_t staging = digit_staging_area(world, digit_id);
        if (staging < world->staging_area_count) {
            const building_clock_route_request_t request = {
                .start_node_id = world->staging_areas[staging].access_node_id,
                .destination_node_id = node,
                .allow_temporary_connections = false,
                .allow_machinery_edges = false,
            };
            building_clock_route_t route;
            rope_access_sufficient &= building_clock_find_route(world, &simulation->navigation, &request, &route) == BUILDING_CLOCK_ROUTE_OK;
        } else {
            rope_access_sufficient = false;
        }
    }
    if (rope_access_sufficient) {
        return true;
    }
    const uint8_t digit_mask = (uint8_t)(1U << digit_id);
    uint8_t attachment_id = BUILDING_CLOCK_INVALID_ID;
    for (uint8_t attachment = 0U; attachment < world->temp_attachment_set_count; attachment++) {
        if ((world->temp_attachment_sets[attachment].assigned_digit_mask & digit_mask) != 0U) {
            attachment_id = attachment;
            break;
        }
    }
    if (attachment_id == BUILDING_CLOCK_INVALID_ID) {
        return true;
    }
    for (uint8_t slot = 0U; slot < BUILDING_CLOCK_MAX_TEMP_STRUCTURES; slot++) {
        building_clock_temp_structure_t* structure = &simulation->state.temp_structures[slot];
        if (structure->status == BUILDING_CLOCK_TEMP_STRUCTURE_UNUSED) {
            continue;
        }
        if (structure->attachment_set_id != attachment_id) {
            continue;
        }
        return structure->status == BUILDING_CLOCK_TEMP_STRUCTURE_ACTIVE;
    }
    for (uint8_t slot = 0U; slot < BUILDING_CLOCK_MAX_TEMP_STRUCTURES; slot++) {
        building_clock_temp_structure_t* structure = &simulation->state.temp_structures[slot];
        if (structure->status != BUILDING_CLOCK_TEMP_STRUCTURE_UNUSED) {
            continue;
        }
        const building_clock_temp_attachment_set_t* attachment = &world->temp_attachment_sets[attachment_id];
        *structure = (building_clock_temp_structure_t){
            .kind = attachment->kind,
            .status = BUILDING_CLOCK_TEMP_STRUCTURE_PLANNED,
            .attachment_set_id = attachment_id,
            .assigned_job_id = BUILDING_CLOCK_INVALID_ID,
        };
        simulation->state.used_temp_structure_slot_count++;
        const uint8_t target_node = attachment_node(world, attachment_id, (uint8_t)(attachment->anchor_count - 1U));
        const building_clock_job_subject_t subject = {
            .kind = BUILDING_CLOCK_JOB_SUBJECT_TEMP_STRUCTURE,
            .object_id = slot,
            .digit_id = digit_id,
            .section_id = BUILDING_CLOCK_INVALID_ID,
        };
        uint8_t job_id = BUILDING_CLOCK_INVALID_ID;
        const building_clock_job_kind_t kind = attachment->kind == BUILDING_CLOCK_TEMP_STRUCTURE_LADDER ? BUILDING_CLOCK_JOB_INSTALL_TEMP_LADDER : BUILDING_CLOCK_JOB_INSTALL_TEMP_PLATFORM;
        if (add_transition_job(simulation, world, kind, subject, BUILDING_CLOCK_INVALID_ID, target_node, 45U, BUILDING_CLOCK_WORKER_CAN_RIG, simulation->pacing.temporary_install_action_ms, &job_id) !=
            BUILDING_CLOCK_SIMULATION_OK) {
            memset(structure, 0, sizeof(*structure));
            simulation->state.used_temp_structure_slot_count--;
            simulation->metrics.transition_capacity_delays++;
            return false;
        }
        structure->assigned_job_id = job_id;
        structure->status = BUILDING_CLOCK_TEMP_STRUCTURE_BUILDING;
        return false;
    }
    simulation->metrics.transition_capacity_delays++;
    return false;
}

static bool section_dependencies_secured(const building_clock_digit_structure_t* structure, const building_clock_section_state_t* section) {
    for (uint8_t dependency = 0U; dependency < structure->section_count; dependency++) {
        if ((section->dependency_mask & (uint16_t)(1U << dependency)) != 0U && structure->sections[dependency].status != BUILDING_CLOCK_SECTION_SECURED) {
            return false;
        }
    }
    return true;
}

static bool cache_nodes_share_platform(const building_clock_world_t* world, uint8_t first, uint8_t second) {
    if (first >= world->navigation.node_count || second >= world->navigation.node_count) {
        return false;
    }
    if (first == second) {
        return true;
    }
    const building_clock_point_t a = world->navigation.nodes[first].position;
    const building_clock_point_t b = world->navigation.nodes[second].position;
    for (uint8_t platform = 0U; platform < world->platform_count; platform++) {
        const building_clock_platform_t* surface = &world->platforms[platform];
        if (a.y == surface->surface_y && b.y == surface->surface_y && a.x >= surface->x_start && a.x <= surface->x_end && b.x >= surface->x_start && b.x <= surface->x_end) {
            return true;
        }
    }
    return false;
}

static uint8_t pair_pending_delivery(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t load_id) {
    building_clock_load_t* load = &simulation->state.loads[load_id];
    building_clock_digit_structure_t* structure = &simulation->state.digits[load->digit_id].incoming;
    const building_clock_section_state_t* section = &structure->sections[load->section_id];
    for (uint8_t job_id = 0U; job_id < BUILDING_CLOCK_MAX_JOBS; job_id++) {
        const building_clock_job_t* job = &simulation->state.jobs[job_id];
        if (job->status != BUILDING_CLOCK_JOB_PENDING || job->kind != BUILDING_CLOCK_JOB_STAGE_MATERIAL || job->subject.digit_id != load->digit_id ||
            job->subject.object_id >= BUILDING_CLOCK_MAX_LOADS) {
            continue;
        }
        building_clock_load_t* first = &simulation->state.loads[job->subject.object_id];
        if (first->status != BUILDING_CLOCK_LOAD_AT_DEPOT || first->companion_load_id != BUILDING_CLOCK_INVALID_ID || !cache_nodes_share_platform(world, first->access_node_id, load->access_node_id)) {
            continue;
        }
        const bool next_on_pier = section->dependency_mask == (uint16_t)(1U << first->section_id);
        const bool center_kit = structure->sections[first->section_id].kind == BUILDING_CLOCK_SECTION_FOUNDATION && section->kind == BUILDING_CLOCK_SECTION_MIDDLE_BEAM;
        if (!next_on_pier && !center_kit) {
            continue;
        }
        first->companion_load_id = load_id;
        load->access_node_id = first->access_node_id;
        simulation->metrics.paired_deliveries++;
        return job_id;
    }
    return BUILDING_CLOCK_INVALID_ID;
}

static bool stage_section_load(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t digit_id, uint8_t section_id) {
    building_clock_section_state_t* section = &simulation->state.digits[digit_id].incoming.sections[section_id];
    if (section->assigned_job_id != BUILDING_CLOCK_INVALID_ID || find_section_load(simulation, digit_id, section_id) != NULL) {
        return true;
    }
    uint8_t digit_loads = 0U;
    for (uint8_t id = 0U; id < BUILDING_CLOCK_MAX_LOADS; id++) {
        if (simulation->state.loads[id].status != BUILDING_CLOCK_LOAD_UNUSED && simulation->state.loads[id].digit_id == digit_id) {
            digit_loads++;
        }
    }
    uint8_t projects = 0U;
    for (uint8_t digit = 0U; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        if (simulation->state.digits[digit].lifecycle != BUILDING_CLOCK_DIGIT_STABLE) {
            projects++;
        }
    }
    const uint8_t supply_limit = projects == 1U ? BUILDING_CLOCK_MAX_LOADS : 3U;
    if (digit_loads >= supply_limit || simulation->state.used_load_slot_count >= BUILDING_CLOCK_MAX_LOADS) {
        return false; /* Bounded supply backpressure, not a failed transition. */
    }
    const uint8_t staging_id = digit_staging_area(world, digit_id);
    if (staging_id == BUILDING_CLOCK_INVALID_ID) {
        return false;
    }
    for (uint8_t load_id = 0U; load_id < BUILDING_CLOCK_MAX_LOADS; load_id++) {
        if (simulation->state.loads[load_id].status != BUILDING_CLOCK_LOAD_UNUSED) {
            continue;
        }
        uint8_t source_node = world->staging_areas[staging_id].access_node_id;
        const uint8_t work_node = section_target_node(world, digit_id, simulation->state.digits[digit_id].target_digit, section->kind);
        building_clock_rect_t bricks[BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION];
        const uint8_t brick_count = building_clock_section_visible_bricks(world->digit_work_areas[digit_id].envelope, simulation->state.digits[digit_id].target_digit, section->kind, bricks);
        /* Couriers deliver to the roof, not back to the base of a ladder that
         * every builder would then have to descend and climb for each section. */
        if (rope_platform(world, work_node, bricks, brick_count) != BUILDING_CLOCK_INVALID_ID) {
            const building_clock_route_request_t request = {
                .start_node_id = world->depots[0].access_node_id,
                .destination_node_id = work_node,
                .allow_temporary_connections = true,
            };
            building_clock_route_t route;
            if (building_clock_find_route(world, &simulation->navigation, &request, &route) != BUILDING_CLOCK_ROUTE_OK) {
                return false; /* Wait for the validated temporary access. */
            }
            source_node = work_node;
        }
        building_clock_point_t staged_position = world->navigation.nodes[source_node].position;
        staged_position.x = (int16_t)(staged_position.x + (section_id % 2U == 0U ? -8 : 8));
        staged_position.y = (int16_t)(staged_position.y - 7);
        simulation->state.loads[load_id] = (building_clock_load_t){
            .material_kind = BUILDING_CLOCK_MATERIAL_STRUCTURAL_SECTION,
            .status = BUILDING_CLOCK_LOAD_AT_DEPOT,
            .quantity = 3U,
            .position = staged_position,
            .digit_id = digit_id,
            .section_id = section_id,
            .location_id = staging_id,
            .access_node_id = source_node,
            .carrier_worker_id = BUILDING_CLOCK_INVALID_ID,
            .companion_load_id = BUILDING_CLOCK_INVALID_ID,
            .machinery_kind = BUILDING_CLOCK_MACHINERY_NONE,
            .machinery_id = BUILDING_CLOCK_INVALID_ID,
        };
        simulation->state.used_load_slot_count++;
        if (simulation->state.used_load_slot_count > simulation->metrics.peak_load_slots) {
            simulation->metrics.peak_load_slots = simulation->state.used_load_slot_count;
        }
        const building_clock_job_subject_t subject = {
            .kind = BUILDING_CLOCK_JOB_SUBJECT_LOAD,
            .object_id = load_id,
            .digit_id = digit_id,
            .section_id = section_id,
        };
        uint8_t job_id = pair_pending_delivery(simulation, world, load_id);
        if (job_id == BUILDING_CLOCK_INVALID_ID && add_transition_job(simulation, world, BUILDING_CLOCK_JOB_STAGE_MATERIAL, subject, world->depots[0].access_node_id, source_node, 35U,
                                                                      BUILDING_CLOCK_WORKER_CAN_BUILD, simulation->pacing.staging_action_ms, &job_id) != BUILDING_CLOCK_SIMULATION_OK) {
            memset(&simulation->state.loads[load_id], 0, sizeof(simulation->state.loads[load_id]));
            simulation->state.used_load_slot_count--;
            return false;
        }
        section->assigned_job_id = job_id;
        simulation->state.loads[load_id].position = world->navigation.nodes[world->depots[0].access_node_id].position;
        simulation->state.loads[load_id].position.y -= 7;
        if (simulation->state.depot_inventory[0].quantity[BUILDING_CLOCK_MATERIAL_STRUCTURAL_SECTION] == 0U) {
            simulation->state.depot_inventory[0].quantity[BUILDING_CLOCK_MATERIAL_STRUCTURAL_SECTION] = 64U;
        }
        simulation->state.depot_inventory[0].quantity[BUILDING_CLOCK_MATERIAL_STRUCTURAL_SECTION]--;
        return true;
    }
    simulation->metrics.transition_capacity_delays++;
    return false;
}

static void schedule_debris_clearing(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t digit_id) {
    const uint8_t bay_id = world->digit_work_areas[digit_id].debris_bay_id;
    if (bay_id >= world->debris_bay_count) {
        return;
    }
    for (uint8_t debris_id = 0U; debris_id < BUILDING_CLOCK_MAX_DEBRIS_PIECES; debris_id++) {
        const building_clock_debris_t* debris = &simulation->state.debris[debris_id];
        if (debris->status == BUILDING_CLOCK_DEBRIS_UNUSED || debris->source_digit_id != digit_id || debris_has_job(simulation, debris_id)) {
            continue;
        }
        const building_clock_job_subject_t subject = {
            .kind = BUILDING_CLOCK_JOB_SUBJECT_DEBRIS,
            .object_id = debris_id,
            .digit_id = digit_id,
            .section_id = debris->source_section_id,
        };
        const uint8_t lower_node = digit_access_node(world, digit_id, BUILDING_CLOCK_DIGIT_ACCESS_LOWER);
        const uint8_t right_node = digit_access_node(world, digit_id, BUILDING_CLOCK_DIGIT_ACCESS_RIGHT);
        uint8_t service_node = lower_node;
        if (lower_node < world->navigation.node_count && right_node < world->navigation.node_count) {
            int32_t lower_distance = (int32_t)world->navigation.nodes[lower_node].position.x - debris->position.x;
            int32_t right_distance = (int32_t)world->navigation.nodes[right_node].position.x - debris->position.x;
            lower_distance = lower_distance < 0 ? -lower_distance : lower_distance;
            right_distance = right_distance < 0 ? -right_distance : right_distance;
            service_node = right_distance < lower_distance ? right_node : lower_node;
        }
        if (debris->status == BUILDING_CLOCK_DEBRIS_RECOVERABLE) {
            int32_t nearest = INT32_MAX;
            for (uint8_t node = 0U; node < world->navigation.node_count; node++) {
                const building_clock_point_t p = world->navigation.nodes[node].position;
                int dy = p.y - debris->position.y;
                dy = dy < 0 ? -dy : dy;
                int dx = p.x - debris->position.x;
                dx = dx < 0 ? -dx : dx;
                if (dy <= 4 && dx < nearest && same_platform(world, p, (building_clock_point_t){debris->position.x, p.y})) {
                    service_node = node;
                    nearest = dx;
                }
            }
        }
        if (add_transition_job(simulation, world, BUILDING_CLOCK_JOB_CLEAR_DEBRIS, subject, service_node, world->debris_bays[bay_id].access_node_id, 50U, BUILDING_CLOCK_WORKER_CAN_CLEAR,
                               simulation->pacing.debris_clear_action_ms, NULL) != BUILDING_CLOCK_SIMULATION_OK) {
            simulation->metrics.transition_capacity_delays++;
            return;
        }
    }
}

static bool digit_has_debris(const building_clock_simulation_t* simulation, uint8_t digit_id) {
    for (uint8_t debris_id = 0U; debris_id < BUILDING_CLOCK_MAX_DEBRIS_PIECES; debris_id++) {
        if (simulation->state.debris[debris_id].status != BUILDING_CLOCK_DEBRIS_UNUSED && simulation->state.debris[debris_id].status != BUILDING_CLOCK_DEBRIS_CARRIED &&
            simulation->state.debris[debris_id].source_digit_id == digit_id) {
            return true;
        }
    }
    return false;
}

static void schedule_digit_sections(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t digit_id, uint8_t maximum_tier) {
    building_clock_digit_structure_t* structure = &simulation->state.digits[digit_id].incoming;
    for (uint8_t section_id = 0U; section_id < structure->section_count; section_id++) {
        building_clock_section_state_t* section = &structure->sections[section_id];
        if (section_collapse_priority(section->kind) > maximum_tier || !section_dependencies_secured(structure, section)) {
            continue;
        }
        if (section->status == BUILDING_CLOCK_SECTION_ABSENT) {
            (void)stage_section_load(simulation, world, digit_id, section_id);
        }
        if (section->assigned_job_id != BUILDING_CLOCK_INVALID_ID) {
            continue;
        }
        const building_clock_job_subject_t subject = {
            .kind = BUILDING_CLOCK_JOB_SUBJECT_DIGIT_SECTION,
            .object_id = BUILDING_CLOCK_INVALID_ID,
            .digit_id = digit_id,
            .section_id = section_id,
        };
        uint8_t job_id = BUILDING_CLOCK_INVALID_ID;
        if (section->status == BUILDING_CLOCK_SECTION_STAGED) {
            const uint8_t staging_id = digit_staging_area(world, digit_id);
            if (staging_id == BUILDING_CLOCK_INVALID_ID) {
                continue;
            }
            const building_clock_load_t* load = find_section_load(simulation, digit_id, section_id);
            if (load == NULL) {
                continue;
            }
            if (add_transition_job(simulation, world, BUILDING_CLOCK_JOB_PLACE_SECTION, subject, load->access_node_id, section_target_node(world, digit_id, structure->numeral, section->kind), 40U,
                                   BUILDING_CLOCK_WORKER_CAN_BUILD, simulation->pacing.section_place_action_ms, &job_id) == BUILDING_CLOCK_SIMULATION_OK) {
                section->assigned_job_id = job_id;
            }
        } else if (section->status == BUILDING_CLOCK_SECTION_PLACED &&
                   add_transition_job(simulation, world, BUILDING_CLOCK_JOB_SECURE_SECTION, subject, BUILDING_CLOCK_INVALID_ID, section_target_node(world, digit_id, structure->numeral, section->kind),
                                      45U, BUILDING_CLOCK_WORKER_CAN_BUILD, simulation->pacing.section_secure_action_ms, &job_id) == BUILDING_CLOCK_SIMULATION_OK) {
            section->assigned_job_id = job_id;
        }
    }
}

static bool digit_tiers_complete(const building_clock_digit_structure_t* structure, uint8_t maximum_tier) {
    for (uint8_t section = 0U; section < structure->section_count; section++) {
        if (section_collapse_priority(structure->sections[section].kind) <= maximum_tier && structure->sections[section].status != BUILDING_CLOCK_SECTION_SECURED) {
            return false;
        }
    }
    return true;
}

static void schedule_finishing_inspection(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t digit_id) {
    building_clock_digit_state_t* digit = &simulation->state.digits[digit_id];
    if (digit->finishing_complete || digit->finishing_job_id != BUILDING_CLOCK_INVALID_ID || digit->incoming.section_count == 0U) {
        return;
    }
    const uint8_t section_id = (uint8_t)(digit->incoming.section_count - 1U);
    const building_clock_job_subject_t subject = {
        .kind = BUILDING_CLOCK_JOB_SUBJECT_DIGIT_SECTION,
        .object_id = BUILDING_CLOCK_INVALID_ID,
        .digit_id = digit_id,
        .section_id = section_id,
    };
    uint8_t job_id = BUILDING_CLOCK_INVALID_ID;
    if (add_transition_job(simulation, world, BUILDING_CLOCK_JOB_INSPECT, subject, BUILDING_CLOCK_INVALID_ID,
                           section_target_node(world, digit_id, digit->incoming.numeral, digit->incoming.sections[section_id].kind), 30U, BUILDING_CLOCK_WORKER_CAN_INSPECT,
                           simulation->pacing.finishing_action_ms, &job_id) == BUILDING_CLOCK_SIMULATION_OK) {
        digit->finishing_job_id = job_id;
    }
}

static void remove_digit_temporary_access(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t digit_id) {
    const uint8_t digit_mask = (uint8_t)(1U << digit_id);
    for (uint8_t slot = 0U; slot < BUILDING_CLOCK_MAX_TEMP_STRUCTURES; slot++) {
        building_clock_temp_structure_t* structure = &simulation->state.temp_structures[slot];
        if (structure->status == BUILDING_CLOCK_TEMP_STRUCTURE_UNUSED || structure->attachment_set_id >= world->temp_attachment_set_count ||
            (world->temp_attachment_sets[structure->attachment_set_id].assigned_digit_mask & digit_mask) == 0U) {
            continue;
        }
        (void)building_clock_navigation_set_temp_active(world, &simulation->navigation, structure->attachment_set_id, false);
        memset(structure, 0, sizeof(*structure));
        if (simulation->state.used_temp_structure_slot_count > 0U) {
            simulation->state.used_temp_structure_slot_count--;
        }
    }
}

static bool digit_temporary_access_in_use(const building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t digit_id) {
    /* Secured masonry is usable before its builder has returned to the roof.
     * Do not replace the incoming plan while jobs still own its slots. */
    for (uint8_t job_id = 0U; job_id < BUILDING_CLOCK_MAX_JOBS; job_id++) {
        const building_clock_job_t* job = &simulation->state.jobs[job_id];
        if (job->subject.kind != BUILDING_CLOCK_JOB_SUBJECT_NONE && job->subject.digit_id == digit_id &&
            (job->status == BUILDING_CLOCK_JOB_ACTIVE || job->status == BUILDING_CLOCK_JOB_PENDING || job->status == BUILDING_CLOCK_JOB_RESERVED || job->status == BUILDING_CLOCK_JOB_BLOCKED)) {
            return true;
        }
    }
    const uint8_t digit_mask = (uint8_t)(1U << digit_id);
    for (uint8_t worker_id = 0U; worker_id < simulation->state.worker_count; worker_id++) {
        const building_clock_worker_t* worker = &simulation->state.workers[worker_id];
        if (worker->current_node_id < world->navigation.node_count) {
            const building_clock_nav_node_t* node = &world->navigation.nodes[worker->current_node_id];
            if (node->source_kind == BUILDING_CLOCK_NAV_SOURCE_TEMP_ATTACHMENT && node->source_id < world->temp_attachment_set_count &&
                (world->temp_attachment_sets[node->source_id].assigned_digit_mask & digit_mask) != 0U) {
                return true;
            }
        }
        if (worker->reserved_edge_id < world->navigation.edge_count) {
            const building_clock_nav_edge_t* edge = &world->navigation.edges[worker->reserved_edge_id];
            if (edge->kind == BUILDING_CLOCK_NAV_EDGE_TEMP_CONNECTION && edge->temp_attachment_set_id < world->temp_attachment_set_count &&
                (world->temp_attachment_sets[edge->temp_attachment_set_id].assigned_digit_mask & digit_mask) != 0U) {
                return true;
            }
        }
    }
    return false;
}

static void promote_completed_digit(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t digit_id) {
    building_clock_digit_state_t* digit = &simulation->state.digits[digit_id];
    digit->current_digit = digit->target_digit;
    digit->outgoing = digit->incoming;
    initialize_digit_structure(&digit->incoming, digit->target_digit, BUILDING_CLOCK_SECTION_ABSENT);
    digit->lifecycle = BUILDING_CLOCK_DIGIT_STABLE;
    digit->transition_delay_ms = 0U;
    digit->evacuation_lead_ms = 0U;
    digit->lifecycle_elapsed_ms = 0U;
    digit->finishing_job_id = BUILDING_CLOCK_INVALID_ID;
    digit->finishing_complete = false;
    remove_digit_temporary_access(simulation, world, digit_id);
    simulation->metrics.digits_completed++;
    record_site_event(simulation, BUILDING_CLOCK_SITE_DIGIT_COMPLETED, building_clock_rect_center(world->digit_work_areas[digit_id].envelope), digit_id);
    for (uint8_t worker = 0U; worker < simulation->state.worker_count; worker++) {
        building_clock_worker_t* crew = &simulation->state.workers[worker];
        if (crew->current_job_id == BUILDING_CLOCK_INVALID_ID && crew->route_purpose == BUILDING_CLOCK_WORKER_ROUTE_NONE && crew->rope.phase == BUILDING_CLOCK_ROPE_NONE &&
            (crew->platform_activity_phase == BUILDING_CLOCK_PLATFORM_NONE || crew->platform_activity_phase == BUILDING_CLOCK_PLATFORM_REST)) {
            crew->state = BUILDING_CLOCK_WORKER_CELEBRATE;
            crew->behavior_timer_ms = 0U;
        }
    }
}

static void recycle_finished_transition_jobs(building_clock_simulation_t* simulation) {
    for (uint8_t job_id = 0U; job_id < BUILDING_CLOCK_MAX_JOBS; job_id++) {
        const building_clock_job_t* job = &simulation->state.jobs[job_id];
        if (job->subject.kind == BUILDING_CLOCK_JOB_SUBJECT_NONE || (job->status != BUILDING_CLOCK_JOB_COMPLETE && job->status != BUILDING_CLOCK_JOB_CANCELLED)) {
            continue;
        }
        (void)building_clock_simulation_recycle_job(simulation, job_id);
    }
}

static void advance_digit_lifecycles(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint32_t elapsed_ms) {
    for (uint8_t digit_id = 0U; digit_id < BUILDING_CLOCK_DIGIT_COUNT; digit_id++) {
        building_clock_digit_state_t* digit = &simulation->state.digits[digit_id];
        if (digit->lifecycle >= BUILDING_CLOCK_DIGIT_CLEARING && digit->lifecycle <= BUILDING_CLOCK_DIGIT_COMPLETE) {
            digit->reconstruction_elapsed_ms += elapsed_ms;
        }
        /* Bag recovery can outlive foundation clearance or an interrupted job.
         * Never orphan a put-down sack just because its digit has advanced. */
        if (digit->lifecycle == BUILDING_CLOCK_DIGIT_STABLE || digit->lifecycle >= BUILDING_CLOCK_DIGIT_CLEARING) {
            schedule_debris_clearing(simulation, world, digit_id);
        }
        /* Milestones share one clock. Work that runs late must not incur a new
         * artificial ten-second hold at every subsequent lifecycle label. */
        const uint32_t foundation_at = simulation->pacing.clearing_minimum_ms + simulation->pacing.foundation_minimum_ms;
        const uint32_t structure_at = foundation_at + simulation->pacing.structure_minimum_ms;
        const uint32_t recognizable_at = structure_at + simulation->pacing.recognizable_minimum_ms;
        const uint32_t finishing_at = recognizable_at + simulation->pacing.finishing_minimum_ms;
        /* Safe supply/rigging routes can run during preparation and collapse.
         * Placement still waits for clearing, access and real dependencies. */
        if (digit->lifecycle >= BUILDING_CLOCK_DIGIT_PREPARING && digit->lifecycle <= BUILDING_CLOCK_DIGIT_RECOGNIZABLE) {
            for (uint8_t section = 0U; section < digit->incoming.section_count; section++) {
                if (digit->incoming.sections[section].status == BUILDING_CLOCK_SECTION_ABSENT) {
                    (void)stage_section_load(simulation, world, digit_id, section);
                }
            }
        }
        switch (digit->lifecycle) {
        case BUILDING_CLOCK_DIGIT_PREPARING:
            digit->lifecycle_elapsed_ms += elapsed_ms;
            if (digit->lifecycle_elapsed_ms >= simulation->pacing.preparation_ms + digit->transition_delay_ms) {
                digit->lifecycle = BUILDING_CLOCK_DIGIT_EVACUATING;
                digit->lifecycle_elapsed_ms = 0U;
            }
            break;
        case BUILDING_CLOCK_DIGIT_EVACUATING:
            digit->lifecycle_elapsed_ms += elapsed_ms;
            if (digit->lifecycle_elapsed_ms >= digit->evacuation_lead_ms && workers_are_safe_for_digit(simulation, world, digit_id)) {
                digit->lifecycle = BUILDING_CLOCK_DIGIT_COLLAPSING;
                digit->lifecycle_elapsed_ms = 0U;
                mark_next_section_failed(&digit->outgoing);
            }
            break;
        case BUILDING_CLOCK_DIGIT_COLLAPSING: {
            digit->lifecycle_elapsed_ms += elapsed_ms;
            uint8_t launched_count = 0U;
            for (uint8_t section = 0U; section < digit->outgoing.section_count; section++) {
                if (digit->outgoing.sections[section].status == BUILDING_CLOCK_SECTION_FALLING || digit->outgoing.sections[section].status == BUILDING_CLOCK_SECTION_DEBRIS) {
                    launched_count++;
                }
            }
            const uint32_t due_count = digit->lifecycle_elapsed_ms / simulation->pacing.section_failure_interval_ms;
            while (launched_count < due_count && launched_count < digit->outgoing.section_count) {
                if (!launch_failed_section(simulation, world, digit_id)) {
                    break;
                }
                launched_count++;
                mark_next_section_failed(&digit->outgoing);
            }
            if (digit_collapse_complete(digit)) {
                digit->lifecycle = BUILDING_CLOCK_DIGIT_SETTLING;
                digit->lifecycle_elapsed_ms = 0U;
            }
            break;
        }
        case BUILDING_CLOCK_DIGIT_SETTLING:
            digit->lifecycle_elapsed_ms += elapsed_ms;
            if (digit->lifecycle_elapsed_ms >= simulation->pacing.settling_ms) {
                digit->lifecycle = BUILDING_CLOCK_DIGIT_CLEARING;
                digit->lifecycle_elapsed_ms = 0U;
                deactivate_digit_hazard(simulation, digit_id);
            }
            break;
        case BUILDING_CLOCK_DIGIT_CLEARING:
            digit->lifecycle_elapsed_ms += elapsed_ms;
            const bool access_ready = ensure_digit_temporary_access(simulation, world, digit_id);
            if (!digit_has_debris(simulation, digit_id) && access_ready && digit->lifecycle_elapsed_ms >= simulation->pacing.clearing_minimum_ms) {
                digit->lifecycle = BUILDING_CLOCK_DIGIT_FOUNDATION;
                digit->lifecycle_elapsed_ms = 0U;
            }
            break;
        case BUILDING_CLOCK_DIGIT_FOUNDATION:
            digit->lifecycle_elapsed_ms += elapsed_ms;
            schedule_digit_sections(simulation, world, digit_id, 4U);
            if (digit_tiers_complete(&digit->incoming, 0U) && digit->reconstruction_elapsed_ms >= foundation_at) {
                digit->lifecycle = BUILDING_CLOCK_DIGIT_STRUCTURE;
                digit->lifecycle_elapsed_ms = 0U;
            }
            break;
        case BUILDING_CLOCK_DIGIT_STRUCTURE:
            digit->lifecycle_elapsed_ms += elapsed_ms;
            schedule_digit_sections(simulation, world, digit_id, 4U);
            if (digit_tiers_complete(&digit->incoming, 2U) && digit->reconstruction_elapsed_ms >= structure_at) {
                digit->lifecycle = BUILDING_CLOCK_DIGIT_RECOGNIZABLE;
                digit->lifecycle_elapsed_ms = 0U;
            }
            break;
        case BUILDING_CLOCK_DIGIT_RECOGNIZABLE:
            digit->lifecycle_elapsed_ms += elapsed_ms;
            schedule_digit_sections(simulation, world, digit_id, 4U);
            if (digit_tiers_complete(&digit->incoming, 4U) && digit->reconstruction_elapsed_ms >= recognizable_at) {
                digit->lifecycle = BUILDING_CLOCK_DIGIT_FINISHING;
                digit->lifecycle_elapsed_ms = 0U;
            }
            break;
        case BUILDING_CLOCK_DIGIT_FINISHING:
            digit->lifecycle_elapsed_ms += elapsed_ms;
            schedule_finishing_inspection(simulation, world, digit_id);
            if (digit->finishing_complete && digit->reconstruction_elapsed_ms >= finishing_at) {
                digit->lifecycle = BUILDING_CLOCK_DIGIT_COMPLETE;
                digit->lifecycle_elapsed_ms = 0U;
            }
            break;
        case BUILDING_CLOCK_DIGIT_COMPLETE:
            digit->lifecycle_elapsed_ms += elapsed_ms;
            if (digit->lifecycle_elapsed_ms >= simulation->pacing.complete_hold_ms && !digit_temporary_access_in_use(simulation, world, digit_id)) {
                promote_completed_digit(simulation, world, digit_id);
            }
            break;
        case BUILDING_CLOCK_DIGIT_STABLE:
        default:
            break;
        }
    }
    if (!building_clock_simulation_transition_active(simulation)) {
        simulation->state.current_hour = simulation->state.target_hour;
        simulation->state.current_minute = simulation->state.target_minute;
    }
}

static void schedule_site_recovery(building_clock_simulation_t* simulation, const building_clock_world_t* world) {
    if (building_clock_simulation_transition_active(simulation)) {
        simulation->next_recovery_dispatch_ms = simulation->state.elapsed_ms + 1500U;
        return;
    }
    if (!simulation->state.clock_initialized || simulation->state.elapsed_ms < simulation->next_recovery_dispatch_ms || world->depot_count == 0U) {
        return;
    }
    simulation->next_recovery_dispatch_ms = simulation->state.elapsed_ms + 1500U;
    for (uint8_t id = 0U; id < simulation->state.cart_count; id++) {
        const building_clock_cart_t* cart = &simulation->state.carts[id];
        const building_clock_cart_route_t* route = &world->cart_routes[id];
        if (cart->operator_worker_id != BUILDING_CLOCK_INVALID_ID || route->stop_count < 2U) {
            continue;
        }
        bool reserved = false;
        for (uint8_t job_id = 0U; job_id < BUILDING_CLOCK_MAX_JOBS; job_id++) {
            const building_clock_job_t* job = &simulation->state.jobs[job_id];
            reserved |= job_is_live(job) && job->kind == BUILDING_CLOCK_JOB_PUSH_CART && job->subject.object_id == id;
        }
        if (reserved) {
            continue;
        }
        const building_clock_point_t position = cart_position(world, id, cart);
        bool cargo_available = cart->rubble_sacks > 0U;
        for (uint8_t bay = 0U; bay < world->debris_bay_count; bay++) {
            const building_clock_debris_bay_t* b = &world->debris_bays[bay];
            if (simulation->state.recovery_sacks[bay] > 0U && b->access_node_id < world->navigation.node_count) {
                building_clock_point_t p = world->navigation.nodes[b->access_node_id].position;
                p.x = building_clock_rect_center(b->area).x;
                cargo_available |= p.x >= route->stops[0].position.x && p.x <= route->stops[route->stop_count - 1U].position.x && same_platform(world, position, p);
            }
        }
        if (!cargo_available) {
            continue;
        }
        uint8_t target = BUILDING_CLOCK_INVALID_ID;
        int nearest = INT32_MAX;
        for (uint8_t stop = 0U; stop < route->stop_count; stop++) {
            const int dx = route->stops[stop].position.x - position.x;
            const int distance = dx < 0 ? -dx : dx;
            if (route->stops[stop].nav_node_id < world->navigation.node_count && distance < nearest) {
                nearest = distance;
                target = route->stops[stop].nav_node_id;
            }
        }
        const building_clock_job_subject_t subject = {
            .kind = BUILDING_CLOCK_JOB_SUBJECT_CART,
            .object_id = id,
            .digit_id = BUILDING_CLOCK_INVALID_ID,
            .section_id = BUILDING_CLOCK_INVALID_ID,
        };
        /* At most one new background job per dispatch; real construction
         * retains every worker while it needs them. */
        (void)add_transition_job(simulation, world, BUILDING_CLOCK_JOB_PUSH_CART, subject, BUILDING_CLOCK_INVALID_ID, target, 1U, BUILDING_CLOCK_WORKER_CAN_CLEAR, 0U, NULL);
        return;
    }
}

static bool worker_route_intersects_active_hazard(const building_clock_simulation_t* simulation, const building_clock_world_t* world, const building_clock_worker_t* worker) {
    for (uint8_t node = worker->route_node_index; node < worker->route_node_count; node++) {
        if (point_in_active_hazard(simulation, world->navigation.nodes[worker->route_node_ids[node]].position)) {
            return true;
        }
    }
    for (uint8_t edge = worker->route_node_index; edge < worker->route_edge_count; edge++) {
        const building_clock_point_t from = world->navigation.nodes[worker->route_node_ids[edge]].position;
        const building_clock_point_t to = world->navigation.nodes[worker->route_node_ids[edge + 1U]].position;
        for (uint8_t hazard = 0U; hazard < simulation->state.hazard_count; hazard++) {
            if (segment_may_cross_rect(from, to, simulation->state.hazards[hazard].area)) {
                return true;
            }
        }
    }
    return false;
}

static bool plan_worker_evacuation(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id, uint8_t digit_id) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    release_transit_reservations(simulation, world, worker_id);
    return_worker_to_current_node(worker, world);
    clear_worker_route(worker);
    worker->state = BUILDING_CLOCK_WORKER_IDLE;
    (void)building_clock_navigation_reserve_node(world, &simulation->navigation, worker->current_node_id, worker_id);

    const building_clock_digit_work_area_t* area = &world->digit_work_areas[digit_id];
    building_clock_route_t best_route;
    memset(&best_route, 0, sizeof(best_route));
    bool found_route = false;
    for (uint8_t safe = 0U; safe < area->safe_retreat_node_count; safe++) {
        const uint8_t node_id = area->safe_retreat_node_ids[safe];
        if (node_id >= world->navigation.node_count || point_in_active_hazard(simulation, world->navigation.nodes[node_id].position)) {
            continue;
        }
        const building_clock_route_request_t request = {
            .start_node_id = worker->current_node_id,
            .destination_node_id = node_id,
            .allow_temporary_connections = true,
            .carries_large_load = false,
            .allow_machinery_edges = false,
        };
        building_clock_route_t route;
        if (building_clock_find_route(world, &simulation->navigation, &request, &route) != BUILDING_CLOCK_ROUTE_OK) {
            continue;
        }
        if (!found_route || route.total_cost < best_route.total_cost || (route.total_cost == best_route.total_cost && node_id < best_route.node_ids[best_route.node_count - 1U])) {
            best_route = route;
            found_route = true;
        }
    }
    if (!found_route) {
        simulation->metrics.evacuation_failures++;
        worker->state = BUILDING_CLOCK_WORKER_AVOID_DANGER;
        worker->evacuation_digit_id = digit_id;
        return false;
    }

    copy_route_to_worker(worker, &best_route);
    worker->route_purpose = BUILDING_CLOCK_WORKER_ROUTE_EVACUATION;
    worker->evacuation_digit_id = digit_id;
    worker->state = BUILDING_CLOCK_WORKER_AVOID_DANGER;
    return true;
}

static void enforce_active_hazards(building_clock_simulation_t* simulation, const building_clock_world_t* world) {
    if (simulation->state.hazard_count == 0U) {
        return;
    }
    for (uint8_t job_id = 0U; job_id < BUILDING_CLOCK_MAX_JOBS; job_id++) {
        building_clock_job_t* job = &simulation->state.jobs[job_id];
        if ((job->status == BUILDING_CLOCK_JOB_PENDING || job->status == BUILDING_CLOCK_JOB_RESERVED || job->status == BUILDING_CLOCK_JOB_ACTIVE || job->status == BUILDING_CLOCK_JOB_BLOCKED) &&
            job->target_node_id < world->navigation.node_count && point_in_active_hazard(simulation, world->navigation.nodes[job->target_node_id].position)) {
            cancel_job_internal(simulation, world, job_id, false, true);
        }
    }
    for (uint8_t worker_id = 0U; worker_id < simulation->state.worker_count; worker_id++) {
        building_clock_worker_t* worker = &simulation->state.workers[worker_id];
        if (worker->platform_activity_phase != BUILDING_CLOCK_PLATFORM_NONE) {
            if (point_in_active_hazard(simulation, worker->position)) {
                park_worker_cart(simulation, worker_id);
                worker->platform_activity_phase = BUILDING_CLOCK_PLATFORM_RETURN;
            }
            continue;
        }
        if (worker->rope.phase != BUILDING_CLOCK_ROPE_NONE) {
            if (point_in_active_hazard(simulation, worker->position) && worker->current_job_id < BUILDING_CLOCK_MAX_JOBS) {
                cancel_job_internal(simulation, world, worker->current_job_id, false, true);
            }
            continue;
        }
        if (worker->route_purpose == BUILDING_CLOCK_WORKER_ROUTE_JOB && worker_route_intersects_active_hazard(simulation, world, worker)) {
            cancel_job_internal(simulation, world, worker->current_job_id, false, true);
        }

        uint8_t hazard_digit = BUILDING_CLOCK_INVALID_ID;
        for (uint8_t hazard = 0U; hazard < simulation->state.hazard_count; hazard++) {
            if (simulation->state.hazards[hazard].kind == BUILDING_CLOCK_HAZARD_DIGIT_COLLAPSE && point_in_rect(worker->position, simulation->state.hazards[hazard].area)) {
                hazard_digit = simulation->state.hazards[hazard].source_digit_id;
                break;
            }
        }
        if (hazard_digit == BUILDING_CLOCK_INVALID_ID) {
            continue;
        }
        if (worker->current_job_id < BUILDING_CLOCK_MAX_JOBS) {
            cancel_job_internal(simulation, world, worker->current_job_id, false, true);
        }
        if (worker->route_purpose != BUILDING_CLOCK_WORKER_ROUTE_EVACUATION || worker->evacuation_digit_id != hazard_digit) {
            (void)plan_worker_evacuation(simulation, world, worker_id, hazard_digit);
        }
    }
}

static void prepare_workers_for_quantum(building_clock_simulation_t* simulation, const building_clock_world_t* world) {
    for (uint8_t worker_id = 0U; worker_id < simulation->state.worker_count; worker_id++) {
        building_clock_worker_t* worker = &simulation->state.workers[worker_id];
        if (worker->current_job_id < BUILDING_CLOCK_MAX_JOBS && simulation->state.jobs[worker->current_job_id].kind == BUILDING_CLOCK_JOB_PUSH_CART) {
            for (uint8_t job_id = 0U; job_id < BUILDING_CLOCK_MAX_JOBS; job_id++) {
                const building_clock_job_t* job = &simulation->state.jobs[job_id];
                if (job->priority > 1U && worker_can_do_job(worker, job, simulation->state.elapsed_ms)) {
                    worker->recovery_recalled = true;
                }
            }
        }
        if (worker->current_job_id == BUILDING_CLOCK_INVALID_ID && worker->rope.phase == BUILDING_CLOCK_ROPE_NONE && worker->route_purpose == BUILDING_CLOCK_WORKER_ROUTE_NONE) {
            bool transferring = false;
            for (uint8_t id = 0U; id < simulation->state.cart_count; id++) {
                transferring |= simulation->state.carts[id].operator_worker_id == worker_id && worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_CART_TRANSFER &&
                                simulation->state.carts[id].transfer_elapsed_ms > 0U;
            }
            /* Finish the sack already on the hand line before accepting a
             * different job. Dispatch can preempt between sacks (<900 ms). */
            if (transferring) {
                continue;
            }
            if (worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_WALK_OUT || worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_RETURN) {
                continue;
            }
            /* Rollover may cancel work outside the hazard on an exclusive
             * scaffold landing. Yield it too, or it can block evacuation. */
            plan_idle_egress(simulation, world, worker_id);
            if (worker->route_purpose != BUILDING_CLOCK_WORKER_ROUTE_NONE) {
                continue;
            }
            if (assign_job(simulation, world, worker_id) && worker->platform_activity_phase != BUILDING_CLOCK_PLATFORM_NONE) {
                park_worker_cart(simulation, worker_id);
                worker->platform_activity_phase = BUILDING_CLOCK_PLATFORM_RETURN;
            }
        }
    }
    for (uint8_t worker_id = 0U; worker_id < simulation->state.worker_count; worker_id++) {
        const building_clock_worker_t* worker = &simulation->state.workers[worker_id];
        if (worker->rope.phase == BUILDING_CLOCK_ROPE_NONE && worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_NONE &&
            (worker->state == BUILDING_CLOCK_WORKER_CHOOSE_JOB || worker->state == BUILDING_CLOCK_WORKER_WAIT || worker->state == BUILDING_CLOCK_WORKER_YIELD_OR_TURN ||
             worker->state == BUILDING_CLOCK_WORKER_AVOID_DANGER) &&
            worker->route_node_index < worker->route_edge_count) {
            release_current_node_for_departure(simulation, world, worker_id);
        }
    }
}

static void admit_relief_worker(building_clock_simulation_t* simulation, const building_clock_world_t* world) {
    if (simulation->state.worker_count >= simulation->crew_limit || simulation->state.worker_count >= BUILDING_CLOCK_MAX_WORKERS || simulation->state.elapsed_ms < simulation->next_crew_entry_ms ||
        !building_clock_simulation_transition_active(simulation)) {
        return;
    }
    simulation->next_crew_entry_ms = simulation->state.elapsed_ms + simulation->pacing.relief_entry_interval_ms;
    uint8_t busy = 0U;
    uint8_t pending = 0U;
    for (uint8_t worker = 0U; worker < simulation->state.worker_count; worker++) {
        if (simulation->state.workers[worker].current_job_id < BUILDING_CLOCK_MAX_JOBS) {
            busy++;
        }
    }
    for (uint8_t job = 0U; job < BUILDING_CLOCK_MAX_JOBS; job++) {
        if (simulation->state.jobs[job].status == BUILDING_CLOCK_JOB_PENDING) {
            pending++;
        }
    }
    /* Do not add spectators when the real limit is dependencies or access. */
    if (busy + 1U < simulation->state.worker_count || pending < 2U) {
        return;
    }
    const uint8_t worker_id = simulation->state.worker_count;
    for (uint8_t offset = 0U; offset < world->worker_spawn_count; offset++) {
        const uint8_t spawn = (uint8_t)((worker_id + offset) % world->worker_spawn_count);
        const uint8_t node = world->worker_spawns[spawn].nav_node_id;
        if (point_in_active_hazard(simulation, world->navigation.nodes[node].position) ||
            building_clock_navigation_reserve_node(world, &simulation->navigation, node, worker_id) != BUILDING_CLOCK_RESERVATION_ACQUIRED) {
            continue;
        }
        building_clock_worker_t* worker = &simulation->state.workers[worker_id];
        initialize_worker(worker, worker_id, world);
        worker->current_node_id = node;
        worker->position = world->navigation.nodes[node].position;
        simulation->state.worker_count++;
        simulation->metrics.relief_workers_entered++;
        return;
    }
}

static void update_peak_active_workers(building_clock_simulation_t* simulation) {
    uint8_t active_workers = 0U;
    for (uint8_t worker_id = 0U; worker_id < simulation->state.worker_count; worker_id++) {
        if (simulation->state.workers[worker_id].current_job_id != BUILDING_CLOCK_INVALID_ID) {
            active_workers++;
        }
    }
    if (active_workers > simulation->metrics.peak_active_workers) {
        simulation->metrics.peak_active_workers = active_workers;
    }
}

/* An idle colleague may help at a nearby rope, but does not own a job, move
 * into position, or affect the builder's mechanics. Useful work always wins.
 * Evaluate after all movement so cranking means actual vertical displacement,
 * not merely a descent/return phase that is blocked or already stationary. */
static void update_optional_rope_tenders(building_clock_simulation_t* simulation, const building_clock_world_t* world) {
    uint16_t assisted = 0U;
    for (uint8_t id = 0U; id < simulation->state.worker_count; id++) {
        building_clock_worker_t* worker = &simulation->state.workers[id];
        if ((worker->capabilities & BUILDING_CLOCK_WORKER_CAN_RIG) == 0U || worker->current_job_id != BUILDING_CLOCK_INVALID_ID || worker->carried_load_id != BUILDING_CLOCK_INVALID_ID ||
            worker->rope.phase != BUILDING_CLOCK_ROPE_NONE || worker->route_purpose != BUILDING_CLOCK_WORKER_ROUTE_NONE ||
            (worker->platform_activity_phase != BUILDING_CLOCK_PLATFORM_NONE && worker->platform_activity_phase != BUILDING_CLOCK_PLATFORM_REST) ||
            point_in_active_hazard(simulation, worker->position)) {
            continue;
        }
        bool useful_work = false;
        for (uint8_t job_id = 0U; job_id < BUILDING_CLOCK_MAX_JOBS; job_id++) {
            useful_work |= worker_can_do_job(worker, &simulation->state.jobs[job_id], simulation->state.elapsed_ms);
        }
        if (useful_work) {
            continue;
        }
        for (uint8_t builder_id = 0U; builder_id < simulation->state.worker_count; builder_id++) {
            const building_clock_worker_t* builder = &simulation->state.workers[builder_id];
            const building_clock_rope_t* rope = &builder->rope;
            if (!rope->moved_vertically || rope->phase == BUILDING_CLOCK_ROPE_NONE || rope->platform_id >= world->platform_count || builder->position.y <= rope->anchor.y ||
                (assisted & (uint16_t)(1U << builder_id)) != 0U) {
                continue;
            }
            const building_clock_platform_t* platform = &world->platforms[rope->platform_id];
            int32_t dx = (int32_t)worker->position.x - rope->anchor.x;
            dx = dx < 0 ? -dx : dx;
            if (worker->position.y == platform->surface_y && worker->position.x >= platform->x_start && worker->position.x <= platform->x_end && dx >= 14 && dx <= 64) {
                worker->tended_builder_id = builder_id;
                worker->state = BUILDING_CLOCK_WORKER_OPERATE_MACHINERY;
                assisted |= (uint16_t)(1U << builder_id);
                break;
            }
        }
    }
}

static void update_platform_passing(building_clock_simulation_t* simulation, const building_clock_world_t* world) {
    for (uint8_t id = 0U; id < simulation->state.worker_count; id++) {
        simulation->state.workers[id].passing_side = 0;
    }
    for (uint8_t id = 0U; id < simulation->state.worker_count; id++) {
        building_clock_worker_t* a = &simulation->state.workers[id];
        if (a->state != BUILDING_CLOCK_WORKER_WALK || a->rope.phase != BUILDING_CLOCK_ROPE_NONE || building_clock_navigation_node_capacity(world, a->current_node_id) <= 1U) {
            continue;
        }
        for (uint8_t other = (uint8_t)(id + 1U); other < simulation->state.worker_count; other++) {
            building_clock_worker_t* b = &simulation->state.workers[other];
            const int dx = a->position.x - b->position.x;
            if (b->state == BUILDING_CLOCK_WORKER_WALK && b->rope.phase == BUILDING_CLOCK_ROPE_NONE && a->facing != b->facing && dx > -20 && dx < 20 &&
                building_clock_navigation_node_capacity(world, b->current_node_id) > 1U && same_platform(world, a->position, b->position)) {
                a->passing_side = a->facing == BUILDING_CLOCK_FACING_RIGHT ? 1 : -1;
                b->passing_side = (int8_t)-a->passing_side;
            }
        }
    }
}

building_clock_simulation_step_result_t building_clock_simulation_step(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint32_t elapsed_ms) {
    building_clock_simulation_step_result_t result = {
        .status = BUILDING_CLOCK_SIMULATION_OK,
        .requested_ms = elapsed_ms,
        .processed_ms = elapsed_ms,
        .unconsumed_ms = 0U,
    };
    if (simulation == NULL || world == NULL) {
        result.status = BUILDING_CLOCK_SIMULATION_INVALID_ARGUMENT;
        result.processed_ms = 0U;
        result.unconsumed_ms = elapsed_ms;
        return result;
    }
    if (!world_is_usable(world) || !pacing_is_usable(&simulation->pacing) || simulation->state.worker_count == 0U || simulation->state.worker_count > BUILDING_CLOCK_MAX_WORKERS) {
        result.status = BUILDING_CLOCK_SIMULATION_INVALID_STATE;
        result.processed_ms = 0U;
        result.unconsumed_ms = elapsed_ms;
        return result;
    }
    if (result.processed_ms > BUILDING_CLOCK_SIMULATION_MAX_STEP_MS) {
        result.processed_ms = BUILDING_CLOCK_SIMULATION_MAX_STEP_MS;
        result.unconsumed_ms = elapsed_ms - result.processed_ms;
        result.status = BUILDING_CLOCK_SIMULATION_DELTA_CLAMPED;
    }

    uint32_t remaining_ms = result.processed_ms;
    while (remaining_ms > 0U) {
        const uint32_t quantum_ms = remaining_ms < BUILDING_CLOCK_SIMULATION_QUANTUM_MS ? remaining_ms : BUILDING_CLOCK_SIMULATION_QUANTUM_MS;
        for (uint8_t id = 0U; id < simulation->state.worker_count; id++) {
            building_clock_worker_t* worker = &simulation->state.workers[id];
            worker->rope.moved_vertically = false;
            worker->tended_builder_id = BUILDING_CLOCK_INVALID_ID;
            if (worker->state == BUILDING_CLOCK_WORKER_OPERATE_MACHINERY && worker->current_job_id == BUILDING_CLOCK_INVALID_ID) {
                worker->state = BUILDING_CLOCK_WORKER_WATCH;
            }
        }
        simulation->state.elapsed_ms += quantum_ms;
        recycle_finished_transition_jobs(simulation);
        advance_falling_sections(simulation, world, quantum_ms);
        advance_digit_lifecycles(simulation, world, quantum_ms);
        enforce_active_hazards(simulation, world);
        schedule_site_recovery(simulation, world);
        prepare_workers_for_quantum(simulation, world);
        admit_relief_worker(simulation, world);
        for (uint8_t worker_id = 0U; worker_id < simulation->state.worker_count; worker_id++) {
            advance_worker(simulation, world, worker_id, quantum_ms);
        }
        update_optional_rope_tenders(simulation, world);
        update_platform_passing(simulation, world);
        update_peak_active_workers(simulation);
        remaining_ms -= quantum_ms;
    }
    return result;
}

const char* building_clock_simulation_status_name(building_clock_simulation_status_t status) {
    switch (status) {
    case BUILDING_CLOCK_SIMULATION_OK:
        return "ok";
    case BUILDING_CLOCK_SIMULATION_DELTA_CLAMPED:
        return "elapsed time clamped";
    case BUILDING_CLOCK_SIMULATION_INVALID_ARGUMENT:
        return "invalid argument";
    case BUILDING_CLOCK_SIMULATION_INVALID_WORLD:
        return "invalid world";
    case BUILDING_CLOCK_SIMULATION_INVALID_STATE:
        return "invalid simulation state";
    case BUILDING_CLOCK_SIMULATION_INVALID_TIME:
        return "invalid clock time";
    case BUILDING_CLOCK_SIMULATION_TRANSITION_ACTIVE:
        return "digit transition active";
    case BUILDING_CLOCK_SIMULATION_CAPACITY_EXCEEDED:
        return "capacity exceeded";
    case BUILDING_CLOCK_SIMULATION_UNSUPPORTED_JOB:
        return "unsupported job";
    case BUILDING_CLOCK_SIMULATION_UNSAFE_TARGET:
        return "job target is inside an active hazard";
    default:
        return "unknown simulation status";
    }
}
