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

static void clear_worker_route(building_clock_worker_t* worker) {
    worker->destination_node_id = BUILDING_CLOCK_INVALID_ID;
    worker->route_node_count = 0U;
    worker->route_edge_count = 0U;
    worker->route_node_index = 0U;
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
        worker->state = BUILDING_CLOCK_WORKER_YIELD_OR_TURN;
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

    const uint8_t worker_id = job->reserved_worker_id;
    if (worker_id < simulation->state.worker_count && simulation->state.workers[worker_id].current_job_id == job_id) {
        clear_worker_job(simulation, world, worker_id);
        plan_idle_egress(simulation, world, worker_id);
    }
    job->status = BUILDING_CLOCK_JOB_CANCELLED;
    job->reserved_worker_id = BUILDING_CLOCK_INVALID_ID;
    simulation->metrics.jobs_cancelled++;
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
        if (worker->current_job_id < BUILDING_CLOCK_MAX_JOBS) {
            begin_job_action(simulation, worker_id);
        } else {
            worker->state = BUILDING_CLOCK_WORKER_IDLE;
            clear_worker_route(worker);
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
        worker->state = worker->current_job_id < BUILDING_CLOCK_MAX_JOBS ? BUILDING_CLOCK_WORKER_CHOOSE_JOB : BUILDING_CLOCK_WORKER_YIELD_OR_TURN;
    }
    return consumed;
}

static void complete_current_job(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    if (worker->current_job_id < BUILDING_CLOCK_MAX_JOBS) {
        building_clock_job_t* job = &simulation->state.jobs[worker->current_job_id];
        job->status = BUILDING_CLOCK_JOB_COMPLETE;
        job->reserved_worker_id = BUILDING_CLOCK_INVALID_ID;
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

static void prepare_workers_for_quantum(building_clock_simulation_t* simulation, const building_clock_world_t* world) {
    for (uint8_t worker_id = 0U; worker_id < simulation->state.worker_count; worker_id++) {
        building_clock_worker_t* worker = &simulation->state.workers[worker_id];
        if (worker->current_job_id == BUILDING_CLOCK_INVALID_ID && worker->state == BUILDING_CLOCK_WORKER_IDLE) {
            (void)assign_job(simulation, world, worker_id);
        }
    }
    for (uint8_t worker_id = 0U; worker_id < simulation->state.worker_count; worker_id++) {
        const building_clock_worker_t* worker = &simulation->state.workers[worker_id];
        if ((worker->state == BUILDING_CLOCK_WORKER_CHOOSE_JOB || worker->state == BUILDING_CLOCK_WORKER_WAIT || worker->state == BUILDING_CLOCK_WORKER_YIELD_OR_TURN) &&
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
    };
    if (simulation == NULL || world == NULL) {
        result.status = BUILDING_CLOCK_SIMULATION_INVALID_ARGUMENT;
        result.processed_ms = 0U;
        return result;
    }
    if (!world_is_usable(world) || simulation->state.worker_count == 0U || simulation->state.worker_count > BUILDING_CLOCK_MAX_WORKERS) {
        result.status = BUILDING_CLOCK_SIMULATION_INVALID_STATE;
        result.processed_ms = 0U;
        return result;
    }
    if (result.processed_ms > BUILDING_CLOCK_SIMULATION_MAX_STEP_MS) {
        result.processed_ms = BUILDING_CLOCK_SIMULATION_MAX_STEP_MS;
        result.status = BUILDING_CLOCK_SIMULATION_DELTA_CLAMPED;
    }

    uint32_t remaining_ms = result.processed_ms;
    while (remaining_ms > 0U) {
        const uint32_t quantum_ms = remaining_ms < BUILDING_CLOCK_SIMULATION_QUANTUM_MS ? remaining_ms : BUILDING_CLOCK_SIMULATION_QUANTUM_MS;
        simulation->state.elapsed_ms += quantum_ms;
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
    case BUILDING_CLOCK_SIMULATION_CAPACITY_EXCEEDED:
        return "capacity exceeded";
    case BUILDING_CLOCK_SIMULATION_UNSUPPORTED_JOB:
        return "unsupported job";
    default:
        return "unknown simulation status";
    }
}
