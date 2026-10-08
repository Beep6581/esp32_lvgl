#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "building_clock_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUILDING_CLOCK_MAX_ROUTE_NODES BUILDING_CLOCK_MAX_NAV_NODES
#define BUILDING_CLOCK_MAX_ROUTE_EDGES (BUILDING_CLOCK_MAX_ROUTE_NODES - 1U)

typedef struct {
    uint8_t start_node_id;
    uint8_t destination_node_id;
    bool allow_temporary_connections;
    bool carries_large_load;
    /* Ride edges are topology only; this does not imply machinery readiness. */
    bool allow_machinery_edges;
} building_clock_route_request_t;

typedef struct {
    uint32_t total_cost;
    uint8_t node_count;
    uint8_t edge_count;
    uint8_t node_ids[BUILDING_CLOCK_MAX_ROUTE_NODES];
    uint16_t edge_ids[BUILDING_CLOCK_MAX_ROUTE_EDGES];
    /* Bounded search diagnostics useful to host tests and profiling. */
    uint8_t settled_node_count;
    uint16_t examined_edge_count;
} building_clock_route_t;

typedef enum {
    BUILDING_CLOCK_ROUTE_OK = 0,
    BUILDING_CLOCK_ROUTE_INVALID_ARGUMENT,
    BUILDING_CLOCK_ROUTE_INVALID_GRAPH,
    BUILDING_CLOCK_ROUTE_INVALID_NODE,
    BUILDING_CLOCK_ROUTE_UNREACHABLE,
    BUILDING_CLOCK_ROUTE_CAPACITY_EXCEEDED,
} building_clock_route_status_t;

typedef struct {
    uint16_t active_temp_attachment_mask;
    uint16_t node_owner_masks[BUILDING_CLOCK_MAX_NAV_NODES];
    uint16_t edge_owner_masks[BUILDING_CLOCK_MAX_NAV_EDGES];
} building_clock_navigation_runtime_t;

typedef enum {
    BUILDING_CLOCK_RESERVATION_ACQUIRED = 0,
    BUILDING_CLOCK_RESERVATION_ALREADY_HELD,
    BUILDING_CLOCK_RESERVATION_UNAVAILABLE,
    BUILDING_CLOCK_RESERVATION_INVALID_ARGUMENT,
    BUILDING_CLOCK_RESERVATION_INVALID_RESOURCE,
    BUILDING_CLOCK_RESERVATION_INVALID_OWNER,
    BUILDING_CLOCK_RESERVATION_NOT_HELD,
    BUILDING_CLOCK_RESERVATION_RELEASED,
} building_clock_reservation_status_t;

void building_clock_navigation_runtime_init(building_clock_navigation_runtime_t* runtime);
void building_clock_navigation_reset_reservations(building_clock_navigation_runtime_t* runtime);
bool building_clock_navigation_set_temp_active(const building_clock_world_t* world, building_clock_navigation_runtime_t* runtime, uint8_t attachment_set_id, bool active);
bool building_clock_navigation_temp_is_active(const building_clock_navigation_runtime_t* runtime, uint8_t attachment_set_id);

/*
 * On failure, node_count, edge_count, and total_cost remain zero. Search
 * diagnostics are retained for a valid but unreachable request.
 */
building_clock_route_status_t building_clock_find_route(const building_clock_world_t* world, const building_clock_navigation_runtime_t* runtime, const building_clock_route_request_t* request,
                                                        building_clock_route_t* route);
const char* building_clock_route_status_name(building_clock_route_status_t status);

/* A node's reservation capacity is the minimum capacity of its incident edges. */
uint8_t building_clock_navigation_node_capacity(const building_clock_world_t* world, uint8_t node_id);

building_clock_reservation_status_t building_clock_navigation_reserve_node(const building_clock_world_t* world, building_clock_navigation_runtime_t* runtime, uint8_t node_id, uint8_t worker_id);
building_clock_reservation_status_t building_clock_navigation_release_node(const building_clock_world_t* world, building_clock_navigation_runtime_t* runtime, uint8_t node_id, uint8_t worker_id);
building_clock_reservation_status_t building_clock_navigation_reserve_edge(const building_clock_world_t* world, building_clock_navigation_runtime_t* runtime, uint16_t edge_id, uint8_t worker_id);
building_clock_reservation_status_t building_clock_navigation_release_edge(const building_clock_world_t* world, building_clock_navigation_runtime_t* runtime, uint16_t edge_id, uint8_t worker_id);
void building_clock_navigation_release_owner(building_clock_navigation_runtime_t* runtime, uint8_t worker_id);

uint16_t building_clock_navigation_node_owner_mask(const building_clock_world_t* world, const building_clock_navigation_runtime_t* runtime, uint8_t node_id);
uint16_t building_clock_navigation_edge_owner_mask(const building_clock_world_t* world, const building_clock_navigation_runtime_t* runtime, uint16_t edge_id);
const char* building_clock_reservation_status_name(building_clock_reservation_status_t status);

#ifdef __cplusplus
}
#endif
