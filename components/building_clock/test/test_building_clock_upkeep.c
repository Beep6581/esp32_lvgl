#include "building_clock_generator.h"
#include "building_clock_scene.h"
#include "building_clock_upkeep.h"
#include "building_clock_world_selection.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef NDEBUG
#error "Upkeep tests require assertions"
#endif

static building_clock_world_t world;
static building_clock_simulation_t simulation;
static building_clock_simulation_t replay;
static building_clock_scene_snapshot_t snapshot;

static void setup(uint64_t seed) {
    const building_clock_world_selection_result_t selected = building_clock_select_world(&world, seed, BUILDING_CLOCK_GENERATOR_VERSION);
    assert(selected.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED || selected.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED_FALLBACK);
    assert(building_clock_simulation_init(&simulation, &world, 6U) == BUILDING_CLOCK_SIMULATION_OK);
    assert(building_clock_simulation_set_time(&simulation, &world, 14U, 37U) == BUILDING_CLOCK_SIMULATION_OK);
}

static void step(uint32_t elapsed) {
    const building_clock_simulation_t before = simulation;
    const building_clock_simulation_step_result_t result = building_clock_simulation_step(&simulation, &world, elapsed);
    assert(result.status == BUILDING_CLOCK_SIMULATION_OK && result.unconsumed_ms == 0U);
    for (uint8_t id = 0U; id < simulation.state.worker_count; id++) {
        const building_clock_worker_t* worker = &simulation.state.workers[id];
        const building_clock_worker_t* previous = &before.state.workers[id];
        /* No quiet-task interruption may snap a worker back to its nav anchor. */
        assert(abs(worker->position.x - previous->position.x) <= (int)(elapsed / BUILDING_CLOCK_WALK_MS_PER_UNIT) + 2);
        if (worker->site_activity == BUILDING_CLOCK_UPKEEP_NONE) {
            continue;
        }
        assert(worker->site_station_id < world.platform_count && worker->site_activity <= BUILDING_CLOCK_UPKEEP_SURVEY);
        const building_clock_platform_t* platform = &world.platforms[worker->site_station_id];
        assert(worker->position.y == platform->surface_y && worker->position.x >= platform->x_start && worker->position.x <= platform->x_end);
        assert(worker->platform_activity_position.y == platform->surface_y && worker->platform_activity_position.x >= platform->x_start && worker->platform_activity_position.x <= platform->x_end);
        assert(worker->current_job_id == BUILDING_CLOCK_INVALID_ID && worker->reserved_edge_id == BUILDING_CLOCK_INVALID_EDGE_ID);
        if (worker->site_partner_id < simulation.state.worker_count) {
            const building_clock_worker_t* partner = &simulation.state.workers[worker->site_partner_id];
            assert(partner->site_partner_id == id && partner->site_station_id == worker->site_station_id);
            assert(abs(worker->platform_activity_position.x - partner->platform_activity_position.x) == 24);
        }
    }
    for (uint8_t id = 0U; id < world.platform_count; id++) {
        const building_clock_upkeep_station_t* station = &simulation.state.upkeep[id];
        if (!station->enabled) {
            continue;
        }
        assert(station->tool_position.y == world.platforms[id].surface_y);
        assert(station->tool_position.x >= world.platforms[id].x_start && station->tool_position.x <= world.platforms[id].x_end);
        if (station->owner_worker_id < simulation.state.worker_count) {
            const building_clock_worker_t* owner = &simulation.state.workers[station->owner_worker_id];
            assert(owner->site_station_id == id && owner->site_activity != BUILDING_CLOCK_UPKEEP_NONE);
            if (owner->site_activity <= BUILDING_CLOCK_UPKEEP_CHECK_TOOLS && owner->site_phase >= BUILDING_CLOCK_UPKEEP_APPROACH) {
                assert(memcmp(&station->tool_position, &owner->position, sizeof(owner->position)) == 0);
            }
        }
    }
}

static void test_living_site(void) {
    const uint64_t seeds[] = {UINT64_C(0x2827ef2c6643fe98), UINT64_C(0x44b05394719c3d48), UINT64_C(0x6a09e667f3bcc909), UINT64_C(0xf9fd2ded6d5b25c6), UINT64_C(0xa678d9daf251c133)};
    unsigned kinds = 0U, families = 0U, interrupted_phases = 0U;
    uint32_t tool_travel = 0U, changed_dust = 0U, meetings = 0U;
    uint64_t active = 0U, moving = 0U, samples = 0U;
    for (unsigned seed = 0U; seed < sizeof(seeds) / sizeof(seeds[0]); seed++) {
        setup(seeds[seed]);
        families |= 1U << world.topology_family;
        replay = simulation;
        for (uint32_t ms = 0U; ms < 180000U; ms += 50U) {
            const building_clock_simulation_state_t before = simulation.state;
            step(50U);
            (void)building_clock_simulation_step(&replay, &world, 50U);
            assert(memcmp(&simulation, &replay, sizeof(simulation)) == 0);
            for (uint8_t id = 0U; id < simulation.state.worker_count; id++) {
                const building_clock_worker_t* worker = &simulation.state.workers[id];
                if (worker->site_activity != BUILDING_CLOCK_UPKEEP_NONE) {
                    kinds |= 1U << worker->site_activity;
                    active++;
                    if ((interrupted_phases & (1U << worker->site_phase)) == 0U) {
                        const building_clock_simulation_t saved = simulation;
                        const uint8_t partner = worker->site_partner_id;
                        const uint8_t station = worker->site_station_id;
                        interrupted_phases |= 1U << worker->site_phase;
                        building_clock_upkeep_interrupt(&simulation, &world, id);
                        assert(simulation.state.workers[id].site_activity == BUILDING_CLOCK_UPKEEP_NONE);
                        assert(memcmp(&saved.state.workers[id].position, &simulation.state.workers[id].position, sizeof(worker->position)) == 0);
                        assert(memcmp(&saved.state.upkeep[station].tool_position, &simulation.state.upkeep[station].tool_position, sizeof(building_clock_point_t)) == 0);
                        if (partner < simulation.state.worker_count) {
                            assert(simulation.state.workers[partner].site_activity == BUILDING_CLOCK_UPKEEP_NONE);
                        }
                        const building_clock_simulation_t interrupted = simulation;
                        building_clock_upkeep_interrupt(&simulation, &world, id);
                        assert(memcmp(&simulation, &interrupted, sizeof(simulation)) == 0);
                        simulation = saved;
                    }
                    if (worker->site_partner_id < simulation.state.worker_count && worker->site_phase == BUILDING_CLOCK_UPKEEP_WORK) {
                        meetings++;
                    }
                }
                moving += memcmp(&before.workers[id].position, &worker->position, sizeof(worker->position)) != 0 ? 1U : 0U;
            }
            for (uint8_t id = 0U; id < world.platform_count; id++) {
                tool_travel += memcmp(&before.upkeep[id].tool_position, &simulation.state.upkeep[id].tool_position, sizeof(building_clock_point_t)) != 0 ? 1U : 0U;
                changed_dust += before.upkeep[id].dust_mask != 0U && simulation.state.upkeep[id].dust_mask == 0U ? 1U : 0U;
            }
            if (ms % 1000U == 0U) {
                const building_clock_simulation_t saved = simulation;
                assert(building_clock_scene_capture(&world, &simulation, &snapshot) == BUILDING_CLOCK_SCENE_OK);
                assert(memcmp(&snapshot.state, &simulation.state, sizeof(snapshot.state)) == 0);
                assert(memcmp(&simulation, &saved, sizeof(saved)) == 0);
            }
            samples++;
        }
    }
    assert(families == 31U && kinds == 126U && interrupted_phases == 63U && meetings > 100U && changed_dust > 20U && tool_travel > 1000U);
    printf("Quiet site: 5 families x 180 s, %.2f active / %.2f moving workers, %u dust cleanups, %u tool-motion quanta, %u paired-work quanta; byte-identical replay\n",
           (double)active / (double)samples, (double)moving / (double)samples, changed_dust, tool_travel, meetings);
}

static void test_interruptions(void) {
    setup(UINT64_C(0x6a09e667f3bcc909));
    for (uint32_t ms = 0U; ms < 10000U; ms += 50U) {
        step(50U);
    }
    bool saw_tool = false;
    for (uint8_t id = 0U; id < simulation.state.worker_count; id++) {
        if (simulation.state.workers[id].site_activity >= BUILDING_CLOCK_UPKEEP_SWEEP && simulation.state.workers[id].site_activity <= BUILDING_CLOCK_UPKEEP_CHECK_TOOLS) {
            saw_tool = true;
        }
    }
    assert(saw_tool);
    const building_clock_simple_job_spec_t spec = {.kind = BUILDING_CLOCK_JOB_INSPECT, .priority = 10U, .target_node_id = world.worker_spawns[0].nav_node_id, .action_duration_ms = 500U};
    uint8_t job = BUILDING_CLOCK_INVALID_ID;
    assert(building_clock_simulation_add_job(&simulation, &world, &spec, &job) == BUILDING_CLOCK_SIMULATION_OK);
    for (uint32_t ms = 0U; ms < 30000U && simulation.state.jobs[job].status != BUILDING_CLOCK_JOB_COMPLETE; ms += 50U) {
        step(50U);
    }
    assert(simulation.state.jobs[job].status == BUILDING_CLOCK_JOB_COMPLETE);
    assert(building_clock_simulation_set_time(&simulation, &world, 14U, 38U) == BUILDING_CLOCK_SIMULATION_OK);
    (void)building_clock_simulation_step(&simulation, &world, 50U);
    for (uint8_t id = 0U; id < simulation.state.worker_count; id++) {
        assert(simulation.state.workers[id].site_activity == BUILDING_CLOCK_UPKEEP_NONE);
    }
    for (uint8_t id = 0U; id < world.platform_count; id++) {
        assert(simulation.state.upkeep[id].owner_worker_id == BUILDING_CLOCK_INVALID_ID);
    }
}

static void test_post_quiet_transitions(void) {
    const uint8_t minutes[] = {37U, 31U, 38U};
    unsigned under_minute = 0U;
    uint32_t maximum = 0U;
    uint64_t total = 0U;
    for (unsigned scenario = 0U; scenario < 3U; scenario++) {
        unsigned scenario_under = 0U;
        for (unsigned seed = 0U; seed < 32U; seed++) {
            setup(UINT64_C(0x6a09e667f3bcc909) + UINT64_C(0x9e3779b97f4a7c15) * seed);
            assert(building_clock_simulation_init(&simulation, &world, 6U) == BUILDING_CLOCK_SIMULATION_OK);
            assert(building_clock_simulation_set_time(&simulation, &world, 14U, minutes[scenario]) == BUILDING_CLOCK_SIMULATION_OK);
            for (uint32_t ms = 0U; ms < 15000U + seed * 250U; ms += 50U) {
                (void)building_clock_simulation_step(&simulation, &world, 50U);
            }
            assert(building_clock_simulation_set_time(&simulation, &world, 14U, (uint8_t)(minutes[scenario] + 1U)) == BUILDING_CLOCK_SIMULATION_OK);
            uint32_t elapsed = 0U;
            while (building_clock_simulation_transition_active(&simulation) && elapsed < 120000U) {
                (void)building_clock_simulation_step(&simulation, &world, 50U);
                elapsed += 50U;
            }
            assert(!building_clock_simulation_transition_active(&simulation) && simulation.metrics.evacuation_failures == 0U);
            under_minute += elapsed <= 60000U ? 1U : 0U;
            scenario_under += elapsed <= 60000U ? 1U : 0U;
            total += elapsed;
            if (elapsed > maximum) {
                maximum = elapsed;
            }
            /* Exercise an unfinished next rollover, then verify claims cannot leak. */
            assert(building_clock_simulation_set_time(&simulation, &world, 14U, 39U) == BUILDING_CLOCK_SIMULATION_OK);
            (void)building_clock_simulation_step(&simulation, &world, 1000U);
            assert(building_clock_simulation_set_time(&simulation, &world, 14U, 40U) == BUILDING_CLOCK_SIMULATION_OK);
            for (uint8_t id = 0U; id < world.platform_count; id++) {
                assert(simulation.state.upkeep[id].owner_worker_id == BUILDING_CLOCK_INVALID_ID);
            }
        }
        printf("After quiet work: 14:%02u->14:%02u %u/32 within 60 s\n", minutes[scenario], minutes[scenario] + 1U, scenario_under);
    }
    printf("After quiet work: %u/96 within 60 s, mean=%" PRIu64 " ms max=%u ms, no timeout/evacuation failures\n", under_minute, total / 96U, maximum);
    assert(under_minute >= 87U);
}

int main(void) {
    test_living_site();
    test_interruptions();
    test_post_quiet_transitions();
    printf("Memory: worker=%zu station=%zu state=%zu context=%zu snapshot=%zu bytes\n", sizeof(building_clock_worker_t), sizeof(building_clock_upkeep_station_t),
           sizeof(building_clock_simulation_state_t), sizeof(building_clock_simulation_t), sizeof(building_clock_scene_snapshot_t));
    return 0;
}
