/* Run the viewer's exact time/demo inputs without PNG compression or display.
 * Keeping this host-only avoids instrumentation in the firmware simulation. */
#define main building_clock_viewer_main
#include "building_clock_viewer.c"
#undef main

int main(int argc, char** argv) {
    viewer_t viewer = {.root_seed = DEFAULT_SEED, .generator_version = BUILDING_CLOCK_GENERATOR_VERSION, .scenario = SCENARIO_STABLE};
    if (argc > 1)
        viewer.root_seed = strtoull(argv[1], NULL, 0);
    if (argc > 2)
        viewer.scenario = parse_scenario(argv[2]);
    const uint32_t duration = argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 0) : 180000U;
    if (!reset_viewer(&viewer, true))
        return 1;
    FILE* csv = argc > 4 ? fopen(argv[4], "w") : NULL;
    if (argc > 4 && csv == NULL)
        return 1;
    if (csv != NULL)
        fprintf(csv, "ms,id,x,y,state,job,site,phase,partner\n");
    uint64_t movement = 0U, useful = 0U, crowds = 0U, still_crowds = 0U, samples = 0U;
    uint32_t stationary[BUILDING_CLOCK_MAX_WORKERS] = {0};
    uint32_t longest_still = 0U;
    const clock_t start = clock();
    for (uint32_t ms = 0U; ms < duration; ms += 250U) {
        const building_clock_simulation_state_t before = viewer.simulation.state;
        advance_viewer(&viewer, 250U);
        for (uint8_t id = 0U; id < viewer.simulation.state.worker_count; id++) {
            const building_clock_worker_t* worker = &viewer.simulation.state.workers[id];
            const bool moved = worker->position.x != before.workers[id].position.x || worker->position.y != before.workers[id].position.y;
            movement += moved ? 1U : 0U;
            unsigned site = 0U, phase = 0U, partner = BUILDING_CLOCK_INVALID_ID;
#ifdef BUILDING_CLOCK_TRACE_UPKEEP
            site = worker->site_activity;
            phase = worker->site_phase;
            partner = worker->site_partner_id;
#endif
            useful += worker->current_job_id < BUILDING_CLOCK_MAX_JOBS || site != 0U ? 1U : 0U;
            stationary[id] = moved ? 0U : stationary[id] + 250U;
            if (stationary[id] > longest_still)
                longest_still = stationary[id];
            if (csv != NULL)
                fprintf(csv, "%u,%u,%d,%d,%u,%u,%u,%u,%u\n", ms + 250U, id, worker->position.x, worker->position.y, worker->state, worker->current_job_id, site, phase, partner);
            for (uint8_t other = (uint8_t)(id + 1U); other < viewer.simulation.state.worker_count; other++) {
                const building_clock_worker_t* colleague = &viewer.simulation.state.workers[other];
                if (abs(worker->position.x - colleague->position.x) < 12 && abs(worker->position.y - colleague->position.y) < 8) {
                    crowds++;
                    const bool colleague_moved = memcmp(&before.workers[other].position, &colleague->position, sizeof(colleague->position)) != 0;
                    still_crowds += !moved && !colleague_moved ? 1U : 0U;
                }
            }
        }
        samples++;
    }
    if (csv != NULL && fclose(csv) != 0)
        return 1;
    printf("Living site: samples=%" PRIu64 " moving=%.2f useful=%.2f close-pairs=%.3f stationary-pairs=%.3f longest-still=%u ms context=%zu state=%zu worker=%zu cpu=%.3f s\n", samples,
           (double)movement / (double)samples, (double)useful / (double)samples, (double)crowds / (double)samples, (double)still_crowds / (double)samples, longest_still, sizeof(viewer.simulation),
           sizeof(viewer.simulation.state), sizeof(building_clock_worker_t), (double)(clock() - start) / CLOCKS_PER_SEC);
    return 0;
}
