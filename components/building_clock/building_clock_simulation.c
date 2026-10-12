#include "building_clock_simulation.h"

#include <stddef.h>
#include <string.h>

#define WALK_MS_PER_COST_UNIT 12U
#define CLIMB_MS_PER_COST_UNIT 20U
#define TEMP_MS_PER_COST_UNIT 16U
#define MINIMUM_TRAVERSAL_MS 100U
#define DEFAULT_PATROL_ACTION_MS 200U
#define DEFAULT_INSPECT_ACTION_MS 800U
#define DEFAULT_HAMMER_ACTION_MS 1000U
#define MAX_WORKER_TRANSITIONS_PER_QUANTUM 8U
#define FALLING_VARIATION_MS 200U

static const building_clock_transition_pacing_t s_default_transition_pacing = {
    .preparation_ms = 500U,
    .minimum_evacuation_ms = 250U,
    .section_failure_interval_ms = 200U,
    .falling_section_ms = 700U,
    .settling_ms = 500U,
    .changed_digit_stagger_ms = 300U,
};

_Static_assert(BUILDING_CLOCK_MAX_NAV_EDGES <= UINT8_MAX, "worker route edge IDs must fit uint8_t");
_Static_assert(BUILDING_CLOCK_MAX_WORKER_ROUTE_NODES == BUILDING_CLOCK_MAX_ROUTE_NODES, "worker and route node capacities must match");
_Static_assert(BUILDING_CLOCK_MAX_WORKER_ROUTE_EDGES == BUILDING_CLOCK_MAX_ROUTE_EDGES, "worker and route edge capacities must match");

static void copy_route_to_worker(building_clock_worker_t* worker, const building_clock_route_t* route);

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
    return pacing->section_failure_interval_ms > 0U && pacing->falling_section_ms > 0U;
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
    worker->capabilities = BUILDING_CLOCK_WORKER_CAN_BUILD | BUILDING_CLOCK_WORKER_CAN_INSPECT;
    worker->state = BUILDING_CLOCK_WORKER_IDLE;
    worker->position = world->navigation.nodes[node_id].position;
    worker->facing = BUILDING_CLOCK_FACING_RIGHT;
    worker->current_job_id = BUILDING_CLOCK_INVALID_ID;
    worker->carried_load_id = BUILDING_CLOCK_INVALID_ID;
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
    simulation->state.cart_count = world->cart_route_count;
    simulation->state.crane_count = world->crane_count;
    simulation->state.lift_count = world->lift_count;

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
            .target_node_id = spec->target_node_id,
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
    release_transit_reservations(simulation, world, worker_id);
    return_worker_to_current_node(worker, world);
    (void)building_clock_navigation_reserve_node(world, &simulation->navigation, worker->current_node_id, worker_id);
    worker->current_job_id = BUILDING_CLOCK_INVALID_ID;
    worker->state = BUILDING_CLOCK_WORKER_IDLE;
    worker->action_phase = 0U;
    clear_worker_route(worker);
}

static void plan_idle_egress(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    if (building_clock_navigation_node_capacity(world, worker->current_node_id) >= BUILDING_CLOCK_MAX_WORKERS) {
        return;
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

static void clear_job_owner_references(building_clock_simulation_t* simulation, uint8_t job_id) {
    for (uint8_t digit = 0U; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
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

static void cancel_job_internal(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t job_id, bool plan_egress, bool interrupted_by_transition) {
    building_clock_job_t* job = &simulation->state.jobs[job_id];
    const uint8_t worker_id = job->reserved_worker_id;
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
        const building_clock_route_request_t request = {
            .start_node_id = worker->current_node_id,
            .destination_node_id = job->target_node_id,
            .allow_temporary_connections = true,
            .carries_large_load = false,
            .allow_machinery_edges = false,
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
        const building_clock_job_t* best_job = best_job_id == BUILDING_CLOCK_INVALID_ID ? NULL : &simulation->state.jobs[best_job_id];
        if (job_is_better(job, route.total_cost, job_id, best_job, best_cost, best_job_id)) {
            best_job_id = job_id;
            best_cost = route.total_cost;
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
    case BUILDING_CLOCK_JOB_INSPECT:
        return DEFAULT_INSPECT_ACTION_MS;
    case BUILDING_CLOCK_JOB_SECURE_SECTION:
        return DEFAULT_HAMMER_ACTION_MS;
    case BUILDING_CLOCK_JOB_PATROL:
    default:
        return DEFAULT_PATROL_ACTION_MS;
    }
}

static void begin_job_action(building_clock_simulation_t* simulation, uint8_t worker_id) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    const building_clock_job_t* job = &simulation->state.jobs[worker->current_job_id];
    worker->state_elapsed_ms = 0U;
    worker->action_phase = 0U;
    switch (job->kind) {
    case BUILDING_CLOCK_JOB_INSPECT:
        worker->state = BUILDING_CLOCK_WORKER_INSPECT;
        break;
    case BUILDING_CLOCK_JOB_SECURE_SECTION:
        worker->state = BUILDING_CLOCK_WORKER_HAMMER;
        break;
    case BUILDING_CLOCK_JOB_PATROL:
    default:
        worker->state = BUILDING_CLOCK_WORKER_FINISH_JOB;
        break;
    }
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

static bool begin_next_traversal(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    if (worker->route_node_index >= worker->route_edge_count) {
        if (worker->route_purpose == BUILDING_CLOCK_WORKER_ROUTE_JOB && worker->current_job_id < BUILDING_CLOCK_MAX_JOBS) {
            begin_job_action(simulation, worker_id);
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
    if (edge->kind == BUILDING_CLOCK_NAV_EDGE_CLIMB || (edge->kind == BUILDING_CLOCK_NAV_EDGE_TEMP_CONNECTION && edge->temp_attachment_set_id < world->temp_attachment_set_count &&
                                                        world->temp_attachment_sets[edge->temp_attachment_set_id].kind == BUILDING_CLOCK_TEMP_STRUCTURE_LADDER)) {
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
    const uint32_t consumed = available_ms < remaining ? available_ms : remaining;
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
    return consumed;
}

static void complete_current_job(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    if (worker->current_job_id < BUILDING_CLOCK_MAX_JOBS) {
        building_clock_job_t* job = &simulation->state.jobs[worker->current_job_id];
        job->status = BUILDING_CLOCK_JOB_COMPLETE;
        job->reserved_worker_id = BUILDING_CLOCK_INVALID_ID;
        clear_job_owner_references(simulation, worker->current_job_id);
        simulation->metrics.jobs_completed++;
    }
    clear_worker_job(simulation, world, worker_id);
    plan_idle_egress(simulation, world, worker_id);
}

static uint32_t advance_action(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id, uint32_t available_ms) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    const building_clock_job_t* job = &simulation->state.jobs[worker->current_job_id];
    const uint32_t duration = action_duration(job);
    const uint32_t remaining = duration - worker->state_elapsed_ms;
    const uint32_t consumed = available_ms < remaining ? available_ms : remaining;
    worker->state_elapsed_ms += consumed;
    worker->action_phase = (uint8_t)(((uint64_t)worker->state_elapsed_ms * 4U) / duration);
    if (worker->state_elapsed_ms >= duration) {
        complete_current_job(simulation, world, worker_id);
    }
    return consumed;
}

static void fail_current_job(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id) {
    const uint8_t job_id = simulation->state.workers[worker_id].current_job_id;
    if (job_id < BUILDING_CLOCK_MAX_JOBS) {
        simulation->state.jobs[job_id].status = BUILDING_CLOCK_JOB_CANCELLED;
        simulation->state.jobs[job_id].reserved_worker_id = BUILDING_CLOCK_INVALID_ID;
        clear_job_owner_references(simulation, job_id);
        simulation->metrics.jobs_cancelled++;
    }
    clear_worker_job(simulation, world, worker_id);
    plan_idle_egress(simulation, world, worker_id);
}

static void advance_worker(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id, uint32_t elapsed_ms) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    uint32_t remaining_ms = elapsed_ms;

    for (uint8_t transition = 0U; transition < MAX_WORKER_TRANSITIONS_PER_QUANTUM && remaining_ms > 0U; transition++) {
        if (worker->current_job_id == BUILDING_CLOCK_INVALID_ID && worker->state == BUILDING_CLOCK_WORKER_IDLE) {
            worker->behavior_timer_ms += remaining_ms;
            return;
        }

        switch (worker->state) {
        case BUILDING_CLOCK_WORKER_CHOOSE_JOB:
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
        case BUILDING_CLOCK_WORKER_HAMMER:
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
    if (building_clock_simulation_transition_active(simulation)) {
        return BUILDING_CLOCK_SIMULATION_TRANSITION_ACTIVE;
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
            initialize_digit_structure(&state->outgoing, numerals[digit], BUILDING_CLOCK_SECTION_SECURED);
            initialize_digit_structure(&state->incoming, numerals[digit], BUILDING_CLOCK_SECTION_ABSENT);
        }
        return BUILDING_CLOCK_SIMULATION_OK;
    }

    uint8_t changed_count = 0U;
    simulation->state.target_hour = hour;
    simulation->state.target_minute = minute;
    for (uint8_t digit = 0U; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        building_clock_digit_state_t* state = &simulation->state.digits[digit];
        state->target_digit = numerals[digit];
        if (state->current_digit == numerals[digit]) {
            continue;
        }
        state->lifecycle = BUILDING_CLOCK_DIGIT_PREPARING;
        state->lifecycle_elapsed_ms = 0U;
        state->transition_delay_ms = (uint32_t)changed_count * simulation->pacing.changed_digit_stagger_ms;
        initialize_digit_structure(&state->incoming, numerals[digit], BUILDING_CLOCK_SECTION_ABSENT);
        changed_count++;
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
        structure->sections[section].progress_per_mille = 0U;
    }
}

static building_clock_point_t section_start_position(const building_clock_digit_work_area_t* area, building_clock_section_kind_t kind) {
    const int16_t left = area->envelope.x;
    const int16_t right = (int16_t)(area->envelope.x + (int16_t)area->envelope.width);
    const int16_t center_x = (int16_t)(area->envelope.x + (int16_t)(area->envelope.width / 2U));
    const int16_t top = area->envelope.y;
    const int16_t middle_y = (int16_t)(area->envelope.y + (int16_t)(area->envelope.height / 2U));
    const int16_t bottom = (int16_t)(area->envelope.y + (int16_t)area->envelope.height);
    switch (kind) {
    case BUILDING_CLOCK_SECTION_FOUNDATION:
        return (building_clock_point_t){.x = center_x, .y = bottom};
    case BUILDING_CLOCK_SECTION_TOP_BEAM:
        return (building_clock_point_t){.x = center_x, .y = top};
    case BUILDING_CLOCK_SECTION_MIDDLE_BEAM:
        return (building_clock_point_t){.x = center_x, .y = middle_y};
    case BUILDING_CLOCK_SECTION_UPPER_LEFT_PIER:
        return (building_clock_point_t){.x = left, .y = (int16_t)((top + middle_y) / 2)};
    case BUILDING_CLOCK_SECTION_UPPER_RIGHT_PIER:
        return (building_clock_point_t){.x = right, .y = (int16_t)((top + middle_y) / 2)};
    case BUILDING_CLOCK_SECTION_LOWER_LEFT_PIER:
        return (building_clock_point_t){.x = left, .y = (int16_t)((middle_y + bottom) / 2)};
    case BUILDING_CLOCK_SECTION_LOWER_RIGHT_PIER:
        return (building_clock_point_t){.x = right, .y = (int16_t)((middle_y + bottom) / 2)};
    case BUILDING_CLOCK_SECTION_CORNER_OR_DIAGONAL:
    case BUILDING_CLOCK_SECTION_FINISHING_OR_INFILL:
    default:
        return (building_clock_point_t){.x = center_x, .y = middle_y};
    }
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
    const int32_t horizontal_velocity = (int32_t)building_clock_random_bounded(&simulation->transition_random, 25U) - 12;
    building_clock_falling_section_t* falling = &simulation->state.falling_sections[simulation->state.falling_section_count++];
    *falling = (building_clock_falling_section_t){
        .section_kind = section->kind,
        .position = section_start_position(&world->digit_work_areas[digit_id], section->kind),
        .velocity = {.x = (int16_t)horizontal_velocity, .y = (int16_t)(45 + building_clock_random_bounded(&simulation->transition_random, 21U))},
        .angular_velocity_degrees_per_second = (int16_t)(30 + building_clock_random_bounded(&simulation->transition_random, 91U)),
        .source_digit_id = digit_id,
        .source_section_id = section_id,
        .target_debris_bay_id = world->digit_work_areas[digit_id].debris_bay_id,
        .duration_ms = simulation->pacing.falling_section_ms + building_clock_random_bounded(&simulation->transition_random, FALLING_VARIATION_MS + 1U),
    };
    if ((building_clock_random_next(&simulation->transition_random) & 1U) != 0U) {
        falling->angular_velocity_degrees_per_second = (int16_t)-falling->angular_velocity_degrees_per_second;
    }
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
    const building_clock_rect_t bay = world->debris_bays[falling->target_debris_bay_id].area;
    simulation->state.debris[slot] = (building_clock_debris_t){
        .status = BUILDING_CLOCK_DEBRIS_SETTLED,
        .material_kind = BUILDING_CLOCK_MATERIAL_LOOSE_MASONRY,
        .position =
            {
                .x = (int16_t)(bay.x + (int16_t)building_clock_random_bounded(&simulation->transition_random, bay.width)),
                .y = (int16_t)(bay.y + (int16_t)building_clock_random_bounded(&simulation->transition_random, bay.height)),
            },
        .source_digit_id = falling->source_digit_id,
        .source_section_id = falling->source_section_id,
        .debris_bay_id = falling->target_debris_bay_id,
        .reserved_worker_id = BUILDING_CLOCK_INVALID_ID,
    };
    simulation->state.used_debris_slot_count++;
    simulation->metrics.debris_created++;
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
        falling->position.x = (int16_t)(falling->position.x + ((int32_t)falling->velocity.x * (int32_t)consumed) / 1000);
        falling->position.y = (int16_t)(falling->position.y + ((int32_t)falling->velocity.y * (int32_t)consumed) / 1000);
        falling->angle_degrees = (int16_t)(falling->angle_degrees + ((int32_t)falling->angular_velocity_degrees_per_second * (int32_t)consumed) / 1000);
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

static void advance_digit_lifecycles(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint32_t elapsed_ms) {
    for (uint8_t digit_id = 0U; digit_id < BUILDING_CLOCK_DIGIT_COUNT; digit_id++) {
        building_clock_digit_state_t* digit = &simulation->state.digits[digit_id];
        switch (digit->lifecycle) {
        case BUILDING_CLOCK_DIGIT_PREPARING:
            digit->lifecycle_elapsed_ms += elapsed_ms;
            if (digit->lifecycle_elapsed_ms >= simulation->pacing.preparation_ms + digit->transition_delay_ms) {
                if (activate_digit_hazard(simulation, world, digit_id)) {
                    digit->lifecycle = BUILDING_CLOCK_DIGIT_EVACUATING;
                    digit->lifecycle_elapsed_ms = 0U;
                } else {
                    simulation->metrics.transition_capacity_delays++;
                }
            }
            break;
        case BUILDING_CLOCK_DIGIT_EVACUATING:
            digit->lifecycle_elapsed_ms += elapsed_ms;
            if (digit->lifecycle_elapsed_ms >= simulation->pacing.minimum_evacuation_ms && workers_are_safe_for_digit(simulation, world, digit_id)) {
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
        case BUILDING_CLOCK_DIGIT_STABLE:
        case BUILDING_CLOCK_DIGIT_CLEARING:
        case BUILDING_CLOCK_DIGIT_FOUNDATION:
        case BUILDING_CLOCK_DIGIT_STRUCTURE:
        case BUILDING_CLOCK_DIGIT_RECOGNIZABLE:
        case BUILDING_CLOCK_DIGIT_FINISHING:
        case BUILDING_CLOCK_DIGIT_COMPLETE:
        default:
            break;
        }
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
        if (worker->current_job_id == BUILDING_CLOCK_INVALID_ID && worker->state == BUILDING_CLOCK_WORKER_IDLE) {
            (void)assign_job(simulation, world, worker_id);
        }
    }
    for (uint8_t worker_id = 0U; worker_id < simulation->state.worker_count; worker_id++) {
        const building_clock_worker_t* worker = &simulation->state.workers[worker_id];
        if ((worker->state == BUILDING_CLOCK_WORKER_CHOOSE_JOB || worker->state == BUILDING_CLOCK_WORKER_WAIT || worker->state == BUILDING_CLOCK_WORKER_YIELD_OR_TURN ||
             worker->state == BUILDING_CLOCK_WORKER_AVOID_DANGER) &&
            worker->route_node_index < worker->route_edge_count) {
            release_current_node_for_departure(simulation, world, worker_id);
        }
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
        simulation->state.elapsed_ms += quantum_ms;
        advance_falling_sections(simulation, world, quantum_ms);
        advance_digit_lifecycles(simulation, world, quantum_ms);
        enforce_active_hazards(simulation, world);
        prepare_workers_for_quantum(simulation, world);
        for (uint8_t worker_id = 0U; worker_id < simulation->state.worker_count; worker_id++) {
            advance_worker(simulation, world, worker_id, quantum_ms);
        }
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
