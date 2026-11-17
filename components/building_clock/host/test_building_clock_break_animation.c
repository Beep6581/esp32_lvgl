/* Exercise the real viewer, not a second implementation of its animations. */
#ifdef NDEBUG
#undef NDEBUG /* Keep these checks active in optimized viewer builds too. */
#endif
#define main building_clock_viewer_main
#include "building_clock_viewer.c"
#undef main
#include <assert.h>
#include <limits.h>

static void prepare_break_scene(building_clock_scene_snapshot_t* scene, building_clock_world_t* world) {
    memset(world, 0, sizeof(*world));
    memset(scene, 0, sizeof(*scene));
    scene->world = world;
    world->platform_count = 3U;
    world->platforms[0] = (building_clock_platform_t){.x_start = 20, .x_end = 230, .surface_y = 150};
    world->platforms[1] = (building_clock_platform_t){.x_start = 250, .x_end = 420, .surface_y = 150};
    world->platforms[2] = (building_clock_platform_t){.x_start = 10, .x_end = 465, .surface_y = 320};
    scene->state.worker_count = 4U;
    for (uint8_t id = 0U; id < scene->state.worker_count; id++) {
        building_clock_worker_t* worker = &scene->state.workers[id];
        worker->state = (id & 1U) == 0U ? BUILDING_CLOCK_WORKER_SMOKE : BUILDING_CLOCK_WORKER_DRINK;
        worker->facing = id < 2U ? BUILDING_CLOCK_FACING_RIGHT : BUILDING_CLOCK_FACING_LEFT;
        worker->position = (building_clock_point_t){(int16_t)(id == 3U ? 414 : 65 + id * 100U), 150};
        worker->current_job_id = BUILDING_CLOCK_INVALID_ID;
        worker->reserved_edge_id = BUILDING_CLOCK_INVALID_EDGE_ID;
        worker->carried_load_id = BUILDING_CLOCK_INVALID_ID;
        worker->carried_debris_id = BUILDING_CLOCK_INVALID_ID;
        scene->worker_presentation_positions[id] = worker->position;
    }
}

static void render_break_scene(SDL_Renderer* renderer, building_clock_scene_snapshot_t* scene, uint32_t ms) {
    scene->state.elapsed_ms = ms;
    set_color(renderer, COLOR_BACKGROUND);
    (void)SDL_RenderClear(renderer);
    draw_platforms(renderer, scene->world);
    for (uint8_t id = 0U; id < scene->state.worker_count; id++) {
        scene->state.workers[id].behavior_timer_ms = 4000U + ms;
        if (scene->state.workers[id].state == BUILDING_CLOCK_WORKER_DRINK) {
            scene->state.bottle_throws[id] = ms < BUILDING_CLOCK_BOTTLE_RELEASE_MS
                                                 ? (building_clock_bottle_throw_t){0}
                                                 : (building_clock_bottle_throw_t){.elapsed_ms = BUILDING_CLOCK_BOTTLE_RELEASE_MS,
                                                                                   .origin = scene->state.workers[id].position,
                                                                                   .direction = scene->state.workers[id].facing == BUILDING_CLOCK_FACING_LEFT ? -1 : 1};
        }
        draw_worker(renderer, scene, id, false);
    }
    draw_bottle_throws(renderer, scene);
    SDL_RenderPresent(renderer);
}

static uint32_t draw_and_hash(SDL_Renderer* renderer, SDL_Surface* canvas, const building_clock_scene_snapshot_t* scene) {
    set_color(renderer, COLOR_BACKGROUND);
    (void)SDL_RenderClear(renderer);
    draw_worker(renderer, scene, 0U, false);
    draw_bottle_throws(renderer, scene);
    SDL_RenderPresent(renderer);
    uint32_t hash = UINT32_C(2166136261);
    for (int y = 0; y < canvas->h; y++) {
        const uint32_t* row = (const uint32_t*)((const uint8_t*)canvas->pixels + y * canvas->pitch);
        for (int x = 0; x < canvas->w; x++)
            hash = (hash ^ row[x]) * UINT32_C(16777619);
    }
    return hash;
}

static void test_timing_and_landings(const building_clock_world_t* world) {
    building_clock_worker_t first_break = {.behavior_timer_ms = 1000U};
    assert(break_elapsed_ms(&first_break) == 0U);
    first_break.behavior_timer_ms = 2500U;
    assert(break_elapsed_ms(&first_break) == 2000U);
    first_break.behavior_timer_ms = 3999U;
    assert(break_elapsed_ms(&first_break) > 3990U);
    first_break.behavior_timer_ms = 4000U;
    assert(break_elapsed_ms(&first_break) == 0U);
    const break_pose_t smoking_down = break_pose(BUILDING_CLOCK_WORKER_SMOKE, 0U);
    const break_pose_t smoking_up = break_pose(BUILDING_CLOCK_WORKER_SMOKE, 700U);
    const break_pose_t smoking_again = break_pose(BUILDING_CLOCK_WORKER_SMOKE, 2450U);
    assert(smoking_down.hand_y > smoking_up.hand_y && smoking_again.hand_y == smoking_up.hand_y);
    assert(break_pose(BUILDING_CLOCK_WORKER_DRINK, 1000U).hand_y < break_pose(BUILDING_CLOCK_WORKER_DRINK, 0U).hand_y);
    assert(break_pose(BUILDING_CLOCK_WORKER_DRINK, 2150U).hand_x == -4);
    assert(break_pose(BUILDING_CLOCK_WORKER_DRINK, 2600U).hand_x == 6);
    const bottle_landing_t upper = tossed_bottle_landing(world, (building_clock_point_t){165, 150}, 1);
    const bottle_landing_t lower = tossed_bottle_landing(world, (building_clock_point_t){414, 150}, -1);
    assert(upper.hit && upper.position.y == 147 && upper.elapsed_ms > 150U);
    assert(lower.hit && lower.position.y == 317 && lower.elapsed_ms > upper.elapsed_ms);
    assert(lower.elapsed_ms < 1400U);
    const building_clock_world_t empty = {0};
    assert(!tossed_bottle_landing(&empty, (building_clock_point_t){165, 150}, 1).hit);
    for (uint32_t ms = 0U; ms < 1400U; ms++) {
        const building_clock_point_t right = tossed_bottle_position((building_clock_point_t){240, 150}, 1, ms);
        const building_clock_point_t left = tossed_bottle_position((building_clock_point_t){240, 150}, -1, ms);
        assert(right.x + left.x == 480 && right.y == left.y);
        const building_clock_point_t next = tossed_bottle_position((building_clock_point_t){240, 150}, 1, ms + 1U);
        if (ms >= 150U)
            assert(next.y >= right.y);
    }
}

static void test_arm_reach_and_pouring(void) {
    for (unsigned activity = 0U; activity < 2U; activity++) {
        for (uint32_t ms = 0U; ms < 4000U; ms++) {
            const break_pose_t pose = break_pose(activity == 0U ? BUILDING_CLOCK_WORKER_SMOKE : BUILDING_CLOCK_WORKER_DRINK, ms);
            const int upper_x = pose.elbow_x - 2, upper_y = pose.elbow_y + 9;
            const int fore_x = pose.hand_x - pose.elbow_x, fore_y = pose.hand_y - pose.elbow_y;
            assert(upper_x * upper_x + upper_y * upper_y <= 36);
            assert(fore_x * fore_x + fore_y * fore_y <= 25);
        }
    }
    assert(held_bottle_position(1000U).x - break_pose(BUILDING_CLOCK_WORKER_DRINK, 1000U).hand_x >= 6);
    for (uint32_t ms = 0U; ms < 360U; ms += 10U) {
        for (unsigned drop = 0U; drop < 4U; drop++) {
            const building_clock_point_t a = pouring_drop_position(ms, drop, 1);
            const building_clock_point_t b = pouring_drop_position(ms, drop, -1);
            assert(a.x == -b.x && a.y == b.y);
            assert(a.x >= 3 && a.x <= 10 && a.y >= -14 && a.y <= -11);
        }
    }
    const building_clock_point_t early = pouring_drop_position(0U, 0U, 1);
    const building_clock_point_t later = pouring_drop_position(250U, 0U, 1);
    assert(later.x < early.x && later.y > early.y);
}

static unsigned count_ink(SDL_Surface* canvas) {
    const uint32_t background = SDL_MapRGBA(canvas->format, COLOR_BACKGROUND.r, COLOR_BACKGROUND.g, COLOR_BACKGROUND.b, 255U);
    unsigned ink = 0U;
    for (int y = 0; y < canvas->h; y++) {
        const uint32_t* row = (const uint32_t*)((const uint8_t*)canvas->pixels + y * canvas->pitch);
        for (int x = 0; x < canvas->w; x++)
            ink += row[x] != background ? 1U : 0U;
    }
    return ink;
}

static void test_bottle_and_smash(SDL_Renderer* renderer, SDL_Surface* canvas) {
    unsigned minimum = UINT_MAX, maximum = 0U;
    for (unsigned pose = 0U; pose < 8U; pose++) {
        for (int direction = -1; direction <= 1; direction += 2) {
            set_color(renderer, COLOR_BACKGROUND);
            (void)SDL_RenderClear(renderer);
            draw_bottle(renderer, 240, 240, pose, direction);
            SDL_RenderPresent(renderer);
            const unsigned ink = count_ink(canvas);
            const uint32_t green = SDL_MapRGBA(canvas->format, COLOR_BOTTLE.r, COLOR_BOTTLE.g, COLOR_BOTTLE.b, 255U);
            const uint32_t blue = SDL_MapRGBA(canvas->format, COLOR_BOTTLE_LABEL.r, COLOR_BOTTLE_LABEL.g, COLOR_BOTTLE_LABEL.b, 255U);
            const uint32_t background = SDL_MapRGBA(canvas->format, COLOR_BACKGROUND.r, COLOR_BACKGROUND.g, COLOR_BACKGROUND.b, 255U);
            for (int y = 233; y <= 247; y++) {
                const uint32_t* row = (const uint32_t*)((const uint8_t*)canvas->pixels + y * canvas->pitch);
                for (int x = 233; x <= 247; x++)
                    assert(row[x] == green || row[x] == blue || row[x] == background);
            }
            minimum = ink < minimum ? ink : minimum;
            maximum = ink > maximum ? ink : maximum;
        }
    }
    assert(minimum >= 20U && maximum - minimum <= 4U);
    const building_clock_platform_t platform = {.x_start = 0, .x_end = 479, .surface_y = 243};
    for (uint32_t age = 0U; age <= 750U; age += 50U) {
        set_color(renderer, COLOR_BACKGROUND);
        (void)SDL_RenderClear(renderer);
        draw_bottle_smash(renderer, (building_clock_point_t){240, 240}, &platform, 1, age);
        SDL_RenderPresent(renderer);
        assert(age < 750U ? count_ink(canvas) == 5U : count_ink(canvas) == 0U);
        for (unsigned id = 0U; id < 5U; id++) {
            const building_clock_point_t start = rolling_bottle_fragment((building_clock_point_t){240, 240}, 1, 0U, id);
            const building_clock_point_t now = rolling_bottle_fragment((building_clock_point_t){240, 240}, 1, age, id);
            assert(now.x <= start.x && start.x - now.x <= 8 && now.y >= 241 && now.y <= 242);
            const building_clock_point_t mirrored = rolling_bottle_fragment((building_clock_point_t){240, 240}, -1, age, id);
            assert(mirrored.x + now.x == 480 && mirrored.y == now.y);
        }
    }
    printf("Bottle rotation footprint: %u..%u pixels; five rolling pixels for 750 ms\n", minimum, maximum);
}

static void test_render_replay(SDL_Renderer* renderer, SDL_Surface* canvas, building_clock_scene_snapshot_t* scene) {
    for (unsigned state = 0U; state < 2U; state++) {
        building_clock_worker_t* worker = &scene->state.workers[0];
        worker->state = state == 0U ? BUILDING_CLOCK_WORKER_SMOKE : BUILDING_CLOCK_WORKER_DRINK;
        for (unsigned facing = 0U; facing < 2U; facing++) {
            worker->facing = facing == 0U ? BUILDING_CLOCK_FACING_LEFT : BUILDING_CLOCK_FACING_RIGHT;
            uint32_t first_hash = 0U;
            unsigned changed = 0U;
            for (uint32_t ms = 0U; ms < 4000U; ms += 50U) {
                worker->behavior_timer_ms = 4000U + ms;
                const building_clock_scene_snapshot_t before = *scene;
                const uint32_t hash = draw_and_hash(renderer, canvas, scene);
                assert(memcmp(&before, scene, sizeof(before)) == 0);
                assert(hash == draw_and_hash(renderer, canvas, scene));
                worker->behavior_timer_ms += 4000U;
                assert(hash == draw_and_hash(renderer, canvas, scene));
                if (ms == 0U)
                    first_hash = hash;
                else if (hash != first_hash)
                    changed++;
            }
            assert(changed > 40U);
        }
    }
    /* Leaving the activity removes held props. Only an actual recorded release
     * may keep a bottle visible after the worker changes jobs. */
    building_clock_worker_t* worker = &scene->state.workers[0];
    worker->state = BUILDING_CLOCK_WORKER_INSPECT;
    worker->behavior_timer_ms = 0U;
    const uint32_t working = draw_and_hash(renderer, canvas, scene);
    for (uint32_t ms = 0U; ms < 4000U; ms += 50U) {
        worker->behavior_timer_ms = ms;
        assert(working == draw_and_hash(renderer, canvas, scene));
    }
    worker->state = BUILDING_CLOCK_WORKER_DRINK;
    worker->site_activity = BUILDING_CLOCK_UPKEEP_TEA;
    worker->site_phase = BUILDING_CLOCK_UPKEEP_WORK;
    worker->site_elapsed_ms = 1000U;
    worker->behavior_timer_ms = 0U;
    const uint32_t tea = draw_and_hash(renderer, canvas, scene);
    worker->behavior_timer_ms = 3000U;
    assert(tea == draw_and_hash(renderer, canvas, scene));
}

static void test_real_throw_persists(SDL_Renderer* renderer, SDL_Surface* canvas) {
    for (unsigned rollover = 0U; rollover < 2U; rollover++) {
        viewer_t viewer = {.root_seed = DEFAULT_SEED, .generator_version = BUILDING_CLOCK_GENERATOR_VERSION, .scenario = SCENARIO_STABLE};
        assert(reset_viewer(&viewer, true));
        for (unsigned step = 0U; step < 60U; step++)
            advance_viewer(&viewer, 50U);
        const building_clock_bottle_throw_t released = viewer.simulation.state.bottle_throws[5];
        assert(released.direction == 1 && released.elapsed_ms == 2613U);
        assert(viewer.simulation.state.workers[5].state != BUILDING_CLOCK_WORKER_DRINK);
        if (rollover != 0U)
            assert(building_clock_simulation_set_time(&viewer.simulation, &viewer.world, 14U, 38U) == BUILDING_CLOCK_SIMULATION_OK);
        const bottle_landing_t landing = tossed_bottle_landing(&viewer.world, released.origin, released.direction);
        assert(landing.hit);
        unsigned rolling_frames = 0U;
        for (unsigned step = 0U; step < 20U; step++) {
            advance_viewer(&viewer, 50U);
            assert(memcmp(&released, &viewer.simulation.state.bottle_throws[5], sizeof(released)) == 0);
            assert(building_clock_scene_capture(&viewer.world, &viewer.simulation, &viewer.scene) == BUILDING_CLOCK_SCENE_OK);
            const uint64_t since_release = viewer.scene.state.elapsed_ms - released.elapsed_ms;
            if (since_release >= landing.elapsed_ms && since_release < landing.elapsed_ms + 750U) {
                set_color(renderer, COLOR_BACKGROUND);
                (void)SDL_RenderClear(renderer);
                draw_bottle_throws(renderer, &viewer.scene);
                SDL_RenderPresent(renderer);
                assert(count_ink(canvas) == 5U);
                rolling_frames++;
            }
        }
        assert(rolling_frames >= 12U);
        /* Reset really clears release history, not merely the character pose. */
        assert(reset_viewer(&viewer, false));
        assert(viewer.simulation.state.bottle_throws[5].direction == 0);
        printf("Real interrupted break: %u visible ground-roll frames after %s\n", rolling_frames, rollover == 0U ? "worker resumed useful work" : "minute rollover");
    }
}

static void test_useful_work_interrupts(void) {
    building_clock_world_t world;
    const building_clock_world_selection_result_t selected = building_clock_select_world(&world, DEFAULT_SEED, BUILDING_CLOCK_GENERATOR_VERSION);
    assert(selected.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED);
    for (unsigned activity = 0U; activity < 2U; activity++) {
        for (uint32_t ms = 1000U; ms < 4000U; ms += 250U) {
            building_clock_simulation_t simulation;
            assert(building_clock_simulation_init(&simulation, &world, 1U) == BUILDING_CLOCK_SIMULATION_OK);
            assert(building_clock_simulation_set_time(&simulation, &world, 14U, 37U) == BUILDING_CLOCK_SIMULATION_OK);
            building_clock_worker_t* worker = &simulation.state.workers[0];
            worker->state = activity == 0U ? BUILDING_CLOCK_WORKER_SMOKE : BUILDING_CLOCK_WORKER_DRINK;
            worker->behavior_timer_ms = ms;
            const building_clock_simple_job_spec_t job = {.kind = BUILDING_CLOCK_JOB_INSPECT, .target_node_id = worker->current_node_id, .action_duration_ms = 500U};
            uint8_t job_id;
            assert(building_clock_simulation_add_job(&simulation, &world, &job, &job_id) == BUILDING_CLOCK_SIMULATION_OK);
            for (unsigned step = 0U; step < 10U; step++)
                assert(building_clock_simulation_step(&simulation, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK);
            assert(simulation.state.jobs[job_id].status == BUILDING_CLOCK_JOB_COMPLETE);
            assert(worker->state != BUILDING_CLOCK_WORKER_SMOKE && worker->state != BUILDING_CLOCK_WORKER_DRINK);
        }
    }
}

int main(int argc, char** argv) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return 1;
    SDL_Surface* canvas = SDL_CreateRGBSurfaceWithFormat(0, 480, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* renderer = canvas == NULL ? NULL : SDL_CreateSoftwareRenderer(canvas);
    if (renderer == NULL)
        return 1;
    building_clock_world_t world;
    building_clock_scene_snapshot_t scene;
    prepare_break_scene(&scene, &world);
    test_timing_and_landings(&world);
    test_arm_reach_and_pouring();
    test_bottle_and_smash(renderer, canvas);
    test_render_replay(renderer, canvas, &scene);
    test_useful_work_interrupts();
    test_real_throw_persists(renderer, canvas);
    puts("Break animation timing, landing, replay, read-only rendering and interruptions passed");
    prepare_break_scene(&scene, &world);
    const bool benchmark = argc == 2 && strcmp(argv[1], "--benchmark") == 0;
    if (argc == 2 && !benchmark) {
        for (uint32_t ms = 0U; ms < 4000U; ms += 50U) {
            render_break_scene(renderer, &scene, ms);
            char path[1024];
            (void)snprintf(path, sizeof(path), "%s/%06u.png", argv[1], ms / 50U);
            if (!save_png(canvas, path))
                return 1;
        }
    }
    for (unsigned repeat = 0U; benchmark && repeat < 3U; repeat++) {
        const clock_t start = clock();
        for (uint32_t frame = 0U; frame < 20000U; frame++) {
            render_break_scene(renderer, &scene, (frame * 50U) % 4000U);
        }
        printf("Break rendering: four workers, repeat=%u, %.3f us/frame\n", repeat, (double)(clock() - start) * 1e6 / CLOCKS_PER_SEC / 20000.0);
    }
    printf("Persistent sizes: worker=%zu state=%zu snapshot=%zu\n", sizeof(building_clock_worker_t), sizeof(building_clock_simulation_state_t), sizeof(building_clock_scene_snapshot_t));
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(canvas);
    SDL_Quit();
    return 0;
}
