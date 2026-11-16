#include "building_clock_upkeep.h"

#include <stddef.h>
#include <stdlib.h>

#define WORK_SPACING 23
#define PICKUP_MS 650U
#define WORK_MS 3400U
#define MEETING_MS 4200U
#define MEETING_PATIENCE_MS 4000U

static bool on_platform(const building_clock_platform_t* platform, building_clock_point_t point) {
    return point.y == platform->surface_y && point.x >= platform->x_start && point.x <= platform->x_end;
}

static bool clear_ladders(const building_clock_world_t* world, building_clock_point_t point) {
    for (uint8_t id = 0U; id < world->ladder_count; id++) {
        const building_clock_ladder_t* ladder = &world->ladders[id];
        if ((point.y == ladder->top_y || point.y == ladder->bottom_y) && abs(point.x - ladder->x) < 14) {
            return false;
        }
    }
    return true;
}

static bool available(const building_clock_worker_t* worker) {
    return worker->current_job_id == BUILDING_CLOCK_INVALID_ID && worker->route_purpose == BUILDING_CLOCK_WORKER_ROUTE_NONE && worker->rope.phase == BUILDING_CLOCK_ROPE_NONE &&
           worker->carried_load_id == BUILDING_CLOCK_INVALID_ID && worker->carried_debris_id == BUILDING_CLOCK_INVALID_ID && worker->site_activity == BUILDING_CLOCK_UPKEEP_NONE &&
           (worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_NONE || worker->platform_activity_phase == BUILDING_CLOCK_PLATFORM_REST);
}

static bool clear_position(const building_clock_simulation_t* simulation, building_clock_point_t point, uint8_t self, uint8_t partner) {
    for (uint8_t id = 0U; id < simulation->state.worker_count; id++) {
        if (id == self || id == partner) {
            continue;
        }
        const building_clock_worker_t* other = &simulation->state.workers[id];
        const building_clock_point_t target = other->site_activity != BUILDING_CLOCK_UPKEEP_NONE ? other->platform_activity_position : other->position;
        if (target.y == point.y && abs(target.x - point.x) < WORK_SPACING) {
            return false;
        }
    }
    return true;
}

void building_clock_upkeep_init(building_clock_simulation_t* simulation, const building_clock_world_t* world) {
    for (uint8_t id = 0U; id < world->platform_count; id++) {
        const building_clock_platform_t* platform = &world->platforms[id];
        building_clock_upkeep_station_t* station = &simulation->state.upkeep[id];
        station->owner_worker_id = BUILDING_CLOCK_INVALID_ID;
        if (platform->x_end - platform->x_start < 82) {
            continue;
        }
        /* Keep the bench away from ladder mouths and leave room either side
         * for a colleague. This does not add geometry or navigation nodes. */
        for (int16_t x = (int16_t)(platform->x_start + 26); x <= platform->x_end - 26; x = (int16_t)(x + 9)) {
            const building_clock_point_t point = {x, platform->surface_y};
            if (clear_ladders(world, point) && clear_ladders(world, (building_clock_point_t){(int16_t)(x + 24), point.y})) {
                station->home = point;
                station->tool_position = point;
                station->enabled = true;
                station->dust_mask = (uint8_t)(1U << (id % 3U));
                station->loose_joint = 1U;
                station->wear_elapsed_ms = (uint16_t)(id * 1201U);
                break;
            }
        }
    }
}

void building_clock_upkeep_tick(building_clock_simulation_t* simulation, uint32_t elapsed_ms) {
    if (!simulation->state.clock_initialized || building_clock_simulation_transition_active(simulation)) {
        return;
    }
    for (uint8_t id = 0U; id < BUILDING_CLOCK_MAX_PLATFORM_SPANS; id++) {
        building_clock_upkeep_station_t* station = &simulation->state.upkeep[id];
        if (!station->enabled) {
            continue;
        }
        station->wear_elapsed_ms = (uint16_t)(station->wear_elapsed_ms + elapsed_ms);
        if (station->wear_elapsed_ms >= 30000U) {
            station->wear_elapsed_ms = 0U;
            station->dust_mask |= (uint8_t)(1U << (station->rounds_completed % 3U));
            station->loose_joint = 1U;
        }
    }
}

static void start_worker(building_clock_worker_t* worker, uint8_t station, uint8_t kind, uint8_t partner, building_clock_point_t target, bool fetch) {
    worker->site_activity = kind;
    worker->site_phase = fetch ? BUILDING_CLOCK_UPKEEP_FETCH : BUILDING_CLOCK_UPKEEP_APPROACH;
    worker->site_station_id = station;
    worker->site_partner_id = partner;
    worker->site_elapsed_ms = 0U;
    worker->platform_activity_phase = BUILDING_CLOCK_PLATFORM_UPKEEP;
    worker->platform_activity_position = target;
    worker->platform_motion_remainder_ms = 0U;
    worker->state = BUILDING_CLOCK_WORKER_WALK;
}

bool building_clock_upkeep_start(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    if (!simulation->state.clock_initialized || building_clock_simulation_transition_active(simulation) || !available(worker) || worker->behavior_timer_ms < 1800U + worker_id * 239U ||
        worker->current_node_id >= world->navigation.node_count || building_clock_navigation_node_capacity(world, worker->current_node_id) < BUILDING_CLOCK_MAX_WORKERS) {
        return false;
    }
    for (uint8_t id = 0U; id < world->platform_count; id++) {
        building_clock_upkeep_station_t* station = &simulation->state.upkeep[id];
        const building_clock_platform_t* platform = &world->platforms[id];
        if (!station->enabled || !on_platform(platform, worker->position)) {
            continue;
        }
        const uint8_t choice = (uint8_t)(station->rounds_completed % 7U);
        /* Most rounds are useful. A short shared plan review or tea break is
         * an occasional destination, not the fallback pose of the whole crew. */
        uint8_t kind = choice == 3U ? BUILDING_CLOCK_UPKEEP_PLAN : choice == 6U ? BUILDING_CLOCK_UPKEEP_TEA : BUILDING_CLOCK_UPKEEP_CHECK_TOOLS;
        if (choice < 3U) {
            kind = station->dust_mask != 0U ? BUILDING_CLOCK_UPKEEP_SWEEP : station->loose_joint != 0U ? BUILDING_CLOCK_UPKEEP_TIGHTEN : BUILDING_CLOCK_UPKEEP_CHECK_TOOLS;
        }
        /* Colleagues need not queue for the shared kit. A pocket clipboard
         * lets them check another joint while the fitter/sweeper is working. */
        if (station->owner_worker_id != BUILDING_CLOCK_INVALID_ID) {
            kind = BUILDING_CLOCK_UPKEEP_SURVEY;
        }
        uint8_t partner = BUILDING_CLOCK_INVALID_ID;
        building_clock_point_t work = station->home;
        if (kind == BUILDING_CLOCK_UPKEEP_SWEEP) {
            bool found_patch = false;
            for (unsigned patch = 0U; patch < 3U; patch++) {
                const building_clock_point_t point = {(int16_t)(platform->x_end - 34 - (int)patch * 13), platform->surface_y};
                if ((station->dust_mask & (1U << patch)) != 0U && clear_ladders(world, point) && clear_position(simulation, point, worker_id, BUILDING_CLOCK_INVALID_ID)) {
                    work = point;
                    found_patch = true;
                    break;
                }
            }
            if (!found_patch) {
                kind = BUILDING_CLOCK_UPKEEP_SURVEY;
            }
        }
        if (kind == BUILDING_CLOCK_UPKEEP_PLAN || kind == BUILDING_CLOCK_UPKEEP_TEA) {
            for (uint8_t other = 0U; other < simulation->state.worker_count; other++) {
                const building_clock_worker_t* colleague = &simulation->state.workers[other];
                if (other != worker_id && available(colleague) && on_platform(platform, colleague->position) && abs(colleague->position.x - work.x) <= 100 &&
                    building_clock_navigation_node_capacity(world, colleague->current_node_id) >= BUILDING_CLOCK_MAX_WORKERS && colleague->behavior_timer_ms >= 1000U) {
                    partner = other;
                    break;
                }
            }
            if (partner == BUILDING_CLOCK_INVALID_ID && kind == BUILDING_CLOCK_UPKEEP_PLAN) {
                kind = BUILDING_CLOCK_UPKEEP_CHECK_TOOLS;
            }
        }
        if (kind == BUILDING_CLOCK_UPKEEP_TIGHTEN || kind == BUILDING_CLOCK_UPKEEP_SURVEY) {
            /* Work at a real girder joint/dust patch, separated from the kit.
             * Reserve the eventual work position before fetching equipment. */
            work.x = (int16_t)(platform->x_end - 23 - (int)(station->rounds_completed % 3U) * 13);
        }
        int best_distance = kind == BUILDING_CLOCK_UPKEEP_SWEEP ? 0 : INT32_MAX;
        building_clock_point_t best = work;
        const int right_margin = partner < simulation->state.worker_count ? 42 : 18;
        for (int16_t x = (int16_t)(platform->x_start + 18); x <= platform->x_end - right_margin; x = (int16_t)(x + 7)) {
            const building_clock_point_t candidate = {x, platform->surface_y};
            const building_clock_point_t other = {(int16_t)(x + 24), candidate.y};
            if (clear_ladders(world, candidate) && clear_position(simulation, candidate, worker_id, partner) &&
                (partner == BUILDING_CLOCK_INVALID_ID || (clear_ladders(world, other) && clear_position(simulation, other, worker_id, partner))) && abs(x - work.x) < best_distance) {
                best_distance = abs(x - work.x);
                best = candidate;
            }
        }
        if (best_distance == INT32_MAX) {
            continue;
        }
        work = best;
        if (kind == BUILDING_CLOCK_UPKEEP_TEA && abs(work.x - station->home.x) > 12) {
            kind = partner == BUILDING_CLOCK_INVALID_ID ? BUILDING_CLOCK_UPKEEP_CHECK_TOOLS : BUILDING_CLOCK_UPKEEP_PLAN;
        }
        if (kind != BUILDING_CLOCK_UPKEEP_SURVEY) {
            station->owner_worker_id = worker_id;
        }
        const bool fetch = kind == BUILDING_CLOCK_UPKEEP_SWEEP || kind == BUILDING_CLOCK_UPKEEP_TIGHTEN || kind == BUILDING_CLOCK_UPKEEP_CHECK_TOOLS;
        start_worker(worker, id, kind, partner, work, fetch);
        if (partner != BUILDING_CLOCK_INVALID_ID) {
            const building_clock_point_t other_position = {(int16_t)(work.x + 24), work.y};
            start_worker(&simulation->state.workers[partner], id, kind, worker_id, other_position, false);
        }
        return true;
    }
    return false;
}

static void finish_worker(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    const building_clock_platform_t* platform = &world->platforms[worker->site_station_id];
    /* Rejoin the nearest existing broad graph anchor on this continuous
     * platform, not the far end where the round began. Reserve first, then
     * release the old anchor. RETURN still walks every remaining unit. */
    uint8_t best = worker->current_node_id;
    int distance = abs(worker->position.x - world->navigation.nodes[best].position.x);
    for (uint8_t node = 0U; node < world->navigation.node_count; node++) {
        const building_clock_point_t point = world->navigation.nodes[node].position;
        if (on_platform(platform, point) && building_clock_navigation_node_capacity(world, node) >= BUILDING_CLOCK_MAX_WORKERS && abs(point.x - worker->position.x) < distance) {
            best = node;
            distance = abs(point.x - worker->position.x);
        }
    }
    if (best != worker->current_node_id) {
        const building_clock_reservation_status_t reserved = building_clock_navigation_reserve_node(world, &simulation->navigation, best, worker_id);
        if (reserved == BUILDING_CLOCK_RESERVATION_ACQUIRED || reserved == BUILDING_CLOCK_RESERVATION_ALREADY_HELD) {
            (void)building_clock_navigation_release_node(world, &simulation->navigation, worker->current_node_id, worker_id);
            worker->current_node_id = best;
        }
    }
    worker->site_activity = BUILDING_CLOCK_UPKEEP_NONE;
    worker->site_partner_id = BUILDING_CLOCK_INVALID_ID;
    worker->site_station_id = BUILDING_CLOCK_INVALID_ID;
    worker->site_elapsed_ms = 0U;
    worker->behavior_timer_ms = 0U;
    worker->platform_activity_phase = BUILDING_CLOCK_PLATFORM_RETURN;
    worker->platform_motion_remainder_ms = 0U;
    worker->state = BUILDING_CLOCK_WORKER_WALK;
}

void building_clock_upkeep_interrupt(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    if (worker->site_activity == BUILDING_CLOCK_UPKEEP_NONE) {
        return;
    }
    building_clock_upkeep_station_t* station = &simulation->state.upkeep[worker->site_station_id];
    const uint8_t partner = worker->site_partner_id;
    /* If carried, the tool position was updated alongside the worker. Releasing
     * ownership puts it down here, not magically back at its home bench. */
    if (station->owner_worker_id == worker_id || station->owner_worker_id == partner) {
        station->owner_worker_id = BUILDING_CLOCK_INVALID_ID;
    }
    finish_worker(simulation, world, worker_id);
    if (partner < simulation->state.worker_count && simulation->state.workers[partner].site_partner_id == worker_id) {
        finish_worker(simulation, world, partner);
    }
}

static bool walk_to(building_clock_worker_t* worker, building_clock_point_t point, uint32_t elapsed_ms) {
    const uint32_t motion = worker->platform_motion_remainder_ms + elapsed_ms;
    const int distance = abs(point.x - worker->position.x);
    int step = (int)(motion / BUILDING_CLOCK_WALK_MS_PER_UNIT);
    worker->platform_motion_remainder_ms = (uint8_t)(motion % BUILDING_CLOCK_WALK_MS_PER_UNIT);
    if (step > distance) {
        step = distance;
    }
    worker->facing = point.x < worker->position.x ? BUILDING_CLOCK_FACING_LEFT : BUILDING_CLOCK_FACING_RIGHT;
    worker->position.x = (int16_t)(worker->position.x + (point.x < worker->position.x ? -step : step));
    worker->state = BUILDING_CLOCK_WORKER_WALK;
    return worker->position.x == point.x;
}

bool building_clock_upkeep_advance(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id, uint32_t elapsed_ms) {
    building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    if (worker->site_activity == BUILDING_CLOCK_UPKEEP_NONE) {
        return false;
    }
    building_clock_upkeep_station_t* station = &simulation->state.upkeep[worker->site_station_id];
    worker->site_elapsed_ms = (uint16_t)(worker->site_elapsed_ms + elapsed_ms);
    switch (worker->site_phase) {
    case BUILDING_CLOCK_UPKEEP_FETCH:
        if (walk_to(worker, station->tool_position, elapsed_ms)) {
            worker->site_phase = BUILDING_CLOCK_UPKEEP_PICK_UP;
            worker->site_elapsed_ms = 0U;
        }
        break;
    case BUILDING_CLOCK_UPKEEP_PICK_UP:
        worker->state = BUILDING_CLOCK_WORKER_INSPECT;
        if (worker->site_elapsed_ms >= PICKUP_MS) {
            worker->site_phase = BUILDING_CLOCK_UPKEEP_APPROACH;
            worker->site_elapsed_ms = 0U;
        }
        break;
    case BUILDING_CLOCK_UPKEEP_APPROACH:
        if (walk_to(worker, worker->platform_activity_position, elapsed_ms)) {
            worker->site_phase = BUILDING_CLOCK_UPKEEP_WORK;
            worker->site_elapsed_ms = 0U;
        }
        break;
    case BUILDING_CLOCK_UPKEEP_WORK: {
        worker->state = worker->site_activity == BUILDING_CLOCK_UPKEEP_SWEEP     ? BUILDING_CLOCK_WORKER_CLEAR_DEBRIS
                        : worker->site_activity == BUILDING_CLOCK_UPKEEP_TIGHTEN ? BUILDING_CLOCK_WORKER_HAMMER
                        : worker->site_activity == BUILDING_CLOCK_UPKEEP_TEA     ? BUILDING_CLOCK_WORKER_DRINK
                                                                                 : BUILDING_CLOCK_WORKER_INSPECT;
        if (worker->site_activity == BUILDING_CLOCK_UPKEEP_SWEEP) {
            worker->facing = BUILDING_CLOCK_FACING_RIGHT;
        }
        worker->action_phase = (uint8_t)((worker->site_elapsed_ms / 180U) % 8U);
        if (worker->site_partner_id < simulation->state.worker_count) {
            building_clock_worker_t* partner = &simulation->state.workers[worker->site_partner_id];
            worker->facing = partner->position.x < worker->position.x ? BUILDING_CLOCK_FACING_LEFT : BUILDING_CLOCK_FACING_RIGHT;
            if (partner->site_phase != BUILDING_CLOCK_UPKEEP_WORK) {
                worker->state = BUILDING_CLOCK_WORKER_WATCH;
                if (worker->site_elapsed_ms > MEETING_PATIENCE_MS) {
                    building_clock_upkeep_interrupt(simulation, world, worker_id);
                }
                break;
            }
            /* Both start the conversation when the second arrives, not while
             * the first is standing alone. Only the station owner times it. */
            if (station->owner_worker_id != worker_id) {
                break;
            }
            const uint16_t together_ms = worker->site_elapsed_ms < partner->site_elapsed_ms ? worker->site_elapsed_ms : partner->site_elapsed_ms;
            if (together_ms < MEETING_MS) {
                break;
            }
            station->rounds_completed++;
            building_clock_upkeep_interrupt(simulation, world, worker_id);
            break;
        }
        if (worker->site_elapsed_ms >= WORK_MS) {
            if (worker->site_activity == BUILDING_CLOCK_UPKEEP_SWEEP) {
                const int patch = (world->platforms[worker->site_station_id].x_end - 34 - worker->position.x) / 13;
                if (patch >= 0 && patch < 3) {
                    station->dust_mask &= (uint8_t)~(1U << (unsigned)patch);
                }
            } else if (worker->site_activity == BUILDING_CLOCK_UPKEEP_TIGHTEN) {
                station->loose_joint = 0U;
            }
            if (worker->site_activity == BUILDING_CLOCK_UPKEEP_TEA || worker->site_activity == BUILDING_CLOCK_UPKEEP_SURVEY) {
                station->rounds_completed++;
                building_clock_upkeep_interrupt(simulation, world, worker_id);
            } else {
                worker->site_phase = BUILDING_CLOCK_UPKEEP_RETURN_TOOL;
                worker->site_elapsed_ms = 0U;
            }
        }
        break;
    }
    case BUILDING_CLOCK_UPKEEP_RETURN_TOOL:
        if (walk_to(worker, station->home, elapsed_ms)) {
            worker->site_phase = BUILDING_CLOCK_UPKEEP_PUT_DOWN;
            worker->site_elapsed_ms = 0U;
        }
        break;
    case BUILDING_CLOCK_UPKEEP_PUT_DOWN:
        worker->state = BUILDING_CLOCK_WORKER_INSPECT;
        if (worker->site_elapsed_ms >= PICKUP_MS) {
            station->tool_position = station->home;
            station->rounds_completed++;
            building_clock_upkeep_interrupt(simulation, world, worker_id);
        }
        break;
    default:
        building_clock_upkeep_interrupt(simulation, world, worker_id);
        break;
    }
    if (worker->site_activity >= BUILDING_CLOCK_UPKEEP_SWEEP && worker->site_activity <= BUILDING_CLOCK_UPKEEP_CHECK_TOOLS && worker->site_phase >= BUILDING_CLOCK_UPKEEP_APPROACH) {
        station->tool_position = worker->position;
    }
    return true;
}
