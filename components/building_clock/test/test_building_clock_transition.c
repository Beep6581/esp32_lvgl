#include "building_clock_generator.h"
#include "building_clock_simulation.h"
#include "building_clock_world_selection.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TRANSITION_TIMEOUT_MS 10000U
#define TRANSITION_BATCH_COUNT 60U

static unsigned int failures;

static void expect_true(bool condition, const char* description) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", description);
        failures++;
    }
}

static bool point_in_rect(building_clock_point_t point, building_clock_rect_t rect) {
    return point.x >= rect.x && point.y >= rect.y && point.x < (int32_t)rect.x + rect.width && point.y < (int32_t)rect.y + rect.height;
}

static building_clock_point_t rect_center(building_clock_rect_t rect) {
    return (building_clock_point_t){
        .x = (int16_t)(rect.x + (int16_t)(rect.width / 2U)),
        .y = (int16_t)(rect.y + (int16_t)(rect.height / 2U)),
    };
}

static void select_test_world(building_clock_world_t* world, uint64_t root_seed) {
    const building_clock_world_selection_result_t result = building_clock_select_world(world, root_seed, BUILDING_CLOCK_GENERATOR_VERSION);
    expect_true(result.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED || result.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED_FALLBACK, "transition test world is accepted");
}

static uint8_t time_digits(uint8_t hour, uint8_t minute, uint8_t digits[BUILDING_CLOCK_DIGIT_COUNT]) {
    digits[0] = (uint8_t)(hour / 10U);
    digits[1] = (uint8_t)(hour % 10U);
    digits[2] = (uint8_t)(minute / 10U);
    digits[3] = (uint8_t)(minute % 10U);
    return BUILDING_CLOCK_DIGIT_COUNT;
}

static uint8_t changed_digit_mask(uint8_t from_hour, uint8_t from_minute, uint8_t to_hour, uint8_t to_minute) {
    uint8_t from[BUILDING_CLOCK_DIGIT_COUNT];
    uint8_t to[BUILDING_CLOCK_DIGIT_COUNT];
    (void)time_digits(from_hour, from_minute, from);
    (void)time_digits(to_hour, to_minute, to);
    uint8_t mask = 0U;
    for (uint8_t digit = 0U; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        if (from[digit] != to[digit]) {
            mask |= (uint8_t)(1U << digit);
        }
    }
    return mask;
}

static bool reached_handoff(const building_clock_simulation_t* simulation, uint8_t changed_mask) {
    for (uint8_t digit = 0U; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        const building_clock_digit_lifecycle_t expected = (changed_mask & (uint8_t)(1U << digit)) != 0U ? BUILDING_CLOCK_DIGIT_CLEARING : BUILDING_CLOCK_DIGIT_STABLE;
        if (simulation->state.digits[digit].lifecycle != expected) {
            return false;
        }
    }
    return true;
}

static bool run_to_handoff(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t changed_mask) {
    for (uint32_t elapsed = 0U; elapsed < TRANSITION_TIMEOUT_MS; elapsed += BUILDING_CLOCK_SIMULATION_QUANTUM_MS) {
        const building_clock_simulation_step_result_t result = building_clock_simulation_step(simulation, world, BUILDING_CLOCK_SIMULATION_QUANTUM_MS);
        if (result.status != BUILDING_CLOCK_SIMULATION_OK || result.unconsumed_ms != 0U) {
            return false;
        }
        if (reached_handoff(simulation, changed_mask)) {
            return true;
        }
    }
    return false;
}

static void drain_worker_egress(building_clock_simulation_t* simulation, const building_clock_world_t* world) {
    for (uint32_t elapsed = 0U; elapsed < 3000U; elapsed += BUILDING_CLOCK_SIMULATION_QUANTUM_MS) {
        (void)building_clock_simulation_step(simulation, world, BUILDING_CLOCK_SIMULATION_QUANTUM_MS);
    }
}

static bool edge_reservations_clear(const building_clock_world_t* world, const building_clock_simulation_t* simulation) {
    for (uint16_t edge = 0U; edge < world->navigation.edge_count; edge++) {
        if (building_clock_navigation_edge_owner_mask(world, &simulation->navigation, edge) != 0U) {
            return false;
        }
    }
    return true;
}

static bool job_references_clear(const building_clock_simulation_t* simulation, uint8_t job_id) {
    for (uint8_t digit = 0U; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        const building_clock_digit_structure_t* structures[] = {
            &simulation->state.digits[digit].outgoing,
            &simulation->state.digits[digit].incoming,
        };
        for (uint8_t structure = 0U; structure < 2U; structure++) {
            for (uint8_t section = 0U; section < structures[structure]->section_count; section++) {
                if (structures[structure]->sections[section].assigned_job_id == job_id) {
                    return false;
                }
            }
        }
    }
    return true;
}

static bool debris_is_valid(const building_clock_simulation_t* simulation, const building_clock_world_t* world) {
    uint8_t used_count = 0U;
    for (uint8_t debris = 0U; debris < BUILDING_CLOCK_MAX_DEBRIS_PIECES; debris++) {
        const building_clock_debris_t* item = &simulation->state.debris[debris];
        if (item->status == BUILDING_CLOCK_DEBRIS_UNUSED) {
            continue;
        }
        used_count++;
        if (item->source_digit_id >= BUILDING_CLOCK_DIGIT_COUNT || item->debris_bay_id >= world->debris_bay_count ||
            item->source_section_id >= simulation->state.digits[item->source_digit_id].outgoing.section_count || world->digit_work_areas[item->source_digit_id].debris_bay_id != item->debris_bay_id ||
            !point_in_rect(item->position, world->debris_bays[item->debris_bay_id].area)) {
            return false;
        }
    }
    return used_count == simulation->state.used_debris_slot_count;
}

static void verify_transition_result(const building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t changed_mask) {
    expect_true(reached_handoff(simulation, changed_mask), "changed digits reach clearing handoff and unchanged digits remain stable");
    expect_true(simulation->state.hazard_count == 0U && simulation->state.falling_section_count == 0U, "handoff has no active hazards or falling sections");
    expect_true(simulation->state.used_debris_slot_count <= BUILDING_CLOCK_MAX_DEBRIS_PIECES, "debris stays within fixed capacity");
    expect_true(debris_is_valid(simulation, world), "debris stays in the correct generated bay with valid section references");
    expect_true(simulation->metrics.evacuation_failures == 0U && simulation->metrics.transition_capacity_delays == 0U, "transition has no evacuation or capacity failures");
    for (uint8_t digit = 0U; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        const building_clock_digit_state_t* state = &simulation->state.digits[digit];
        for (uint8_t section = 0U; section < state->outgoing.section_count; section++) {
            const building_clock_section_status_t expected = (changed_mask & (uint8_t)(1U << digit)) != 0U ? BUILDING_CLOCK_SECTION_DEBRIS : BUILDING_CLOCK_SECTION_SECURED;
            expect_true(state->outgoing.sections[section].status == expected, "outgoing section status matches changed-digit lifecycle");
        }
    }
}

static void prepare_transition(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t from_hour, uint8_t from_minute, uint8_t to_hour, uint8_t to_minute,
                               uint8_t worker_count) {
    expect_true(building_clock_simulation_init(simulation, world, worker_count) == BUILDING_CLOCK_SIMULATION_OK, "transition simulation initializes");
    expect_true(building_clock_simulation_set_time(simulation, world, from_hour, from_minute) == BUILDING_CLOCK_SIMULATION_OK, "initial clock time is established without a transition");
    expect_true(building_clock_simulation_set_time(simulation, world, to_hour, to_minute) == BUILDING_CLOCK_SIMULATION_OK, "target clock time starts a transition");
}

static void test_single_digit_and_determinism(void) {
    building_clock_world_t world;
    select_test_world(&world, UINT64_C(0x6a09e667f3bcc909));
    building_clock_simulation_t first;
    building_clock_simulation_t second;
    expect_true(building_clock_simulation_init(&first, &world, 6U) == BUILDING_CLOCK_SIMULATION_OK, "clock API validation fixture initializes");
    expect_true(building_clock_simulation_set_time(&first, &world, 24U, 0U) == BUILDING_CLOCK_SIMULATION_INVALID_TIME, "clock API rejects an invalid hour");
    prepare_transition(&first, &world, 14U, 37U, 14U, 38U, 6U);
    prepare_transition(&second, &world, 14U, 37U, 14U, 38U, 6U);
    const uint8_t changed_mask = changed_digit_mask(14U, 37U, 14U, 38U);

    first.state.workers[0].position = rect_center(world.digit_work_areas[3].hazard_area);
    second.state.workers[0].position = first.state.workers[0].position;
    expect_true(first.state.digits[3].lifecycle == BUILDING_CLOCK_DIGIT_PREPARING && first.state.digits[0].lifecycle == BUILDING_CLOCK_DIGIT_STABLE,
                "single-digit change starts only the minute-units lifecycle");
    expect_true(building_clock_simulation_set_time(&first, &world, 14U, 39U) == BUILDING_CLOCK_SIMULATION_TRANSITION_ACTIVE, "clock API rejects a second target while collapse is unresolved");

    for (uint32_t elapsed = 0U; elapsed < TRANSITION_TIMEOUT_MS && !reached_handoff(&first, changed_mask); elapsed += BUILDING_CLOCK_SIMULATION_QUANTUM_MS) {
        (void)building_clock_simulation_step(&first, &world, BUILDING_CLOCK_SIMULATION_QUANTUM_MS);
        (void)building_clock_simulation_step(&second, &world, BUILDING_CLOCK_SIMULATION_QUANTUM_MS);
        expect_true(memcmp(&first, &second, sizeof(first)) == 0, "identical transition replay remains byte-equivalent");
    }
    verify_transition_result(&first, &world, changed_mask);
    expect_true(first.state.digits[3].outgoing.numeral == 7U && first.state.digits[3].incoming.numeral == 8U, "changed digit retains outgoing seven and target eight plans");
    expect_true(first.metrics.workers_evacuated > 0U, "worker inside single-digit hazard is evacuated");
}

static void test_multi_digit_and_midnight(void) {
    building_clock_world_t world;
    select_test_world(&world, UINT64_C(0xbb67ae8584caa73b));

    building_clock_simulation_t rollover;
    prepare_transition(&rollover, &world, 14U, 59U, 15U, 0U, 6U);
    const uint8_t rollover_mask = changed_digit_mask(14U, 59U, 15U, 0U);
    expect_true(rollover.state.digits[1].transition_delay_ms < rollover.state.digits[2].transition_delay_ms &&
                    rollover.state.digits[2].transition_delay_ms < rollover.state.digits[3].transition_delay_ms,
                "multi-digit collapse uses deterministic changed-digit staggering");
    expect_true(run_to_handoff(&rollover, &world, rollover_mask), "14:59 to 15:00 reaches post-collapse handoff");
    verify_transition_result(&rollover, &world, rollover_mask);

    building_clock_simulation_t midnight;
    prepare_transition(&midnight, &world, 23U, 59U, 0U, 0U, BUILDING_CLOCK_MAX_WORKERS);
    const uint8_t midnight_mask = changed_digit_mask(23U, 59U, 0U, 0U);
    expect_true(midnight_mask == 0x0fU, "midnight changes all four digits");
    expect_true(run_to_handoff(&midnight, &world, midnight_mask), "23:59 to 00:00 reaches post-collapse handoff");
    drain_worker_egress(&midnight, &world);
    verify_transition_result(&midnight, &world, midnight_mask);
    expect_true(edge_reservations_clear(&world, &midnight), "midnight transition leaves no edge reservation leaks");
    expect_true(midnight.metrics.peak_falling_sections <= BUILDING_CLOCK_MAX_FALLING_SECTIONS && midnight.metrics.peak_debris_pieces <= BUILDING_CLOCK_MAX_DEBRIS_PIECES,
                "midnight transition respects falling-section and debris capacities");
}

static uint16_t required_capability(building_clock_job_kind_t kind) {
    return kind == BUILDING_CLOCK_JOB_INSPECT ? BUILDING_CLOCK_WORKER_CAN_INSPECT : BUILDING_CLOCK_WORKER_CAN_BUILD;
}

static void make_active_job(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id, building_clock_job_kind_t kind, uint8_t* job_id) {
    const building_clock_simple_job_spec_t spec = {
        .kind = kind,
        .priority = 20U,
        .target_node_id = simulation->state.workers[worker_id].current_node_id,
        .required_worker_capabilities = required_capability(kind),
        .action_duration_ms = 5000U,
    };
    expect_true(building_clock_simulation_add_job(simulation, world, &spec, job_id) == BUILDING_CLOCK_SIMULATION_OK, "interruption job is added");
    simulation->state.jobs[*job_id].status = BUILDING_CLOCK_JOB_ACTIVE;
    simulation->state.jobs[*job_id].reserved_worker_id = worker_id;
    simulation->state.workers[worker_id].current_job_id = *job_id;
    simulation->state.workers[worker_id].route_purpose = BUILDING_CLOCK_WORKER_ROUTE_JOB;
}

static void test_worker_interruption(void) {
    building_clock_world_t world;
    select_test_world(&world, UINT64_C(0x3c6ef372fe94f82b));
    building_clock_simulation_t simulation;
    prepare_transition(&simulation, &world, 14U, 37U, 14U, 38U, 4U);
    for (uint32_t elapsed = 0U; elapsed < 450U; elapsed += 50U) {
        (void)building_clock_simulation_step(&simulation, &world, 50U);
    }

    uint8_t jobs[3];
    make_active_job(&simulation, &world, 1U, BUILDING_CLOCK_JOB_PATROL, &jobs[0]);
    make_active_job(&simulation, &world, 2U, BUILDING_CLOCK_JOB_INSPECT, &jobs[1]);
    make_active_job(&simulation, &world, 3U, BUILDING_CLOCK_JOB_SECURE_SECTION, &jobs[2]);
    simulation.state.workers[1].state = BUILDING_CLOCK_WORKER_WALK;
    simulation.state.workers[2].state = BUILDING_CLOCK_WORKER_INSPECT;
    simulation.state.workers[3].state = BUILDING_CLOCK_WORKER_WAIT;
    simulation.state.digits[3].outgoing.sections[0].assigned_job_id = jobs[1];

    const uint16_t edge_id = 0U;
    const uint8_t destination = world.navigation.edges[edge_id].to_node_id;
    expect_true(building_clock_navigation_reserve_edge(&world, &simulation.navigation, edge_id, 1U) == BUILDING_CLOCK_RESERVATION_ACQUIRED, "walking worker holds an edge before interruption");
    expect_true(building_clock_navigation_reserve_node(&world, &simulation.navigation, destination, 1U) == BUILDING_CLOCK_RESERVATION_ACQUIRED,
                "walking worker holds a destination before interruption");
    simulation.state.workers[1].reserved_edge_id = edge_id;
    simulation.state.workers[1].reserved_destination_node_id = destination;
    simulation.state.workers[1].route_node_count = 2U;
    simulation.state.workers[1].route_edge_count = 1U;
    simulation.state.workers[1].route_node_ids[0] = simulation.state.workers[1].current_node_id;
    simulation.state.workers[1].route_node_ids[1] = destination;
    simulation.state.workers[1].route_edge_ids[0] = (uint8_t)edge_id;

    const building_clock_point_t danger = rect_center(world.digit_work_areas[3].hazard_area);
    for (uint8_t worker = 0U; worker < simulation.state.worker_count; worker++) {
        simulation.state.workers[worker].position = danger;
    }

    (void)building_clock_simulation_step(&simulation, &world, 50U);
    expect_true(simulation.state.digits[3].lifecycle == BUILDING_CLOCK_DIGIT_EVACUATING, "hazard activates after preparation");
    expect_true(simulation.metrics.jobs_interrupted == 3U, "walking, action, and waiting jobs are interrupted by safety");
    for (uint8_t worker = 0U; worker < simulation.state.worker_count; worker++) {
        expect_true(simulation.state.workers[worker].route_purpose == BUILDING_CLOCK_WORKER_ROUTE_EVACUATION && simulation.state.workers[worker].evacuation_digit_id == 3U,
                    "idle and working occupants begin generated safe-retreat routes");
    }
    for (uint8_t job = 0U; job < 3U; job++) {
        expect_true(simulation.state.jobs[jobs[job]].status == BUILDING_CLOCK_JOB_CANCELLED, "interrupted ordinary job is cancelled");
        expect_true(job_references_clear(&simulation, jobs[job]), "interrupted job has no stale section owner reference");
    }
    expect_true(building_clock_navigation_edge_owner_mask(&world, &simulation.navigation, edge_id) == 0U, "evacuation interruption releases active traversal reservation");

    const uint8_t changed_mask = changed_digit_mask(14U, 37U, 14U, 38U);
    expect_true(run_to_handoff(&simulation, &world, changed_mask), "interrupted-worker transition reaches handoff");
    drain_worker_egress(&simulation, &world);
    expect_true(edge_reservations_clear(&world, &simulation), "interrupted-worker transition leaves no edge reservation leaks");
}

typedef struct {
    uint32_t transitions;
    uint32_t digits_changed;
    uint32_t workers_evacuated;
    uint32_t jobs_interrupted;
    uint32_t debris_created;
    uint32_t evacuation_failures;
    uint32_t capacity_delays;
    uint8_t maximum_falling;
    uint8_t maximum_debris;
} transition_batch_stats_t;

static transition_batch_stats_t run_transition_batch(void) {
    transition_batch_stats_t stats = {0};
    for (uint32_t index = 0U; index < TRANSITION_BATCH_COUNT; index++) {
        building_clock_world_t world;
        select_test_world(&world, UINT64_C(0x510e527fade682d1) + UINT64_C(0x9e3779b97f4a7c15) * (index % 4U));
        const uint16_t from_total = (uint16_t)((14U * 60U + index) % (24U * 60U));
        const uint16_t to_total = (uint16_t)((from_total + 1U) % (24U * 60U));
        const uint8_t from_hour = (uint8_t)(from_total / 60U);
        const uint8_t from_minute = (uint8_t)(from_total % 60U);
        const uint8_t to_hour = (uint8_t)(to_total / 60U);
        const uint8_t to_minute = (uint8_t)(to_total % 60U);
        const uint8_t changed_mask = changed_digit_mask(from_hour, from_minute, to_hour, to_minute);

        building_clock_simulation_t simulation;
        prepare_transition(&simulation, &world, from_hour, from_minute, to_hour, to_minute, 6U);
        for (uint8_t digit = 0U; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
            if ((changed_mask & (uint8_t)(1U << digit)) != 0U) {
                simulation.state.workers[0].position = rect_center(world.digit_work_areas[digit].hazard_area);
                break;
            }
        }
        expect_true(run_to_handoff(&simulation, &world, changed_mask), "long-run minute transition reaches handoff");
        drain_worker_egress(&simulation, &world);
        verify_transition_result(&simulation, &world, changed_mask);
        expect_true(edge_reservations_clear(&world, &simulation), "long-run transition has no edge reservation leak");

        stats.transitions++;
        stats.digits_changed += simulation.metrics.digits_changed;
        stats.workers_evacuated += simulation.metrics.workers_evacuated;
        stats.jobs_interrupted += simulation.metrics.jobs_interrupted;
        stats.debris_created += simulation.metrics.debris_created;
        stats.evacuation_failures += simulation.metrics.evacuation_failures;
        stats.capacity_delays += simulation.metrics.transition_capacity_delays;
        if (simulation.metrics.peak_falling_sections > stats.maximum_falling) {
            stats.maximum_falling = simulation.metrics.peak_falling_sections;
        }
        if (simulation.metrics.peak_debris_pieces > stats.maximum_debris) {
            stats.maximum_debris = simulation.metrics.peak_debris_pieces;
        }
    }
    return stats;
}

static void test_long_run_transition_batch(void) {
    const transition_batch_stats_t first = run_transition_batch();
    const transition_batch_stats_t replay = run_transition_batch();
    expect_true(memcmp(&first, &replay, sizeof(first)) == 0, "long-run transition batch replays deterministically");
    expect_true(first.transitions == TRANSITION_BATCH_COUNT && first.evacuation_failures == 0U && first.capacity_delays == 0U,
                "long-run transition batch completes without safety or capacity failures");

    printf("Transition batch: %" PRIu32 " minute changes, %" PRIu32 " digit collapses, %" PRIu32 " workers evacuated, %" PRIu32 " jobs interrupted\n", first.transitions, first.digits_changed,
           first.workers_evacuated, first.jobs_interrupted);
    printf("  debris: %" PRIu32 " created, max %u simultaneous; falling sections: max %u; evacuation failures: %" PRIu32 "; capacity delays: %" PRIu32 "\n", first.debris_created, first.maximum_debris,
           first.maximum_falling, first.evacuation_failures, first.capacity_delays);
}

int main(void) {
    test_single_digit_and_determinism();
    test_multi_digit_and_midnight();
    test_worker_interruption();
    test_long_run_transition_batch();

    if (failures != 0U) {
        fprintf(stderr, "%u Building Clock transition test(s) failed\n", failures);
        return 1;
    }
    puts("Building Clock transition tests passed");
    return 0;
}
