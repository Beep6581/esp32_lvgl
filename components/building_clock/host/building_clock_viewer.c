#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "building_clock_generator.h"
#include "building_clock_scene.h"
#include "building_clock_world_selection.h"
#include "glcdfont_5x7.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
} viewer_scenario_t;

typedef struct {
    bool navigation;
    bool node_ids;
    bool locations;
    bool hazards;
    bool worker_routes;
    bool digit_areas;
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
} viewer_t;

static const SDL_Color COLOR_BACKGROUND = {7, 8, 13, 255};
static const SDL_Color COLOR_STEEL = {105, 122, 193, 255};
static const SDL_Color COLOR_STEEL_DARK = {48, 57, 99, 255};
static const SDL_Color COLOR_LADDER = {238, 206, 105, 255};
static const SDL_Color COLOR_MASONRY = {205, 92, 54, 255};
static const SDL_Color COLOR_MASONRY_DARK = {104, 42, 34, 255};
static const SDL_Color COLOR_WORKER = {239, 228, 195, 255};
static const SDL_Color COLOR_WORKER_ACTION = {255, 242, 129, 255};
static const SDL_Color COLOR_HAZARD = {221, 54, 62, 255};
static const SDL_Color COLOR_DEBUG = {76, 215, 199, 255};
static const SDL_Color COLOR_TEXT = {213, 220, 232, 255};

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
    set_color(renderer, COLOR_STEEL);
    for (uint8_t index = 0U; index < world->platform_count; index++) {
        const building_clock_platform_t* platform = &world->platforms[index];
        const SDL_Rect beam = {.x = platform->x_start, .y = platform->surface_y, .w = platform->x_end - platform->x_start + 1, .h = 4};
        (void)SDL_RenderFillRect(renderer, &beam);
        for (int x = platform->x_start + 3; x < platform->x_end; x += 12) {
            (void)SDL_RenderDrawLine(renderer, x, platform->surface_y + 4, x + 5, platform->surface_y + 8);
        }
    }
}

static void draw_supports(SDL_Renderer* renderer, const building_clock_world_t* world) {
    for (uint8_t index = 0U; index < world->major_support_count; index++) {
        const building_clock_rect_t bounds = world->major_supports[index].bounds;
        outline_rect(renderer, bounds, COLOR_STEEL_DARK);
        set_color(renderer, COLOR_STEEL_DARK);
        (void)SDL_RenderDrawLine(renderer, bounds.x, bounds.y, bounds.x + bounds.width, bounds.y + bounds.height);
        (void)SDL_RenderDrawLine(renderer, bounds.x + bounds.width, bounds.y, bounds.x, bounds.y + bounds.height);
    }
}

static void draw_ladders(SDL_Renderer* renderer, const building_clock_world_t* world) {
    set_color(renderer, COLOR_LADDER);
    for (uint8_t index = 0U; index < world->ladder_count; index++) {
        const building_clock_ladder_t* ladder = &world->ladders[index];
        (void)SDL_RenderDrawLine(renderer, ladder->x - 3, ladder->top_y, ladder->x - 3, ladder->bottom_y);
        (void)SDL_RenderDrawLine(renderer, ladder->x + 3, ladder->top_y, ladder->x + 3, ladder->bottom_y);
        for (int y = ladder->top_y + 3; y < ladder->bottom_y; y += 7) {
            (void)SDL_RenderDrawLine(renderer, ladder->x - 3, y, ladder->x + 3, y);
        }
    }
}

static void draw_locations(SDL_Renderer* renderer, const building_clock_world_t* world, bool overlay) {
    for (uint8_t index = 0U; index < world->debris_bay_count; index++) {
        const building_clock_rect_t area = world->debris_bays[index].area;
        fill_rect(renderer, area.x, area.y, area.width, area.height, (SDL_Color){39, 26, 29, 255});
        if (overlay) {
            outline_rect(renderer, area, COLOR_HAZARD);
        }
    }
    for (uint8_t index = 0U; index < world->staging_area_count; index++) {
        const building_clock_rect_t area = world->staging_areas[index].area;
        fill_rect(renderer, area.x, area.y, area.width, area.height, (SDL_Color){37, 40, 63, 255});
        if (overlay) {
            outline_rect(renderer, area, COLOR_DEBUG);
        }
    }
    for (uint8_t index = 0U; index < world->depot_count; index++) {
        const building_clock_rect_t area = world->depots[index].area;
        fill_rect(renderer, area.x, area.y, area.width, area.height, (SDL_Color){68, 56, 34, 255});
        outline_rect(renderer, area, COLOR_LADDER);
        draw_text(renderer, area.x + 2, area.y + 2, 1, COLOR_LADDER, "DEPOT");
    }
}

static void draw_machinery(SDL_Renderer* renderer, const building_clock_scene_snapshot_t* scene) {
    const building_clock_world_t* world = scene->world;
    set_color(renderer, COLOR_LADDER);
    for (uint8_t crane = 0U; crane < world->crane_count; crane++) {
        const building_clock_crane_layout_t* layout = &world->cranes[crane];
        const building_clock_crane_t* state = &scene->state.cranes[crane];
        (void)SDL_RenderDrawLine(renderer, layout->base_position.x, layout->base_position.y, layout->base_position.x, layout->home_hook_position.y - 12);
        const int boom_end = layout->base_position.x < BUILDING_CLOCK_WORLD_WIDTH / 2 ? layout->working_area.x + layout->working_area.width : layout->working_area.x;
        (void)SDL_RenderDrawLine(renderer, layout->base_position.x, layout->home_hook_position.y - 12, boom_end, layout->home_hook_position.y - 12);
        const building_clock_point_t hook = state->hook_position.x == 0 && state->hook_position.y == 0 ? layout->home_hook_position : state->hook_position;
        (void)SDL_RenderDrawLine(renderer, hook.x, layout->home_hook_position.y - 12, hook.x, hook.y);
        fill_rect(renderer, hook.x - 2, hook.y, 5, 4, COLOR_LADDER);
    }
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
            const uint8_t stop = scene->state.carts[route].current_stop_id < cart_route->stop_count ? scene->state.carts[route].current_stop_id : 0U;
            const building_clock_point_t position = cart_route->stops[stop].position;
            fill_rect(renderer, position.x - 7, position.y - 4, 14, 5, COLOR_LADDER);
        }
    }
}

static SDL_Rect section_rect(building_clock_rect_t envelope, building_clock_section_kind_t kind) {
    const int thickness = 9;
    const int middle_y = envelope.y + envelope.height / 2 - thickness / 2;
    const int right_x = envelope.x + envelope.width - thickness;
    const int lower_y = middle_y + thickness;
    const int vertical_height = envelope.height / 2 - thickness;
    switch (kind) {
    case BUILDING_CLOCK_SECTION_FOUNDATION:
        return (SDL_Rect){envelope.x + 5, envelope.y + envelope.height - thickness, envelope.width - 10, thickness};
    case BUILDING_CLOCK_SECTION_TOP_BEAM:
        return (SDL_Rect){envelope.x + 5, envelope.y, envelope.width - 10, thickness};
    case BUILDING_CLOCK_SECTION_MIDDLE_BEAM:
        return (SDL_Rect){envelope.x + 5, middle_y, envelope.width - 10, thickness};
    case BUILDING_CLOCK_SECTION_UPPER_LEFT_PIER:
        return (SDL_Rect){envelope.x, envelope.y + 5, thickness, vertical_height};
    case BUILDING_CLOCK_SECTION_UPPER_RIGHT_PIER:
        return (SDL_Rect){right_x, envelope.y + 5, thickness, vertical_height};
    case BUILDING_CLOCK_SECTION_LOWER_LEFT_PIER:
        return (SDL_Rect){envelope.x, lower_y, thickness, vertical_height};
    case BUILDING_CLOCK_SECTION_LOWER_RIGHT_PIER:
        return (SDL_Rect){right_x, lower_y, thickness, vertical_height};
    case BUILDING_CLOCK_SECTION_CORNER_OR_DIAGONAL:
    case BUILDING_CLOCK_SECTION_FINISHING_OR_INFILL:
    default:
        return (SDL_Rect){envelope.x + envelope.width / 2 - 4, envelope.y + envelope.height / 2 - 4, 8, 8};
    }
}

static void draw_structure(SDL_Renderer* renderer, building_clock_rect_t envelope, const building_clock_digit_structure_t* structure, bool incoming) {
    for (uint8_t index = 0U; index < structure->section_count; index++) {
        const building_clock_section_state_t* section = &structure->sections[index];
        if (section->status == BUILDING_CLOCK_SECTION_ABSENT || section->status == BUILDING_CLOCK_SECTION_STAGED || section->status == BUILDING_CLOCK_SECTION_IN_TRANSIT ||
            section->status == BUILDING_CLOCK_SECTION_FALLING || section->status == BUILDING_CLOCK_SECTION_DEBRIS) {
            continue;
        }
        SDL_Color color = COLOR_MASONRY;
        if (section->status == BUILDING_CLOCK_SECTION_FAILED) {
            color = COLOR_MASONRY_DARK;
        } else if (incoming && section->status == BUILDING_CLOCK_SECTION_PLACED) {
            color = COLOR_STEEL;
        }
        const SDL_Rect rect = section_rect(envelope, section->kind);
        set_color(renderer, color);
        (void)SDL_RenderFillRect(renderer, &rect);
        set_color(renderer, COLOR_MASONRY_DARK);
        for (int x = rect.x + 6; x < rect.x + rect.w; x += 8) {
            (void)SDL_RenderDrawLine(renderer, x, rect.y, x, rect.y + rect.h - 1);
        }
    }
}

static void draw_loads(SDL_Renderer* renderer, const building_clock_scene_snapshot_t* scene) {
    for (uint8_t load_id = 0U; load_id < BUILDING_CLOCK_MAX_LOADS; load_id++) {
        const building_clock_load_t* load = &scene->state.loads[load_id];
        if (load->status != BUILDING_CLOCK_LOAD_STAGED && load->status != BUILDING_CLOCK_LOAD_CARRIED) {
            continue;
        }
        const SDL_Color color = load->status == BUILDING_CLOCK_LOAD_CARRIED ? COLOR_LADDER : COLOR_STEEL;
        fill_rect(renderer, load->position.x - 4, load->position.y - 7, 9, 5, color);
        outline_rect(renderer, (building_clock_rect_t){.x = (int16_t)(load->position.x - 4), .y = (int16_t)(load->position.y - 7), .width = 9, .height = 5}, COLOR_MASONRY_DARK);
    }
}

static void draw_digits(SDL_Renderer* renderer, const building_clock_scene_snapshot_t* scene) {
    for (uint8_t digit = 0U; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        const building_clock_rect_t envelope = scene->world->digit_work_areas[digit].envelope;
        draw_structure(renderer, envelope, &scene->state.digits[digit].outgoing, false);
        draw_structure(renderer, envelope, &scene->state.digits[digit].incoming, true);
        if (scene->state.digits[digit].lifecycle != BUILDING_CLOCK_DIGIT_STABLE) {
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
        const bool vertical = section->section_kind == BUILDING_CLOCK_SECTION_UPPER_LEFT_PIER || section->section_kind == BUILDING_CLOCK_SECTION_UPPER_RIGHT_PIER ||
                              section->section_kind == BUILDING_CLOCK_SECTION_LOWER_LEFT_PIER || section->section_kind == BUILDING_CLOCK_SECTION_LOWER_RIGHT_PIER;
        const int width = vertical ? 9 : envelope.width - 10;
        const int height = vertical ? envelope.height / 2 - 9 : 9;
        fill_rect(renderer, section->position.x - width / 2, section->position.y - height / 2, width, height, COLOR_MASONRY);
        set_color(renderer, COLOR_LADDER);
        const int tilt = (section->angle_degrees / 15) % 5;
        (void)SDL_RenderDrawLine(renderer, section->position.x - width / 2, section->position.y - tilt, section->position.x + width / 2, section->position.y + tilt);
    }
    for (uint8_t index = 0U; index < BUILDING_CLOCK_MAX_DEBRIS_PIECES; index++) {
        const building_clock_debris_t* debris = &scene->state.debris[index];
        if (debris->status == BUILDING_CLOCK_DEBRIS_UNUSED || debris->status == BUILDING_CLOCK_DEBRIS_CLEARED) {
            continue;
        }
        fill_rect(renderer, debris->position.x - 3, debris->position.y - 2, 7, 4, COLOR_MASONRY);
    }
}

static void draw_temp_structures(SDL_Renderer* renderer, const building_clock_scene_snapshot_t* scene) {
    for (uint8_t index = 0U; index < BUILDING_CLOCK_MAX_TEMP_STRUCTURES; index++) {
        const building_clock_temp_structure_t* state = &scene->state.temp_structures[index];
        if (state->status == BUILDING_CLOCK_TEMP_STRUCTURE_UNUSED || state->attachment_set_id >= scene->world->temp_attachment_set_count) {
            continue;
        }
        const building_clock_temp_attachment_set_t* attachment = &scene->world->temp_attachment_sets[state->attachment_set_id];
        outline_rect(renderer, attachment->allowed_area, COLOR_LADDER);
        for (uint8_t anchor = 0U; anchor < attachment->anchor_count; anchor++) {
            fill_rect(renderer, attachment->anchors[anchor].x - 2, attachment->anchors[anchor].y - 2, 5, 5, COLOR_LADDER);
        }
    }
}

static void draw_worker(SDL_Renderer* renderer, const building_clock_scene_snapshot_t* scene, uint8_t worker_id, bool show_id) {
    const building_clock_worker_t* worker = &scene->state.workers[worker_id];
    const bool action = worker->state == BUILDING_CLOCK_WORKER_HAMMER || worker->state == BUILDING_CLOCK_WORKER_INSPECT || worker->state == BUILDING_CLOCK_WORKER_AVOID_DANGER;
    const SDL_Color color = action ? COLOR_WORKER_ACTION : COLOR_WORKER;
    const int x = worker->position.x;
    const int y = worker->position.y;
    fill_rect(renderer, x - 2, y - 13, 5, 5, color);
    fill_rect(renderer, x - 2, y - 8, 5, 7, color);
    set_color(renderer, color);
    if (worker->state == BUILDING_CLOCK_WORKER_CLIMB) {
        (void)SDL_RenderDrawLine(renderer, x - 2, y - 5, x - 5, y - 1);
        (void)SDL_RenderDrawLine(renderer, x + 2, y - 5, x + 5, y - 9);
    } else if (worker->state == BUILDING_CLOCK_WORKER_HAMMER) {
        const int direction = worker->facing == BUILDING_CLOCK_FACING_LEFT ? -1 : 1;
        (void)SDL_RenderDrawLine(renderer, x, y - 7, x + direction * 6, y - 12);
        fill_rect(renderer, x + direction * 5 - 1, y - 14, 3, 4, COLOR_LADDER);
    } else {
        (void)SDL_RenderDrawLine(renderer, x - 2, y - 5, x - 5, y - 3);
        (void)SDL_RenderDrawLine(renderer, x + 2, y - 5, x + 5, y - 3);
    }
    (void)SDL_RenderDrawLine(renderer, x - 1, y - 1, x - 3, y + 3);
    (void)SDL_RenderDrawLine(renderer, x + 1, y - 1, x + 3, y + 3);
    if (show_id) {
        char label[4];
        (void)snprintf(label, sizeof(label), "%u", worker_id);
        draw_text(renderer, x + 5, y - 12, 1, color, label);
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
    char text[40];
    fill_rect(renderer, 0, scene->world->date_band.y, BUILDING_CLOCK_WORLD_WIDTH, scene->world->date_band.height, (SDL_Color){9, 11, 17, 255});
    (void)snprintf(text, sizeof(text), "%04u-%02u-%02u", scene->state.date.year, scene->state.date.month, scene->state.date.day);
    draw_text(renderer, 192, scene->world->date_band.y + 26, 2, COLOR_STEEL, text);
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
    draw_digits(renderer, &viewer->scene);
    draw_temp_structures(renderer, &viewer->scene);
    draw_falling_and_debris(renderer, &viewer->scene, viewer->overlays.hazards);
    draw_loads(renderer, &viewer->scene);
    draw_debug_areas(renderer, &viewer->scene, &viewer->overlays);
    for (uint8_t worker = 0U; worker < viewer->scene.state.worker_count; worker++) {
        draw_worker(renderer, &viewer->scene, worker, viewer->overlays.navigation || viewer->overlays.worker_routes);
    }
    draw_date(renderer, &viewer->scene);
    draw_status(renderer, viewer);
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
        if (viewer->world.navigation.nodes[node].kind == BUILDING_CLOCK_NAV_NODE_LADDER_TOP) {
            targets[target_count++] = node;
        }
    }
    for (uint8_t staging = 0U; staging < viewer->world.staging_area_count && target_count < 24U; staging++) {
        targets[target_count++] = viewer->world.staging_areas[staging].access_node_id;
    }
    for (uint8_t digit = 0U; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        const building_clock_digit_work_area_t* area = &viewer->world.digit_work_areas[digit];
        for (uint8_t access = 0U; access < area->access_count && target_count < 24U; access++) {
            targets[target_count++] = area->access[access].nav_node_id;
        }
    }
    return target_count;
}

static void add_demo_jobs(viewer_t* viewer, uint8_t desired_count) {
    uint8_t targets[24];
    const uint8_t target_count = collect_demo_targets(viewer, targets);
    uint8_t attempts = 0U;
    while (viewer->simulation.state.used_job_slot_count < desired_count && target_count > 0U && attempts < 24U) {
        const building_clock_job_kind_t kind =
            viewer->job_serial % 3U == 0U ? BUILDING_CLOCK_JOB_PATROL : (viewer->job_serial % 3U == 1U ? BUILDING_CLOCK_JOB_INSPECT : BUILDING_CLOCK_JOB_SECURE_SECTION);
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
    add_demo_jobs(viewer, 12U);
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
    viewer->next_job_refresh_ms = 1000U;
    if (viewer->scenario == SCENARIO_STABLE) {
        add_demo_jobs(viewer, 18U);
    }
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
        if (viewer->simulation.state.elapsed_ms >= viewer->next_job_refresh_ms) {
            refresh_demo_jobs(viewer);
            viewer->next_job_refresh_ms = viewer->simulation.state.elapsed_ms + 1000U;
        }
    }
}

static void save_screenshot(SDL_Surface* surface, const char* path) {
    if (SDL_SaveBMP(surface, path) != 0) {
        fprintf(stderr, "Unable to save %s: %s\n", path, SDL_GetError());
    } else {
        printf("Saved %s\n", path);
    }
}

static void print_controls(void) {
    puts("Controls: SPACE pause | . step 50ms | R replay | N next seed | 0 stable | 1 14:37->14:38 | 2 14:59->15:00 | 3 23:59->00:00");
    puts("          V navigation | I node IDs | L locations | H hazards | W worker routes | D digit areas | S screenshot | +/- speed | ESC quit");
}

static void handle_key(viewer_t* viewer, SDL_Surface* surface, SDL_Keycode key) {
    switch (key) {
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
        viewer->scenario = key == SDLK_0 ? SCENARIO_STABLE : (key == SDLK_1 ? SCENARIO_SINGLE : (key == SDLK_2 ? SCENARIO_ROLLOVER : SCENARIO_MIDNIGHT));
        (void)reset_viewer(viewer, false);
        break;
    case SDLK_v:
        viewer->overlays.navigation = !viewer->overlays.navigation;
        break;
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
        save_screenshot(surface, "building_clock_view.bmp");
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

int main(int argc, char** argv) {
    viewer_t viewer = {
        .root_seed = DEFAULT_SEED,
        .generator_version = BUILDING_CLOCK_GENERATOR_VERSION,
        .scenario = SCENARIO_SINGLE,
        .paused = false,
        .running = true,
        .speed = 1U,
        .overlays = {.hazards = true},
    };
    const char* screenshot_path = NULL;
    uint32_t duration_ms = 0U;
    uint32_t diagnostic_count = 0U;
    for (int argument = 1; argument < argc; argument++) {
        if (strcmp(argv[argument], "--seed") == 0 && argument + 1 < argc) {
            viewer.root_seed = strtoull(argv[++argument], NULL, 0);
        } else if (strcmp(argv[argument], "--scenario") == 0 && argument + 1 < argc) {
            viewer.scenario = parse_scenario(argv[++argument]);
        } else if (strcmp(argv[argument], "--screenshot") == 0 && argument + 1 < argc) {
            screenshot_path = argv[++argument];
        } else if (strcmp(argv[argument], "--duration-ms") == 0 && argument + 1 < argc) {
            duration_ms = (uint32_t)strtoul(argv[++argument], NULL, 0);
        } else if (strcmp(argv[argument], "--diagnose") == 0 && argument + 1 < argc) {
            diagnostic_count = (uint32_t)strtoul(argv[++argument], NULL, 0);
        } else if (strcmp(argv[argument], "--all-overlays") == 0) {
            viewer.overlays = (viewer_overlays_t){true, true, true, true, true, true};
        } else {
            fprintf(stderr, "Usage: %s [--seed N] [--scenario stable|single|rollover|midnight] [--duration-ms N] [--screenshot FILE] [--all-overlays] [--diagnose N]\n", argv[0]);
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
