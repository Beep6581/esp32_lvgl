#include "building_clock_generator.h"
#include "building_clock_simulation.h"
#include "building_clock_world_selection.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

/* This executable uses assertions for checked simulation calls in all builds. */
#ifdef NDEBUG
#error "Build the throughput tests with assertions enabled"
#endif

/* Host-only sampling; no profiling overhead in the firmware simulation. */
enum { BUILD, SUPPLY, CARRY, TRAVEL, ROPE_TRAVEL, TEND, CLEAR, WAIT, AMBIENT, ACTIVITY_COUNT };
static const char* const activities[] = {"build", "supply", "carry", "travel", "rope-travel", "tend", "clear", "wait", "ambient"};

enum { WAIT_NAVIGATION, WAIT_TENDER, WAIT_ROPE_LANE, WAIT_TURN, WAIT_KIND_COUNT };

static unsigned wait_kind(const building_clock_worker_t* worker, const building_clock_simulation_t* simulation) {
    if (worker->state == BUILDING_CLOCK_WORKER_YIELD_OR_TURN) {
        return WAIT_TURN;
    }
    if (worker->rope.phase == BUILDING_CLOCK_ROPE_NONE) {
        return WAIT_NAVIGATION;
    }
    if (worker->rope.belay_job_id < BUILDING_CLOCK_MAX_JOBS) {
        const uint8_t tender = simulation->state.jobs[worker->rope.belay_job_id].reserved_worker_id;
        if (tender >= simulation->state.worker_count || simulation->state.workers[tender].state != BUILDING_CLOCK_WORKER_OPERATE_MACHINERY) {
            return WAIT_TENDER;
        }
    }
    return WAIT_ROPE_LANE;
}

static unsigned activity(const building_clock_worker_t* worker, const building_clock_simulation_t* simulation) {
    if (worker->state == BUILDING_CLOCK_WORKER_WAIT || worker->state == BUILDING_CLOCK_WORKER_YIELD_OR_TURN) {
        return WAIT;
    }
    if (worker->rope.phase != BUILDING_CLOCK_ROPE_NONE && worker->rope.phase != BUILDING_CLOCK_ROPE_WORK) {
        return ROPE_TRAVEL;
    }
    if (worker->reserved_edge_id != BUILDING_CLOCK_INVALID_EDGE_ID || worker->route_node_index < worker->route_edge_count) {
        return worker->carried_load_id < BUILDING_CLOCK_MAX_LOADS ? CARRY : TRAVEL;
    }
    if (worker->current_job_id < BUILDING_CLOCK_MAX_JOBS) {
        const building_clock_job_t* job = &simulation->state.jobs[worker->current_job_id];
        if (job->kind == BUILDING_CLOCK_JOB_BELAY_ROPE) {
            return TEND;
        }
        if (job->kind == BUILDING_CLOCK_JOB_STAGE_MATERIAL) {
            return SUPPLY;
        }
        if (job->kind == BUILDING_CLOCK_JOB_CLEAR_DEBRIS) {
            return CLEAR;
        }
        return BUILD;
    }
    return AMBIENT;
}

int main(int argc, char** argv) {
    unsigned failures = 0U;
    const unsigned roots = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 0) : 8U;
    assert(roots > 0U && roots <= 128U);
    static const uint8_t times[][4] = {{14, 37, 14, 38}, {14, 31, 14, 32}, {14, 38, 14, 39}, {14, 59, 15, 0}, {23, 59, 0, 0}};
    for (unsigned mode = 0U; mode < 3U; mode++) {
        const unsigned crew = mode == 1U ? 9U : 6U;
        for (unsigned scenario = 0U; scenario < sizeof(times) / sizeof(times[0]); scenario++) {
            uint64_t samples[ACTIVITY_COUNT] = {0};
            uint64_t waits[WAIT_KIND_COUNT] = {0};
            uint64_t total_ms = 0U;
            uint64_t productive_samples = 0U;
            unsigned peak = 0U, under_minute = 0U, timeouts = 0U, climbs = 0U;
            unsigned pairs = 0U, chains = 0U, relief = 0U;
            uint64_t five_active_samples = 0U;
            uint8_t peak_jobs = 0U, peak_loads = 0U;
            uint32_t durations[128] = {0};
            unsigned families[5] = {0};
            for (unsigned root = 0U; root < roots; root++) {
                building_clock_world_t world;
                building_clock_simulation_t simulation;
                const uint64_t seed = UINT64_C(0x6a09e667f3bcc909) + root * UINT64_C(0x9e3779b97f4a7c15);
                const building_clock_world_selection_result_t selection = building_clock_select_world(&world, seed, BUILDING_CLOCK_GENERATOR_VERSION);
                assert(selection.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED || selection.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED_FALLBACK);
                assert(building_clock_simulation_init(&simulation, &world, (uint8_t)crew) == BUILDING_CLOCK_SIMULATION_OK);
                simulation.crew_limit = mode == 2U ? BUILDING_CLOCK_MAX_WORKERS : (uint8_t)crew;
                assert(building_clock_simulation_set_time(&simulation, &world, times[scenario][0], times[scenario][1]) == BUILDING_CLOCK_SIMULATION_OK);
                assert(building_clock_simulation_set_time(&simulation, &world, times[scenario][2], times[scenario][3]) == BUILDING_CLOCK_SIMULATION_OK);
                families[world.topology_family]++;
                building_clock_rope_phase_t previous[BUILDING_CLOCK_MAX_WORKERS] = {0};
                uint32_t ms = 0U;
                while (building_clock_simulation_transition_active(&simulation) && ms < 600000U) {
                    assert(building_clock_simulation_step(&simulation, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
                    ms += 50U;
                    if (argc > 2 && root == 0U && scenario == 0U && crew == 6U && ms % 5000U == 0U) {
                        fprintf(stderr, "t=%u phase=%u", ms, simulation.state.digits[3].lifecycle);
                        for (uint8_t w = 0U; w < simulation.state.worker_count; w++) {
                            const building_clock_worker_t* worker = &simulation.state.workers[w];
                            fprintf(stderr, " w%u=%u/j%u/r%u", w, worker->state, worker->current_job_id, worker->rope.phase);
                        }
                        fputs(" sections:", stderr);
                        for (uint8_t s = 0U; s < simulation.state.digits[3].incoming.section_count; s++) {
                            fprintf(stderr, " %u", simulation.state.digits[3].incoming.sections[s].status);
                        }
                        fputc('\n', stderr);
                    }
                    unsigned productive = 0U;
                    for (uint8_t w = 0U; w < simulation.state.worker_count; w++) {
                        const building_clock_worker_t* worker = &simulation.state.workers[w];
                        const unsigned kind = activity(worker, &simulation);
                        samples[kind]++;
                        if (kind == WAIT) {
                            waits[wait_kind(worker, &simulation)]++;
                        }
                        productive += kind != AMBIENT && kind != WAIT ? 1U : 0U;
                        climbs += worker->rope.phase == BUILDING_CLOCK_ROPE_ASCEND && previous[w] != worker->rope.phase ? 1U : 0U;
                        previous[w] = worker->rope.phase;
                    }
                    productive_samples += productive;
                    five_active_samples += productive >= 5U ? 1U : 0U;
                    if (productive > peak) {
                        peak = productive;
                    }
                }
                timeouts += building_clock_simulation_transition_active(&simulation) ? 1U : 0U;
                if (building_clock_simulation_transition_active(&simulation)) {
                    fprintf(stderr, "Timeout root=%u seed=%016" PRIx64 " scenario=%u crew=%u\n", root, seed, scenario, crew);
                    for (uint8_t w = 0U; w < simulation.state.worker_count; w++) {
                        const building_clock_worker_t* worker = &simulation.state.workers[w];
                        fprintf(stderr, " worker %u state=%u job=%u node=%u rope=%u pos=%d,%d\n", w, worker->state, worker->current_job_id, worker->current_node_id, worker->rope.phase,
                                worker->position.x, worker->position.y);
                    }
                    for (uint8_t d = 0U; d < 4U; d++) {
                        fprintf(stderr, " digit %u lifecycle=%u\n", d, simulation.state.digits[d].lifecycle);
                    }
                    for (uint8_t j = 0U; j < BUILDING_CLOCK_MAX_JOBS; j++) {
                        const building_clock_job_t* job = &simulation.state.jobs[j];
                        if (job->status != BUILDING_CLOCK_JOB_UNUSED) {
                            fprintf(stderr, " job %u kind=%u status=%u source=%u target=%u digit=%u section=%u\n", j, job->kind, job->status, job->source_node_id, job->target_node_id,
                                    job->subject.digit_id, job->subject.section_id);
                        }
                    }
                }
                under_minute += ms <= 60000U ? 1U : 0U;
                total_ms += ms;
                durations[root] = ms;
                pairs += simulation.metrics.paired_deliveries;
                chains += simulation.metrics.chained_rope_sections;
                relief += simulation.metrics.relief_workers_entered;
                if (simulation.metrics.peak_job_slots > peak_jobs) {
                    peak_jobs = simulation.metrics.peak_job_slots;
                }
                if (simulation.metrics.peak_load_slots > peak_loads) {
                    peak_loads = simulation.metrics.peak_load_slots;
                }
                assert(simulation.metrics.evacuation_failures == 0U);
                if (!building_clock_simulation_transition_active(&simulation)) {
                    assert(simulation.state.used_load_slot_count == 0U);
                    assert(simulation.state.used_debris_slot_count == 0U);
                }
            }
            for (unsigned i = 1U; i < roots; i++) {
                const uint32_t value = durations[i];
                unsigned j = i;
                while (j > 0U && durations[j - 1U] > value) {
                    durations[j] = durations[j - 1U];
                    j--;
                }
                durations[j] = value;
            }
            printf("crew=%u%s %02u:%02u->%02u:%02u roots=%u mean=%" PRIu64 " p50=%u p90=%u max=%u ms <=60s=%u timeout=%u productive=%.2f peak=%u ascents=%.1f families=%u/%u/%u/%u/%u\n", crew,
                   mode == 2U ? "+relief" : "", times[scenario][0], times[scenario][1], times[scenario][2], times[scenario][3], roots, total_ms / roots, durations[roots / 2U],
                   durations[(roots * 9U - 1U) / 10U], durations[roots - 1U], under_minute, timeouts, (double)productive_samples * 50.0 / (double)total_ms, peak, (double)climbs / roots, families[0],
                   families[1], families[2], families[3], families[4]);
            uint64_t all_samples = 0U;
            for (unsigned k = 0U; k < ACTIVITY_COUNT; k++) {
                all_samples += samples[k];
            }
            for (unsigned k = 0U; k < ACTIVITY_COUNT; k++) {
                printf(" %s=%.1f%%", activities[k], 100.0 * (double)samples[k] / (double)all_samples);
            }
            puts("");
            printf(" waits: navigation=%.1f%% tender=%.1f%% rope-lane=%.1f%% turn=%.1f%%; >=5 active=%.1f%% paired-deliveries=%.1f chained-sections=%.1f relief=%.1f peak-jobs=%u peak-loads=%u\n",
                   100.0 * (double)waits[WAIT_NAVIGATION] / (double)all_samples, 100.0 * (double)waits[WAIT_TENDER] / (double)all_samples, 100.0 * (double)waits[WAIT_ROPE_LANE] / (double)all_samples,
                   100.0 * (double)waits[WAIT_TURN] / (double)all_samples, (double)five_active_samples * 5000.0 / (double)total_ms, (double)pairs / roots, (double)chains / roots,
                   (double)relief / roots, peak_jobs, peak_loads);
            failures += timeouts;
        }
    }
    printf("Host sizes: simulation=%zu state=%zu worker=%zu load=%zu\n", sizeof(building_clock_simulation_t), sizeof(building_clock_simulation_state_t), sizeof(building_clock_worker_t),
           sizeof(building_clock_load_t));
    return failures == 0U ? 0 : 1;
}
