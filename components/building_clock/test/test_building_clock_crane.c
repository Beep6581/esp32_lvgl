#include "building_clock_generator.h"
#include "building_clock_simulation.h"
#include "building_clock_world_selection.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef NDEBUG
#error "Crane tests require assertions"
#endif

static building_clock_world_t world;
static building_clock_simulation_t simulation;
static building_clock_simulation_t replay;

static void setup(uint64_t seed) {
    const building_clock_world_selection_result_t selected = building_clock_select_world(&world, seed, BUILDING_CLOCK_GENERATOR_VERSION);
    assert(selected.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED || selected.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED_FALLBACK);
    assert(building_clock_simulation_init(&simulation, &world, 6) == BUILDING_CLOCK_SIMULATION_OK);
    assert(building_clock_simulation_set_time(&simulation, &world, 14, 37) == BUILDING_CLOCK_SIMULATION_OK);
    assert(building_clock_simulation_set_time(&simulation, &world, 14, 38) == BUILDING_CLOCK_SIMULATION_OK);
}

static void check_ownership(void) {
    unsigned used = 0;
    unsigned operating_jobs = 0U;
    for (unsigned id = 0U; id < BUILDING_CLOCK_MAX_JOBS; id++) {
        const building_clock_job_t* job = &simulation.state.jobs[id];
        operating_jobs += job->kind == BUILDING_CLOCK_JOB_OPERATE_CRANE && job->status == BUILDING_CLOCK_JOB_ACTIVE ? 1U : 0U;
    }
    assert(operating_jobs <= 1U);
    for (unsigned id = 0; id < BUILDING_CLOCK_MAX_LOADS; id++) {
        const building_clock_load_t* load = &simulation.state.loads[id];
        used += load->status != BUILDING_CLOCK_LOAD_UNUSED;
        if (load->status == BUILDING_CLOCK_LOAD_SUSPENDED) {
            assert(load->machinery_kind == BUILDING_CLOCK_MACHINERY_CRANE && load->machinery_id < simulation.state.crane_count);
            const building_clock_crane_t* c = &simulation.state.cranes[load->machinery_id];
            const uint8_t companion = simulation.state.loads[c->suspended_load_id].companion_load_id;
            assert(c->suspended_load_id == id || companion == id);
            assert(load->carrier_worker_id == BUILDING_CLOCK_INVALID_ID);
            assert(load->position.x == c->hook_position.x + (c->suspended_load_id == id ? 0 : 7) && load->position.y == c->hook_position.y + 9);
        }
        if (load->status == BUILDING_CLOCK_LOAD_CARRIED) {
            assert(load->carrier_worker_id < simulation.state.worker_count);
            uint8_t first = simulation.state.workers[load->carrier_worker_id].carried_load_id;
            assert(first < BUILDING_CLOCK_MAX_LOADS && (first == id || simulation.state.loads[first].companion_load_id == id));
        }
        if (load->status == BUILDING_CLOCK_LOAD_LANDED) {
            assert(load->machinery_kind == BUILDING_CLOCK_MACHINERY_NONE && load->carrier_worker_id == BUILDING_CLOCK_INVALID_ID);
            assert(load->access_node_id < world.navigation.node_count);
            assert(load->position.y + 7 == world.navigation.nodes[load->access_node_id].position.y);
        }
    }
    assert(used == simulation.state.used_load_slot_count);
    for (unsigned id = 0; id < simulation.state.crane_count; id++) {
        const building_clock_crane_t* c = &simulation.state.cranes[id];
        if (c->status == BUILDING_CLOCK_CRANE_IDLE || c->status == BUILDING_CLOCK_CRANE_RETURNING) {
            assert(c->suspended_load_id == BUILDING_CLOCK_INVALID_ID && c->operator_job_id == BUILDING_CLOCK_INVALID_ID && c->operator_worker_id == BUILDING_CLOCK_INVALID_ID);
        } else {
            assert(c->operator_worker_id < simulation.state.worker_count && c->operator_job_id < BUILDING_CLOCK_MAX_JOBS);
            assert(simulation.state.workers[c->operator_worker_id].current_job_id == c->operator_job_id);
            assert(simulation.state.jobs[c->operator_job_id].kind == BUILDING_CLOCK_JOB_OPERATE_CRANE);
            if (c->suspended_load_id < BUILDING_CLOCK_MAX_LOADS && !c->abort_requested) {
                const building_clock_job_t* delivery = &simulation.state.jobs[c->delivery_job_id];
                assert(delivery->status == BUILDING_CLOCK_JOB_BLOCKED);
                assert(delivery->reserved_worker_id == BUILDING_CLOCK_INVALID_ID);
                for (uint8_t w = 0U; w < simulation.state.worker_count; w++) {
                    assert(simulation.state.workers[w].current_job_id != c->delivery_job_id);
                }
            }
        }
    }
    for (unsigned d = 0; d < 4; d++)
        for (unsigned s = 0; s < simulation.state.digits[d].incoming.section_count; s++) {
            uint8_t job = simulation.state.digits[d].incoming.sections[s].assigned_job_id;
            if (job < BUILDING_CLOCK_MAX_JOBS) {
                const building_clock_job_t* j = &simulation.state.jobs[job];
                assert(j->status == BUILDING_CLOCK_JOB_PENDING || j->status == BUILDING_CLOCK_JOB_BLOCKED || j->status == BUILDING_CLOCK_JOB_ACTIVE || j->status == BUILDING_CLOCK_JOB_RESERVED);
                assert(j->kind != BUILDING_CLOCK_JOB_OPERATE_CRANE);
            }
        }
}

static void test_cancellation(void) {
    const building_clock_crane_status_t phases[] = {BUILDING_CLOCK_CRANE_SLEWING, BUILDING_CLOCK_CRANE_LOWERING, BUILDING_CLOCK_CRANE_WAITING_FOR_ATTACHMENT,
                                                    BUILDING_CLOCK_CRANE_LIFTING, BUILDING_CLOCK_CRANE_CARRYING, BUILDING_CLOCK_CRANE_LOWERING_AT_DESTINATION};
    for (unsigned kind = 0; kind < 3; kind++)
        for (unsigned phase = 0; phase < sizeof(phases) / sizeof(phases[0]); phase++) {
            setup(UINT64_C(0xf9fd2ded6d5b25c6));
            for (unsigned ms = 0; ms < 30000 && simulation.state.cranes[0].status != phases[phase]; ms += 50) {
                assert(building_clock_simulation_step(&simulation, &world, 50).status == BUILDING_CLOCK_SIMULATION_OK);
                check_ownership();
            }
            assert(simulation.state.cranes[0].status == phases[phase]);
            simulation.crane_enabled = false;
            const building_clock_simulation_t before = simulation;
            const uint8_t cargo = simulation.state.cranes[0].suspended_load_id;
            const uint8_t companion = simulation.state.loads[cargo].companion_load_id;
            if (kind == 2) {
                assert(building_clock_simulation_set_time(&simulation, &world, 14, 39) == BUILDING_CLOCK_SIMULATION_OK);
            } else {
                uint8_t job = kind == 0 ? simulation.state.cranes[0].delivery_job_id : simulation.state.cranes[0].operator_job_id;
                assert(building_clock_simulation_cancel_job(&simulation, &world, job) == BUILDING_CLOCK_SIMULATION_OK);
                assert(building_clock_simulation_cancel_job(&simulation, &world, job) == BUILDING_CLOCK_SIMULATION_OK);
            }
            for (unsigned w = 0; w < before.state.worker_count; w++) {
                const uint8_t j = before.state.workers[w].current_job_id;
                const bool crane_crew =
                    j < BUILDING_CLOCK_MAX_JOBS && (before.state.jobs[j].kind == BUILDING_CLOCK_JOB_OPERATE_CRANE || before.state.jobs[j].required_machinery_kind == BUILDING_CLOCK_MACHINERY_CRANE);
                if (kind != 2 || crane_crew)
                    assert(memcmp(&before.state.workers[w].position, &simulation.state.workers[w].position, sizeof(building_clock_point_t)) == 0);
            }
            assert(memcmp(&before.state.loads[cargo].position, &simulation.state.loads[cargo].position, sizeof(building_clock_point_t)) == 0);
            for (unsigned ms = 0; ms < 30000 && simulation.state.cranes[0].status != BUILDING_CLOCK_CRANE_IDLE; ms += 50) {
                assert(building_clock_simulation_step(&simulation, &world, 50).status == BUILDING_CLOCK_SIMULATION_OK);
                check_ownership();
            }
            assert(simulation.state.cranes[0].status == BUILDING_CLOCK_CRANE_IDLE);
            assert(simulation.state.loads[cargo].machinery_kind != BUILDING_CLOCK_MACHINERY_CRANE && simulation.state.loads[cargo].status != BUILDING_CLOCK_LOAD_SUSPENDED);
            assert(companion == BUILDING_CLOCK_INVALID_ID ||
                   (simulation.state.loads[companion].machinery_kind != BUILDING_CLOCK_MACHINERY_CRANE && simulation.state.loads[companion].status != BUILDING_CLOCK_LOAD_SUSPENDED));
            assert(simulation.metrics.crane_aborts == 1 && simulation.metrics.crane_kits_returned == 2);
            assert(simulation.state.upkeep[0].owner_worker_id == BUILDING_CLOCK_INVALID_ID);
            /* The next target must finish; any recovered bay stock is a real
             * subsequent supply source, not stranded ownership on obsolete IDs. */
            for (unsigned ms = 0; ms < 150000 && building_clock_simulation_transition_active(&simulation); ms += 50) {
                assert(building_clock_simulation_step(&simulation, &world, 50).status == BUILDING_CLOCK_SIMULATION_OK);
                check_ownership();
            }
            assert(!building_clock_simulation_transition_active(&simulation));
            assert(simulation.state.used_load_slot_count == 0 && simulation.state.used_temp_structure_slot_count == 0 && simulation.metrics.evacuation_failures == 0);
            if (kind == 2U && phases[phase] == BUILDING_CLOCK_CRANE_CARRYING) {
                assert(building_clock_simulation_set_time(&simulation, &world, 14U, 40U) == BUILDING_CLOCK_SIMULATION_OK);
                for (unsigned ms = 0U; ms < 180000U && building_clock_simulation_transition_active(&simulation); ms += 50U) {
                    assert(building_clock_simulation_step(&simulation, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
                    check_ownership();
                }
                assert(!building_clock_simulation_transition_active(&simulation) && simulation.state.cranes[0].returned_kits == 0U);
            }
        }
    puts("Crane cancellation: all 6 in-flight phases x delivery/operator/new-minute interruption, ownership and physical continuity pass");
}

static void test_landed_cancellation(void) {
    for (unsigned changed = 0U; changed < 3U; changed++) {
        setup(UINT64_C(0xf9fd2ded6d5b25c6));
        while (simulation.state.cranes[0].status != BUILDING_CLOCK_CRANE_WAITING_FOR_UNLOAD) {
            assert(simulation.state.elapsed_ms < 30000U);
            assert(building_clock_simulation_step(&simulation, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
        }
        const uint8_t job_id = simulation.state.cranes[0].delivery_job_id;
        const uint8_t cargo = simulation.state.jobs[job_id].subject.object_id;
        const building_clock_point_t position = simulation.state.loads[cargo].position;
        assert(simulation.state.loads[cargo].status == BUILDING_CLOCK_LOAD_LANDED);
        if (changed == 1U) {
            assert(building_clock_simulation_set_time(&simulation, &world, 14U, 39U) == BUILDING_CLOCK_SIMULATION_OK);
        } else {
            const uint8_t cancelled_job = changed == 2U ? simulation.state.cranes[0].operator_job_id : job_id;
            assert(building_clock_simulation_cancel_job(&simulation, &world, cancelled_job) == BUILDING_CLOCK_SIMULATION_OK);
        }
        assert(simulation.state.loads[cargo].status == BUILDING_CLOCK_LOAD_LANDED);
        assert(memcmp(&position, &simulation.state.loads[cargo].position, sizeof(position)) == 0);
        assert(simulation.state.loads[cargo].digit_id == (changed == 2U ? 3U : BUILDING_CLOCK_INVALID_ID));
        check_ownership();
        assert(building_clock_simulation_step(&simulation, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
        assert(simulation.state.cranes[0].status == BUILDING_CLOCK_CRANE_RETURNING && simulation.metrics.crane_aborts == 0U);
        for (unsigned ms = 0U; ms < 180000U && building_clock_simulation_transition_active(&simulation); ms += 50U) {
            assert(building_clock_simulation_step(&simulation, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
            check_ownership();
        }
        assert(!building_clock_simulation_transition_active(&simulation) && simulation.state.used_load_slot_count == 0U);
    }
    puts("Landed cargo detaches immediately, survives cancellation and is reused at its real roof location");
}

static void test_replay_and_contention(void) {
    setup(UINT64_C(0xf9fd2ded6d5b25c6));
    replay = simulation;
    unsigned phases = 0;
    for (unsigned ms = 0; ms < 90000 && building_clock_simulation_transition_active(&simulation); ms += 50) {
        const building_clock_crane_t before = simulation.state.cranes[0];
        assert(building_clock_simulation_step(&simulation, &world, 50).status == BUILDING_CLOCK_SIMULATION_OK);
        assert(building_clock_simulation_step(&replay, &world, 50).status == BUILDING_CLOCK_SIMULATION_OK);
        assert(memcmp(&simulation, &replay, sizeof(simulation)) == 0);
        check_ownership();
        const building_clock_crane_t* c = &simulation.state.cranes[0];
        phases |= 1U << c->status;
        if (before.suspended_load_id < BUILDING_CLOCK_MAX_LOADS && c->suspended_load_id == before.suspended_load_id) {
            assert(abs(c->hook_position.x - before.hook_position.x) <= 5 && abs(c->hook_position.y - before.hook_position.y) <= 8);
            assert(c->hook_position.x == before.hook_position.x || c->hook_position.y == before.hook_position.y);
        }
    }
    assert(!building_clock_simulation_transition_active(&simulation) && simulation.metrics.crane_kits_delivered == 2);
    assert((phases & 0x1ffU) == 0x1ffU);
    assert(building_clock_simulation_init(&simulation, &world, 6) == BUILDING_CLOCK_SIMULATION_OK);
    simulation.crane_enabled = false;
    assert(building_clock_simulation_set_time(&simulation, &world, 14, 59) == BUILDING_CLOCK_SIMULATION_OK);
    assert(building_clock_simulation_set_time(&simulation, &world, 15, 0) == BUILDING_CLOCK_SIMULATION_OK);
    for (unsigned ms = 0; ms < 180000 && building_clock_simulation_transition_active(&simulation); ms += 50) {
        assert(building_clock_simulation_step(&simulation, &world, 50).status == BUILDING_CLOCK_SIMULATION_OK);
        check_ownership();
    }
    assert(!building_clock_simulation_transition_active(&simulation) && simulation.metrics.crane_trips == 0);
    puts("Crane replay, bounded velocity, manual-only multi-digit fallback and stable slot references pass");
}

static void test_dispatch_guards(void) {
    setup(UINT64_C(0xf9fd2ded6d5b25c6));
    while (simulation.state.cranes[0].status != BUILDING_CLOCK_CRANE_CARRYING) {
        assert(building_clock_simulation_step(&simulation, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
    }
    simulation.crane_enabled = false;
    for (uint32_t ms = 0U; ms < 120000U && building_clock_simulation_transition_active(&simulation); ms += 50U) {
        assert(building_clock_simulation_step(&simulation, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
        check_ownership();
    }
    assert(!building_clock_simulation_transition_active(&simulation) && simulation.metrics.crane_trips == 1U && simulation.metrics.crane_kits_delivered == 2U);
    /* Fault-inject insufficient reach: do not change geometry or add edges. */
    world.cranes[0].working_area.width = 1U;
    assert(building_clock_simulation_init(&simulation, &world, 6U) == BUILDING_CLOCK_SIMULATION_OK);
    assert(building_clock_simulation_set_time(&simulation, &world, 14U, 37U) == BUILDING_CLOCK_SIMULATION_OK);
    assert(building_clock_simulation_set_time(&simulation, &world, 14U, 38U) == BUILDING_CLOCK_SIMULATION_OK);
    for (uint32_t ms = 0U; ms < 120000U && building_clock_simulation_transition_active(&simulation); ms += 50U) {
        assert(building_clock_simulation_step(&simulation, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
    }
    assert(!building_clock_simulation_transition_active(&simulation) && simulation.metrics.crane_trips == 0U);
    puts("Crane dispatch guards: insufficient reach falls back; disabling new dispatch does not abandon in-flight cargo");
}

static void test_unfinished_rollovers(void) {
    const uint64_t seeds[] = {UINT64_C(0xf9fd2ded6d5b25c6), UINT64_C(0xa678d9daf251c133), UINT64_C(0x44b05394719c3d48), UINT64_C(0x19e3bcf86097df40)};
    for (unsigned seed = 0U; seed < sizeof(seeds) / sizeof(seeds[0]); seed++) {
        setup(seeds[seed]);
        replay = simulation;
        for (uint32_t ms = 0U; ms < 120000U; ms += 50U) {
            if (ms > 0U && ms % 15000U == 0U) {
                const uint8_t minute = (uint8_t)(38U + ms / 15000U);
                assert(building_clock_simulation_set_time(&simulation, &world, 14U, minute) == BUILDING_CLOCK_SIMULATION_OK);
                assert(building_clock_simulation_set_time(&replay, &world, 14U, minute) == BUILDING_CLOCK_SIMULATION_OK);
            }
            assert(building_clock_simulation_step(&simulation, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
            assert(building_clock_simulation_step(&replay, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
            assert(memcmp(&simulation, &replay, sizeof(simulation)) == 0);
            check_ownership();
        }
        for (uint32_t ms = 0U; ms < 300000U && building_clock_simulation_transition_active(&simulation); ms += 50U) {
            assert(building_clock_simulation_step(&simulation, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
            check_ownership();
        }
        assert(!building_clock_simulation_transition_active(&simulation) && simulation.metrics.evacuation_failures == 0U);
        assert(simulation.state.cranes[0].status == BUILDING_CLOCK_CRANE_IDLE);
    }
    puts("Repeated unfinished targets on four crane worlds: deterministic, safe and eventually stable");
}

static void test_retraction_and_reuse(void) {
    setup(UINT64_C(0x72db14d36a85161a));
    replay = simulation;
    const uint8_t minutes[] = {38U, 47U, 48U, 57U, 58U};
    unsigned retracting_quanta = 0U;
    for (unsigned minute = 0U; minute < sizeof(minutes) / sizeof(minutes[0]); minute++) {
        assert(building_clock_simulation_set_time(&simulation, &world, 14U, minutes[minute]) == BUILDING_CLOCK_SIMULATION_OK);
        assert(building_clock_simulation_set_time(&replay, &world, 14U, minutes[minute]) == BUILDING_CLOCK_SIMULATION_OK);
        for (uint32_t ms = 0U; ms < 240000U && building_clock_simulation_transition_active(&simulation); ms += 50U) {
            const building_clock_crane_t before = simulation.state.cranes[0];
            assert(building_clock_simulation_step(&simulation, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
            assert(building_clock_simulation_step(&replay, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
            assert(memcmp(&simulation, &replay, sizeof(simulation)) == 0);
            check_ownership();
            const building_clock_crane_t* crane = &simulation.state.cranes[0];
            if (before.status == BUILDING_CLOCK_CRANE_RETURNING) {
                if (before.hook_position.y != before.travel_y) {
                    assert(crane->hook_position.x == before.hook_position.x);
                    assert(crane->hook_position.y <= before.hook_position.y && before.hook_position.y - crane->hook_position.y <= 8);
                } else {
                    assert(crane->hook_position.y == before.travel_y);
                    assert(abs(crane->hook_position.x - before.hook_position.x) <= 5);
                    assert(abs(crane->hook_position.x - before.pickup_position.x) <= abs(before.hook_position.x - before.pickup_position.x));
                }
                if (crane->status != BUILDING_CLOCK_CRANE_RETURNING) {
                    assert(crane->hook_position.y == before.travel_y && crane->hook_position.x == before.pickup_position.x);
                }
                retracting_quanta++;
            }
        }
        assert(!building_clock_simulation_transition_active(&simulation));
        assert(simulation.state.cranes[0].status == BUILDING_CLOCK_CRANE_IDLE);
        assert(simulation.state.cranes[0].hook_position.y == simulation.state.cranes[0].travel_y);
        assert(simulation.state.used_load_slot_count == 0U);
    }
    assert(simulation.metrics.crane_trips >= 2U && simulation.metrics.crane_kits_delivered >= 4U && retracting_quanta > 0U);
    puts("One reusable crane: two real deliveries over five transitions, automatic vertical retraction and deterministic replay");
}

static void test_continuous_clock(void) {
    setup(UINT64_C(0xf9fd2ded6d5b25c6));
    replay = simulation;
    unsigned minutes_using_crane = 0U;
    unsigned hour_rollover_lifts = 0U;
    for (unsigned minute = 0U; minute < 40U; minute++) {
        const unsigned time = 14U * 60U + 38U + minute;
        const uint32_t before = simulation.metrics.crane_trips;
        assert(building_clock_simulation_set_time(&simulation, &world, (uint8_t)(time / 60U), (uint8_t)(time % 60U)) == BUILDING_CLOCK_SIMULATION_OK);
        assert(building_clock_simulation_set_time(&replay, &world, (uint8_t)(time / 60U), (uint8_t)(time % 60U)) == BUILDING_CLOCK_SIMULATION_OK);
        for (unsigned ms = 0U; ms < 60000U; ms += 50U) {
            assert(building_clock_simulation_step(&simulation, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
            assert(building_clock_simulation_step(&replay, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
            assert(memcmp(&simulation, &replay, sizeof(simulation)) == 0);
            check_ownership();
        }
        const uint32_t lifts = simulation.metrics.crane_trips - before;
        minutes_using_crane += lifts > 0U ? 1U : 0U;
        if (time == 15U * 60U) {
            hour_rollover_lifts = lifts;
        }
    }
    assert(minutes_using_crane > 1U && hour_rollover_lifts > 0U && simulation.metrics.evacuation_failures == 0U);
    printf("Continuous clock: %u lifts in %u/40 minutes, %u at 15:00; deterministic and safe\n", simulation.metrics.crane_trips, minutes_using_crane, hour_rollover_lifts);
}

int main(int argc, char** argv) {
    test_cancellation();
    test_landed_cancellation();
    test_replay_and_contention();
    test_dispatch_guards();
    test_unfinished_rollovers();
    test_retraction_and_reuse();
    test_continuous_clock();
    const unsigned roots = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 0) : 4U;
    assert(roots > 0U && roots <= 128U);
    const uint8_t times[][4] = {{14, 37, 14, 38}, {14, 31, 14, 32}, {14, 38, 14, 39}, {14, 59, 15, 0}, {23, 59, 0, 0}, {14, 37, 14, 38}, {14, 31, 14, 32}, {14, 38, 14, 39}};
    unsigned under[2][2] = {{0}};
    for (unsigned scenario = 0; scenario < sizeof(times) / sizeof(times[0]); scenario++) {
        for (unsigned root = 0; root < roots; root++) {
            const uint64_t seed = UINT64_C(0x6a09e667f3bcc909) + UINT64_C(0x9e3779b97f4a7c15) * root;
            const building_clock_world_selection_result_t selected = building_clock_select_world(&world, seed, BUILDING_CLOCK_GENERATOR_VERSION);
            assert(selected.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED || selected.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED_FALLBACK);
            for (unsigned enabled = 0; enabled < 2; enabled++) {
                assert(building_clock_simulation_init(&simulation, &world, 6) == BUILDING_CLOCK_SIMULATION_OK);
                simulation.crane_enabled = enabled != 0;
                assert(building_clock_simulation_set_time(&simulation, &world, times[scenario][0], times[scenario][1]) == BUILDING_CLOCK_SIMULATION_OK);
                if (scenario >= 5)
                    for (uint32_t quiet = 0; quiet < 15000U + root * 250U; quiet += 50U) {
                        assert(building_clock_simulation_step(&simulation, &world, 50).status == BUILDING_CLOCK_SIMULATION_OK);
                    }
                assert(building_clock_simulation_set_time(&simulation, &world, times[scenario][2], times[scenario][3]) == BUILDING_CLOCK_SIMULATION_OK);
                const clock_t cpu_start = clock();
                unsigned staged = 0;
                uint32_t ms = 0;
                while (building_clock_simulation_transition_active(&simulation) && ms < 300000) {
                    assert(building_clock_simulation_step(&simulation, &world, 50).status == BUILDING_CLOCK_SIMULATION_OK);
                    ms += 50;
                    check_ownership();
                    if (argc > 2 && root == (argc > 3 ? (unsigned)strtoul(argv[3], NULL, 0) : 0U) && scenario == (argc > 4 ? (unsigned)strtoul(argv[4], NULL, 0) : 0U) &&
                        (ms % 1000 == 0 || staged != simulation.metrics.sections_staged)) {
                        const building_clock_crane_t* c = &simulation.state.cranes[0];
                        fprintf(stderr, "t%u mode%u crane%u hook%d,%d cargo%u op%u staged%u placed%u secured%u", ms, enabled, c->status, c->hook_position.x, c->hook_position.y, c->suspended_load_id,
                                c->operator_worker_id, simulation.metrics.sections_staged, simulation.metrics.sections_placed, simulation.metrics.sections_secured);
                        for (unsigned w = 0; w < simulation.state.worker_count; w++)
                            fprintf(stderr, " w%u=s%u/j%u@%d,%d(r%u,%u/%u,p%u)", w, simulation.state.workers[w].state, simulation.state.workers[w].current_job_id,
                                    simulation.state.workers[w].position.x, simulation.state.workers[w].position.y, simulation.state.workers[w].route_purpose,
                                    simulation.state.workers[w].route_node_index, simulation.state.workers[w].route_edge_count, simulation.state.workers[w].platform_activity_phase);
                        fputc('\n', stderr);
                    }
                    staged = simulation.metrics.sections_staged;
                }
                const uint64_t cpu_us = (uint64_t)(clock() - cpu_start) * UINT64_C(1000000) / CLOCKS_PER_SEC;
                printf("scenario=%u root=%u crane=%u ms=%u trips=%u kits=%u aborts=%u estimate=%u cpu_us=%" PRIu64 "\n", scenario, root, enabled, ms, simulation.metrics.crane_trips,
                       simulation.metrics.crane_kits_delivered, simulation.metrics.crane_aborts, simulation.metrics.crane_estimated_saving_ms, cpu_us);
                assert(!building_clock_simulation_transition_active(&simulation));
                if (scenario < 3 || scenario >= 5)
                    under[scenario >= 5 ? 1 : 0][enabled] += (ms <= 60000U);
            }
        }
    }
    printf("Single-digit <=60s: reset manual=%u/%u auto=%u/%u; after-quiet manual=%u/%u auto=%u/%u\n", under[0][0], roots * 3U, under[0][1], roots * 3U, under[1][0], roots * 3U, under[1][1],
           roots * 3U);
    if (roots >= 32)
        assert(under[0][1] * 10U >= roots * 3U * 9U && under[1][1] * 10U >= roots * 3U * 9U);
    return 0;
}
