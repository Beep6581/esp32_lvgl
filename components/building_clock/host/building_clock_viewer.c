#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "building_clock_generator.h"
#include "building_clock_geometry.h"
#include "building_clock_scene.h"
#include "building_clock_world_selection.h"
#include "glcdfont_5x7.h"

#include <inttypes.h>
#include <png.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VIEW_SCALE 2
#define VIEW_STEP_MS 50U
#define DEFAULT_SEED UINT64_C(0x6a09e667f3bcc909)
#define NEXT_SEED_DELTA UINT64_C(0x9e3779b97f4a7c15)
#define DIAGNOSTIC_STAGE_COUNT 5U

typedef enum {
    SCENARIO_STABLE = 0,
    SCENARIO_SINGLE,
    SCENARIO_ROLLOVER,
    SCENARIO_MIDNIGHT,
    SCENARIO_UNFINISHED,
} viewer_scenario_t;

typedef struct {
    bool navigation;
    bool node_ids;
    bool locations;
    bool hazards;
    bool worker_routes;
    bool digit_areas;
    bool status;
} viewer_overlays_t;

typedef struct {
    uint64_t root_seed;
    uint32_t generator_version;
    viewer_scenario_t scenario;
    building_clock_world_t world;
    building_clock_world_selection_result_t selection;
    building_clock_simulation_t simulation;
    building_clock_scene_snapshot_t scene;
    viewer_overlays_t overlays;
    bool paused;
    bool running;
    uint32_t speed;
    uint32_t job_serial;
    uint64_t next_job_refresh_ms;
    uint64_t next_minute_ms;
} viewer_t;

static const SDL_Color COLOR_BACKGROUND = {7, 8, 13, 255};
static const SDL_Color COLOR_STEEL = {105, 122, 193, 255};
static const SDL_Color COLOR_STEEL_DARK = {48, 57, 99, 255};
static const SDL_Color COLOR_LADDER = {238, 206, 105, 255};
static const SDL_Color COLOR_CRANE = {62, 15, 105, 255};
static const SDL_Color COLOR_CRANE_DARK = {39, 16, 85, 255};
static const SDL_Color COLOR_MASONRY = {205, 92, 54, 255};
static const SDL_Color COLOR_MASONRY_LIGHT = {239, 126, 67, 255};
static const SDL_Color COLOR_MASONRY_DARK = {104, 42, 34, 255};
static const SDL_Color COLOR_WORKER = {239, 228, 195, 255};
static const SDL_Color COLOR_WORKER_ACTION = {255, 242, 129, 255};
static const SDL_Color COLOR_HAZARD = {221, 54, 62, 255};
static const SDL_Color COLOR_DEBUG = {76, 215, 199, 255};
static const SDL_Color COLOR_TEXT = {213, 220, 232, 255};
static const SDL_Color COLOR_ROPE = {157, 104, 57, 255};
static const SDL_Color COLOR_SKIN = {226, 169, 116, 255};
static const SDL_Color COLOR_BOOTS = {87, 57, 48, 255};

static const char* topology_name(building_clock_topology_family_t topology) {
    switch (topology) {
    case BUILDING_CLOCK_TOPOLOGY_CONTINUOUS_LOWER_BRANCHES:
        return "CONTINUOUS LOWER";
    case BUILDING_CLOCK_TOPOLOGY_SPLIT_LOWER_RAISED_BRIDGE:
        return "SPLIT + BRIDGE";
    case BUILDING_CLOCK_TOPOLOGY_ASYMMETRIC_LOOP:
        return "ASYMMETRIC LOOP";
    case BUILDING_CLOCK_TOPOLOGY_TWIN_TOWERS:
        return "TWIN TOWERS";
    case BUILDING_CLOCK_TOPOLOGY_CENTRAL_ROUTE_MACHINERY:
        return "CENTRAL MACHINERY";
    default:
        return "UNKNOWN";
    }
}

static const char* lifecycle_name(building_clock_digit_lifecycle_t lifecycle) {
    switch (lifecycle) {
    case BUILDING_CLOCK_DIGIT_STABLE:
        return "STABLE";
    case BUILDING_CLOCK_DIGIT_PREPARING:
        return "PREP";
    case BUILDING_CLOCK_DIGIT_EVACUATING:
        return "EVAC";
    case BUILDING_CLOCK_DIGIT_COLLAPSING:
        return "FALL";
    case BUILDING_CLOCK_DIGIT_SETTLING:
        return "SETTLE";
    case BUILDING_CLOCK_DIGIT_CLEARING:
        return "CLEAR";
    case BUILDING_CLOCK_DIGIT_FOUNDATION:
        return "FOUND";
    case BUILDING_CLOCK_DIGIT_STRUCTURE:
        return "BUILD";
    case BUILDING_CLOCK_DIGIT_RECOGNIZABLE:
        return "SHAPE";
    case BUILDING_CLOCK_DIGIT_FINISHING:
        return "FINISH";
    case BUILDING_CLOCK_DIGIT_COMPLETE:
        return "DONE";
    default:
        return "?";
    }
}

static void set_color(SDL_Renderer* renderer, SDL_Color color) {
    (void)SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

static void draw_limb(SDL_Renderer* renderer, int x1, int y1, int x2, int y2, SDL_Color color) {
    set_color(renderer, color);
    (void)SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
    (void)SDL_RenderDrawLine(renderer, x1 + 1, y1, x2 + 1, y2);
}

static void fill_rect(SDL_Renderer* renderer, int x, int y, int width, int height, SDL_Color color) {
    const SDL_Rect rect = {.x = x, .y = y, .w = width, .h = height};
    set_color(renderer, color);
    (void)SDL_RenderFillRect(renderer, &rect);
}

static void outline_rect(SDL_Renderer* renderer, building_clock_rect_t area, SDL_Color color) {
    const SDL_Rect rect = {.x = area.x, .y = area.y, .w = area.width, .h = area.height};
    set_color(renderer, color);
    (void)SDL_RenderDrawRect(renderer, &rect);
}

static void draw_text(SDL_Renderer* renderer, int x, int y, int scale, SDL_Color color, const char* text) {
    set_color(renderer, color);
    for (const unsigned char* character = (const unsigned char*)text; *character != 0U; character++) {
        const uint8_t* columns = &glcdfont_5x7[(size_t)(*character) * GLCDFONT_5X7_GLYPH_BYTES];
        for (int column = 0; column < 5; column++) {
            for (int row = 0; row < 7; row++) {
                if ((columns[column] & (uint8_t)(1U << row)) != 0U) {
                    const SDL_Rect pixel = {.x = x + column * scale, .y = y + row * scale, .w = scale, .h = scale};
                    (void)SDL_RenderFillRect(renderer, &pixel);
                }
            }
        }
        x += 6 * scale;
    }
}

static void draw_platforms(SDL_Renderer* renderer, const building_clock_world_t* world) {
    for (uint8_t index = 0U; index < world->platform_count; index++) {
        const building_clock_platform_t* platform = &world->platforms[index];
        const SDL_Rect beam = {.x = platform->x_start, .y = platform->surface_y, .w = platform->x_end - platform->x_start + 1, .h = 10};
        set_color(renderer, COLOR_STEEL_DARK);
        (void)SDL_RenderFillRect(renderer, &beam);
        fill_rect(renderer, beam.x, beam.y, beam.w, 3, COLOR_STEEL);
        fill_rect(renderer, beam.x, beam.y + beam.h - 2, beam.w, 2, COLOR_STEEL);
        for (int x = beam.x + 5; x + 7 < beam.x + beam.w; x += 13) {
            fill_rect(renderer, x, beam.y + 4, 7, 3, COLOR_BACKGROUND);
        }
    }
}

static void draw_supports(SDL_Renderer* renderer, const building_clock_world_t* world) {
    for (uint8_t index = 0U; index < world->major_support_count; index++) {
        const building_clock_rect_t bounds = world->major_supports[index].bounds;
        fill_rect(renderer, bounds.x, bounds.y, bounds.width, bounds.height, COLOR_STEEL_DARK);
        fill_rect(renderer, bounds.x, bounds.y, 2, bounds.height, COLOR_STEEL);
        fill_rect(renderer, (int)(bounds.x + bounds.width - 2U), bounds.y, 2, bounds.height, COLOR_STEEL);
        for (int y = bounds.y + 6; y + 7 < bounds.y + bounds.height; y += 13) {
            fill_rect(renderer, bounds.x + 3, y, (int)bounds.width - 6, 7, COLOR_BACKGROUND);
        }
    }
}

static void draw_ladders(SDL_Renderer* renderer, const building_clock_world_t* world) {
    for (uint8_t index = 0U; index < world->ladder_count; index++) {
        const building_clock_ladder_t* ladder = &world->ladders[index];
        fill_rect(renderer, ladder->x - 4, ladder->top_y, 2, ladder->bottom_y - ladder->top_y + 1, COLOR_LADDER);
        fill_rect(renderer, ladder->x + 3, ladder->top_y, 2, ladder->bottom_y - ladder->top_y + 1, COLOR_LADDER);
        for (int y = ladder->top_y + 3; y < ladder->bottom_y; y += 7) {
            fill_rect(renderer, ladder->x - 3, y, 7, 2, COLOR_LADDER);
        }
    }
}

static void draw_locations(SDL_Renderer* renderer, const building_clock_world_t* world, bool overlay) {
    for (uint8_t index = 0U; index < world->debris_bay_count; index++) {
        const building_clock_rect_t area = world->debris_bays[index].area;
        fill_rect(renderer, area.x, area.y + (int)area.height - 3, area.width, 3, COLOR_BOOTS);
        fill_rect(renderer, area.x, area.y + 3, 2, (int)area.height - 3, COLOR_BOOTS);
        fill_rect(renderer, area.x + (int)area.width - 2, area.y + 3, 2, (int)area.height - 3, COLOR_BOOTS);
        if (overlay) {
            outline_rect(renderer, area, COLOR_HAZARD);
        }
    }
    if (overlay) {
        for (uint8_t digit = 0U; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
            outline_rect(renderer, world->digit_work_areas[digit].collapse_catch_area, COLOR_MASONRY_DARK);
        }
    }
    for (uint8_t index = 0U; index < world->staging_area_count; index++) {
        const building_clock_rect_t area = world->staging_areas[index].area;
        fill_rect(renderer, area.x, area.y + (int)area.height - 2, area.width, 2, COLOR_BOOTS);
        for (int x = area.x + 2; x < area.x + area.width - 2; x += 7) {
            fill_rect(renderer, x, area.y + (int)area.height - 4, 4, 2, COLOR_ROPE);
        }
        if (overlay) {
            outline_rect(renderer, area, COLOR_DEBUG);
        }
    }
    for (uint8_t index = 0U; index < world->depot_count; index++) {
        const building_clock_rect_t area = world->depots[index].area;
        fill_rect(renderer, area.x, area.y, area.width, area.height, COLOR_STEEL_DARK);
        fill_rect(renderer, area.x, area.y, area.width, 3, COLOR_STEEL);
        for (int y = area.y + 8; y < area.y + area.height - 4; y += 9) {
            fill_rect(renderer, area.x + 3, y, (int)area.width - 6, 3, COLOR_BOOTS);
            fill_rect(renderer, area.x + 4, y - 5, 6, 4, COLOR_MASONRY);
        }
        fill_rect(renderer, area.x, area.y, 2, area.height, COLOR_STEEL);
        fill_rect(renderer, area.x + (int)area.width - 2, area.y, 2, area.height, COLOR_STEEL);
    }
}

static void draw_machinery(SDL_Renderer* renderer, const building_clock_scene_snapshot_t* scene) {
    const building_clock_world_t* world = scene->world;
    for (uint8_t crane = 0U; crane < world->crane_count; crane++) {
        const building_clock_crane_layout_t* layout = &world->cranes[crane];
        const building_clock_crane_t* state = &scene->state.cranes[crane];
        const int bx = layout->base_position.x, by = layout->home_hook_position.y - 12;
        const int direction = bx < BUILDING_CLOCK_WORLD_WIDTH / 2 ? 1 : -1;
        const int boom_end = state->status == BUILDING_CLOCK_CRANE_IDLE ? bx + direction * 120 : (direction > 0 ? layout->working_area.x + layout->working_area.width : layout->working_area.x);
        const int left = bx < boom_end ? bx : boom_end;
        fill_rect(renderer, bx - 4, by, 9, layout->base_position.y - by, COLOR_CRANE_DARK);
        fill_rect(renderer, bx - 4, by, 2, layout->base_position.y - by, COLOR_CRANE);
        fill_rect(renderer, bx + 3, by, 2, layout->base_position.y - by, COLOR_CRANE);
        set_color(renderer, COLOR_CRANE);
        for (int y = by; y + 12 < layout->base_position.y; y += 12) {
            (void)SDL_RenderDrawLine(renderer, bx - 2, y, bx + 2, y + 6);
            (void)SDL_RenderDrawLine(renderer, bx + 2, y + 6, bx - 2, y + 12);
        }
        fill_rect(renderer, left, by - 2, abs(boom_end - bx) + 1, 2, COLOR_CRANE);
        fill_rect(renderer, left, by + 5, abs(boom_end - bx) + 1, 2, COLOR_CRANE);
        set_color(renderer, COLOR_CRANE);
        for (int x = left; x + 12 < left + abs(boom_end - bx); x += 12) {
            (void)SDL_RenderDrawLine(renderer, x, by + 4, x + 6, by);
            (void)SDL_RenderDrawLine(renderer, x + 6, by, x + 12, by + 4);
        }
        fill_rect(renderer, bx - 7, by - 9, 15, 13, COLOR_CRANE);
        fill_rect(renderer, bx - 4, by - 6, 9, 6, COLOR_CRANE_DARK);
        const building_clock_point_t hook = state->hook_position.x == 0 && state->hook_position.y == 0 ? layout->home_hook_position : state->hook_position;
        set_color(renderer, COLOR_CRANE);
        (void)SDL_RenderDrawLine(renderer, hook.x, layout->home_hook_position.y - 12, hook.x, hook.y);
        fill_rect(renderer, hook.x - 2, hook.y, 5, 4, COLOR_CRANE);
    }
    set_color(renderer, COLOR_LADDER);
    for (uint8_t lift = 0U; lift < world->lift_count; lift++) {
        const building_clock_lift_layout_t* layout = &world->lifts[lift];
        if (layout->stop_count == 0U) {
            continue;
        }
        (void)SDL_RenderDrawLine(renderer, layout->x, layout->stops[0].position.y, layout->x, layout->stops[layout->stop_count - 1U].position.y);
        const uint8_t stop = scene->state.lifts[lift].current_stop_id < layout->stop_count ? scene->state.lifts[lift].current_stop_id : 0U;
        fill_rect(renderer, layout->x - 6, layout->stops[stop].position.y - 3, 13, 5, COLOR_LADDER);
    }
    for (uint8_t route = 0U; route < world->cart_route_count; route++) {
        const building_clock_cart_route_t* cart_route = &world->cart_routes[route];
        for (uint8_t stop = 1U; stop < cart_route->stop_count; stop++) {
            (void)SDL_RenderDrawLine(renderer, cart_route->stops[stop - 1U].position.x, cart_route->stops[stop - 1U].position.y + 5, cart_route->stops[stop].position.x,
                                     cart_route->stops[stop].position.y + 5);
        }
        if (cart_route->stop_count > 0U) {
            const building_clock_cart_t* cart = &scene->state.carts[route];
            const uint8_t stop = cart->current_stop_id < cart_route->stop_count ? cart->current_stop_id : 0U;
            const uint8_t target = cart->target_stop_id < cart_route->stop_count ? cart->target_stop_id : stop;
            building_clock_point_t position = cart_route->stops[stop].position;
            position.x = (int16_t)(position.x + ((int32_t)cart_route->stops[target].position.x - position.x) * cart->route_progress_per_mille / 1000);
            fill_rect(renderer, position.x - 8, position.y - 7, 17, 4, COLOR_STEEL);
            fill_rect(renderer, position.x - 9, position.y - 10, 2, 5, COLOR_STEEL);
            fill_rect(renderer, position.x + 8, position.y - 10, 2, 5, COLOR_STEEL);
            for (int side = -1; side <= 1; side += 2) {
                fill_rect(renderer, position.x + side * 5 - 2, position.y - 3, 5, 5, COLOR_STEEL_DARK);
                fill_rect(renderer, position.x + side * 5 - 1 + ((position.x / 3) & 1), position.y - 2, 2, 2, COLOR_WORKER);
            }
        }
    }
}

static void draw_masonry_brick(SDL_Renderer* renderer, building_clock_rect_t brick, SDL_Color color) {
    fill_rect(renderer, brick.x, brick.y, brick.width, brick.height, color);
    set_color(renderer, COLOR_MASONRY_DARK);
    (void)SDL_RenderDrawLine(renderer, brick.x, brick.y + (int)brick.height - 1, brick.x + (int)brick.width - 1, brick.y + (int)brick.height - 1);
    (void)SDL_RenderDrawLine(renderer, brick.x + (int)brick.width - 1, brick.y + 1, brick.x + (int)brick.width - 1, brick.y + (int)brick.height - 1);
    set_color(renderer, COLOR_MASONRY_LIGHT);
    (void)SDL_RenderDrawLine(renderer, brick.x + 1, brick.y + 1, brick.x + (int)brick.width - 2, brick.y + 1);
}

static void draw_section_at_position(SDL_Renderer* renderer, building_clock_rect_t envelope, uint8_t numeral, building_clock_section_kind_t kind, building_clock_point_t center,
                                     uint16_t progress_per_mille, SDL_Color color) {
    building_clock_rect_t bricks[BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION];
    const uint8_t brick_count = building_clock_section_visible_bricks(envelope, numeral, kind, bricks);
    const building_clock_point_t origin = building_clock_rect_center(building_clock_section_bounds(envelope, kind));
    uint8_t visible_count = (uint8_t)(((uint32_t)brick_count * progress_per_mille + 999U) / 1000U);
    if (visible_count > brick_count) {
        visible_count = brick_count;
    }
    for (uint8_t brick_id = 0U; brick_id < visible_count; brick_id++) {
        bricks[brick_id].x = (int16_t)(bricks[brick_id].x + center.x - origin.x);
        bricks[brick_id].y = (int16_t)(bricks[brick_id].y + center.y - origin.y);
        draw_masonry_brick(renderer, bricks[brick_id], color);
    }
}

static void draw_structure(SDL_Renderer* renderer, building_clock_rect_t envelope, const building_clock_digit_structure_t* structure, bool incoming) {
    for (uint8_t index = 0U; index < structure->section_count; index++) {
        const building_clock_section_state_t* section = &structure->sections[index];
        if (section->status == BUILDING_CLOCK_SECTION_ABSENT || section->status == BUILDING_CLOCK_SECTION_STAGED || section->status == BUILDING_CLOCK_SECTION_FALLING ||
            section->status == BUILDING_CLOCK_SECTION_DEBRIS || (section->status == BUILDING_CLOCK_SECTION_IN_TRANSIT && section->progress_per_mille == 0U)) {
            continue;
        }
        SDL_Color color = COLOR_MASONRY;
        if (section->status == BUILDING_CLOCK_SECTION_FAILED) {
            color = COLOR_MASONRY_DARK;
        } else if (incoming && section->status == BUILDING_CLOCK_SECTION_PLACED) {
            color = COLOR_MASONRY_LIGHT;
        }
        const uint16_t progress = section->progress_per_mille;
        const building_clock_point_t center = building_clock_rect_center(building_clock_section_bounds(envelope, section->kind));
        draw_section_at_position(renderer, envelope, structure->numeral, section->kind, center, progress, color);
    }
}

static void draw_load_bundle(SDL_Renderer* renderer, building_clock_point_t position, uint8_t count, SDL_Color color) {
    for (uint8_t brick = 0U; brick < count; brick++) {
        const building_clock_rect_t bounds = {
            .x = (int16_t)(position.x - 7 + (brick % 2U) * 8),
            .y = (int16_t)(position.y - (brick / 2U) * 6),
            .width = 7,
            .height = 5,
        };
        draw_masonry_brick(renderer, bounds, color);
    }
}

static void draw_loads(SDL_Renderer* renderer, const building_clock_scene_snapshot_t* scene) {
    for (uint8_t load_id = 0U; load_id < BUILDING_CLOCK_MAX_LOADS; load_id++) {
        const building_clock_load_t* load = &scene->state.loads[load_id];
        if (load->status != BUILDING_CLOCK_LOAD_STAGED && load->status != BUILDING_CLOCK_LOAD_CARRIED) {
            continue;
        }
        if (load->digit_id >= BUILDING_CLOCK_DIGIT_COUNT || load->section_id >= scene->state.digits[load->digit_id].incoming.section_count) {
            continue;
        }
        building_clock_point_t position = load->position;
        if (load->status == BUILDING_CLOCK_LOAD_CARRIED && load->carrier_worker_id < scene->state.worker_count && scene->state.workers[load->carrier_worker_id].state != BUILDING_CLOCK_WORKER_HAMMER) {
            const building_clock_point_t logical = scene->state.workers[load->carrier_worker_id].position;
            const building_clock_point_t presented = scene->worker_presentation_positions[load->carrier_worker_id];
            position.x = (int16_t)(position.x + presented.x - logical.x);
            position.y = (int16_t)(position.y + presented.y - logical.y);
        }
        const SDL_Color color = load->status == BUILDING_CLOCK_LOAD_CARRIED ? COLOR_MASONRY_LIGHT : COLOR_MASONRY;
        uint8_t visible = (uint8_t)load->quantity;
        if (load->placement_progress_per_mille > 0U) {
            visible = (uint8_t)(((uint32_t)visible * (1000U - load->placement_progress_per_mille) + 999U) / 1000U);
        }
        if (load->status == BUILDING_CLOCK_LOAD_CARRIED && load->carrier_worker_id < scene->state.worker_count) {
            const building_clock_worker_t* carrier = &scene->state.workers[load->carrier_worker_id];
            const building_clock_point_t p = scene->worker_presentation_positions[load->carrier_worker_id];
            /* A kit is a brick basket, not a huge prefabricated bar obscuring
             * its carrier. During placement one brick is visibly at the hand. */
            const building_clock_job_t* action = carrier->current_job_id < BUILDING_CLOCK_MAX_JOBS ? &scene->state.jobs[carrier->current_job_id] : NULL;
            if (carrier->state == BUILDING_CLOCK_WORKER_HAMMER && carrier->rope.phase == BUILDING_CLOCK_ROPE_WORK && action != NULL && action->kind == BUILDING_CLOCK_JOB_PLACE_SECTION &&
                action->subject.digit_id == load->digit_id && action->subject.section_id == load->section_id) {
                /* The placing hand below carries this brick to its joint. */
                continue;
            } else {
                const int side = carrier->state == BUILDING_CLOCK_WORKER_CLIMB ? -1 : (carrier->facing == BUILDING_CLOCK_FACING_RIGHT ? 1 : -1);
                const int bx = p.x + side * 7;
                fill_rect(renderer, bx - 5, p.y - 10, 11, 7, COLOR_BOOTS);
                fill_rect(renderer, bx - 5, p.y - 10, 11, 1, COLOR_ROPE);
                for (uint8_t brick = 0U; brick < (visible > 2U ? 2U : visible); brick++) {
                    draw_masonry_brick(renderer, (building_clock_rect_t){(int16_t)(bx - 4 + brick * 5U), (int16_t)(p.y - 12), 5U, 4U}, color);
                }
                for (int x = bx - 3; x <= bx + 3; x += 3)
                    fill_rect(renderer, x, p.y - 8, 1, 4, COLOR_ROPE);
            }
        } else {
            draw_load_bundle(renderer, position, visible > 4U ? 4U : visible, color);
            fill_rect(renderer, position.x - 8, position.y + 5, 17, 2, COLOR_BOOTS);
        }
    }
}

static void draw_digits(SDL_Renderer* renderer, const building_clock_scene_snapshot_t* scene, bool debug) {
    for (uint8_t digit = 0U; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        const building_clock_rect_t envelope = scene->world->digit_work_areas[digit].envelope;
        draw_structure(renderer, envelope, &scene->state.digits[digit].outgoing, false);
        draw_structure(renderer, envelope, &scene->state.digits[digit].incoming, true);
        if (debug && scene->state.digits[digit].lifecycle != BUILDING_CLOCK_DIGIT_STABLE) {
            draw_text(renderer, envelope.x, envelope.y - 10, 1, COLOR_TEXT, lifecycle_name(scene->state.digits[digit].lifecycle));
        }
    }
    const building_clock_rect_t colon = scene->world->colon_envelope;
    fill_rect(renderer, colon.x + colon.width / 2 - 4, colon.y + colon.height / 3 - 4, 8, 8, COLOR_MASONRY);
    fill_rect(renderer, colon.x + colon.width / 2 - 4, colon.y + 2 * colon.height / 3 - 4, 8, 8, COLOR_MASONRY);
}

static void draw_falling_and_debris(SDL_Renderer* renderer, const building_clock_scene_snapshot_t* scene, bool show_trajectories) {
    for (uint8_t index = 0U; index < scene->state.falling_section_count; index++) {
        const building_clock_falling_section_t* section = &scene->state.falling_sections[index];
        if (section->source_digit_id >= BUILDING_CLOCK_DIGIT_COUNT) {
            continue;
        }
        if (show_trajectories) {
            set_color(renderer, COLOR_MASONRY_DARK);
            (void)SDL_RenderDrawLine(renderer, section->start_position.x, section->start_position.y, section->landing_position.x, section->landing_position.y);
            outline_rect(renderer, (building_clock_rect_t){.x = (int16_t)(section->landing_position.x - 3), .y = (int16_t)(section->landing_position.y - 2), .width = 7, .height = 4}, COLOR_DEBUG);
        }
        const building_clock_rect_t envelope = scene->world->digit_work_areas[section->source_digit_id].envelope;
        const uint8_t numeral = scene->state.digits[section->source_digit_id].outgoing.numeral;
        building_clock_rect_t bricks[BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION];
        const uint8_t count = building_clock_section_visible_bricks(envelope, numeral, section->section_kind, bricks);
        const building_clock_point_t center = building_clock_rect_center(building_clock_section_bounds(envelope, section->section_kind));
        static const int16_t cosine[8] = {256, 181, 0, -181, -256, -181, 0, 181};
        const unsigned turn = (unsigned)((section->angle_degrees + 720) / 45) % 8U;
        const int c = cosine[turn], s = cosine[(turn + 6U) % 8U];
        const unsigned visible = ((unsigned)count * section->masonry_progress_per_mille + 999U) / 1000U;
        for (unsigned brick = 0U; brick < count && brick < visible; brick++) {
            const building_clock_point_t b = building_clock_rect_center(bricks[brick]);
            const int dx = b.x - center.x, dy = b.y - center.y;
            bricks[brick].x = (int16_t)(section->position.x + (dx * c - dy * s) / 256 - bricks[brick].width / 2);
            bricks[brick].y = (int16_t)(section->position.y + (dx * s + dy * c) / 256 - bricks[brick].height / 2);
            draw_masonry_brick(renderer, bricks[brick], COLOR_MASONRY);
        }
    }
    for (uint8_t index = 0U; index < BUILDING_CLOCK_MAX_DEBRIS_PIECES; index++) {
        const building_clock_debris_t* debris = &scene->state.debris[index];
        if (debris->status == BUILDING_CLOCK_DEBRIS_UNUSED || debris->status == BUILDING_CLOCK_DEBRIS_CLEARED) {
            continue;
        }
        draw_masonry_brick(renderer, (building_clock_rect_t){(int16_t)(debris->position.x - 5), (int16_t)(debris->position.y - 3), 7U, 4U}, COLOR_MASONRY);
        fill_rect(renderer, debris->position.x + 2, debris->position.y - 1, 4, 3, COLOR_MASONRY_DARK);
    }
}

static void draw_temp_structures(SDL_Renderer* renderer, const building_clock_scene_snapshot_t* scene) {
    for (uint8_t index = 0U; index < BUILDING_CLOCK_MAX_TEMP_STRUCTURES; index++) {
        const building_clock_temp_structure_t* state = &scene->state.temp_structures[index];
        if (state->status == BUILDING_CLOCK_TEMP_STRUCTURE_UNUSED || state->attachment_set_id >= scene->world->temp_attachment_set_count) {
            continue;
        }
        const building_clock_temp_attachment_set_t* attachment = &scene->world->temp_attachment_sets[state->attachment_set_id];
        if (attachment->anchor_count < 2U) {
            continue;
        }
        const int x = attachment->anchors[0].x;
        const int full_top = attachment->anchors[0].y;
        const int bottom = attachment->anchors[attachment->anchor_count - 1U].y;
        const int top = bottom - (int)((uint32_t)(bottom - full_top) * state->progress_per_mille / 1000U);
        if (state->progress_per_mille == 0U) {
            fill_rect(renderer, x - 7, bottom - 3, 14, 3, COLOR_LADDER);
            continue;
        }
        if (attachment->kind == BUILDING_CLOCK_TEMP_STRUCTURE_LADDER) {
            fill_rect(renderer, x - 4, top, 2, bottom - top + 1, COLOR_LADDER);
            fill_rect(renderer, x + 3, top, 2, bottom - top + 1, COLOR_LADDER);
            for (int y = top + 3; y < bottom; y += 7) {
                fill_rect(renderer, x - 3, y, 7, 2, COLOR_LADDER);
            }
        } else {
            fill_rect(renderer, x - 6, top, 2, bottom - top + 1, COLOR_STEEL);
            fill_rect(renderer, x + 5, top, 2, bottom - top + 1, COLOR_STEEL);
            for (uint8_t anchor = 0U; anchor < attachment->anchor_count; anchor++) {
                const int deck_y = attachment->anchors[anchor].y;
                if (deck_y < top) {
                    continue;
                }
                fill_rect(renderer, x - 8, deck_y - 2, 17, 4, COLOR_STEEL);
                if (anchor + 1U < attachment->anchor_count) {
                    const int next_y = attachment->anchors[anchor + 1U].y;
                    set_color(renderer, COLOR_STEEL_DARK);
                    (void)SDL_RenderDrawLine(renderer, x - 5, deck_y + 2, x + 5, next_y - 2);
                    (void)SDL_RenderDrawLine(renderer, x + 5, deck_y + 2, x - 5, next_y - 2);
                }
            }
            fill_rect(renderer, x - 1, top, 2, bottom - top + 1, COLOR_LADDER);
            for (int y = top + 4; y < bottom; y += 7) {
                fill_rect(renderer, x - 4, y, 8, 1, COLOR_LADDER);
            }
        }
    }
}

/* Discrete animation comes only from semantic time/position. Pausing or taking
 * extra screenshots cannot advance a pose or consume simulation randomness. */
static unsigned worker_frame(const building_clock_scene_snapshot_t* scene, const building_clock_worker_t* worker, uint8_t id) {
    if (worker->reserved_edge_id < scene->world->navigation.edge_count || worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_WALK_OUT ||
        worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_RETURN || worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_CART_APPROACH ||
        worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_CART_PUSH || worker->rope.phase == BUILDING_CLOCK_ROPE_DESCEND || worker->rope.phase == BUILDING_CLOCK_ROPE_ASCEND ||
        worker->rope.phase == BUILDING_CLOCK_ROPE_SHIFT) {
        return (unsigned)((abs(worker->position.x) + abs(worker->position.y)) / 3) % 8U;
    }
    return (unsigned)(scene->state.elapsed_ms / 110U + id * 3U) % 8U;
}

static void draw_rope_equipment(SDL_Renderer* renderer, const building_clock_scene_snapshot_t* scene, uint8_t id) {
    const building_clock_worker_t* worker = &scene->state.workers[id];
    if (worker->rope.phase == BUILDING_CLOCK_ROPE_NONE) {
        return;
    }
    const int x = worker->position.x, y = worker->position.y;
    const int ax = worker->rope.anchor.x, ay = worker->rope.anchor.y;
    /* The builder controls a harness ascender/descender and travelling clamp.
     * Any nearby winch helper is optional and follows actual rope movement. */
    fill_rect(renderer, ax - 5, ay - 5, 11, 3, COLOR_STEEL);
    fill_rect(renderer, ax - 5, ay - 2, 2, 6, COLOR_STEEL_DARK);
    fill_rect(renderer, ax + 4, ay - 2, 2, 6, COLOR_STEEL_DARK);
    fill_rect(renderer, ax - 2, ay, 5, 5, COLOR_BOOTS);
    fill_rect(renderer, ax, ay + 1, 1, 2, COLOR_LADDER);
    set_color(renderer, COLOR_ROPE);
    (void)SDL_RenderDrawLine(renderer, ax, ay + 4, x, y - 5);
    fill_rect(renderer, x - 5, y - 5, 11, 2, COLOR_ROPE);
    fill_rect(renderer, x - 1, y - 8, 3, 5, COLOR_STEEL);
    for (uint8_t tender = 0U; tender < scene->state.worker_count; tender++) {
        if (scene->state.workers[tender].tended_builder_id == id && worker->rope.moved_vertically && scene->state.workers[tender].state == BUILDING_CLOCK_WORKER_OPERATE_MACHINERY &&
            scene->worker_presentation_positions[tender].y == ay) {
            const building_clock_point_t p = scene->worker_presentation_positions[tender];
            set_color(renderer, COLOR_ROPE);
            (void)SDL_RenderDrawLine(renderer, p.x + 7, p.y - 5, ax, ay + 2);
            fill_rect(renderer, p.x + 3, p.y - 7, 9, 6, COLOR_BOOTS);
            fill_rect(renderer, p.x + 4, p.y - 6, 7, 2, COLOR_ROPE);
            const int crank = (int)worker_frame(scene, &scene->state.workers[tender], tender);
            draw_limb(renderer, p.x + 7, p.y - 4, p.x + 7 + (crank < 4 ? 3 : -3), p.y - 4 + (crank % 4 < 2 ? -3 : 3), COLOR_LADDER);
        }
    }
}

static void draw_worker(SDL_Renderer* renderer, const building_clock_scene_snapshot_t* scene, uint8_t worker_id, bool show_id) {
    static const int stride[8] = {-3, -2, 0, 2, 3, 2, 0, -2};
    static const SDL_Color uniforms[3] = {{92, 104, 191, 255}, {74, 143, 151, 255}, {139, 88, 136, 255}};
    const building_clock_worker_t* worker = &scene->state.workers[worker_id];
    const building_clock_job_t* job = worker->current_job_id < BUILDING_CLOCK_MAX_JOBS ? &scene->state.jobs[worker->current_job_id] : NULL;
    const unsigned frame = worker_frame(scene, worker, worker_id);
    const int x = scene->worker_presentation_positions[worker_id].x, y = scene->worker_presentation_positions[worker_id].y;
    const int direction = worker->facing == BUILDING_CLOCK_FACING_LEFT ? -1 : 1;
    const bool climbing = worker->state == BUILDING_CLOCK_WORKER_CLIMB;
    const bool moving = worker->state == BUILDING_CLOCK_WORKER_WALK || worker->state == BUILDING_CLOCK_WORKER_PUSH ||
                        (worker->reserved_edge_id < scene->world->navigation.edge_count && worker->state != BUILDING_CLOCK_WORKER_WAIT);
    const bool sitting = worker->state == BUILDING_CLOCK_WORKER_REST;
    const bool clearing = worker->state == BUILDING_CLOCK_WORKER_CLEAR_DEBRIS;
    const bool carrying = worker->carried_load_id < BUILDING_CLOCK_MAX_LOADS;
    const int crouch = sitting ? 5 : (clearing || (job != NULL && job->kind == BUILDING_CLOCK_JOB_STAGE_MATERIAL && worker->state == BUILDING_CLOCK_WORKER_FINISH_JOB)) && frame < 4U ? 3 : 0;
    const int head_y = y - 16 + crouch;
    const SDL_Color uniform = uniforms[worker_id % 3U];
    const SDL_Color helmet = worker_id == 0U ? COLOR_WORKER : COLOR_LADDER;
    draw_rope_equipment(renderer, scene, worker_id);
    /* Hard hat, shaded face, nose and tiny moustache distinguish these workers
     * from pale markers even when backed by orange masonry. */
    fill_rect(renderer, x - 4, head_y - 1, 8, 4, COLOR_BOOTS);
    fill_rect(renderer, x - 3, head_y - 2, 6, 3, helmet);
    fill_rect(renderer, x - 5, head_y + 1, 11, 2, helmet);
    fill_rect(renderer, x - 2, head_y + 3, 5, 4, COLOR_SKIN);
    fill_rect(renderer, x + direction * 3, head_y + 4, 2, 2, COLOR_SKIN);
    fill_rect(renderer, x + direction, head_y + 3, 1, 1, COLOR_BACKGROUND);
    if (sitting && frame == 0U)
        fill_rect(renderer, x + direction, head_y + 3, 2, 1, COLOR_BOOTS);
    if ((worker_id & 1U) == 0U)
        fill_rect(renderer, x + direction, head_y + 5, 3, 1, COLOR_BOOTS);
    fill_rect(renderer, x - 3, y - 10 + crouch, 7, 6, uniform);
    fill_rect(renderer, x - 2, y - 10 + crouch, 1, 4, COLOR_WORKER);
    fill_rect(renderer, x + 2, y - 10 + crouch, 1, 4, COLOR_WORKER);
    fill_rect(renderer, x, y - 6 + crouch, 2, 2, COLOR_STEEL_DARK);
    const int leg = moving && !climbing ? stride[frame] : 1;
    const int foot_y = climbing ? (frame < 4U ? -3 : 0) : 0;
    draw_limb(renderer, x - 1, y - 5 + crouch, x - leg - (sitting ? 3 : 0), y + foot_y, uniform);
    draw_limb(renderer, x + 1, y - 5 + crouch, x + leg + (sitting ? 3 : 0), y + (climbing ? -3 - foot_y : 0), uniform);
    fill_rect(renderer, x - leg - 1 - (sitting ? 3 : 0), y + foot_y, 4, 2, COLOR_BOOTS);
    fill_rect(renderer, x + leg - 1 + (sitting ? 3 : 0), y + (climbing ? -3 - foot_y : 0), 4, 2, COLOR_BOOTS);
    int left_x = x - 5, left_y = y - 6 + crouch, right_x = x + 5, right_y = y - 6 + crouch;
    if (climbing) {
        left_y = y - (frame < 4U ? 14 : 7);
        right_y = y - (frame < 4U ? 7 : 14);
    } else if (worker->state == BUILDING_CLOCK_WORKER_HAMMER) {
        const int target_x = worker->rope.phase == BUILDING_CLOCK_ROPE_WORK ? worker->rope.work_position.x : x + direction * 8;
        const int target_y = worker->rope.phase == BUILDING_CLOCK_ROPE_WORK ? worker->rope.work_position.y - 8 : y - 9;
        if (job != NULL && job->kind == BUILDING_CLOCK_JOB_PLACE_SECTION && worker->rope.phase == BUILDING_CLOCK_ROPE_WORK) {
            building_clock_rect_t bricks[BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION];
            const uint8_t digit = job->subject.digit_id;
            const building_clock_section_state_t* section = &scene->state.digits[digit].incoming.sections[job->subject.section_id];
            const uint8_t count = building_clock_section_visible_bricks(scene->world->digit_work_areas[digit].envelope, scene->state.digits[digit].incoming.numeral, section->kind, bricks);
            uint32_t duration = count > 0U ? job->action_duration_ms / count : 100U;
            if (duration < 100U)
                duration = 100U;
            uint32_t progress = worker->state_elapsed_ms * 1000U / duration;
            if (progress > 1000U)
                progress = 1000U;
            const int start_x = x + direction * 6, start_y = y - 8;
            right_x = start_x + (target_x - start_x) * (int)progress / 1000;
            right_y = start_y + (target_y - start_y) * (int)progress / 1000;
            left_x = target_x;
            left_y = target_y + 3;
            draw_masonry_brick(renderer, (building_clock_rect_t){(int16_t)(right_x - 3), (int16_t)(right_y - 2), 6U, 4U}, COLOR_MASONRY_LIGHT);
            fill_rect(renderer, left_x - 2, left_y, 6, 1, COLOR_STEEL);
        } else {
            right_x = frame < 4U ? x + direction * 5 : target_x;
            right_y = frame < 4U ? y - 17 : target_y;
            left_x = target_x;
            left_y = target_y + 2;
            draw_limb(renderer, right_x, right_y, right_x + direction * 2, right_y - 4, COLOR_BOOTS);
            fill_rect(renderer, right_x + direction * 2 - 2, right_y - 6, 6, 3, COLOR_STEEL);
            if (frame == 4U) {
                fill_rect(renderer, target_x + direction * 3, target_y - 3, 2, 2, COLOR_WORKER_ACTION);
                fill_rect(renderer, target_x + direction * 5, target_y, 1, 1, COLOR_WORKER_ACTION);
            }
        }
    } else if (worker->state == BUILDING_CLOCK_WORKER_OPERATE_MACHINERY && worker->tended_builder_id < scene->state.worker_count &&
               scene->state.workers[worker->tended_builder_id].rope.moved_vertically) {
        fill_rect(renderer, x + 3, y - 7, 9, 6, COLOR_BOOTS);
        fill_rect(renderer, x + 4, y - 6, 7, 2, COLOR_ROPE);
        draw_limb(renderer, x + 7, y - 4, x + 7 + (frame < 4U ? 3 : -3), y - 4 + (frame % 4U < 2U ? -3 : 3), COLOR_LADDER);
        right_x = x + 7 + (frame < 4U ? 3 : -3);
        right_y = y - 4 + (frame % 4U < 2U ? -3 : 3);
    } else if (clearing) {
        right_x = x + direction * (frame < 4U ? 4 : 8);
        right_y = y - 5;
        draw_limb(renderer, right_x, right_y, x + direction * 12, y, COLOR_BOOTS);
        fill_rect(renderer, x + direction * 10, y - 1, 6, 3, COLOR_STEEL);
        if (frame >= 4U) {
            fill_rect(renderer, x + direction * 10, y - (int)(frame - 3U), 3, 2, COLOR_MASONRY);
            fill_rect(renderer, x + direction * 13, y - 2, 2, 1, COLOR_MASONRY_DARK);
        }
    } else if (worker->state == BUILDING_CLOCK_WORKER_INSPECT) {
        left_x = x + direction * 5;
        left_y = y - 8;
        right_x = x + direction * (frame < 4U ? 8 : 5);
        right_y = y - (frame < 4U ? 10 : 7);
        fill_rect(renderer, left_x - 2, left_y - 2, 5, 7, COLOR_BOOTS);
        fill_rect(renderer, left_x - 1, left_y - 1, 3, 4, COLOR_WORKER);
    } else if (worker->state == BUILDING_CLOCK_WORKER_CELEBRATE) {
        left_x = x - 6;
        right_x = x + 6;
        left_y = y - 16 - (int)(frame % 3U);
        right_y = y - 18 + (int)(frame % 3U);
    } else if (worker->state == BUILDING_CLOCK_WORKER_DRINK) {
        right_x = x + direction * 5;
        right_y = frame < 4U ? head_y + 6 : y - 6;
        fill_rect(renderer, right_x - 1, right_y - 3, 4, 4, COLOR_WORKER);
        fill_rect(renderer, right_x + 3, right_y - 2, 2, 2, COLOR_BOOTS);
    } else if (worker->state == BUILDING_CLOCK_WORKER_SMOKE) {
        right_x = x + direction * 5;
        right_y = head_y + 6;
        fill_rect(renderer, right_x, right_y, direction * 3 < 0 ? 1 : 3, 1, COLOR_WORKER);
        fill_rect(renderer, right_x + direction * 3, right_y, 1, 1, COLOR_MASONRY_LIGHT);
        fill_rect(renderer, right_x + direction * (4 + (int)frame / 3), right_y - 3 - (int)frame, 2, 2, COLOR_STEEL_DARK);
    } else if (sitting) {
        fill_rect(renderer, x - 5, y - 3, 11, 3, COLOR_BOOTS);
        right_x = x + 4;
        right_y = y - 2;
    } else if (worker->state == BUILDING_CLOCK_WORKER_WAIT || worker->state == BUILDING_CLOCK_WORKER_WATCH || worker->state == BUILDING_CLOCK_WORKER_AVOID_DANGER) {
        right_x = x + direction * 5;
        right_y = frame < 4U ? head_y + 2 : y - 6;
        if (worker->state == BUILDING_CLOCK_WORKER_WAIT && frame == 0U) {
            /* A boot tap and a look towards the blocked route, not a UI icon. */
            fill_rect(renderer, x + direction * 4, y - 1, 3, 2, COLOR_BOOTS);
        }
    } else if (worker->state == BUILDING_CLOCK_WORKER_PUSH) {
        left_x = x + direction * 8;
        right_x = x + direction * 10;
        left_y = right_y = y - 7;
    } else if (carrying || (job != NULL && job->kind == BUILDING_CLOCK_JOB_STAGE_MATERIAL)) {
        right_x = x + direction * 6;
        right_y = y - 6 + (int)(frame % 3U);
        left_x = x - direction * 4;
        left_y = y - 6;
    } else if (moving) {
        left_x -= stride[frame];
        right_x += stride[frame];
    }
    draw_limb(renderer, x - 2, y - 9 + crouch, left_x, left_y, COLOR_WORKER);
    draw_limb(renderer, x + 2, y - 9 + crouch, right_x, right_y, COLOR_WORKER);
    fill_rect(renderer, left_x, left_y, 2, 2, COLOR_SKIN);
    fill_rect(renderer, right_x, right_y, 2, 2, COLOR_SKIN);
    if (carrying && !climbing) {
        fill_rect(renderer, x - 5, y - 5, 11, 2, COLOR_ROPE);
        fill_rect(renderer, x - 5, y - 9, 1, 4, COLOR_ROPE);
        fill_rect(renderer, x + 5, y - 9, 1, 4, COLOR_ROPE);
    }
    if (show_id) {
        char label[4];
        (void)snprintf(label, sizeof(label), "%u", worker_id);
        draw_text(renderer, x + 7, y - 20, 1, COLOR_WORKER, label);
    }
}

static void draw_navigation(SDL_Renderer* renderer, const building_clock_scene_snapshot_t* scene, const viewer_overlays_t* overlays) {
    const building_clock_nav_graph_t* graph = &scene->world->navigation;
    if (overlays->navigation) {
        for (uint16_t edge = 0U; edge < graph->edge_count; edge++) {
            const building_clock_nav_edge_t* item = &graph->edges[edge];
            SDL_Color color = scene->navigation.edge_owner_masks[edge] != 0U ? COLOR_HAZARD : COLOR_STEEL_DARK;
            if (item->kind == BUILDING_CLOCK_NAV_EDGE_CLIMB) {
                color = COLOR_LADDER;
            }
            set_color(renderer, color);
            (void)SDL_RenderDrawLine(renderer, graph->nodes[item->from_node_id].position.x, graph->nodes[item->from_node_id].position.y, graph->nodes[item->to_node_id].position.x,
                                     graph->nodes[item->to_node_id].position.y);
        }
        for (uint8_t node = 0U; node < graph->node_count; node++) {
            const building_clock_point_t position = graph->nodes[node].position;
            const SDL_Color color = scene->navigation.node_owner_masks[node] != 0U ? COLOR_HAZARD : COLOR_DEBUG;
            fill_rect(renderer, position.x - 2, position.y - 2, 5, 5, color);
            if (overlays->node_ids) {
                char label[5];
                (void)snprintf(label, sizeof(label), "%u", node);
                draw_text(renderer, position.x + 3, position.y - 8, 1, COLOR_DEBUG, label);
            }
        }
    }
    if (overlays->worker_routes) {
        set_color(renderer, COLOR_WORKER_ACTION);
        for (uint8_t worker = 0U; worker < scene->state.worker_count; worker++) {
            const building_clock_worker_t* state = &scene->state.workers[worker];
            for (uint8_t node = state->route_node_index + 1U; node < state->route_node_count; node++) {
                const building_clock_point_t from = scene->world->navigation.nodes[state->route_node_ids[node - 1U]].position;
                const building_clock_point_t to = scene->world->navigation.nodes[state->route_node_ids[node]].position;
                (void)SDL_RenderDrawLine(renderer, from.x, from.y - 2, to.x, to.y - 2);
            }
        }
    }
}

static void draw_debug_areas(SDL_Renderer* renderer, const building_clock_scene_snapshot_t* scene, const viewer_overlays_t* overlays) {
    if (overlays->digit_areas) {
        for (uint8_t digit = 0U; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
            outline_rect(renderer, scene->world->digit_work_areas[digit].envelope, COLOR_DEBUG);
            for (uint8_t access = 0U; access < scene->world->digit_work_areas[digit].access_count; access++) {
                const building_clock_point_t point = scene->world->digit_work_areas[digit].access[access].position;
                fill_rect(renderer, point.x - 2, point.y - 2, 5, 5, COLOR_DEBUG);
            }
        }
    }
    if (overlays->hazards) {
        for (uint8_t hazard = 0U; hazard < scene->state.hazard_count; hazard++) {
            outline_rect(renderer, scene->state.hazards[hazard].area, COLOR_HAZARD);
        }
    }
}

static void draw_status(SDL_Renderer* renderer, const viewer_t* viewer) {
    char line[160];
    fill_rect(renderer, 0, 0, BUILDING_CLOCK_WORLD_WIDTH, 25, (SDL_Color){10, 12, 20, 255});
    (void)snprintf(line, sizeof(line), "SEED %016" PRIx64 "  V%" PRIu32 " A%u  %s  N%u E%u", viewer->world.replay.root_seed, viewer->world.replay.generator_version,
                   viewer->world.replay.generation_attempt, topology_name(viewer->world.topology_family), viewer->world.navigation.node_count, viewer->world.navigation.edge_count);
    draw_text(renderer, 4, 3, 1, COLOR_TEXT, line);
    (void)snprintf(line, sizeof(line), "%02u:%02u > %02u:%02u  %s  WAIT %" PRIu32 " CONFLICT %" PRIu32, viewer->scene.state.current_hour, viewer->scene.state.current_minute,
                   viewer->scene.state.target_hour, viewer->scene.state.target_minute, viewer->paused ? "PAUSED" : "RUN", viewer->scene.metrics.wait_events,
                   viewer->scene.metrics.reservation_conflicts);
    draw_text(renderer, 4, 13, 1, COLOR_TEXT, line);
}

static void draw_date(SDL_Renderer* renderer, const building_clock_scene_snapshot_t* scene) {
    static const char* const weekdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char* const months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    char text[40];
    fill_rect(renderer, 0, scene->world->date_band.y, BUILDING_CLOCK_WORLD_WIDTH, scene->world->date_band.height, (SDL_Color){9, 11, 17, 255});
    (void)snprintf(text, sizeof(text), "%s %02u %s %04u", weekdays[scene->state.date.weekday % 7U], scene->state.date.day,
                   months[scene->state.date.month > 0U && scene->state.date.month <= 12U ? scene->state.date.month - 1U : 0U], scene->state.date.year);
    int x = 115;
    for (const char* character = text; *character != '\0'; character++) {
        const char glyph[] = {*character, '\0'};
        const SDL_Color color = *character >= '0' && *character <= '9' ? COLOR_STEEL : COLOR_MASONRY_LIGHT;
        draw_text(renderer, x, scene->world->date_band.y + 23, 3, color, glyph);
        x += 6 * 3;
    }
}

static void draw_site_feedback(SDL_Renderer* renderer, const building_clock_scene_snapshot_t* scene) {
    for (uint8_t id = 0U; id < BUILDING_CLOCK_RECENT_SITE_EVENTS; id++) {
        const building_clock_site_event_t* event = &scene->state.recent_site_events[id];
        const uint64_t age = scene->state.elapsed_ms - event->elapsed_ms;
        if (event->kind == BUILDING_CLOCK_SITE_NO_EVENT || event->kind == BUILDING_CLOCK_SITE_DIGIT_COMPLETED || age > 700U)
            continue;
        const int phase = (int)(age / 100U);
        const int count = event->kind == BUILDING_CLOCK_SITE_BRICK_SEATED ? 2 : 5;
        for (int puff = 0; puff < count; puff++) {
            const int spread = (puff * 5 + id) % 9 - 4;
            fill_rect(renderer, event->position.x + spread * (phase + 2) / 2, event->position.y - phase / 2 - puff % 3, phase > 4 ? 1 : 2, phase > 4 ? 1 : 2, phase > 3 ? COLOR_BOOTS : COLOR_ROPE);
        }
    }
}

static void render_scene(SDL_Renderer* renderer, viewer_t* viewer) {
    if (building_clock_scene_capture(&viewer->world, &viewer->simulation, &viewer->scene) != BUILDING_CLOCK_SCENE_OK) {
        return;
    }
    set_color(renderer, COLOR_BACKGROUND);
    (void)SDL_RenderClear(renderer);
    draw_supports(renderer, viewer->scene.world);
    draw_locations(renderer, viewer->scene.world, viewer->overlays.locations);
    draw_platforms(renderer, viewer->scene.world);
    draw_ladders(renderer, viewer->scene.world);
    draw_machinery(renderer, &viewer->scene);
    draw_navigation(renderer, &viewer->scene, &viewer->overlays);
    draw_digits(renderer, &viewer->scene, viewer->overlays.status);
    draw_temp_structures(renderer, &viewer->scene);
    draw_falling_and_debris(renderer, &viewer->scene, viewer->overlays.hazards);
    draw_loads(renderer, &viewer->scene);
    draw_site_feedback(renderer, &viewer->scene);
    draw_debug_areas(renderer, &viewer->scene, &viewer->overlays);
    for (uint8_t worker = 0U; worker < viewer->scene.state.worker_count; worker++) {
        draw_worker(renderer, &viewer->scene, worker, viewer->overlays.navigation || viewer->overlays.worker_routes);
    }
    draw_date(renderer, &viewer->scene);
    if (viewer->overlays.status) {
        draw_status(renderer, viewer);
    }
    SDL_RenderPresent(renderer);
}

static bool add_job(viewer_t* viewer, uint8_t target, building_clock_job_kind_t kind) {
    const building_clock_simple_job_spec_t spec = {
        .kind = kind,
        .priority = (uint8_t)(10U + viewer->job_serial % 6U),
        .target_node_id = target,
        .required_worker_capabilities = kind == BUILDING_CLOCK_JOB_INSPECT ? BUILDING_CLOCK_WORKER_CAN_INSPECT : BUILDING_CLOCK_WORKER_CAN_BUILD,
        .action_duration_ms = kind == BUILDING_CLOCK_JOB_PATROL ? 200U : 1000U,
    };
    uint8_t job_id = BUILDING_CLOCK_INVALID_ID;
    if (building_clock_simulation_add_job(&viewer->simulation, &viewer->world, &spec, &job_id) != BUILDING_CLOCK_SIMULATION_OK) {
        return false;
    }
    viewer->job_serial++;
    return true;
}

static uint8_t collect_demo_targets(const viewer_t* viewer, uint8_t targets[24]) {
    uint8_t target_count = 0U;
    if (viewer->world.depot_count > 0U) {
        targets[target_count++] = viewer->world.depots[0].access_node_id;
    }
    for (uint8_t node = 0U; node < viewer->world.navigation.node_count && target_count < 24U; node++) {
        if (viewer->world.navigation.nodes[node].kind == BUILDING_CLOCK_NAV_NODE_MACHINERY_CONTROL || viewer->world.navigation.nodes[node].kind == BUILDING_CLOCK_NAV_NODE_LADDER_TOP) {
            targets[target_count++] = node;
        }
    }
    for (uint8_t staging = 0U; staging < viewer->world.staging_area_count && target_count < 24U; staging++) {
        targets[target_count++] = viewer->world.staging_areas[staging].access_node_id;
    }
    return target_count;
}

static void add_demo_jobs(viewer_t* viewer, uint8_t desired_count) {
    uint8_t targets[24];
    const uint8_t target_count = collect_demo_targets(viewer, targets);
    uint8_t attempts = 0U;
    while (viewer->simulation.state.used_job_slot_count < desired_count && target_count > 0U && attempts < 24U) {
        const building_clock_job_kind_t kind = viewer->job_serial % 3U == 0U ? BUILDING_CLOCK_JOB_SECURE_SECTION : BUILDING_CLOCK_JOB_INSPECT;
        (void)add_job(viewer, targets[(uint8_t)(((viewer->job_serial + attempts) * 7U) % target_count)], kind);
        attempts++;
    }
}

static void refresh_demo_jobs(viewer_t* viewer) {
    if (building_clock_simulation_transition_active(&viewer->simulation)) {
        return;
    }
    for (uint8_t job = 0U; job < BUILDING_CLOCK_MAX_JOBS; job++) {
        const building_clock_job_status_t status = viewer->simulation.state.jobs[job].status;
        if (status == BUILDING_CLOCK_JOB_COMPLETE || status == BUILDING_CLOCK_JOB_CANCELLED) {
            (void)building_clock_simulation_recycle_job(&viewer->simulation, job);
        }
    }
    /* Ambient poses come from the simulation; patrol fixtures are stable-only. */
    if (viewer->scenario == SCENARIO_STABLE) {
        add_demo_jobs(viewer, 2U);
    }
}

static void scenario_times(viewer_scenario_t scenario, uint8_t* from_hour, uint8_t* from_minute, uint8_t* to_hour, uint8_t* to_minute) {
    *from_hour = 14U;
    *from_minute = 37U;
    *to_hour = 14U;
    *to_minute = 38U;
    if (scenario == SCENARIO_ROLLOVER) {
        *from_hour = 14U;
        *from_minute = 59U;
        *to_hour = 15U;
        *to_minute = 0U;
    } else if (scenario == SCENARIO_MIDNIGHT) {
        *from_hour = 23U;
        *from_minute = 59U;
        *to_hour = 0U;
        *to_minute = 0U;
    }
}

static bool reset_viewer(viewer_t* viewer, bool select_world) {
    if (select_world) {
        viewer->selection = building_clock_select_world(&viewer->world, viewer->root_seed, viewer->generator_version);
        if (viewer->selection.status != BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED && viewer->selection.status != BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED_FALLBACK) {
            fprintf(stderr, "World selection failed: %s\n", building_clock_world_selection_status_name(viewer->selection.status));
            return false;
        }
    }
    if (building_clock_simulation_init(&viewer->simulation, &viewer->world, 6U) != BUILDING_CLOCK_SIMULATION_OK) {
        return false;
    }
    viewer->job_serial = 0U;
    uint8_t from_hour;
    uint8_t from_minute;
    uint8_t to_hour;
    uint8_t to_minute;
    scenario_times(viewer->scenario, &from_hour, &from_minute, &to_hour, &to_minute);
    if (building_clock_simulation_set_time(&viewer->simulation, &viewer->world, from_hour, from_minute) != BUILDING_CLOCK_SIMULATION_OK) {
        return false;
    }
    viewer->simulation.state.date = (building_clock_date_t){.year = 1984U, .month = 10U, .day = 26U, .weekday = 5U};
    viewer->next_job_refresh_ms = viewer->scenario == SCENARIO_STABLE ? 5000U : 1000U;
    viewer->next_minute_ms = 60000U;
    if (viewer->scenario != SCENARIO_STABLE && building_clock_simulation_set_time(&viewer->simulation, &viewer->world, to_hour, to_minute) != BUILDING_CLOCK_SIMULATION_OK) {
        return false;
    }
    printf("World seed=%016" PRIx64 " version=%" PRIu32 " attempt=%u topology=%s nodes=%u edges=%u%s\n", viewer->world.replay.root_seed, viewer->world.replay.generator_version,
           viewer->world.replay.generation_attempt, topology_name(viewer->world.topology_family), viewer->world.navigation.node_count, viewer->world.navigation.edge_count,
           viewer->selection.fallback_used ? " fallback" : "");
    return true;
}

static void advance_viewer(viewer_t* viewer, uint32_t elapsed_ms) {
    uint32_t remaining = elapsed_ms;
    while (remaining > 0U) {
        const uint32_t delta = remaining > BUILDING_CLOCK_SIMULATION_MAX_STEP_MS ? BUILDING_CLOCK_SIMULATION_MAX_STEP_MS : remaining;
        const building_clock_simulation_step_result_t result = building_clock_simulation_step(&viewer->simulation, &viewer->world, delta);
        if (result.processed_ms == 0U) {
            break;
        }
        remaining -= result.processed_ms;
        if (viewer->scenario == SCENARIO_UNFINISHED && viewer->simulation.state.elapsed_ms >= viewer->next_minute_ms) {
            const uint16_t minutes = (uint16_t)(((uint16_t)viewer->simulation.state.target_hour * 60U + viewer->simulation.state.target_minute + 1U) % 1440U);
            (void)building_clock_simulation_set_time(&viewer->simulation, &viewer->world, (uint8_t)(minutes / 60U), (uint8_t)(minutes % 60U));
            viewer->next_minute_ms += 60000U;
        }
        if (viewer->simulation.state.elapsed_ms >= viewer->next_job_refresh_ms) {
            refresh_demo_jobs(viewer);
            viewer->next_job_refresh_ms = viewer->simulation.state.elapsed_ms + 1000U;
        }
    }
}

static bool save_png(SDL_Surface* surface, const char* path) {
    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0U);
    if (rgba == NULL) {
        return false;
    }
    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        SDL_FreeSurface(rgba);
        return false;
    }
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png == NULL ? NULL : png_create_info_struct(png);
    if (png == NULL || info == NULL) {
        png_destroy_write_struct(&png, &info);
        (void)fclose(file);
        SDL_FreeSurface(rgba);
        return false;
    }
    if (setjmp(png_jmpbuf(png)) != 0) {
        png_destroy_write_struct(&png, &info);
        (void)fclose(file);
        SDL_FreeSurface(rgba);
        return false;
    }

    png_init_io(png, file);
    png_set_IHDR(png, info, (png_uint_32)rgba->w, (png_uint_32)rgba->h, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    png_bytep rows[BUILDING_CLOCK_WORLD_HEIGHT];
    for (int y = 0; y < rgba->h; y++) {
        rows[y] = (png_bytep)((uint8_t*)rgba->pixels + y * rgba->pitch);
    }
    png_write_image(png, rows);
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    (void)fclose(file);
    SDL_FreeSurface(rgba);
    return true;
}

static bool path_is_png(const char* path) {
    const char* extension = strrchr(path, '.');
    return extension != NULL && strcmp(extension, ".png") == 0;
}

static void save_screenshot(SDL_Surface* surface, const char* path) {
    const bool saved = path_is_png(path) ? save_png(surface, path) : SDL_SaveBMP(surface, path) == 0;
    if (saved) {
        printf("Saved %s\n", path);
    } else {
        fprintf(stderr, "Unable to save %s\n", path);
    }
}

static void save_interactive_screenshot(SDL_Surface* surface) {
    char path[64];
    const time_t now = time(NULL);
    const struct tm* local = localtime(&now);
    if (local == NULL || strftime(path, sizeof(path), "building_clock_%Y%m%d_%H%M%S.png", local) == 0U) {
        (void)snprintf(path, sizeof(path), "building_clock.png");
    }
    save_screenshot(surface, path);
}

static void print_controls(void) {
    puts("Controls: SPACE pause | . step 50ms | R replay | N next seed | 0 stable | 1 14:37->14:38 | 2 14:59->15:00 | 3 23:59->00:00");
    puts("          TAB status | V navigation | I node IDs | L locations | H hazards | W worker routes | D digit areas | S screenshot | +/- speed | ESC quit");
}

static void handle_key(viewer_t* viewer, SDL_Surface* surface, SDL_Keycode key) {
    switch (key) {
    case SDLK_TAB:
        viewer->overlays.status = !viewer->overlays.status;
        break;
    case SDLK_ESCAPE:
        viewer->running = false;
        break;
    case SDLK_SPACE:
        viewer->paused = !viewer->paused;
        break;
    case SDLK_PERIOD:
    case SDLK_RIGHT:
        advance_viewer(viewer, VIEW_STEP_MS);
        break;
    case SDLK_r:
        (void)reset_viewer(viewer, false);
        break;
    case SDLK_n:
        viewer->root_seed += NEXT_SEED_DELTA;
        (void)reset_viewer(viewer, true);
        break;
    case SDLK_0:
    case SDLK_1:
    case SDLK_2:
    case SDLK_3:
    case SDLK_4:
        viewer->scenario = key == SDLK_4 ? SCENARIO_UNFINISHED : (key == SDLK_0 ? SCENARIO_STABLE : (key == SDLK_1 ? SCENARIO_SINGLE : (key == SDLK_2 ? SCENARIO_ROLLOVER : SCENARIO_MIDNIGHT)));
        (void)reset_viewer(viewer, false);
        break;
    case SDLK_v:
        viewer->overlays.navigation = !viewer->overlays.navigation;
        break;
    case SDLK_m: {
        const uint16_t minutes = (uint16_t)(((uint16_t)viewer->simulation.state.target_hour * 60U + viewer->simulation.state.target_minute + 1U) % 1440U);
        (void)building_clock_simulation_set_time(&viewer->simulation, &viewer->world, (uint8_t)(minutes / 60U), (uint8_t)(minutes % 60U));
        break;
    }
    case SDLK_i:
        viewer->overlays.node_ids = !viewer->overlays.node_ids;
        viewer->overlays.navigation |= viewer->overlays.node_ids;
        break;
    case SDLK_l:
        viewer->overlays.locations = !viewer->overlays.locations;
        break;
    case SDLK_h:
        viewer->overlays.hazards = !viewer->overlays.hazards;
        break;
    case SDLK_w:
        viewer->overlays.worker_routes = !viewer->overlays.worker_routes;
        break;
    case SDLK_d:
        viewer->overlays.digit_areas = !viewer->overlays.digit_areas;
        break;
    case SDLK_s:
        save_interactive_screenshot(surface);
        break;
    case SDLK_PLUS:
    case SDLK_EQUALS:
    case SDLK_KP_PLUS:
        if (viewer->speed < 8U) {
            viewer->speed *= 2U;
        }
        break;
    case SDLK_MINUS:
    case SDLK_KP_MINUS:
        if (viewer->speed > 1U) {
            viewer->speed /= 2U;
        }
        break;
    default:
        break;
    }
}

typedef struct {
    uint32_t attempted[BUILDING_CLOCK_TOPOLOGY_COUNT];
    uint32_t stages[BUILDING_CLOCK_TOPOLOGY_COUNT][DIAGNOSTIC_STAGE_COUNT];
    uint32_t preliminary_reasons[BUILDING_CLOCK_TOPOLOGY_COUNT][BUILDING_CLOCK_PREVALIDATION_NEGATIVE_SPACE + 1U];
    uint32_t complete_reasons[BUILDING_CLOCK_TOPOLOGY_COUNT][BUILDING_CLOCK_COMPLETE_VALIDATION_DEBRIS_FOUNDATION_CONFLICT + 1U];
    uint32_t first_attempt[BUILDING_CLOCK_TOPOLOGY_COUNT];
    uint32_t accepted[BUILDING_CLOCK_TOPOLOGY_COUNT];
    uint64_t example_seed[BUILDING_CLOCK_TOPOLOGY_COUNT];
    uint32_t fallback_count;
    uint32_t hard_failure_count;
    uint32_t node_sum;
    uint8_t node_minimum;
    uint8_t node_maximum;
} topology_diagnostics_t;

static int run_diagnostics(uint32_t count) {
    topology_diagnostics_t diagnostics = {.node_minimum = UINT8_MAX};
    for (uint32_t index = 0U; index < count; index++) {
        const uint64_t seed = DEFAULT_SEED + NEXT_SEED_DELTA * index;
        building_clock_world_t candidate;
        building_clock_generate_candidate(&candidate, seed, BUILDING_CLOCK_GENERATOR_VERSION, 0U);
        diagnostics.first_attempt[candidate.topology_family]++;

        building_clock_world_t world;
        const building_clock_world_selection_result_t selection = building_clock_select_world(&world, seed, BUILDING_CLOCK_GENERATOR_VERSION);
        for (uint16_t record = 0U; record < selection.record_count; record++) {
            const building_clock_world_attempt_record_t* attempt = &selection.records[record];
            building_clock_generate_candidate(&candidate, attempt->root_seed, attempt->generator_version, attempt->attempt);
            const building_clock_topology_family_t family = candidate.topology_family;
            diagnostics.attempted[family]++;
            if ((unsigned int)attempt->stage < DIAGNOSTIC_STAGE_COUNT) {
                diagnostics.stages[family][attempt->stage]++;
            }
            if (attempt->stage == BUILDING_CLOCK_WORLD_ATTEMPT_PREVALIDATION_FAILED && attempt->prevalidation_reason <= BUILDING_CLOCK_PREVALIDATION_NEGATIVE_SPACE) {
                diagnostics.preliminary_reasons[family][attempt->prevalidation_reason]++;
            }
            if (attempt->stage == BUILDING_CLOCK_WORLD_ATTEMPT_COMPLETE_VALIDATION_FAILED && attempt->complete_validation_reason <= BUILDING_CLOCK_COMPLETE_VALIDATION_DEBRIS_FOUNDATION_CONFLICT) {
                diagnostics.complete_reasons[family][attempt->complete_validation_reason]++;
            }
        }
        if (selection.status == BUILDING_CLOCK_WORLD_SELECTION_HARD_FAILURE || selection.status == BUILDING_CLOCK_WORLD_SELECTION_INVALID_ARGUMENT) {
            diagnostics.hard_failure_count++;
            continue;
        }
        diagnostics.fallback_count += selection.fallback_used ? 1U : 0U;
        const building_clock_topology_family_t family = world.topology_family;
        if (diagnostics.accepted[family] == 0U) {
            diagnostics.example_seed[family] = seed;
        }
        diagnostics.accepted[family]++;
        diagnostics.node_sum += world.navigation.node_count;
        if (world.navigation.node_count < diagnostics.node_minimum) {
            diagnostics.node_minimum = world.navigation.node_count;
        }
        if (world.navigation.node_count > diagnostics.node_maximum) {
            diagnostics.node_maximum = world.navigation.node_count;
        }
    }

    printf("Topology diagnostics across %" PRIu32 " requested roots\n", count);
    puts("family                 first  attempts  prelim  nav  complete  accepted  final  example-seed");
    for (building_clock_topology_family_t family = 0; family < BUILDING_CLOCK_TOPOLOGY_COUNT; family++) {
        printf("%-22s %6" PRIu32 " %9" PRIu32 " %7" PRIu32 " %4" PRIu32 " %9" PRIu32 " %8" PRIu32 " %6" PRIu32 "  %016" PRIx64 "\n", topology_name(family), diagnostics.first_attempt[family],
               diagnostics.attempted[family], diagnostics.stages[family][BUILDING_CLOCK_WORLD_ATTEMPT_PREVALIDATION_FAILED], diagnostics.stages[family][BUILDING_CLOCK_WORLD_ATTEMPT_NAVIGATION_FAILED],
               diagnostics.stages[family][BUILDING_CLOCK_WORLD_ATTEMPT_COMPLETE_VALIDATION_FAILED], diagnostics.stages[family][BUILDING_CLOCK_WORLD_ATTEMPT_ACCEPTED], diagnostics.accepted[family],
               diagnostics.example_seed[family]);
        for (building_clock_prevalidation_reason_t reason = BUILDING_CLOCK_PREVALIDATION_NULL_WORLD; reason <= BUILDING_CLOCK_PREVALIDATION_NEGATIVE_SPACE; reason++) {
            if (diagnostics.preliminary_reasons[family][reason] != 0U) {
                printf("  preliminary: %s (%" PRIu32 ")\n", building_clock_prevalidation_reason_name(reason), diagnostics.preliminary_reasons[family][reason]);
            }
        }
        for (building_clock_complete_validation_reason_t reason = BUILDING_CLOCK_COMPLETE_VALIDATION_NULL_WORLD; reason <= BUILDING_CLOCK_COMPLETE_VALIDATION_DEBRIS_FOUNDATION_CONFLICT; reason++) {
            if (diagnostics.complete_reasons[family][reason] != 0U) {
                printf("  complete: %s (%" PRIu32 ")\n", building_clock_complete_validation_reason_name(reason), diagnostics.complete_reasons[family][reason]);
            }
        }
    }
    const uint32_t accepted_count = count - diagnostics.hard_failure_count;
    printf("fallbacks=%" PRIu32 " hard-failures=%" PRIu32 " navigation-nodes min=%u max=%u average=%.2f\n", diagnostics.fallback_count, diagnostics.hard_failure_count, diagnostics.node_minimum,
           diagnostics.node_maximum, accepted_count == 0U ? 0.0 : (double)diagnostics.node_sum / accepted_count);
    return diagnostics.hard_failure_count == 0U ? 0 : 1;
}

static viewer_scenario_t parse_scenario(const char* name) {
    if (strcmp(name, "unfinished") == 0) {
        return SCENARIO_UNFINISHED;
    }
    if (strcmp(name, "stable") == 0) {
        return SCENARIO_STABLE;
    }
    if (strcmp(name, "rollover") == 0) {
        return SCENARIO_ROLLOVER;
    }
    if (strcmp(name, "midnight") == 0) {
        return SCENARIO_MIDNIGHT;
    }
    return SCENARIO_SINGLE;
}

static bool check_character_animation(SDL_Renderer* renderer, SDL_Surface* canvas, viewer_t* viewer, const char* sheet_path) {
    static const char* const names[] = {"IDLE",   "ASSIGN", "WALK",   "CLIMB",  "RIDE",   "CARRY", "PUSH",  "WAIT",  "TURN", "INSPECT",
                                        "HAMMER", "WINCH",  "SHOVEL", "DANGER", "UNPACK", "CHEER", "SMOKE", "DRINK", "REST", "WATCH"};
    if (building_clock_scene_capture(&viewer->world, &viewer->simulation, &viewer->scene) != BUILDING_CLOCK_SCENE_OK) {
        return false;
    }
    unsigned animated = 0U;
    for (unsigned state = 0U; state < sizeof(names) / sizeof(names[0]); state++) {
        uint32_t first = 0U;
        bool changed = false;
        for (unsigned frame = 0U; frame < 8U; frame++) {
            set_color(renderer, COLOR_BACKGROUND);
            (void)SDL_RenderClear(renderer);
            building_clock_worker_t* worker = &viewer->scene.state.workers[0];
            *worker = (building_clock_worker_t){
                .state = (building_clock_worker_state_kind_t)state,
                .position = {(int16_t)(frame * 3U), 0},
                .facing = BUILDING_CLOCK_FACING_RIGHT,
                .reserved_edge_id = (state == BUILDING_CLOCK_WORKER_WALK || state == BUILDING_CLOCK_WORKER_CLIMB || state == BUILDING_CLOCK_WORKER_CARRY) ? 0U : BUILDING_CLOCK_INVALID_EDGE_ID,
                .current_job_id = state == BUILDING_CLOCK_WORKER_FINISH_JOB ? 0U : BUILDING_CLOCK_INVALID_ID,
                .tended_builder_id = state == BUILDING_CLOCK_WORKER_OPERATE_MACHINERY ? 1U : BUILDING_CLOCK_INVALID_ID,
                .carried_load_id = state == BUILDING_CLOCK_WORKER_CARRY ? 0U : BUILDING_CLOCK_INVALID_ID};
            viewer->scene.state.workers[1].rope.moved_vertically = true;
            viewer->scene.state.jobs[0].kind = BUILDING_CLOCK_JOB_STAGE_MATERIAL;
            viewer->scene.state.elapsed_ms = frame * 110U;
            viewer->scene.worker_presentation_positions[0] = (building_clock_point_t){240, 240};
            draw_worker(renderer, &viewer->scene, 0U, false);
            SDL_RenderPresent(renderer);
            uint32_t hash = UINT32_C(2166136261);
            for (int y = 205; y < 246; y++) {
                const uint32_t* row = (const uint32_t*)((const uint8_t*)canvas->pixels + y * canvas->pitch);
                for (int x = 215; x < 265; x++) {
                    hash = (hash ^ row[x]) * UINT32_C(16777619);
                }
            }
            if (frame == 0U)
                first = hash;
            changed |= hash != first;
        }
        animated += changed ? 1U : 0U;
        if ((state == BUILDING_CLOCK_WORKER_WALK || state == BUILDING_CLOCK_WORKER_CLIMB || state == BUILDING_CLOCK_WORKER_CARRY || state == BUILDING_CLOCK_WORKER_HAMMER ||
             state == BUILDING_CLOCK_WORKER_OPERATE_MACHINERY || state == BUILDING_CLOCK_WORKER_CLEAR_DEBRIS || state == BUILDING_CLOCK_WORKER_INSPECT || state == BUILDING_CLOCK_WORKER_FINISH_JOB) &&
            !changed) {
            fprintf(stderr, "No visible animation for %s\n", names[state]);
            return false;
        }
    }
    if (sheet_path != NULL) {
        set_color(renderer, COLOR_BACKGROUND);
        (void)SDL_RenderClear(renderer);
        for (unsigned state = 0U; state < sizeof(names) / sizeof(names[0]); state++) {
            draw_text(renderer, 5, 5 + (int)state * 23, 1, COLOR_STEEL, names[state]);
            for (unsigned frame = 0U; frame < 8U; frame++) {
                building_clock_worker_t* worker = &viewer->scene.state.workers[0];
                worker->state = (building_clock_worker_state_kind_t)state;
                worker->reserved_edge_id = state == BUILDING_CLOCK_WORKER_CLIMB || state == BUILDING_CLOCK_WORKER_WALK ? 0U : BUILDING_CLOCK_INVALID_EDGE_ID;
                worker->position.x = (int16_t)(frame * 3U);
                worker->current_job_id = state == BUILDING_CLOCK_WORKER_FINISH_JOB ? 0U : BUILDING_CLOCK_INVALID_ID;
                worker->tended_builder_id = state == BUILDING_CLOCK_WORKER_OPERATE_MACHINERY ? 1U : BUILDING_CLOCK_INVALID_ID;
                worker->carried_load_id = state == BUILDING_CLOCK_WORKER_CARRY ? 0U : BUILDING_CLOCK_INVALID_ID;
                viewer->scene.state.elapsed_ms = frame * 110U;
                viewer->scene.worker_presentation_positions[0] = (building_clock_point_t){(int16_t)(135 + frame * 42U), (int16_t)(21 + state * 23U)};
                draw_worker(renderer, &viewer->scene, 0U, false);
            }
        }
        SDL_RenderPresent(renderer);
        if (!save_png(canvas, sheet_path))
            return false;
    }
    printf("Character animation checks passed: %u changing poses\n", animated);
    return true;
}

int main(int argc, char** argv) {
    viewer_t viewer = {
        .root_seed = DEFAULT_SEED,
        .generator_version = BUILDING_CLOCK_GENERATOR_VERSION,
        .scenario = SCENARIO_SINGLE,
        .paused = false,
        .running = true,
        .speed = 1U,
        .overlays = {0},
    };
    const char* screenshot_path = NULL;
    const char* sequence_directory = NULL;
    const char* animation_sheet = NULL;
    bool animation_check = false;
    uint32_t frame_ms = 100U;
    uint32_t duration_ms = 0U;
    uint32_t diagnostic_count = 0U;
    for (int argument = 1; argument < argc; argument++) {
        if (strcmp(argv[argument], "--seed") == 0 && argument + 1 < argc) {
            viewer.root_seed = strtoull(argv[++argument], NULL, 0);
        } else if (strcmp(argv[argument], "--scenario") == 0 && argument + 1 < argc) {
            viewer.scenario = parse_scenario(argv[++argument]);
        } else if (strcmp(argv[argument], "--screenshot") == 0 && argument + 1 < argc) {
            screenshot_path = argv[++argument];
        } else if (strcmp(argv[argument], "--sequence") == 0 && argument + 1 < argc) {
            sequence_directory = argv[++argument];
        } else if (strcmp(argv[argument], "--frame-ms") == 0 && argument + 1 < argc) {
            frame_ms = (uint32_t)strtoul(argv[++argument], NULL, 0);
        } else if (strcmp(argv[argument], "--animation-check") == 0) {
            animation_check = true;
        } else if (strcmp(argv[argument], "--animation-sheet") == 0 && argument + 1 < argc) {
            animation_check = true;
            animation_sheet = argv[++argument];
        } else if (strcmp(argv[argument], "--duration-ms") == 0 && argument + 1 < argc) {
            duration_ms = (uint32_t)strtoul(argv[++argument], NULL, 0);
        } else if (strcmp(argv[argument], "--diagnose") == 0 && argument + 1 < argc) {
            diagnostic_count = (uint32_t)strtoul(argv[++argument], NULL, 0);
        } else if (strcmp(argv[argument], "--all-overlays") == 0) {
            viewer.overlays = (viewer_overlays_t){true, true, true, true, true, true, true};
        } else {
            fprintf(stderr,
                    "Usage: %s [--seed N] [--scenario stable|single|rollover|midnight|unfinished] [--duration-ms N] [--screenshot FILE | --sequence DIR --frame-ms N] [--animation-check | "
                    "--animation-sheet PNG] [--all-overlays] [--diagnose N]\n",
                    argv[0]);
            return 2;
        }
    }
    if (diagnostic_count > 0U) {
        return run_diagnostics(diagnostic_count);
    }
    if (!reset_viewer(&viewer, true)) {
        return 1;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Surface* canvas = SDL_CreateRGBSurfaceWithFormat(0, BUILDING_CLOCK_WORLD_WIDTH, BUILDING_CLOCK_WORLD_HEIGHT, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* renderer = canvas == NULL ? NULL : SDL_CreateSoftwareRenderer(canvas);
    if (canvas == NULL || renderer == NULL) {
        fprintf(stderr, "Unable to create software canvas: %s\n", SDL_GetError());
        SDL_FreeSurface(canvas);
        SDL_Quit();
        return 1;
    }
    if (animation_check || sequence_directory != NULL) {
        bool success = true;
        if (animation_check) {
            success = check_character_animation(renderer, canvas, &viewer, animation_sheet);
        } else if (frame_ms == 0U || frame_ms % VIEW_STEP_MS != 0U) {
            fprintf(stderr, "Sequence interval must be a positive multiple of 50 ms\n");
            success = false;
        } else {
            uint32_t elapsed = 0U;
            unsigned frame = 0U;
            for (;;) {
                render_scene(renderer, &viewer);
                char path[1024];
                const int length = snprintf(path, sizeof(path), "%s/%06u.png", sequence_directory, frame++);
                if (length < 0 || (size_t)length >= sizeof(path) || !save_png(canvas, path)) {
                    success = false;
                    break;
                }
                if (elapsed == duration_ms)
                    break;
                const uint32_t delta = duration_ms - elapsed < frame_ms ? duration_ms - elapsed : frame_ms;
                advance_viewer(&viewer, delta);
                elapsed += delta;
            }
        }
        SDL_DestroyRenderer(renderer);
        SDL_FreeSurface(canvas);
        SDL_Quit();
        return success ? 0 : 1;
    }
    if (screenshot_path != NULL) {
        advance_viewer(&viewer, duration_ms);
        render_scene(renderer, &viewer);
        save_screenshot(canvas, screenshot_path);
        SDL_DestroyRenderer(renderer);
        SDL_FreeSurface(canvas);
        SDL_Quit();
        return 0;
    }

    SDL_Window* window = SDL_CreateWindow("Building Clock semantic viewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, BUILDING_CLOCK_WORLD_WIDTH * VIEW_SCALE,
                                          BUILDING_CLOCK_WORLD_HEIGHT * VIEW_SCALE, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (window == NULL) {
        fprintf(stderr, "Unable to create window: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_FreeSurface(canvas);
        SDL_Quit();
        return 1;
    }
    print_controls();
    uint32_t previous_ticks = SDL_GetTicks();
    uint32_t accumulated_ms = 0U;
    while (viewer.running) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) {
                viewer.running = false;
            } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                handle_key(&viewer, canvas, event.key.keysym.sym);
            }
        }
        const uint32_t now = SDL_GetTicks();
        const uint32_t frame_ms = now - previous_ticks;
        previous_ticks = now;
        if (!viewer.paused) {
            accumulated_ms += frame_ms * viewer.speed;
            while (accumulated_ms >= VIEW_STEP_MS) {
                advance_viewer(&viewer, VIEW_STEP_MS);
                accumulated_ms -= VIEW_STEP_MS;
            }
        }
        render_scene(renderer, &viewer);
        SDL_Surface* window_surface = SDL_GetWindowSurface(window);
        if (window_surface != NULL) {
            (void)SDL_BlitScaled(canvas, NULL, window_surface, NULL);
            (void)SDL_UpdateWindowSurface(window);
        }
        SDL_Delay(10U);
    }
    SDL_DestroyWindow(window);
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(canvas);
    SDL_Quit();
    return 0;
}
