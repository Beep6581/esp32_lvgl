#include "building_clock_generator.h"
#include "building_clock_geometry.h"
#include "building_clock_simulation.h"
#include "building_clock_world_selection.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Host-only, read-only sampling. No diagnostic hooks enter the shared runtime.
 * Post-step gates describe the sampled state, not a hidden scheduler event.
 * Tests step an unsampled replay alongside it to detect accidental mutation. */
static const char* const states[] = {"idle",   "choose", "walk",  "climb",    "ride",   "carry",     "push",  "wait",  "turn", "inspect",
                                     "hammer", "tend",   "clear", "evacuate", "finish", "celebrate", "smoke", "drink", "rest", "watch"};
static const char* const jobs[] = {"supply",         "carry", "cart",  "crane",  "lift",    "install_ladder", "install_platform", "remove_temp",
                                   "remove_support", "clear", "place", "secure", "inspect", "patrol",         "celebrate",        "belay"};
static const char* const sections[] = {"foundation", "top", "middle", "upper_left", "upper_right", "lower_left", "lower_right", "corner", "infill"};
static const char* const phases[] = {"stable", "preparing", "evacuating", "collapsing", "settling", "clearing", "foundation", "structure", "recognizable", "finishing", "complete"};

enum { ACTION, TRAVEL, ROPE_TRAVEL, WAIT_EDGE, WAIT_NODE, WAIT_TENDER, WAIT_ROPE, RETRY, NO_PENDING, BUILDER_LIMIT, DISPATCH_LIMIT, NO_ROUTE, HAZARD, DISPATCH_CHOICE, REASON_COUNT };
static const char* const reasons[] = {"action",         "travel",        "rope_travel",    "edge_busy", "node_busy", "tender_not_ready", "rope_lane_busy", "retry_next_quantum",
                                      "no_pending_job", "builder_limit", "dispatch_limit", "no_route",  "hazard",    "dispatch_choice"};

typedef struct {
    uint32_t ready, assigned, build_assigned, supplied, first_brick, placed, secured;
    uint8_t kind;
} section_times_t;

typedef struct {
    unsigned reason;
    uint16_t resource, owners;
    unsigned pending, reachable, builder_blocked, dispatch_blocked, unreachable, unsafe;
} worker_probe_t;

static void require(bool condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static unsigned bit_count(uint16_t mask) {
    unsigned count = 0U;
    while (mask != 0U) {
        count += mask & 1U;
        mask >>= 1U;
    }
    return count;
}

static bool dependencies_ready(const building_clock_digit_structure_t* structure, const building_clock_section_state_t* section) {
    for (uint8_t id = 0U; id < structure->section_count; id++) {
        if ((section->dependency_mask & (uint16_t)(1U << id)) != 0U && structure->sections[id].status != BUILDING_CLOCK_SECTION_SECURED) {
            return false;
        }
    }
    return true;
}

static bool route_unsafe(const building_clock_simulation_t* simulation, const building_clock_world_t* world, const building_clock_route_t* route) {
    for (uint8_t h = 0U; h < simulation->state.hazard_count; h++) {
        const building_clock_rect_t area = simulation->state.hazards[h].area;
        for (uint8_t i = 0U; i < route->node_count; i++) {
            const building_clock_point_t a = world->navigation.nodes[route->node_ids[i]].position;
            const building_clock_point_t b = i + 1U < route->node_count ? world->navigation.nodes[route->node_ids[i + 1U]].position : a;
            const int low_x = a.x < b.x ? a.x : b.x, high_x = a.x > b.x ? a.x : b.x;
            const int low_y = a.y < b.y ? a.y : b.y, high_y = a.y > b.y ? a.y : b.y;
            if (low_x < area.x + area.width && high_x >= area.x && low_y < area.y + area.height && high_y >= area.y) {
                return true;
            }
        }
    }
    return false;
}

static worker_probe_t probe_worker(const building_clock_world_t* world, const building_clock_simulation_t* simulation, uint8_t worker_id) {
    const building_clock_worker_t* worker = &simulation->state.workers[worker_id];
    worker_probe_t probe = {.reason = ACTION, .resource = BUILDING_CLOCK_INVALID_EDGE_ID};
    if (worker->state == BUILDING_CLOCK_WORKER_WAIT) {
        if (worker->rope.phase != BUILDING_CLOCK_ROPE_NONE) {
            probe.reason = WAIT_ROPE;
            probe.resource = worker->rope.belay_job_id;
            if (worker->rope.belay_job_id < BUILDING_CLOCK_MAX_JOBS) {
                const uint8_t tender = simulation->state.jobs[worker->rope.belay_job_id].reserved_worker_id;
                if (tender >= simulation->state.worker_count || simulation->state.workers[tender].state != BUILDING_CLOCK_WORKER_OPERATE_MACHINERY) {
                    probe.reason = WAIT_TENDER;
                }
            }
            if (probe.reason == WAIT_ROPE && worker->current_job_id < BUILDING_CLOCK_MAX_JOBS) {
                const building_clock_job_t* job = &simulation->state.jobs[worker->current_job_id];
                const building_clock_rect_t envelope = world->digit_work_areas[job->subject.digit_id].envelope;
                const int origin = envelope.x + ((int)envelope.width - 69) / 2;
                const int strip = (worker->rope.work_position.x - origin) / 30;
                int x = origin + strip * 30 + (strip == 2 ? 4 : 14);
                const building_clock_platform_t* roof = &world->platforms[worker->rope.platform_id];
                if (x < roof->x_start + 2)
                    x = roof->x_start + 2;
                if (x > roof->x_end - 2)
                    x = roof->x_end - 2;
                for (uint8_t other = 0U; other < simulation->state.worker_count; other++) {
                    const building_clock_worker_t* adjacent = &simulation->state.workers[other];
                    if (other != worker_id && adjacent->rope.phase != BUILDING_CLOCK_ROPE_NONE &&
                        (adjacent->position.y > adjacent->rope.anchor.y || adjacent->rope.phase == BUILDING_CLOCK_ROPE_DESCEND) && abs(adjacent->rope.anchor.x - x) < 9) {
                        probe.owners |= (uint16_t)(1U << other);
                    }
                }
                probe.resource = (uint16_t)x;
                if (probe.owners == 0U)
                    probe.reason = RETRY;
            }
        } else if (worker->route_node_index < worker->route_edge_count) {
            const uint16_t edge = worker->route_edge_ids[worker->route_node_index];
            const uint8_t node = worker->route_node_ids[worker->route_node_index + 1U];
            probe.resource = edge;
            probe.owners = building_clock_navigation_edge_owner_mask(world, &simulation->navigation, edge);
            probe.reason = WAIT_EDGE;
            const building_clock_nav_edge_t* connection = &world->navigation.edges[edge];
            bool ladder_blocked = false;
            if (connection->kind == BUILDING_CLOCK_NAV_EDGE_CLIMB && connection->capacity > 1U) {
                const building_clock_point_t start = world->navigation.nodes[worker->route_node_ids[worker->route_node_index]].position;
                for (uint8_t other = 0U; other < simulation->state.worker_count; other++) {
                    const building_clock_worker_t* leader = &simulation->state.workers[other];
                    if (other != worker_id && (probe.owners & (uint16_t)(1U << other)) != 0U &&
                        (leader->route_node_ids[leader->route_node_index] != worker->route_node_ids[worker->route_node_index] ||
                         abs(leader->position.y - start.y) < (int)BUILDING_CLOCK_LADDER_SPACING)) {
                        ladder_blocked = true;
                    }
                }
            }
            if (!ladder_blocked && bit_count(probe.owners) < connection->capacity) {
                probe.resource = node;
                probe.owners = building_clock_navigation_node_owner_mask(world, &simulation->navigation, node);
                probe.reason = bit_count(probe.owners) >= building_clock_navigation_node_capacity(world, node) ? WAIT_NODE : RETRY;
            }
        } else {
            probe.reason = RETRY;
        }
        return probe;
    }
    if (worker->rope.phase != BUILDING_CLOCK_ROPE_NONE) {
        probe.reason = worker->rope.phase == BUILDING_CLOCK_ROPE_WORK ? ACTION : ROPE_TRAVEL;
        return probe;
    }
    if (worker->current_job_id < BUILDING_CLOCK_MAX_JOBS || worker->route_purpose != BUILDING_CLOCK_WORKER_ROUTE_NONE) {
        probe.reason = worker->route_node_index < worker->route_edge_count ? TRAVEL : ACTION;
        return probe;
    }
    /* Mirror only the hard dispatch gates. A surviving candidate means
     * dispatch preference/order must be inspected, not proven unreachable. */
    unsigned builders = 0U;
    for (uint8_t j = 0U; j < BUILDING_CLOCK_MAX_JOBS; j++) {
        const building_clock_job_t* job = &simulation->state.jobs[j];
        builders += job->status == BUILDING_CLOCK_JOB_ACTIVE && (job->kind == BUILDING_CLOCK_JOB_PLACE_SECTION || job->kind == BUILDING_CLOCK_JOB_SECURE_SECTION) ? 1U : 0U;
    }
    for (uint8_t j = 0U; j < BUILDING_CLOCK_MAX_JOBS; j++) {
        const building_clock_job_t* job = &simulation->state.jobs[j];
        if (job->status != BUILDING_CLOCK_JOB_PENDING || job->earliest_start_elapsed_ms > simulation->state.elapsed_ms ||
            (worker->capabilities & job->required_worker_capabilities) != job->required_worker_capabilities) {
            continue;
        }
        probe.pending++;
        if ((job->kind == BUILDING_CLOCK_JOB_PLACE_SECTION || job->kind == BUILDING_CLOCK_JOB_SECURE_SECTION) && job->subject.kind == BUILDING_CLOCK_JOB_SUBJECT_DIGIT_SECTION &&
            builders >= (simulation->state.worker_count > 1U ? simulation->state.worker_count / 2U : 1U)) {
            probe.builder_blocked++;
            continue;
        }
        unsigned here = 0U;
        for (uint8_t other = 0U; other < BUILDING_CLOCK_MAX_JOBS; other++) {
            const building_clock_job_t* active = &simulation->state.jobs[other];
            here += active->status == BUILDING_CLOCK_JOB_ACTIVE && active->target_node_id == job->target_node_id &&
                            (job->kind != BUILDING_CLOCK_JOB_STAGE_MATERIAL || active->kind == BUILDING_CLOCK_JOB_STAGE_MATERIAL)
                        ? 1U
                        : 0U;
        }
        const unsigned capacity = job->kind == BUILDING_CLOCK_JOB_STAGE_MATERIAL ? 2U : building_clock_navigation_node_capacity(world, job->target_node_id);
        if (here >= capacity) {
            probe.dispatch_blocked++;
            continue;
        }
        const building_clock_route_request_t request = {.start_node_id = worker->current_node_id,
                                                        .destination_node_id =
                                                            job->source_node_id != BUILDING_CLOCK_INVALID_ID && !job->source_action_complete ? job->source_node_id : job->target_node_id,
                                                        .allow_temporary_connections = true};
        building_clock_route_t route;
        if (building_clock_find_route(world, &simulation->navigation, &request, &route) != BUILDING_CLOCK_ROUTE_OK) {
            probe.unreachable++;
        } else if (route_unsafe(simulation, world, &route)) {
            probe.unsafe++;
        } else {
            probe.reachable++;
        }
    }
    probe.reason =
        probe.pending == 0U
            ? NO_PENDING
            : (probe.reachable > 0U ? DISPATCH_CHOICE : (probe.builder_blocked > 0U ? BUILDER_LIMIT : (probe.dispatch_blocked > 0U ? DISPATCH_LIMIT : (probe.unsafe > 0U ? HAZARD : NO_ROUTE))));
    return probe;
}

static FILE* open_csv(const char* prefix, const char* suffix, const char* header) {
    if (prefix == NULL) {
        return NULL;
    }
    char path[512];
    const int length = snprintf(path, sizeof(path), "%s-%s.csv", prefix, suffix);
    require(length >= 0 && (size_t)length < sizeof(path), "output path fits");
    FILE* file = fopen(path, "w");
    require(file != NULL, "open CSV output");
    fprintf(file, "%s\n", header);
    return file;
}

static void describe_world(const building_clock_world_t* world) {
    for (uint8_t id = 0U; id < world->ladder_count; id++) {
        const building_clock_ladder_t* ladder = &world->ladders[id];
        printf("LADDER %u x=%d y=%d..%d platforms=%u/%u\n", id, ladder->x, ladder->top_y, ladder->bottom_y, ladder->top_platform_id, ladder->bottom_platform_id);
        for (uint8_t other = 0U; other < id; other++) {
            const building_clock_ladder_t* b = &world->ladders[other];
            const int dx = abs(ladder->x - b->x);
            if (dx <= 9 && ladder->top_y <= b->bottom_y && b->top_y <= ladder->bottom_y) {
                printf("LADDER_CONTACT %u/%u dx=%d (viewer rails are x-4..x+4)\n", other, id, dx);
            }
        }
    }
    building_clock_navigation_runtime_t permanent;
    building_clock_navigation_runtime_init(&permanent);
    for (uint8_t p = 0U; p < world->platform_count; p++) {
        const building_clock_platform_t* platform = &world->platforms[p];
        bool reachable = false;
        for (uint8_t node = 0U; node < world->navigation.node_count; node++) {
            const building_clock_nav_node_t* n = &world->navigation.nodes[node];
            if (n->source_kind != BUILDING_CLOCK_NAV_SOURCE_PLATFORM || n->source_id != p) {
                continue;
            }
            const building_clock_route_request_t request = {.start_node_id = world->worker_spawns[0].nav_node_id, .destination_node_id = node};
            building_clock_route_t route;
            reachable |= building_clock_find_route(world, &permanent, &request, &route) == BUILDING_CLOCK_ROUTE_OK;
        }
        printf("PLATFORM %u x=%d..%d y=%d permanent_worker_route=%u\n", p, platform->x_start, platform->x_end, platform->surface_y, reachable);
    }
    for (uint16_t id = 0U; id < world->navigation.edge_count; id++) {
        const building_clock_nav_edge_t* edge = &world->navigation.edges[id];
        if (edge->kind == BUILDING_CLOCK_NAV_EDGE_CLIMB) {
            printf("CLIMB edge=%u ladder=%u nodes=%u/%u cost=%u capacity=%u nominal_ms=%u\n", id, edge->source_id, edge->from_node_id, edge->to_node_id, edge->traversal_cost, edge->capacity,
                   (unsigned)edge->traversal_cost * 20U);
        }
    }
}

int main(int argc, char** argv) {
    const bool rollover = argc > 1 && strcmp(argv[1], "rollover") == 0;
    require(argc <= 4 && (argc < 2 || rollover || strcmp(argv[1], "single") == 0), "usage: trace single|rollover [output-prefix [root-seed]]");
    const uint64_t seed = argc > 3 ? strtoull(argv[3], NULL, 0) : UINT64_C(0x6a09e667f3bcc909);
    const char* prefix = argc > 2 && strcmp(argv[2], "-") != 0 ? argv[2] : NULL;
    building_clock_world_t world;
    const building_clock_world_selection_result_t selection = building_clock_select_world(&world, seed, BUILDING_CLOCK_GENERATOR_VERSION);
    require(selection.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED || selection.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED_FALLBACK, "world selection");
    const building_clock_world_t original_world = world;
    building_clock_simulation_t simulation;
    require(building_clock_simulation_init(&simulation, &world, 6U) == BUILDING_CLOCK_SIMULATION_OK, "simulation initialization");
    require(building_clock_simulation_set_time(&simulation, &world, 14U, rollover ? 59U : 37U) == BUILDING_CLOCK_SIMULATION_OK, "initial time");
    simulation.state.date = (building_clock_date_t){.year = 1984U, .month = 10U, .day = 26U, .weekday = 5U};
    require(building_clock_simulation_set_time(&simulation, &world, rollover ? 15U : 14U, rollover ? 0U : 38U) == BUILDING_CLOCK_SIMULATION_OK, "target time");
    building_clock_simulation_t replay = simulation;
    FILE* workers = open_csv(prefix, "workers",
                             "ms,worker,x,y,node,state,job,kind,digit,section,load,companion,rope,brick,reason,resource,owners,pending,reachable,builder_blocked,dispatch_blocked,unreachable,unsafe");
    FILE* changes = open_csv(prefix, "sections", "ms,digit,section,kind,lifecycle,status,ready,assigned,load,load_status,progress,dependencies");
    FILE* job_events = open_csv(prefix, "jobs", "ms,job,status,kind,worker,source,target,digit,section,object,source_complete");
    uint32_t time_ms[BUILDING_CLOCK_MAX_WORKERS][REASON_COUNT] = {{0}};
    uint32_t carried_wait[BUILDING_CLOCK_MAX_WORKERS][REASON_COUNT] = {{0}};
    section_times_t times[BUILDING_CLOCK_DIGIT_COUNT][BUILDING_CLOCK_MAX_DIGIT_SECTIONS] = {{{0}}};
    printf("seed=%016" PRIx64 " version=%u attempt=%u scenario=%s topology=%u\n", world.replay.root_seed, world.replay.generator_version, world.replay.generation_attempt,
           rollover ? "rollover" : "single", world.topology_family);
    describe_world(&world);
    uint32_t ms = 0U;
    while (building_clock_simulation_transition_active(&simulation) && ms < 600000U) {
        const building_clock_simulation_t before = simulation;
        require(building_clock_simulation_step(&simulation, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK, "sampled simulation step");
        require(building_clock_simulation_step(&replay, &world, 50U).status == BUILDING_CLOCK_SIMULATION_OK, "control replay step");
        ms += 50U;
        for (uint8_t d = 0U; d < BUILDING_CLOCK_DIGIT_COUNT; d++) {
            const building_clock_digit_state_t* digit = &simulation.state.digits[d];
            const building_clock_digit_state_t* old_digit = &before.state.digits[d];
            if (digit->lifecycle != old_digit->lifecycle) {
                printf("PHASE ms=%u digit=%u %s->%s\n", ms, d, phases[old_digit->lifecycle], phases[digit->lifecycle]);
            }
            if (digit->lifecycle == BUILDING_CLOCK_DIGIT_STABLE) {
                continue;
            }
            for (uint8_t s = 0U; s < digit->incoming.section_count; s++) {
                const building_clock_section_state_t* section = &digit->incoming.sections[s];
                const building_clock_section_state_t* old = &old_digit->incoming.sections[s];
                const bool ready = dependencies_ready(&digit->incoming, section);
                section_times_t* timing = &times[d][s];
                timing->kind = (uint8_t)section->kind;
                if (ready && timing->ready == 0U)
                    timing->ready = ms;
                if (section->assigned_job_id != BUILDING_CLOCK_INVALID_ID && timing->assigned == 0U)
                    timing->assigned = ms;
                if (section->assigned_job_id < BUILDING_CLOCK_MAX_JOBS && timing->build_assigned == 0U) {
                    const building_clock_job_t* assigned = &simulation.state.jobs[section->assigned_job_id];
                    if (assigned->kind == BUILDING_CLOCK_JOB_PLACE_SECTION || assigned->kind == BUILDING_CLOCK_JOB_SECURE_SECTION) {
                        timing->build_assigned = ms;
                    }
                }
                if (section->status >= BUILDING_CLOCK_SECTION_STAGED && timing->supplied == 0U)
                    timing->supplied = ms;
                if (section->progress_per_mille > 0U && timing->first_brick == 0U)
                    timing->first_brick = ms;
                if (section->status >= BUILDING_CLOCK_SECTION_PLACED && timing->placed == 0U)
                    timing->placed = ms;
                if (section->status == BUILDING_CLOCK_SECTION_SECURED && timing->secured == 0U)
                    timing->secured = ms;
                if (changes != NULL &&
                    (ms == 50U || memcmp(section, old, sizeof(*section)) != 0 || ready != dependencies_ready(&old_digit->incoming, old) || digit->lifecycle != old_digit->lifecycle)) {
                    unsigned load_id = BUILDING_CLOCK_INVALID_ID, load_status = 0U;
                    for (uint8_t l = 0U; l < BUILDING_CLOCK_MAX_LOADS; l++) {
                        const building_clock_load_t* load = &simulation.state.loads[l];
                        if (load->status != BUILDING_CLOCK_LOAD_UNUSED && load->digit_id == d && load->section_id == s) {
                            load_id = l;
                            load_status = (unsigned)load->status;
                        }
                    }
                    fprintf(changes, "%u,%u,%u,%s,%s,%u,%u,%u,%u,%u,%u,%u\n", ms, d, s, sections[section->kind], phases[digit->lifecycle], section->status, ready, section->assigned_job_id, load_id,
                            load_status, section->progress_per_mille, section->dependency_mask);
                }
            }
        }
        for (uint8_t w = 0U; w < simulation.state.worker_count; w++) {
            const building_clock_worker_t* worker = &simulation.state.workers[w];
            const worker_probe_t probe = probe_worker(&world, &simulation, w);
            time_ms[w][probe.reason] += 50U;
            if (worker->carried_load_id < BUILDING_CLOCK_MAX_LOADS)
                carried_wait[w][probe.reason] += 50U;
            const building_clock_job_t* job = worker->current_job_id < BUILDING_CLOCK_MAX_JOBS ? &simulation.state.jobs[worker->current_job_id] : NULL;
            const unsigned companion = worker->carried_load_id < BUILDING_CLOCK_MAX_LOADS ? simulation.state.loads[worker->carried_load_id].companion_load_id : BUILDING_CLOCK_INVALID_ID;
            if (workers != NULL)
                fprintf(workers, "%u,%u,%d,%d,%u,%s,%u,%s,%u,%u,%u,%u,%u,%u,%s,%u,%u,%u,%u,%u,%u,%u,%u\n", ms, w, worker->position.x, worker->position.y, worker->current_node_id,
                        states[worker->state], worker->current_job_id, job != NULL ? jobs[job->kind] : "none", job != NULL ? job->subject.digit_id : 255U, job != NULL ? job->subject.section_id : 255U,
                        worker->carried_load_id, companion, worker->rope.phase, worker->rope.brick_index, reasons[probe.reason], probe.resource, probe.owners, probe.pending, probe.reachable,
                        probe.builder_blocked, probe.dispatch_blocked, probe.unreachable, probe.unsafe);
        }
        if (job_events != NULL)
            for (uint8_t j = 0U; j < BUILDING_CLOCK_MAX_JOBS; j++) {
                const building_clock_job_t* job = &simulation.state.jobs[j];
                if (memcmp(job, &before.state.jobs[j], sizeof(*job)) != 0)
                    fprintf(job_events, "%u,%u,%u,%s,%u,%u,%u,%u,%u,%u,%u\n", ms, j, job->status, jobs[job->kind], job->reserved_worker_id, job->source_node_id, job->target_node_id,
                            job->subject.digit_id, job->subject.section_id, job->subject.object_id, job->source_action_complete);
            }
        require(memcmp(&simulation, &replay, sizeof(simulation)) == 0, "diagnostics preserve byte-identical control replay");
        require(memcmp(&world, &original_world, sizeof(world)) == 0, "diagnostics preserve immutable world");
    }
    require(!building_clock_simulation_transition_active(&simulation), "complete transition before timeout");
    require(simulation.state.used_load_slot_count == 0U && simulation.state.used_debris_slot_count == 0U && simulation.metrics.evacuation_failures == 0U, "transition resource cleanup and safety");
    printf("COMPLETE ms=%u workers=%u route_failures=%u paired=%u chained=%u\n", ms, simulation.state.worker_count, simulation.metrics.route_failures, simulation.metrics.paired_deliveries,
           simulation.metrics.chained_rope_sections);
    for (uint8_t d = 0U; d < BUILDING_CLOCK_DIGIT_COUNT; d++)
        for (uint8_t s = 0U; s < BUILDING_CLOCK_MAX_DIGIT_SECTIONS; s++) {
            const section_times_t* t = &times[d][s];
            if (t->ready != 0U)
                printf("SECTION digit=%u section=%u %s ready=%u supply_assigned=%u build_assigned=%u supplied=%u first_brick=%u placed=%u secured=%u\n", d, s, sections[t->kind], t->ready, t->assigned,
                       t->build_assigned, t->supplied, t->first_brick, t->placed, t->secured);
        }
    for (uint8_t w = 0U; w < simulation.state.worker_count; w++)
        for (unsigned r = 0U; r < REASON_COUNT; r++) {
            if (time_ms[w][r] != 0U)
                printf("WORKER %u reason=%s ms=%u carrying_ms=%u\n", w, reasons[r], time_ms[w][r], carried_wait[w][r]);
        }
    if (workers != NULL)
        require(fclose(workers) == 0, "close worker output");
    if (changes != NULL)
        require(fclose(changes) == 0, "close section output");
    if (job_events != NULL)
        require(fclose(job_events) == 0, "close job output");
    puts("Read-only probes and deterministic control replay passed");
    return EXIT_SUCCESS;
}
