#include "building_clock_generator.h"
#include "building_clock_simulation.h"
#include "building_clock_world_selection.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#ifdef NDEBUG
#error "Site tests require assertions"
#endif

static building_clock_world_t world;
static building_clock_simulation_t simulation;
static building_clock_simulation_t replay;

static void setup(uint64_t seed) {
    const building_clock_world_selection_result_t result = building_clock_select_world(&world, seed, BUILDING_CLOCK_GENERATOR_VERSION);
    assert(result.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED || result.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED_FALLBACK);
    assert(building_clock_simulation_init(&simulation, &world, 6U) == BUILDING_CLOCK_SIMULATION_OK);
    assert(building_clock_simulation_set_time(&simulation, &world, 14U, 37U) == BUILDING_CLOCK_SIMULATION_OK);
}

static void check_recovery(void) {
    uint32_t accounted = simulation.state.used_debris_slot_count + simulation.metrics.rubble_sacks_recovered;
    for (uint8_t id = 0U; id < world.debris_bay_count; id++) {
        accounted += simulation.state.recovery_sacks[id];
    }
    for (uint8_t id = 0U; id < simulation.state.cart_count; id++) {
        const building_clock_cart_t* cart = &simulation.state.carts[id];
        accounted += cart->rubble_sacks;
        assert(cart->rubble_sacks <= 4U && cart->route_progress_per_mille <= 1000U);
        assert(cart->operator_worker_id == BUILDING_CLOCK_INVALID_ID || cart->operator_worker_id < simulation.state.worker_count);
    }
    assert(accounted == simulation.metrics.debris_created);
    assert(simulation.state.depot_inventory[0].quantity[BUILDING_CLOCK_MATERIAL_LOOSE_MASONRY] == simulation.metrics.rubble_sacks_recovered);
    for (uint8_t id = 0U; id < simulation.state.worker_count; id++) {
        const building_clock_worker_t* worker = &simulation.state.workers[id];
        if (worker->carried_debris_id < BUILDING_CLOCK_MAX_DEBRIS_PIECES) {
            const building_clock_debris_t* debris = &simulation.state.debris[worker->carried_debris_id];
            assert(debris->status == BUILDING_CLOCK_DEBRIS_CARRIED && debris->reserved_worker_id == id);
            assert(debris->position.x == worker->position.x && debris->position.y == worker->position.y - 7);
        }
        if (worker->passing_side != 0) {
            assert(worker->state == BUILDING_CLOCK_WORKER_WALK && worker->rope.phase == BUILDING_CLOCK_ROPE_NONE);
        }
    }
}

static void test_no_empty_shunting(void) {
    setup(UINT64_C(0x6a09e667f3bcc909));
    for (unsigned ms = 0U; ms < 15000U; ms += 50U) {
        assert(building_clock_simulation_step(&simulation, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
        for (uint8_t id = 0U; id < simulation.state.cart_count; id++) {
            assert(simulation.state.carts[id].route_progress_per_mille == 0U);
            assert(simulation.state.carts[id].operator_worker_id == BUILDING_CLOCK_INVALID_ID);
        }
    }
}

static void test_put_down_and_reassign(void) {
    setup(UINT64_C(0x6a09e667f3bcc909));
    assert(building_clock_simulation_set_time(&simulation, &world, 14U, 38U) == BUILDING_CLOCK_SIMULATION_OK);
    bool cancelled = false;
    for (unsigned ms = 0U; ms < 180000U; ms += 50U) {
        assert(building_clock_simulation_step(&simulation, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
        for (uint8_t id = 0U; !cancelled && id < simulation.state.worker_count; id++) {
            building_clock_worker_t* worker = &simulation.state.workers[id];
            if (worker->carried_debris_id < BUILDING_CLOCK_MAX_DEBRIS_PIECES && worker->state == BUILDING_CLOCK_WORKER_WALK) {
                const uint8_t debris_id = worker->carried_debris_id;
                const building_clock_point_t p = worker->position;
                assert(building_clock_simulation_cancel_job(&simulation, &world, worker->current_job_id) == BUILDING_CLOCK_SIMULATION_OK);
                assert(worker->carried_debris_id == BUILDING_CLOCK_INVALID_ID);
                assert(simulation.state.debris[debris_id].status == BUILDING_CLOCK_DEBRIS_RECOVERABLE);
                assert(simulation.state.debris[debris_id].position.x == p.x);
                cancelled = true;
            }
        }
        check_recovery();
    }
    assert(cancelled && !building_clock_simulation_transition_active(&simulation));
    assert(simulation.state.used_debris_slot_count == 0U);
}

static void test_recall_during_travel(void) {
    bool recalled = false;
    for (uint8_t root = 0U; root < 8U && !recalled; root++) {
        setup(UINT64_C(0x6a09e667f3bcc909) + root * UINT64_C(0x9e3779b97f4a7c15));
        /* A solo builder leaves no spare ground crew to fetch the trolley;
         * after rebuilding they must descend from their actual work roof. */
        assert(building_clock_simulation_init(&simulation, &world, 1U) == BUILDING_CLOCK_SIMULATION_OK);
        simulation.crew_limit = 1U;
        assert(building_clock_simulation_set_time(&simulation, &world, 14U, 37U) == BUILDING_CLOCK_SIMULATION_OK);
        assert(building_clock_simulation_set_time(&simulation, &world, 14U, 38U) == BUILDING_CLOCK_SIMULATION_OK);
        for (uint32_t ms = 0U; ms < 600000U && !recalled; ms += 50U) {
            assert(building_clock_simulation_step(&simulation, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
            for (uint8_t id = 0U; id < simulation.state.worker_count; id++) {
                building_clock_worker_t* worker = &simulation.state.workers[id];
                if (worker->current_job_id < BUILDING_CLOCK_MAX_JOBS && simulation.state.jobs[worker->current_job_id].kind == BUILDING_CLOCK_JOB_PUSH_CART &&
                    worker->reserved_edge_id < world.navigation.edge_count && world.navigation.edges[worker->reserved_edge_id].kind == BUILDING_CLOCK_NAV_EDGE_CLIMB) {
                    const building_clock_point_t p = worker->position;
                    const uint16_t edge = worker->reserved_edge_id;
                    assert(building_clock_simulation_set_time(&simulation, &world, 14U, 39U) == BUILDING_CLOCK_SIMULATION_OK);
                    assert(worker->position.x == p.x && worker->position.y == p.y && worker->reserved_edge_id == edge && worker->recovery_recalled);
                    replay = simulation;
                    assert(building_clock_simulation_step(&simulation, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
                    assert(building_clock_simulation_step(&replay, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
                    assert(memcmp(&simulation, &replay, sizeof(simulation)) == 0);
                    const int dy = worker->position.y - p.y;
                    assert(dy >= -3 && dy <= 3);
                    recalled = true;
                    break;
                }
            }
        }
    }
    assert(recalled);
    for (uint32_t ms = 0U; ms < 600000U; ms += 50U) {
        assert(building_clock_simulation_step(&simulation, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
        assert(building_clock_simulation_step(&replay, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
        assert(memcmp(&simulation, &replay, sizeof(simulation)) == 0);
        check_recovery();
    }
    assert(!building_clock_simulation_transition_active(&simulation) && simulation.state.current_minute == 39U);
    assert(simulation.state.used_debris_slot_count == 0U && simulation.state.used_load_slot_count == 0U);
}

static void test_generated_recovery_sequences(void) {
    static const uint8_t scenarios[][4] = {{14U, 37U, 14U, 38U}, {14U, 59U, 15U, 0U}, {23U, 59U, 0U, 0U}};
    uint32_t total_created = 0U, total_stored = 0U, total_aboard = 0U, total_recovered = 0U, total_deliveries = 0U, carried_samples = 0U, separated_samples = 0U, passing_samples = 0U;
    for (uint8_t root = 0U; root < 8U; root++) {
        for (uint8_t scenario = 0U; scenario < 3U; scenario++) {
            setup(UINT64_C(0x6a09e667f3bcc909) + root * UINT64_C(0x9e3779b97f4a7c15));
            /* Fresh initialization for multi-digit starting values. */
            simulation.state.clock_initialized = false;
            assert(building_clock_simulation_set_time(&simulation, &world, scenarios[scenario][0], scenarios[scenario][1]) == BUILDING_CLOCK_SIMULATION_OK);
            assert(building_clock_simulation_set_time(&simulation, &world, scenarios[scenario][2], scenarios[scenario][3]) == BUILDING_CLOCK_SIMULATION_OK);
            replay = simulation;
            const building_clock_world_t original = world;
            for (uint32_t ms = 0U; ms < 240000U; ms += 50U) {
                assert(building_clock_simulation_step(&simulation, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
                assert(building_clock_simulation_step(&replay, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
                assert(memcmp(&simulation, &replay, sizeof(simulation)) == 0);
                check_recovery();
                for (uint8_t id = 0U; id < simulation.state.worker_count; id++) {
                    const building_clock_worker_t* worker = &simulation.state.workers[id];
                    carried_samples += worker->carried_debris_id < BUILDING_CLOCK_MAX_DEBRIS_PIECES ? 1U : 0U;
                    passing_samples += worker->passing_side != 0 ? 1U : 0U;
                    if (worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_WORK) {
                        for (uint8_t other = (uint8_t)(id + 1U); other < simulation.state.worker_count; other++) {
                            const building_clock_worker_t* b = &simulation.state.workers[other];
                            if (b->platform_activity_phase == BUILDING_CLOCK_PLATFORM_WORK && worker->position.y == b->position.y) {
                                const int dx = worker->position.x - b->position.x;
                                assert(dx >= 15 || dx <= -15);
                                separated_samples++;
                            }
                        }
                    }
                }
            }
            assert(memcmp(&world, &original, sizeof(world)) == 0);
            assert(!building_clock_simulation_transition_active(&simulation));
            assert(simulation.state.used_load_slot_count == 0U && simulation.state.used_debris_slot_count == 0U && simulation.state.used_job_slot_count == 0U);
            assert(simulation.metrics.evacuation_failures == 0U);
            total_recovered += simulation.metrics.rubble_sacks_recovered;
            total_deliveries += simulation.metrics.trolley_loads_delivered;
            total_created += simulation.metrics.debris_created;
            for (uint8_t bay = 0U; bay < world.debris_bay_count; bay++) {
                total_stored += simulation.state.recovery_sacks[bay];
            }
            for (uint8_t cart = 0U; cart < simulation.state.cart_count; cart++) {
                total_aboard += simulation.state.carts[cart].rubble_sacks;
            }
        }
    }
    assert(total_recovered > 0U && total_deliveries > 0U && carried_samples > 0U && separated_samples > 0U && passing_samples > 0U);
    printf("Site recovery: 24 full generated sequences, %" PRIu32 " sacks recovered, %" PRIu32 " trolley deliveries, %" PRIu32 " created, %" PRIu32 " stored, %" PRIu32
           " aboard; carried samples=%" PRIu32 ", separate work samples=%" PRIu32 ", passing samples=%" PRIu32 "; conservation and replay passed\n",
           total_recovered, total_deliveries, total_created, total_stored, total_aboard, carried_samples, separated_samples, passing_samples);
}

int main(void) {
    test_no_empty_shunting();
    test_put_down_and_reassign();
    test_recall_during_travel();
    test_generated_recovery_sequences();
    return 0;
}
