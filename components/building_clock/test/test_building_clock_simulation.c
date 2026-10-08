#include "building_clock_generator.h"
#include "building_clock_simulation.h"
#include "building_clock_world_selection.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define GENERATED_WORLD_COUNT 16U
#define GENERATED_RUN_MS 60000U
#define LONG_RUN_MS 180000U
#define LONG_RUN_FEED_MS 120000U

static unsigned int failures;

static void expect_true(bool condition, const char* description) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", description);
        failures++;
    }
}

static void initialize_world(building_clock_world_t* world, uint8_t node_count) {
    memset(world, 0, sizeof(*world));
    world->replay.worker_behavior_seed = UINT64_C(0x123456789abcdef0);
    world->navigation.node_count = node_count;
    for (uint8_t node = 0U; node < node_count; node++) {
        world->navigation.nodes[node] = (building_clock_nav_node_t){
            .kind = BUILDING_CLOCK_NAV_NODE_PLATFORM_ENDPOINT,
            .position = {.x = (int16_t)(node * 20U), .y = 100},
        };
    }
}

static void add_spawn(building_clock_world_t* world, uint8_t node_id) {
    building_clock_worker_spawn_t* spawn = &world->worker_spawns[world->worker_spawn_count++];
    spawn->nav_node_id = node_id;
    spawn->position = world->navigation.nodes[node_id].position;
}

static uint16_t add_edge(building_clock_world_t* world, uint8_t from, uint8_t to, building_clock_nav_edge_kind_t kind, uint16_t cost, uint8_t capacity) {
    const uint16_t edge_id = world->navigation.edge_count++;
    world->navigation.edges[edge_id] = (building_clock_nav_edge_t){
        .from_node_id = from,
        .to_node_id = to,
        .kind = kind,
        .traversal_cost = cost,
        .capacity = capacity,
        .flags = BUILDING_CLOCK_NAV_EDGE_BIDIRECTIONAL,
        .temp_attachment_set_id = BUILDING_CLOCK_INVALID_ID,
    };
    return edge_id;
}

static building_clock_simple_job_spec_t job_spec(building_clock_job_kind_t kind, uint8_t target_node_id, uint16_t required_capability, uint32_t action_duration_ms) {
    return (building_clock_simple_job_spec_t){
        .kind = kind,
        .priority = 10U,
        .target_node_id = target_node_id,
        .required_worker_capabilities = required_capability,
        .action_duration_ms = action_duration_ms,
    };
}

static void run_for(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint32_t duration_ms, uint32_t step_ms) {
    for (uint32_t elapsed = 0U; elapsed < duration_ms; elapsed += step_ms) {
        const building_clock_simulation_step_result_t result = building_clock_simulation_step(simulation, world, step_ms);
        expect_true(result.status == BUILDING_CLOCK_SIMULATION_OK && result.processed_ms == step_ms, "simulation step processes requested elapsed time");
    }
}

static bool all_edge_reservations_clear(const building_clock_world_t* world, const building_clock_simulation_t* simulation) {
    for (uint16_t edge = 0U; edge < world->navigation.edge_count; edge++) {
        if (building_clock_navigation_edge_owner_mask(world, &simulation->navigation, edge) != 0U) {
            return false;
        }
    }
    return true;
}

static bool worker_occupancy_is_consistent(const building_clock_world_t* world, const building_clock_simulation_t* simulation) {
    for (uint8_t worker_id = 0U; worker_id < simulation->state.worker_count; worker_id++) {
        const building_clock_worker_t* worker = &simulation->state.workers[worker_id];
        const uint16_t worker_bit = (uint16_t)(1U << worker_id);
        for (uint8_t node = 0U; node < world->navigation.node_count; node++) {
            const bool owns_node = (building_clock_navigation_node_owner_mask(world, &simulation->navigation, node) & worker_bit) != 0U;
            const bool in_transit_or_waiting = worker->state == BUILDING_CLOCK_WORKER_WALK || worker->state == BUILDING_CLOCK_WORKER_CLIMB || worker->state == BUILDING_CLOCK_WORKER_WAIT;
            const bool expected = node == worker->reserved_destination_node_id || (!in_transit_or_waiting && node == worker->current_node_id);
            if (owns_node != expected) {
                return false;
            }
        }
    }
    return true;
}

static void test_walk_job_completion(void) {
    building_clock_world_t world;
    initialize_world(&world, 2U);
    add_spawn(&world, 0U);
    add_edge(&world, 0U, 1U, BUILDING_CLOCK_NAV_EDGE_WALK, 10U, 2U);

    building_clock_simulation_t simulation;
    expect_true(building_clock_simulation_init(&simulation, &world, 1U) == BUILDING_CLOCK_SIMULATION_OK, "walk fixture initializes");
    const building_clock_simple_job_spec_t spec = job_spec(BUILDING_CLOCK_JOB_PATROL, 1U, 0U, 150U);
    uint8_t job_id = BUILDING_CLOCK_INVALID_ID;
    expect_true(building_clock_simulation_add_job(&simulation, &world, &spec, &job_id) == BUILDING_CLOCK_SIMULATION_OK, "walk job is added");

    bool saw_walk = false;
    for (uint32_t elapsed = 0U; elapsed < 2000U && simulation.state.jobs[job_id].status != BUILDING_CLOCK_JOB_COMPLETE; elapsed += 50U) {
        (void)building_clock_simulation_step(&simulation, &world, 50U);
        saw_walk |= simulation.state.workers[0].state == BUILDING_CLOCK_WORKER_WALK;
    }

    expect_true(saw_walk, "worker enters walk state");
    expect_true(simulation.state.jobs[job_id].status == BUILDING_CLOCK_JOB_COMPLETE, "walk job completes");
    expect_true(simulation.state.workers[0].state == BUILDING_CLOCK_WORKER_IDLE && simulation.state.workers[0].current_node_id == 1U, "worker finishes idle at target");
    expect_true(all_edge_reservations_clear(&world, &simulation), "completed walk releases edge reservation");
    expect_true(worker_occupancy_is_consistent(&world, &simulation), "completed walk retains only current-node occupancy");
    expect_true(simulation.metrics.jobs_started == 1U && simulation.metrics.jobs_completed == 1U, "walk completion updates metrics");
}

static void test_ladder_multi_edge_and_actions(void) {
    building_clock_world_t world;
    initialize_world(&world, 4U);
    add_spawn(&world, 0U);
    add_edge(&world, 0U, 1U, BUILDING_CLOCK_NAV_EDGE_WALK, 5U, 2U);
    add_edge(&world, 1U, 2U, BUILDING_CLOCK_NAV_EDGE_CLIMB, 10U, 1U);
    add_edge(&world, 2U, 3U, BUILDING_CLOCK_NAV_EDGE_WALK, 5U, 2U);

    building_clock_simulation_t simulation;
    expect_true(building_clock_simulation_init(&simulation, &world, 1U) == BUILDING_CLOCK_SIMULATION_OK, "multi-edge fixture initializes");
    building_clock_simple_job_spec_t spec = job_spec(BUILDING_CLOCK_JOB_INSPECT, 3U, BUILDING_CLOCK_WORKER_CAN_INSPECT, 300U);
    uint8_t inspect_job;
    expect_true(building_clock_simulation_add_job(&simulation, &world, &spec, &inspect_job) == BUILDING_CLOCK_SIMULATION_OK, "inspection job is added");

    bool saw_climb = false;
    bool saw_inspect = false;
    for (uint32_t elapsed = 0U; elapsed < 4000U && simulation.state.jobs[inspect_job].status != BUILDING_CLOCK_JOB_COMPLETE; elapsed += 50U) {
        (void)building_clock_simulation_step(&simulation, &world, 50U);
        saw_climb |= simulation.state.workers[0].state == BUILDING_CLOCK_WORKER_CLIMB;
        saw_inspect |= simulation.state.workers[0].state == BUILDING_CLOCK_WORKER_INSPECT;
    }
    expect_true(saw_climb && saw_inspect, "worker traverses ladder and performs inspection");
    expect_true(simulation.state.jobs[inspect_job].status == BUILDING_CLOCK_JOB_COMPLETE && simulation.state.workers[0].current_node_id == 3U, "multi-edge inspection completes at target");

    spec = job_spec(BUILDING_CLOCK_JOB_SECURE_SECTION, 0U, BUILDING_CLOCK_WORKER_CAN_BUILD, 350U);
    uint8_t hammer_job;
    expect_true(building_clock_simulation_add_job(&simulation, &world, &spec, &hammer_job) == BUILDING_CLOCK_SIMULATION_OK, "hammer job is added");
    bool saw_hammer = false;
    for (uint32_t elapsed = 0U; elapsed < 4000U && simulation.state.jobs[hammer_job].status != BUILDING_CLOCK_JOB_COMPLETE; elapsed += 50U) {
        (void)building_clock_simulation_step(&simulation, &world, 50U);
        saw_hammer |= simulation.state.workers[0].state == BUILDING_CLOCK_WORKER_HAMMER;
    }
    expect_true(saw_hammer && simulation.state.jobs[hammer_job].status == BUILDING_CLOCK_JOB_COMPLETE, "worker performs bounded hammer action");
    expect_true(simulation.state.workers[0].current_node_id == 0U, "worker returns over multi-edge route");
}

static void prepare_contention_fixture(building_clock_world_t* world, building_clock_simulation_t* simulation, uint8_t* first_job, uint8_t* second_job) {
    initialize_world(world, 2U);
    world->navigation.nodes[0].kind = BUILDING_CLOCK_NAV_NODE_LADDER_BOTTOM;
    world->navigation.nodes[1].kind = BUILDING_CLOCK_NAV_NODE_LADDER_TOP;
    add_spawn(world, 0U);
    add_spawn(world, 1U);
    add_edge(world, 0U, 1U, BUILDING_CLOCK_NAV_EDGE_CLIMB, 10U, 1U);
    expect_true(building_clock_simulation_init(simulation, world, 2U) == BUILDING_CLOCK_SIMULATION_OK, "contention fixture initializes");
    simulation->state.workers[0].capabilities = BUILDING_CLOCK_WORKER_CAN_INSPECT;
    simulation->state.workers[1].capabilities = BUILDING_CLOCK_WORKER_CAN_BUILD;
    building_clock_simple_job_spec_t spec = job_spec(BUILDING_CLOCK_JOB_INSPECT, 1U, BUILDING_CLOCK_WORKER_CAN_INSPECT, 300U);
    expect_true(building_clock_simulation_add_job(simulation, world, &spec, first_job) == BUILDING_CLOCK_SIMULATION_OK, "first contention job is added");
    spec = job_spec(BUILDING_CLOCK_JOB_SECURE_SECTION, 0U, BUILDING_CLOCK_WORKER_CAN_BUILD, 300U);
    expect_true(building_clock_simulation_add_job(simulation, world, &spec, second_job) == BUILDING_CLOCK_SIMULATION_OK, "second contention job is added");
}

static void test_deterministic_contention(void) {
    building_clock_world_t world;
    building_clock_simulation_t simulation;
    uint8_t first_job;
    uint8_t second_job;
    prepare_contention_fixture(&world, &simulation, &first_job, &second_job);

    (void)building_clock_simulation_step(&simulation, &world, 50U);
    expect_true(simulation.state.workers[0].state == BUILDING_CLOCK_WORKER_CLIMB, "lower worker ID wins simultaneous constrained edge request");
    expect_true(simulation.state.workers[1].state == BUILDING_CLOCK_WORKER_WAIT, "second worker waits for constrained edge");
    expect_true(building_clock_navigation_edge_owner_mask(&world, &simulation.navigation, 0U) == 1U, "capacity-one edge is owned by worker zero");
    expect_true(building_clock_navigation_node_owner_mask(&world, &simulation.navigation, 1U) == 1U, "winner claims destination while waiting worker yields its departure node");

    run_for(&simulation, &world, 2000U, 50U);
    expect_true(simulation.state.jobs[first_job].status == BUILDING_CLOCK_JOB_COMPLETE && simulation.state.jobs[second_job].status == BUILDING_CLOCK_JOB_COMPLETE,
                "waiting worker progresses after winner releases edge");
    expect_true(simulation.metrics.wait_events > 0U && simulation.metrics.reservation_conflicts > 0U, "contention is recorded");
    expect_true(all_edge_reservations_clear(&world, &simulation) && worker_occupancy_is_consistent(&world, &simulation), "contention completion leaves consistent reservations");
}

static void test_cancellation_and_slot_reuse(void) {
    building_clock_world_t world;
    initialize_world(&world, 2U);
    add_spawn(&world, 0U);
    add_edge(&world, 0U, 1U, BUILDING_CLOCK_NAV_EDGE_WALK, 100U, 1U);

    building_clock_simulation_t simulation;
    expect_true(building_clock_simulation_init(&simulation, &world, 1U) == BUILDING_CLOCK_SIMULATION_OK, "cancellation fixture initializes");
    building_clock_simple_job_spec_t spec = job_spec(BUILDING_CLOCK_JOB_PATROL, 1U, 0U, 100U);
    uint8_t job_id;
    expect_true(building_clock_simulation_add_job(&simulation, &world, &spec, &job_id) == BUILDING_CLOCK_SIMULATION_OK, "cancellable job is added");
    (void)building_clock_simulation_step(&simulation, &world, 50U);
    expect_true(simulation.state.workers[0].state == BUILDING_CLOCK_WORKER_WALK, "worker is moving before cancellation");
    expect_true(building_clock_simulation_cancel_job(&simulation, &world, job_id) == BUILDING_CLOCK_SIMULATION_OK, "moving job cancels");
    expect_true(simulation.state.jobs[job_id].status == BUILDING_CLOCK_JOB_CANCELLED && simulation.state.workers[0].current_job_id == BUILDING_CLOCK_INVALID_ID,
                "cancellation clears worker reference");
    expect_true(all_edge_reservations_clear(&world, &simulation) && worker_occupancy_is_consistent(&world, &simulation), "movement cancellation cleans reservations");

    expect_true(building_clock_simulation_recycle_job(&simulation, job_id) == BUILDING_CLOCK_SIMULATION_OK, "cancelled job slot recycles");
    uint8_t reused_job_id = BUILDING_CLOCK_INVALID_ID;
    spec.target_node_id = 0U;
    expect_true(building_clock_simulation_add_job(&simulation, &world, &spec, &reused_job_id) == BUILDING_CLOCK_SIMULATION_OK && reused_job_id == job_id, "first free stable job slot is reused");
    expect_true(simulation.state.workers[0].current_job_id == BUILDING_CLOCK_INVALID_ID, "slot reuse does not recreate stale worker reference");
    run_for(&simulation, &world, 500U, 50U);
    expect_true(simulation.state.jobs[reused_job_id].status == BUILDING_CLOCK_JOB_COMPLETE, "reused job slot completes normally");

    building_clock_simulation_t waiting_simulation;
    uint8_t first_job;
    uint8_t waiting_job;
    prepare_contention_fixture(&world, &waiting_simulation, &first_job, &waiting_job);
    (void)building_clock_simulation_step(&waiting_simulation, &world, 50U);
    expect_true(waiting_simulation.state.workers[1].state == BUILDING_CLOCK_WORKER_WAIT, "second cancellation fixture worker is waiting");
    expect_true(building_clock_simulation_cancel_job(&waiting_simulation, &world, waiting_job) == BUILDING_CLOCK_SIMULATION_OK, "waiting job cancels");
    expect_true(waiting_simulation.state.workers[1].state == BUILDING_CLOCK_WORKER_IDLE && waiting_simulation.state.workers[1].current_job_id == BUILDING_CLOCK_INVALID_ID,
                "waiting cancellation returns worker to idle");
    expect_true(building_clock_navigation_edge_owner_mask(&world, &waiting_simulation.navigation, 0U) == 1U, "waiting cancellation does not disturb current edge owner");
    expect_true(worker_occupancy_is_consistent(&world, &waiting_simulation), "waiting cancellation leaves node occupancy consistent");
}

static void test_deterministic_replay_and_step_bound(void) {
    building_clock_world_t world;
    building_clock_simulation_t first;
    building_clock_simulation_t second;
    uint8_t first_job_a;
    uint8_t first_job_b;
    uint8_t second_job_a;
    uint8_t second_job_b;
    prepare_contention_fixture(&world, &first, &first_job_a, &first_job_b);
    prepare_contention_fixture(&world, &second, &second_job_a, &second_job_b);
    expect_true(first_job_a == second_job_a && first_job_b == second_job_b, "replay fixtures use identical job slots");

    const uint32_t steps[] = {17U, 33U, 50U, 100U, 25U, 75U, 50U, 200U, 50U, 400U};
    for (uint8_t index = 0U; index < sizeof(steps) / sizeof(steps[0]); index++) {
        (void)building_clock_simulation_step(&first, &world, steps[index]);
        (void)building_clock_simulation_step(&second, &world, steps[index]);
        expect_true(memcmp(&first, &second, sizeof(first)) == 0, "identical elapsed-time sequence produces identical semantic state");
    }

    const uint64_t before = first.state.elapsed_ms;
    const building_clock_simulation_step_result_t result = building_clock_simulation_step(&first, &world, BUILDING_CLOCK_SIMULATION_MAX_STEP_MS + 5000U);
    expect_true(result.status == BUILDING_CLOCK_SIMULATION_DELTA_CLAMPED && result.processed_ms == BUILDING_CLOCK_SIMULATION_MAX_STEP_MS, "large elapsed-time request is explicitly bounded");
    expect_true(first.state.elapsed_ms == before + BUILDING_CLOCK_SIMULATION_MAX_STEP_MS, "bounded step advances only processed elapsed time");
}

static uint8_t append_generated_targets(const building_clock_world_t* world, uint8_t targets[BUILDING_CLOCK_MAX_JOBS]) {
    uint8_t count = 0U;
    targets[count++] = world->depots[0].access_node_id;
    for (uint8_t staging = 0U; staging < world->staging_area_count && count < BUILDING_CLOCK_MAX_JOBS; staging++) {
        targets[count++] = world->staging_areas[staging].access_node_id;
    }
    for (uint8_t digit = 0U; digit < BUILDING_CLOCK_DIGIT_COUNT && count < BUILDING_CLOCK_MAX_JOBS; digit++) {
        for (uint8_t access = 0U; access < world->digit_work_areas[digit].access_count; access++) {
            if (world->digit_work_areas[digit].access[access].kind == BUILDING_CLOCK_DIGIT_ACCESS_LOWER) {
                targets[count++] = world->digit_work_areas[digit].access[access].nav_node_id;
                break;
            }
        }
    }
    for (uint8_t crane = 0U; crane < world->crane_count && count < BUILDING_CLOCK_MAX_JOBS; crane++) {
        targets[count++] = world->cranes[crane].operator_node_id;
    }
    for (uint8_t lift = 0U; lift < world->lift_count && count < BUILDING_CLOCK_MAX_JOBS; lift++) {
        targets[count++] = world->lifts[lift].operator_node_id;
    }
    return count;
}

static void add_generated_jobs(building_clock_simulation_t* simulation, const building_clock_world_t* world, const uint8_t* targets, uint8_t target_count, uint32_t* created_jobs) {
    for (uint8_t target = 0U; target < target_count; target++) {
        const building_clock_job_kind_t kind = target % 3U == 0U ? BUILDING_CLOCK_JOB_PATROL : (target % 3U == 1U ? BUILDING_CLOCK_JOB_INSPECT : BUILDING_CLOCK_JOB_SECURE_SECTION);
        const uint16_t capability = kind == BUILDING_CLOCK_JOB_INSPECT ? BUILDING_CLOCK_WORKER_CAN_INSPECT : (kind == BUILDING_CLOCK_JOB_SECURE_SECTION ? BUILDING_CLOCK_WORKER_CAN_BUILD : 0U);
        const building_clock_simple_job_spec_t spec = job_spec(kind, targets[target], capability, 250U + (uint32_t)(target % 3U) * 100U);
        if (building_clock_simulation_add_job(simulation, world, &spec, NULL) == BUILDING_CLOCK_SIMULATION_OK) {
            (*created_jobs)++;
        }
    }
}

static void test_generated_world_integration(void) {
    uint32_t total_jobs = 0U;
    uint32_t total_completed = 0U;
    uint32_t total_waits = 0U;
    uint32_t total_route_failures = 0U;

    for (uint32_t index = 0U; index < GENERATED_WORLD_COUNT; index++) {
        const uint64_t root_seed = UINT64_C(0x1f83d9abfb41bd6b) + UINT64_C(0x9e3779b97f4a7c15) * index;
        building_clock_world_t world;
        const building_clock_world_selection_result_t selection = building_clock_select_world(&world, root_seed, BUILDING_CLOCK_GENERATOR_VERSION);
        expect_true(selection.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED || selection.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED_FALLBACK, "generated integration world is accepted");
        building_clock_simulation_t simulation;
        expect_true(building_clock_simulation_init(&simulation, &world, 4U) == BUILDING_CLOCK_SIMULATION_OK, "generated integration simulation initializes");
        uint8_t targets[BUILDING_CLOCK_MAX_JOBS];
        const uint8_t target_count = append_generated_targets(&world, targets);
        uint32_t created = 0U;
        add_generated_jobs(&simulation, &world, targets, target_count, &created);
        run_for(&simulation, &world, GENERATED_RUN_MS, 100U);

        expect_true(simulation.metrics.jobs_completed == created, "generated-world jobs all complete");
        expect_true(simulation.metrics.jobs_cancelled == 0U, "generated-world jobs are not cancelled");
        expect_true(simulation.metrics.route_failures == 0U, "generated-world jobs have valid walk/climb routes");
        expect_true(all_edge_reservations_clear(&world, &simulation), "generated-world run leaves no edge reservations");
        expect_true(worker_occupancy_is_consistent(&world, &simulation), "generated-world run leaves consistent node occupancy");
        total_jobs += created;
        total_completed += simulation.metrics.jobs_completed;
        total_waits += simulation.metrics.wait_events;
        total_route_failures += simulation.metrics.route_failures;
    }

    printf("Generated simulation batch: %u worlds, %" PRIu32 "/%" PRIu32 " jobs completed, %" PRIu32 " waits, %" PRIu32 " route failures\n", GENERATED_WORLD_COUNT, total_completed, total_jobs,
           total_waits, total_route_failures);
}

static void recycle_terminal_jobs(building_clock_simulation_t* simulation) {
    for (uint8_t job = 0U; job < BUILDING_CLOCK_MAX_JOBS; job++) {
        if (simulation->state.jobs[job].status == BUILDING_CLOCK_JOB_COMPLETE || simulation->state.jobs[job].status == BUILDING_CLOCK_JOB_CANCELLED) {
            expect_true(building_clock_simulation_recycle_job(simulation, job) == BUILDING_CLOCK_SIMULATION_OK, "long-run terminal job recycles");
        }
    }
}

static void test_long_run(void) {
    building_clock_world_t world;
    const building_clock_world_selection_result_t selection = building_clock_select_world(&world, UINT64_C(0x5be0cd19137e2179), BUILDING_CLOCK_GENERATOR_VERSION);
    expect_true(selection.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED || selection.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED_FALLBACK, "long-run world is accepted");
    building_clock_simulation_t simulation;
    expect_true(building_clock_simulation_init(&simulation, &world, 6U) == BUILDING_CLOCK_SIMULATION_OK, "long-run simulation initializes");
    uint8_t targets[BUILDING_CLOCK_MAX_JOBS];
    const uint8_t target_count = append_generated_targets(&world, targets);
    uint32_t created_jobs = 0U;

    for (uint32_t elapsed = 0U; elapsed < LONG_RUN_MS; elapsed += 100U) {
        recycle_terminal_jobs(&simulation);
        if (elapsed < LONG_RUN_FEED_MS) {
            while (simulation.state.used_job_slot_count < 12U) {
                const uint8_t target_index = (uint8_t)(created_jobs % target_count);
                const building_clock_job_kind_t kind = created_jobs % 3U == 0U ? BUILDING_CLOCK_JOB_PATROL : (created_jobs % 3U == 1U ? BUILDING_CLOCK_JOB_INSPECT : BUILDING_CLOCK_JOB_SECURE_SECTION);
                const uint16_t capability = kind == BUILDING_CLOCK_JOB_INSPECT ? BUILDING_CLOCK_WORKER_CAN_INSPECT : (kind == BUILDING_CLOCK_JOB_SECURE_SECTION ? BUILDING_CLOCK_WORKER_CAN_BUILD : 0U);
                const building_clock_simple_job_spec_t spec = job_spec(kind, targets[target_index], capability, 300U);
                expect_true(building_clock_simulation_add_job(&simulation, &world, &spec, NULL) == BUILDING_CLOCK_SIMULATION_OK, "long-run job is added");
                created_jobs++;
            }
        }
        (void)building_clock_simulation_step(&simulation, &world, 100U);
    }
    recycle_terminal_jobs(&simulation);

    expect_true(simulation.state.used_job_slot_count == 0U, "long-run jobs drain without stalls");
    expect_true(simulation.metrics.jobs_completed == created_jobs, "long-run completes every created job");
    expect_true(simulation.metrics.jobs_cancelled == 0U && simulation.metrics.route_failures == 0U, "long-run has no cancellations or route failures");
    expect_true(all_edge_reservations_clear(&world, &simulation), "long-run leaves no edge reservations");
    expect_true(worker_occupancy_is_consistent(&world, &simulation), "long-run leaves consistent node occupancy");
    for (uint8_t worker = 0U; worker < simulation.state.worker_count; worker++) {
        expect_true(simulation.state.workers[worker].current_job_id == BUILDING_CLOCK_INVALID_ID && simulation.state.workers[worker].state == BUILDING_CLOCK_WORKER_IDLE,
                    "long-run workers drain to idle without stale jobs");
    }

    printf("Long run: %u ms, %" PRIu32 " jobs completed, %" PRIu32 " waits, %" PRIu32 " conflicts, peak %u active workers\n", LONG_RUN_MS, simulation.metrics.jobs_completed,
           simulation.metrics.wait_events, simulation.metrics.reservation_conflicts, simulation.metrics.peak_active_workers);
    printf("  reservations acquired: %" PRIu32 " edges, %" PRIu32 " nodes; route failures: %" PRIu32 "; recycled slots: %" PRIu32 "\n", simulation.metrics.edge_reservations_acquired,
           simulation.metrics.node_reservations_acquired, simulation.metrics.route_failures, simulation.metrics.jobs_recycled);
}

int main(void) {
    test_walk_job_completion();
    test_ladder_multi_edge_and_actions();
    test_deterministic_contention();
    test_cancellation_and_slot_reuse();
    test_deterministic_replay_and_step_bound();
    test_generated_world_integration();
    test_long_run();

    printf("Simulation memory: %zu bytes/worker, %zu bytes/job, %zu bytes semantic state, %zu bytes total context\n", sizeof(building_clock_worker_t), sizeof(building_clock_job_t),
           sizeof(building_clock_simulation_state_t), sizeof(building_clock_simulation_t));

    if (failures != 0U) {
        fprintf(stderr, "%u Building Clock simulation test(s) failed\n", failures);
        return 1;
    }
    puts("Building Clock simulation tests passed");
    return 0;
}
