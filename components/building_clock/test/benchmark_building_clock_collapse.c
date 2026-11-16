#include "building_clock_geometry.h"
#include "building_clock_simulation.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Host-only geometry microbenchmark, not a claim about ESP32 frame rate.
 * All numerals, occupied sections and partial masonry use the same fixtures
 * before/after an implementation change. Compile with -O2, without LTO. */
typedef struct {
    building_clock_falling_section_t falling;
    building_clock_debris_t debris;
} fixture_t;

static fixture_t fixtures[70];
static const building_clock_rect_t envelope = {40, 170, 78, 144};
static const building_clock_rect_t catch_area = {44, 322, 70, 14};
static volatile uint32_t checksum;

static unsigned prepare_fixtures(void) {
    unsigned count = 0U;
    for (uint8_t numeral = 0U; numeral < 10U; numeral++) {
        for (building_clock_section_kind_t kind = BUILDING_CLOCK_SECTION_FOUNDATION; kind <= BUILDING_CLOCK_SECTION_LOWER_RIGHT_PIER; kind++) {
            building_clock_rect_t original[BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION];
            const uint8_t total = building_clock_section_visible_bricks(envelope, numeral, kind, original);
            if (total == 0U) {
                continue;
            }
            fixture_t* fixture = &fixtures[count];
            fixture->falling = (building_clock_falling_section_t){
                .section_kind = kind,
                .start_position = building_clock_rect_center(building_clock_section_bounds(envelope, kind)),
                .source_numeral = numeral,
                .failure_brick_id = (uint8_t)(count % total),
                .masonry_progress_per_mille = count % 3U == 0U ? 400U : 1000U,
                .duration_ms = 800U,
            };
            fixture->falling.landing_position = (building_clock_point_t){(int16_t)(fixture->falling.start_position.x + 2), 332};
            fixture->debris = (building_clock_debris_t){
                .section_kind = (uint8_t)kind,
                .source_numeral = numeral,
                .masonry_progress_per_mille = fixture->falling.masonry_progress_per_mille,
                .position = fixture->falling.landing_position,
            };
            count++;
        }
    }
    return count;
}

int main(int argc, char** argv) {
    const unsigned rounds = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 0) : 10000U;
    if (rounds == 0U || rounds > 1000000U) {
        return 2;
    }
    const unsigned count = prepare_fixtures();
    printf("sizes: context=%zu state=%zu falling_section=%zu debris=%zu sample=%zu; fixtures=%u rounds=%u\n", sizeof(building_clock_simulation_t), sizeof(building_clock_simulation_state_t),
           sizeof(building_clock_falling_section_t), sizeof(building_clock_debris_t), sizeof(building_clock_falling_brick_t), count, rounds);
    for (unsigned repeat = 0U; repeat < 3U; repeat++) {
        for (unsigned mode = 0U; mode < 3U; mode++) {
            uint32_t sum = 0U;
            uint64_t bricks = 0U;
            const clock_t start = clock();
            for (unsigned round = 0U; round < rounds; round++) {
                for (unsigned id = 0U; id < count; id++) {
                    fixture_t* fixture = &fixtures[id];
                    uint8_t visible;
                    if (mode == 2U) {
                        building_clock_rect_t pile[BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION];
                        visible = building_clock_debris_bricks(envelope, catch_area, &fixture->debris, pile);
                        for (uint8_t brick = 0U; brick < visible; brick++) {
                            sum += (uint32_t)(uint16_t)pile[brick].x + (uint32_t)(uint16_t)pile[brick].y;
                        }
                    } else {
                        fixture->falling.elapsed_ms = mode == 0U ? (round * 37U) % 800U : 800U;
                        building_clock_falling_brick_t falling[BUILDING_CLOCK_MAX_VISIBLE_BRICKS_PER_SECTION];
                        visible = building_clock_falling_bricks(envelope, catch_area, &fixture->falling, falling);
                        for (uint8_t brick = 0U; brick < visible; brick++) {
                            sum += (uint32_t)(uint16_t)falling[brick].bounds.x + (uint32_t)(uint16_t)falling[brick].bounds.y;
                        }
                    }
                    bricks += visible;
                }
            }
            const double seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
            checksum = sum;
            printf("%s repeat=%u seconds=%.6f ns/section=%.1f ns/brick=%.1f checksum=%" PRIu32 "\n", mode == 0U ? "falling" : (mode == 1U ? "landed" : "debris"), repeat, seconds,
                   seconds * 1e9 / ((double)rounds * count), seconds * 1e9 / (double)bricks, checksum);
        }
    }
    return 0;
}
