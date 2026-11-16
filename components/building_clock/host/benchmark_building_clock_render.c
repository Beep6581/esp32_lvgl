/* Host-only draw benchmark. Include the viewer to exercise its real static
 * drawing functions without adding a public rendering API or duplicating them.
 * BUILDING_CLOCK_VIEWER_SOURCE can point at a repository-local baseline copy. */
#ifndef BUILDING_CLOCK_VIEWER_SOURCE
#define BUILDING_CLOCK_VIEWER_SOURCE "building_clock_viewer.c"
#endif
#define main building_clock_viewer_main
#include BUILDING_CLOCK_VIEWER_SOURCE
#undef main

static void prepare_render_fixture(building_clock_scene_snapshot_t* scene, building_clock_world_t* world) {
    scene->world = world;
    for (uint8_t digit = 0U; digit < 4U; digit++) {
        const int16_t x = (int16_t)(40 + digit * 100U);
        world->digit_work_areas[digit].envelope = (building_clock_rect_t){x, 170, 78, 144};
        world->digit_work_areas[digit].collapse_catch_area = (building_clock_rect_t){(int16_t)(x + 4), 322, 70, 14};
    }
    for (uint8_t id = 0U; id < BUILDING_CLOCK_MAX_DEBRIS_PIECES; id++) {
        const uint8_t digit = (uint8_t)(id % 4U);
        const building_clock_section_kind_t kind = (building_clock_section_kind_t)(id % 7U);
        const building_clock_point_t start = building_clock_rect_center(building_clock_section_bounds(world->digit_work_areas[digit].envelope, kind));
        scene->state.debris[id] = (building_clock_debris_t){
            .status = BUILDING_CLOCK_DEBRIS_SETTLED,
            .section_kind = (uint8_t)kind,
            .source_digit_id = digit,
            .source_numeral = 8U,
            .position = {.x = (int16_t)(start.x + 2), .y = 332},
            .masonry_progress_per_mille = 1000U,
#ifdef BUILDING_CLOCK_BENCHMARK_BASELINE
            .horizontal_drift = 2,
            .failure_brick_id = id,
#endif
        };
        if (id < BUILDING_CLOCK_MAX_FALLING_SECTIONS) {
            scene->state.falling_sections[id] = (building_clock_falling_section_t){
                .section_kind = kind,
                .source_digit_id = digit,
                .source_numeral = 8U,
                .failure_brick_id = id,
                .start_position = start,
                .landing_position = {.x = (int16_t)(start.x + 2), .y = 332},
                .masonry_progress_per_mille = 1000U,
                .duration_ms = 800U,
            };
        }
    }
}

int main(int argc, char** argv) {
    const unsigned frames = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 0) : 5000U;
    if (frames == 0U || frames > 1000000U) {
        return 2;
    }
    building_clock_world_t world = {0};
    building_clock_scene_snapshot_t scene = {0};
    prepare_render_fixture(&scene, &world);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        return 1;
    }
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 480, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* renderer = surface == NULL ? NULL : SDL_CreateSoftwareRenderer(surface);
    if (renderer == NULL) {
        SDL_FreeSurface(surface);
        SDL_Quit();
        return 1;
    }
    for (unsigned repeat = 0U; repeat < 3U; repeat++) {
        for (unsigned mode = 0U; mode < 2U; mode++) {
            scene.state.falling_section_count = mode == 0U ? BUILDING_CLOCK_MAX_FALLING_SECTIONS : 0U;
            for (uint8_t id = 0U; id < BUILDING_CLOCK_MAX_DEBRIS_PIECES; id++) {
                scene.state.debris[id].status = mode == 0U ? BUILDING_CLOCK_DEBRIS_UNUSED : BUILDING_CLOCK_DEBRIS_SETTLED;
            }
            const clock_t start = clock();
            for (unsigned frame = 0U; frame < frames; frame++) {
                for (uint8_t id = 0U; id < BUILDING_CLOCK_MAX_FALLING_SECTIONS; id++) {
                    scene.state.falling_sections[id].elapsed_ms = (frame * 37U) % 801U;
                }
                set_color(renderer, COLOR_BACKGROUND);
                (void)SDL_RenderClear(renderer);
                draw_falling_and_debris(renderer, &scene, false);
                SDL_RenderPresent(renderer);
            }
            const double seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
            printf("%s repeat=%u frames=%u sections=%u us/frame=%.3f\n", mode == 0U ? "falling" : "debris", repeat, frames,
                   mode == 0U ? BUILDING_CLOCK_MAX_FALLING_SECTIONS : BUILDING_CLOCK_MAX_DEBRIS_PIECES, seconds * 1e6 / frames);
        }
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    SDL_Quit();
    return 0;
}
