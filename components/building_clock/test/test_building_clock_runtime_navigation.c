#include "building_clock_generator.h"
#include "building_clock_runtime_navigation.h"
#include "building_clock_world_selection.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define INTEGRATION_WORLD_COUNT 128U

static unsigned int failures;

static void expect_true(bool condition, const char* description) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", description);
        failures++;
    }
}

static void initialize_graph(building_clock_world_t* world, uint8_t node_count) {
    memset(world, 0, sizeof(*world));
    world->navigation.node_count = node_count;
}

static uint16_t add_edge(building_clock_world_t* world, uint8_t from, uint8_t to, building_clock_nav_edge_kind_t kind, uint16_t cost, uint8_t capacity, uint8_t flags, uint8_t attachment_set_id) {
    const uint16_t edge_id = world->navigation.edge_count++;
    world->navigation.edges[edge_id] = (building_clock_nav_edge_t){
        .from_node_id = from,
        .to_node_id = to,
        .kind = kind,
        .traversal_cost = cost,
        .capacity = capacity,
        .flags = flags,
        .temp_attachment_set_id = attachment_set_id,
    };
    return edge_id;
}

static building_clock_route_request_t route_request(uint8_t start, uint8_t destination) {
    return (building_clock_route_request_t){
        .start_node_id = start,
        .destination_node_id = destination,
    };
}

static void test_basic_routes(void) {
    building_clock_world_t world;
    initialize_graph(&world, 4U);
    add_edge(&world, 0U, 1U, BUILDING_CLOCK_NAV_EDGE_WALK, 4U, 2U, BUILDING_CLOCK_NAV_EDGE_BIDIRECTIONAL | BUILDING_CLOCK_NAV_EDGE_ALLOW_LARGE_LOAD, BUILDING_CLOCK_INVALID_ID);
    add_edge(&world, 1U, 2U, BUILDING_CLOCK_NAV_EDGE_CLIMB, 7U, 1U, BUILDING_CLOCK_NAV_EDGE_BIDIRECTIONAL, BUILDING_CLOCK_INVALID_ID);
    const building_clock_world_t original = world;

    building_clock_navigation_runtime_t runtime;
    building_clock_navigation_runtime_init(&runtime);
    building_clock_route_t route;

    building_clock_route_request_t request = route_request(0U, 0U);
    expect_true(building_clock_find_route(&world, &runtime, &request, &route) == BUILDING_CLOCK_ROUTE_OK, "start equal to destination succeeds");
    expect_true(route.node_count == 1U && route.edge_count == 0U && route.node_ids[0] == 0U && route.total_cost == 0U, "zero-length route has one node and no edges");

    request = route_request(0U, 1U);
    expect_true(building_clock_find_route(&world, &runtime, &request, &route) == BUILDING_CLOCK_ROUTE_OK, "simple walk route succeeds");
    expect_true(route.node_count == 2U && route.edge_count == 1U && route.node_ids[0] == 0U && route.node_ids[1] == 1U && route.edge_ids[0] == 0U && route.total_cost == 4U,
                "simple walk route contains expected node and edge IDs");

    request = route_request(0U, 2U);
    expect_true(building_clock_find_route(&world, &runtime, &request, &route) == BUILDING_CLOCK_ROUTE_OK, "route requiring ladder succeeds");
    expect_true(route.node_count == 3U && route.edge_count == 2U && route.edge_ids[1] == 1U && world.navigation.edges[route.edge_ids[1]].kind == BUILDING_CLOCK_NAV_EDGE_CLIMB,
                "ladder route identifies climb edge");

    request = route_request(2U, 0U);
    expect_true(building_clock_find_route(&world, &runtime, &request, &route) == BUILDING_CLOCK_ROUTE_OK, "bidirectional reverse route succeeds");

    request = route_request(0U, 3U);
    expect_true(building_clock_find_route(&world, &runtime, &request, &route) == BUILDING_CLOCK_ROUTE_UNREACHABLE, "disconnected destination is unreachable");

    request = route_request(0U, 4U);
    expect_true(building_clock_find_route(&world, &runtime, &request, &route) == BUILDING_CLOCK_ROUTE_INVALID_NODE, "invalid destination is rejected");
    expect_true(building_clock_find_route(NULL, &runtime, &request, &route) == BUILDING_CLOCK_ROUTE_INVALID_ARGUMENT, "null world is rejected");
    expect_true(memcmp(&world, &original, sizeof(world)) == 0, "route finding does not mutate world data");
}

static void test_constraints_and_machinery(void) {
    building_clock_world_t world;
    initialize_graph(&world, 4U);
    add_edge(&world, 0U, 1U, BUILDING_CLOCK_NAV_EDGE_WALK, 2U, 1U, BUILDING_CLOCK_NAV_EDGE_BIDIRECTIONAL, BUILDING_CLOCK_INVALID_ID);
    add_edge(&world, 1U, 2U, BUILDING_CLOCK_NAV_EDGE_RIDE_CART, 3U, 2U, BUILDING_CLOCK_NAV_EDGE_BIDIRECTIONAL | BUILDING_CLOCK_NAV_EDGE_ALLOW_LARGE_LOAD, BUILDING_CLOCK_INVALID_ID);
    add_edge(&world, 2U, 3U, BUILDING_CLOCK_NAV_EDGE_RIDE_LIFT, 5U, 3U, BUILDING_CLOCK_NAV_EDGE_BIDIRECTIONAL | BUILDING_CLOCK_NAV_EDGE_ALLOW_LARGE_LOAD, BUILDING_CLOCK_INVALID_ID);

    building_clock_navigation_runtime_t runtime;
    building_clock_navigation_runtime_init(&runtime);
    building_clock_route_t route;
    building_clock_route_request_t request = route_request(0U, 3U);

    expect_true(building_clock_find_route(&world, &runtime, &request, &route) == BUILDING_CLOCK_ROUTE_UNREACHABLE, "machinery edges require explicit topological permission");
    request.allow_machinery_edges = true;
    expect_true(building_clock_find_route(&world, &runtime, &request, &route) == BUILDING_CLOCK_ROUTE_OK, "cart and lift topology can be planned");
    expect_true(route.edge_count == 3U && world.navigation.edges[route.edge_ids[1]].kind == BUILDING_CLOCK_NAV_EDGE_RIDE_CART &&
                    world.navigation.edges[route.edge_ids[2]].kind == BUILDING_CLOCK_NAV_EDGE_RIDE_LIFT,
                "route preserves cart and lift edges for later machinery handling");

    request = route_request(0U, 1U);
    request.carries_large_load = true;
    expect_true(building_clock_find_route(&world, &runtime, &request, &route) == BUILDING_CLOCK_ROUTE_UNREACHABLE, "large load rejects edge without load permission");
}

static void test_temporary_connections(void) {
    building_clock_world_t world;
    initialize_graph(&world, 2U);
    world.temp_attachment_set_count = 1U;
    add_edge(&world, 0U, 1U, BUILDING_CLOCK_NAV_EDGE_TEMP_CONNECTION, 6U, 1U, BUILDING_CLOCK_NAV_EDGE_BIDIRECTIONAL | BUILDING_CLOCK_NAV_EDGE_REQUIRES_TEMP_STRUCTURE, 0U);
    const building_clock_world_t original = world;

    building_clock_navigation_runtime_t runtime;
    building_clock_navigation_runtime_init(&runtime);
    building_clock_route_t route;
    building_clock_route_request_t request = route_request(0U, 1U);
    request.allow_temporary_connections = true;

    expect_true(building_clock_find_route(&world, &runtime, &request, &route) == BUILDING_CLOCK_ROUTE_UNREACHABLE, "inactive temporary connection is not traversable");
    expect_true(building_clock_navigation_set_temp_active(&world, &runtime, 0U, true), "valid temporary attachment can be enabled");
    expect_true(building_clock_navigation_temp_is_active(&runtime, 0U), "temporary attachment reports active");
    expect_true(building_clock_find_route(&world, &runtime, &request, &route) == BUILDING_CLOCK_ROUTE_OK && route.edge_ids[0] == 0U, "enabled temporary connection is traversable");

    request.allow_temporary_connections = false;
    expect_true(building_clock_find_route(&world, &runtime, &request, &route) == BUILDING_CLOCK_ROUTE_UNREACHABLE, "request may forbid an active temporary connection");
    expect_true(building_clock_navigation_set_temp_active(&world, &runtime, 0U, false), "temporary attachment can be disabled");
    expect_true(!building_clock_navigation_set_temp_active(&world, &runtime, 1U, true), "invalid temporary attachment is rejected");
    expect_true(memcmp(&world, &original, sizeof(world)) == 0, "temporary activation does not mutate immutable world data");
}

static void test_equal_cost_tie_breaking(void) {
    building_clock_world_t world;
    initialize_graph(&world, 4U);
    add_edge(&world, 0U, 2U, BUILDING_CLOCK_NAV_EDGE_WALK, 5U, 1U, BUILDING_CLOCK_NAV_EDGE_BIDIRECTIONAL, BUILDING_CLOCK_INVALID_ID);
    add_edge(&world, 2U, 3U, BUILDING_CLOCK_NAV_EDGE_WALK, 5U, 1U, BUILDING_CLOCK_NAV_EDGE_BIDIRECTIONAL, BUILDING_CLOCK_INVALID_ID);
    add_edge(&world, 0U, 1U, BUILDING_CLOCK_NAV_EDGE_WALK, 5U, 1U, BUILDING_CLOCK_NAV_EDGE_BIDIRECTIONAL, BUILDING_CLOCK_INVALID_ID);
    add_edge(&world, 1U, 3U, BUILDING_CLOCK_NAV_EDGE_WALK, 5U, 1U, BUILDING_CLOCK_NAV_EDGE_BIDIRECTIONAL, BUILDING_CLOCK_INVALID_ID);

    building_clock_navigation_runtime_t runtime;
    building_clock_navigation_runtime_init(&runtime);
    building_clock_route_request_t request = route_request(0U, 3U);
    building_clock_route_t first;
    building_clock_route_t second;
    expect_true(building_clock_find_route(&world, &runtime, &request, &first) == BUILDING_CLOCK_ROUTE_OK, "equal-cost route succeeds");
    expect_true(building_clock_find_route(&world, &runtime, &request, &second) == BUILDING_CLOCK_ROUTE_OK, "equal-cost route repeats");
    expect_true(first.node_count == 3U && first.node_ids[1] == 1U, "equal-cost tie chooses lower stable predecessor node ID");
    expect_true(memcmp(&first, &second, sizeof(first)) == 0, "equal-cost route result is byte deterministic");
}

static void test_maximum_route_length(void) {
    building_clock_world_t world;
    initialize_graph(&world, BUILDING_CLOCK_MAX_NAV_NODES);
    for (uint8_t node = 1U; node < BUILDING_CLOCK_MAX_NAV_NODES; node++) {
        add_edge(&world, (uint8_t)(node - 1U), node, BUILDING_CLOCK_NAV_EDGE_WALK, 1U, 1U, BUILDING_CLOCK_NAV_EDGE_BIDIRECTIONAL, BUILDING_CLOCK_INVALID_ID);
    }

    building_clock_navigation_runtime_t runtime;
    building_clock_navigation_runtime_init(&runtime);
    building_clock_route_request_t request = route_request(0U, BUILDING_CLOCK_MAX_NAV_NODES - 1U);
    building_clock_route_t route;
    expect_true(building_clock_find_route(&world, &runtime, &request, &route) == BUILDING_CLOCK_ROUTE_OK, "maximum simple route fits fixed route storage");
    expect_true(route.node_count == BUILDING_CLOCK_MAX_ROUTE_NODES && route.edge_count == BUILDING_CLOCK_MAX_ROUTE_EDGES, "maximum route reports full node and edge counts");
    expect_true(route.node_ids[0] == 0U && route.node_ids[route.node_count - 1U] == BUILDING_CLOCK_MAX_NAV_NODES - 1U, "maximum route endpoints are retained");
}

static void test_reservations(void) {
    building_clock_world_t world;
    initialize_graph(&world, 3U);
    add_edge(&world, 0U, 1U, BUILDING_CLOCK_NAV_EDGE_CLIMB, 1U, 1U, BUILDING_CLOCK_NAV_EDGE_BIDIRECTIONAL, BUILDING_CLOCK_INVALID_ID);
    add_edge(&world, 1U, 2U, BUILDING_CLOCK_NAV_EDGE_WALK, 1U, 2U, BUILDING_CLOCK_NAV_EDGE_BIDIRECTIONAL, BUILDING_CLOCK_INVALID_ID);
    world.temp_attachment_set_count = 1U;

    building_clock_navigation_runtime_t runtime;
    building_clock_navigation_runtime_init(&runtime);
    expect_true(building_clock_navigation_set_temp_active(&world, &runtime, 0U, true), "reservation fixture enables temporary state");

    expect_true(building_clock_navigation_reserve_edge(&world, &runtime, 0U, 2U) == BUILDING_CLOCK_RESERVATION_ACQUIRED, "worker acquires free capacity-one edge");
    expect_true(building_clock_navigation_reserve_edge(&world, &runtime, 0U, 2U) == BUILDING_CLOCK_RESERVATION_ALREADY_HELD, "same owner acquisition is idempotent");
    expect_true(building_clock_navigation_reserve_edge(&world, &runtime, 0U, 1U) == BUILDING_CLOCK_RESERVATION_UNAVAILABLE, "conflicting edge acquisition is rejected");
    expect_true(building_clock_navigation_edge_owner_mask(&world, &runtime, 0U) == (uint16_t)(1U << 2U), "edge owner mask identifies current owner");
    expect_true(building_clock_navigation_release_edge(&world, &runtime, 0U, 1U) == BUILDING_CLOCK_RESERVATION_NOT_HELD, "non-owner cannot release reservation");
    expect_true(building_clock_navigation_release_edge(&world, &runtime, 0U, 2U) == BUILDING_CLOCK_RESERVATION_RELEASED, "owner releases edge reservation");
    expect_true(building_clock_navigation_reserve_edge(&world, &runtime, 0U, 1U) == BUILDING_CLOCK_RESERVATION_ACQUIRED, "released edge can be reacquired");

    expect_true(building_clock_navigation_reserve_edge(&world, &runtime, 1U, 0U) == BUILDING_CLOCK_RESERVATION_ACQUIRED, "first worker acquires capacity-two edge");
    expect_true(building_clock_navigation_reserve_edge(&world, &runtime, 1U, 3U) == BUILDING_CLOCK_RESERVATION_ACQUIRED, "second worker acquires capacity-two edge");
    expect_true(building_clock_navigation_reserve_edge(&world, &runtime, 1U, 4U) == BUILDING_CLOCK_RESERVATION_UNAVAILABLE, "capacity-two edge rejects third worker");
    expect_true(building_clock_navigation_node_capacity(&world, 2U) == 2U, "node capacity derives from incident capacity-two edge");
    expect_true(building_clock_navigation_reserve_node(&world, &runtime, 2U, 0U) == BUILDING_CLOCK_RESERVATION_ACQUIRED, "first worker acquires capacity-two node");
    expect_true(building_clock_navigation_reserve_node(&world, &runtime, 2U, 3U) == BUILDING_CLOCK_RESERVATION_ACQUIRED, "second worker acquires capacity-two node");
    expect_true(building_clock_navigation_reserve_node(&world, &runtime, 2U, 4U) == BUILDING_CLOCK_RESERVATION_UNAVAILABLE, "capacity-two node rejects third worker");

    building_clock_navigation_release_owner(&runtime, 3U);
    expect_true((building_clock_navigation_edge_owner_mask(&world, &runtime, 1U) & (uint16_t)(1U << 3U)) == 0U &&
                    (building_clock_navigation_node_owner_mask(&world, &runtime, 2U) & (uint16_t)(1U << 3U)) == 0U,
                "owner cancellation releases all node and edge reservations");
    expect_true(building_clock_navigation_reserve_edge(&world, &runtime, 2U, 0U) == BUILDING_CLOCK_RESERVATION_INVALID_RESOURCE, "invalid edge ID is rejected");
    expect_true(building_clock_navigation_reserve_node(&world, &runtime, 3U, 0U) == BUILDING_CLOCK_RESERVATION_INVALID_RESOURCE, "invalid node ID is rejected");
    expect_true(building_clock_navigation_reserve_edge(&world, &runtime, 0U, BUILDING_CLOCK_MAX_WORKERS) == BUILDING_CLOCK_RESERVATION_INVALID_OWNER, "invalid owner ID is rejected");

    building_clock_navigation_reset_reservations(&runtime);
    expect_true(building_clock_navigation_edge_owner_mask(&world, &runtime, 0U) == 0U && building_clock_navigation_node_owner_mask(&world, &runtime, 2U) == 0U,
                "reservation reset clears all ownership");
    expect_true(building_clock_navigation_temp_is_active(&runtime, 0U), "reservation reset preserves temporary-edge activation");

    building_clock_route_request_t request = route_request(0U, 2U);
    building_clock_route_t before;
    building_clock_route_t after;
    expect_true(building_clock_find_route(&world, &runtime, &request, &before) == BUILDING_CLOCK_ROUTE_OK, "route exists before reservation");
    expect_true(building_clock_navigation_reserve_edge(&world, &runtime, 0U, 0U) == BUILDING_CLOCK_RESERVATION_ACQUIRED, "route edge can be reserved independently");
    expect_true(building_clock_find_route(&world, &runtime, &request, &after) == BUILDING_CLOCK_ROUTE_OK && memcmp(&before, &after, sizeof(before)) == 0,
                "reservation occupancy does not alter deterministic topology search");
}

typedef struct {
    uint32_t query_count;
    uint64_t total_route_edges;
    uint8_t maximum_route_nodes;
    uint8_t maximum_settled_nodes;
    uint16_t maximum_examined_edges;
} route_batch_stats_t;

static void run_integration_route(const building_clock_world_t* world, const building_clock_navigation_runtime_t* runtime, uint8_t start, uint8_t destination, route_batch_stats_t* stats,
                                  const char* description) {
    building_clock_route_request_t request = route_request(start, destination);
    request.allow_machinery_edges = true;
    building_clock_route_t route;
    const building_clock_route_status_t status = building_clock_find_route(world, runtime, &request, &route);
    expect_true(status == BUILDING_CLOCK_ROUTE_OK, description);
    if (status != BUILDING_CLOCK_ROUTE_OK) {
        return;
    }
    stats->query_count++;
    stats->total_route_edges += route.edge_count;
    if (route.node_count > stats->maximum_route_nodes) {
        stats->maximum_route_nodes = route.node_count;
    }
    if (route.settled_node_count > stats->maximum_settled_nodes) {
        stats->maximum_settled_nodes = route.settled_node_count;
    }
    if (route.examined_edge_count > stats->maximum_examined_edges) {
        stats->maximum_examined_edges = route.examined_edge_count;
    }
}

static uint8_t lower_digit_access_node(const building_clock_world_t* world, uint8_t digit) {
    for (uint8_t access = 0U; access < world->digit_work_areas[digit].access_count; access++) {
        if (world->digit_work_areas[digit].access[access].kind == BUILDING_CLOCK_DIGIT_ACCESS_LOWER) {
            return world->digit_work_areas[digit].access[access].nav_node_id;
        }
    }
    return BUILDING_CLOCK_INVALID_ID;
}

static void test_accepted_world_integration(void) {
    route_batch_stats_t stats = {0};
    for (uint32_t index = 0U; index < INTEGRATION_WORLD_COUNT; index++) {
        const uint64_t root_seed = UINT64_C(0x510e527fade682d1) + UINT64_C(0x9e3779b97f4a7c15) * index;
        building_clock_world_t world;
        const building_clock_world_selection_result_t selection = building_clock_select_world(&world, root_seed, BUILDING_CLOCK_GENERATOR_VERSION);
        expect_true(selection.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED || selection.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED_FALLBACK, "integration world selection succeeds");
        if (selection.status != BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED && selection.status != BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED_FALLBACK) {
            continue;
        }
        const building_clock_world_t original = world;
        building_clock_navigation_runtime_t runtime;
        building_clock_navigation_runtime_init(&runtime);

        const uint8_t spawn = world.worker_spawns[0].nav_node_id;
        const uint8_t depot = world.depots[0].access_node_id;
        run_integration_route(&world, &runtime, spawn, depot, &stats, "accepted world routes spawn to depot");
        for (uint8_t staging = 0U; staging < world.staging_area_count; staging++) {
            run_integration_route(&world, &runtime, depot, world.staging_areas[staging].access_node_id, &stats, "accepted world routes depot to staging");
        }
        for (uint8_t digit = 0U; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
            const uint8_t access = lower_digit_access_node(&world, digit);
            expect_true(access != BUILDING_CLOCK_INVALID_ID, "accepted world has lower digit access");
            if (access != BUILDING_CLOCK_INVALID_ID) {
                run_integration_route(&world, &runtime, spawn, access, &stats, "accepted world routes spawn to lower digit access");
                run_integration_route(&world, &runtime, world.staging_areas[0].access_node_id, access, &stats, "accepted world routes staging to lower digit access");
            }
        }
        for (uint8_t crane = 0U; crane < world.crane_count; crane++) {
            run_integration_route(&world, &runtime, spawn, world.cranes[crane].operator_node_id, &stats, "accepted world routes spawn to crane control");
        }
        for (uint8_t lift = 0U; lift < world.lift_count; lift++) {
            run_integration_route(&world, &runtime, spawn, world.lifts[lift].operator_node_id, &stats, "accepted world routes spawn to lift control");
        }
        for (uint8_t bay = 0U; bay < world.debris_bay_count; bay++) {
            run_integration_route(&world, &runtime, world.debris_bays[bay].access_node_id, depot, &stats, "accepted world routes debris bay to depot");
        }
        expect_true(memcmp(&world, &original, sizeof(world)) == 0, "integration routes do not mutate accepted world");
    }

    expect_true(stats.query_count > INTEGRATION_WORLD_COUNT, "integration batch exercises multiple semantic routes per world");
    printf("Runtime navigation batch: %u accepted worlds, %" PRIu32 " routes, average %.2f edges\n", INTEGRATION_WORLD_COUNT, stats.query_count,
           stats.query_count == 0U ? 0.0 : (double)stats.total_route_edges / stats.query_count);
    printf("  maximum route: %u nodes\n", stats.maximum_route_nodes);
    printf("  maximum search: %u settled nodes, %u examined edge entries\n", stats.maximum_settled_nodes, stats.maximum_examined_edges);
}

int main(void) {
    test_basic_routes();
    test_constraints_and_machinery();
    test_temporary_connections();
    test_equal_cost_tie_breaking();
    test_maximum_route_length();
    test_reservations();
    test_accepted_world_integration();

    if (failures != 0U) {
        fprintf(stderr, "%u Building Clock runtime-navigation test(s) failed\n", failures);
        return 1;
    }
    puts("Building Clock runtime-navigation tests passed");
    return 0;
}
