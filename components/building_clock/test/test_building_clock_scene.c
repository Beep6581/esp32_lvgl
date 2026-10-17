#include "building_clock_generator.h"
#include "building_clock_scene.h"
#include "building_clock_world_selection.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

static void make_scene(building_clock_world_t* world, building_clock_simulation_t* simulation) {
    const building_clock_world_selection_result_t selection = building_clock_select_world(world, UINT64_C(0x243f6a8885a308d3), BUILDING_CLOCK_GENERATOR_VERSION);
    expect_true(selection.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED || selection.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED_FALLBACK, "scene test world is accepted");
    expect_true(building_clock_simulation_init(simulation, world, 6U) == BUILDING_CLOCK_SIMULATION_OK, "scene test simulation initializes");
    expect_true(building_clock_simulation_set_time(simulation, world, 14U, 37U) == BUILDING_CLOCK_SIMULATION_OK, "scene test initial time is set");
}

static void test_capture_is_deterministic_and_read_only(void) {
    building_clock_world_t world;
    building_clock_simulation_t simulation;
    make_scene(&world, &simulation);

    const building_clock_simulation_t before = simulation;
    building_clock_scene_snapshot_t first;
    building_clock_scene_snapshot_t second;
    expect_true(building_clock_scene_capture(&world, &simulation, &first) == BUILDING_CLOCK_SCENE_OK, "first scene capture succeeds");
    expect_true(building_clock_scene_capture(&world, &simulation, &second) == BUILDING_CLOCK_SCENE_OK, "second scene capture succeeds");
    expect_true(first.world == &world && second.world == &world, "scene references the immutable accepted world");
    expect_true(memcmp(&first.state, &second.state, sizeof(first.state)) == 0 && memcmp(&first.navigation, &second.navigation, sizeof(first.navigation)) == 0 &&
                    memcmp(&first.metrics, &second.metrics, sizeof(first.metrics)) == 0 &&
                    memcmp(first.worker_presentation_positions, second.worker_presentation_positions, sizeof(first.worker_presentation_positions)) == 0,
                "unchanged simulation produces byte-equivalent semantic snapshots");
    expect_true(memcmp(&before, &simulation, sizeof(simulation)) == 0, "scene capture does not mutate simulation state");
}

static void test_broad_area_worker_spacing(void) {
    building_clock_world_t world;
    building_clock_simulation_t simulation;
    make_scene(&world, &simulation);

    const uint8_t shared_node = world.worker_spawns[0].nav_node_id;
    expect_true(building_clock_navigation_node_capacity(&world, shared_node) > 1U, "spacing fixture uses a broad semantic location");
    const building_clock_point_t shared_position = world.navigation.nodes[shared_node].position;
    for (uint8_t worker = 0U; worker < 4U; worker++) {
        simulation.state.workers[worker].state = BUILDING_CLOCK_WORKER_IDLE;
        simulation.state.workers[worker].position = shared_position;
        simulation.state.workers[worker].current_node_id = shared_node;
        simulation.state.workers[worker].reserved_edge_id = BUILDING_CLOCK_INVALID_EDGE_ID;
    }

    building_clock_scene_snapshot_t snapshot;
    expect_true(building_clock_scene_capture(&world, &simulation, &snapshot) == BUILDING_CLOCK_SCENE_OK, "shared broad-area scene capture succeeds");
    for (uint8_t first = 0U; first < 4U; first++) {
        expect_true(snapshot.state.workers[first].position.x == shared_position.x && snapshot.state.workers[first].position.y == shared_position.y,
                    "presentation spacing does not alter logical worker occupancy");
        for (uint8_t second = (uint8_t)(first + 1U); second < 4U; second++) {
            expect_true(snapshot.worker_presentation_positions[first].x != snapshot.worker_presentation_positions[second].x ||
                            snapshot.worker_presentation_positions[first].y != snapshot.worker_presentation_positions[second].y,
                        "workers sharing a broad location remain individually visible");
        }
    }
}

static void test_mid_edge_worker_and_active_job(void) {
    building_clock_world_t world;
    building_clock_simulation_t simulation;
    make_scene(&world, &simulation);

    building_clock_worker_t* worker = &simulation.state.workers[0];
    worker->state = BUILDING_CLOCK_WORKER_CLIMB;
    worker->facing = BUILDING_CLOCK_FACING_LEFT;
    worker->current_job_id = 4U;
    worker->reserved_edge_id = 3U;
    worker->reserved_destination_node_id = 7U;
    worker->edge_progress_per_mille = 375U;
    worker->position = (building_clock_point_t){.x = 123, .y = 234};
    simulation.state.jobs[4] = (building_clock_job_t){
        .kind = BUILDING_CLOCK_JOB_INSPECT,
        .status = BUILDING_CLOCK_JOB_ACTIVE,
        .target_node_id = 7U,
        .reserved_worker_id = 0U,
    };
    simulation.state.used_job_slot_count = 1U;

    building_clock_scene_snapshot_t snapshot;
    expect_true(building_clock_scene_capture(&world, &simulation, &snapshot) == BUILDING_CLOCK_SCENE_OK, "mid-edge scene capture succeeds");
    expect_true(snapshot.state.workers[0].position.x == 123 && snapshot.state.workers[0].position.y == 234 && snapshot.state.workers[0].edge_progress_per_mille == 375U,
                "snapshot preserves interpolated worker position and edge progress");
    expect_true(snapshot.state.workers[0].state == BUILDING_CLOCK_WORKER_CLIMB && snapshot.state.workers[0].facing == BUILDING_CLOCK_FACING_LEFT, "snapshot preserves worker pose semantics");
    expect_true(snapshot.state.jobs[4].kind == BUILDING_CLOCK_JOB_INSPECT && snapshot.state.jobs[4].status == BUILDING_CLOCK_JOB_ACTIVE, "snapshot exposes the worker's active semantic job");
}

static void test_collapse_scene_contents(void) {
    building_clock_world_t world;
    building_clock_simulation_t simulation;
    make_scene(&world, &simulation);
    expect_true(building_clock_simulation_set_time(&simulation, &world, 14U, 38U) == BUILDING_CLOCK_SIMULATION_OK, "collapse scene transition starts");

    bool saw_hazard = false;
    bool saw_falling = false;
    bool saw_debris = false;
    for (uint32_t elapsed = 0U; elapsed < 8000U; elapsed += BUILDING_CLOCK_SIMULATION_QUANTUM_MS) {
        (void)building_clock_simulation_step(&simulation, &world, BUILDING_CLOCK_SIMULATION_QUANTUM_MS);
        building_clock_scene_snapshot_t snapshot;
        expect_true(building_clock_scene_capture(&world, &simulation, &snapshot) == BUILDING_CLOCK_SCENE_OK, "transition scene capture succeeds");
        saw_hazard |= snapshot.state.hazard_count > 0U;
        saw_falling |= snapshot.state.falling_section_count > 0U;
        saw_debris |= snapshot.state.used_debris_slot_count > 0U;
        for (uint8_t hazard = 0U; hazard < snapshot.state.hazard_count; hazard++) {
            expect_true(snapshot.state.hazards[hazard].source_digit_id < BUILDING_CLOCK_DIGIT_COUNT, "snapshot hazard references a valid digit");
            for (uint8_t safe = 0U; safe < snapshot.state.hazards[hazard].safe_node_count; safe++) {
                expect_true(snapshot.state.hazards[hazard].safe_node_ids[safe] < world.navigation.node_count, "snapshot hazard references a valid retreat node");
            }
        }
        for (uint8_t falling = 0U; falling < snapshot.state.falling_section_count; falling++) {
            const building_clock_falling_section_t* section = &snapshot.state.falling_sections[falling];
            expect_true(section->source_digit_id < BUILDING_CLOCK_DIGIT_COUNT, "falling section references a valid digit");
            if (section->source_digit_id < BUILDING_CLOCK_DIGIT_COUNT && section->target_debris_bay_id < world.debris_bay_count) {
                expect_true(point_in_rect(section->landing_position, world.digit_work_areas[section->source_digit_id].collapse_catch_area), "snapshot exposes a valid local landing target");
                expect_true(section->progress_per_mille <= 1000U, "snapshot exposes bounded fall progress");
            }
        }
    }
    expect_true(saw_hazard && saw_falling && saw_debris, "snapshot exposes hazard, falling-section, and settled-debris phases");
}

static void test_invalid_counts_are_rejected(void) {
    building_clock_world_t world;
    building_clock_simulation_t simulation;
    make_scene(&world, &simulation);
    building_clock_scene_snapshot_t snapshot;

    simulation.state.worker_count = BUILDING_CLOCK_MAX_WORKERS + 1U;
    expect_true(building_clock_scene_capture(&world, &simulation, &snapshot) == BUILDING_CLOCK_SCENE_INVALID_STATE, "snapshot rejects an invalid runtime count");
    expect_true(snapshot.world == NULL, "failed capture clears the output snapshot");
}

int main(void) {
    test_capture_is_deterministic_and_read_only();
    test_broad_area_worker_spacing();
    test_mid_edge_worker_and_active_job();
    test_collapse_scene_contents();
    test_invalid_counts_are_rejected();

    if (failures != 0U) {
        fprintf(stderr, "%u Building Clock scene test(s) failed\n", failures);
        return 1;
    }
    puts("Building Clock scene tests passed");
    return 0;
}
