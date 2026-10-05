/* Version: 2026-10-05 */

#include "particle_engine.h"

#include <limits.h>
#include <string.h>

#include "esp_heap_caps.h"

#define PARTICLE_COUNT 2000U
#define PARTICLE_POSITION_SHIFT 8
#define PARTICLE_POSITION_ONE (1 << PARTICLE_POSITION_SHIFT)
#define PARTICLE_DISINTEGRATE_MS 700U
#define PARTICLE_TRANSITION_MS 1750U
#define PARTICLE_COLON_SHARE 12U
#define PARTICLE_HEAT_CELL_SIZE 4U
#define SPARK_COUNT 16U
#define SPARK_EMIT_INTERVAL_MS 125U

typedef enum {
    PARTICLE_MODE_FLAME,
    PARTICLE_MODE_DISINTEGRATE,
    PARTICLE_MODE_ATTRACT,
} particle_mode_t;

typedef struct {
    int32_t x;
    int32_t y;
    int16_t velocity_x;
    int16_t velocity_y;
    int16_t target_x;
    int16_t target_y;
    uint16_t age_ms;
    uint16_t life_ms;
    uint8_t phase;
    uint8_t phase_step;
    bool colon;
} particle_t;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t start_y;
    int16_t velocity_x;
    int16_t velocity_y;
    uint16_t age_ms;
    uint16_t life_ms;
    bool active;
} flame_spark_t;

struct particle_engine {
    particle_t particles[PARTICLE_COUNT];
    flame_spark_t sparks[SPARK_COUNT];
    clock_mask_t mask;
    uint8_t* heat;
    uint32_t random_state;
    uint32_t transition_elapsed_ms;
    uint32_t spark_emit_elapsed_ms;
    uint16_t width;
    uint16_t height;
    uint16_t heat_width;
    uint16_t heat_height;
    uint8_t colon_flare;
    particle_mode_t mode;
    bool mask_ready;
};

static uint32_t particle_random(particle_engine_t* engine) {
    uint32_t value = engine->random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    engine->random_state = value;
    return value;
}

static int32_t random_range(particle_engine_t* engine, int32_t minimum, int32_t maximum) {
    const uint32_t span = (uint32_t)(maximum - minimum + 1);
    return minimum + (int32_t)(particle_random(engine) % span);
}

static bool mask_bounds_valid(const clock_mask_t* mask, bool colon) {
    if (mask == NULL || mask->alpha == NULL || mask->width == 0 || mask->height == 0) {
        return false;
    }
    if (colon) {
        return mask->colon_x1 <= mask->colon_x2 && mask->colon_y1 <= mask->colon_y2;
    }
    return mask->x1 <= mask->x2 && mask->y1 <= mask->y2;
}

static void sample_mask(particle_engine_t* engine, bool colon, int16_t* x, int16_t* y) {
    const clock_mask_t* mask = &engine->mask;
    if (!mask_bounds_valid(mask, colon)) {
        *x = engine->width / 2;
        *y = engine->height / 2;
        return;
    }

    const uint16_t x1 = colon ? mask->colon_x1 : mask->x1;
    const uint16_t y1 = colon ? mask->colon_y1 : mask->y1;
    const uint16_t x2 = colon ? mask->colon_x2 : mask->x2;
    const uint16_t y2 = colon ? mask->colon_y2 : mask->y2;

    for (uint32_t attempt = 0; attempt < 128U; attempt++) {
        const uint16_t candidate_x = (uint16_t)random_range(engine, x1, x2);
        const uint16_t candidate_y = (uint16_t)random_range(engine, y1, y2);
        const uint8_t alpha = mask->alpha[candidate_y * mask->stride + candidate_x];
        if (alpha != 0 && (particle_random(engine) & 0xffU) <= alpha) {
            *x = (int16_t)candidate_x;
            *y = (int16_t)candidate_y;
            return;
        }
    }

    *x = (int16_t)((x1 + x2) / 2U);
    *y = (int16_t)((y1 + y2) / 2U);
}

static void sample_mask_upper_edge(particle_engine_t* engine, int16_t* x, int16_t* y) {
    const clock_mask_t* mask = &engine->mask;
    for (uint32_t attempt = 0; attempt < 256U; attempt++) {
        const uint16_t candidate_x = (uint16_t)random_range(engine, mask->x1, mask->x2);
        const uint16_t candidate_y = (uint16_t)random_range(engine, mask->y1, mask->y2);
        const uint8_t alpha = mask->alpha[candidate_y * mask->stride + candidate_x];
        const int32_t above_y = (int32_t)candidate_y - 2 * PARTICLE_HEAT_CELL_SIZE;
        const bool open_above = above_y < 0 || mask->alpha[(uint32_t)above_y * mask->stride + candidate_x] == 0U;

        if (alpha != 0U && open_above && (particle_random(engine) & 0xffU) <= alpha) {
            *x = (int16_t)candidate_x;
            *y = (int16_t)candidate_y;
            return;
        }
    }

    sample_mask(engine, false, x, y);
}

static void set_particle_target(particle_engine_t* engine, particle_t* particle, size_t index) {
    particle->colon = (index % PARTICLE_COLON_SHARE) == 0U;
    sample_mask(engine, particle->colon, &particle->target_x, &particle->target_y);
}

static void set_flame_velocity(particle_engine_t* engine, particle_t* particle) {
    particle->velocity_x = (int16_t)(random_range(engine, -8, 8) * PARTICLE_POSITION_ONE);
    particle->velocity_y = (int16_t)(-random_range(engine, 14, 42) * PARTICLE_POSITION_ONE);
    particle->phase = (uint8_t)particle_random(engine);
    particle->phase_step = (uint8_t)random_range(engine, 2, 6);
}

static void reset_flame_particle(particle_engine_t* engine, particle_t* particle, size_t index, bool spread_age) {
    set_particle_target(engine, particle, index);
    particle->life_ms = (uint16_t)random_range(engine, 750, 1450);
    particle->age_ms = spread_age ? (uint16_t)(particle_random(engine) % particle->life_ms) : 0;
    set_flame_velocity(engine, particle);

    particle->x = particle->target_x * PARTICLE_POSITION_ONE + random_range(engine, -2, 2) * PARTICLE_POSITION_ONE;
    particle->y = particle->target_y * PARTICLE_POSITION_ONE;
    if (spread_age) {
        particle->x += (int32_t)particle->velocity_x * particle->age_ms / 1000;
        particle->y += (int32_t)particle->velocity_y * particle->age_ms / 1000;
    }
}

particle_engine_t* particle_engine_create(uint16_t width, uint16_t height) {
    if (width == 0 || height == 0 || width > INT16_MAX || height > INT16_MAX) {
        return NULL;
    }

    particle_engine_t* engine = heap_caps_calloc(1, sizeof(*engine), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (engine == NULL) {
        return NULL;
    }
    engine->width = width;
    engine->height = height;
    engine->heat_width = (width + PARTICLE_HEAT_CELL_SIZE - 1U) / PARTICLE_HEAT_CELL_SIZE;
    engine->heat_height = (height + PARTICLE_HEAT_CELL_SIZE - 1U) / PARTICLE_HEAT_CELL_SIZE;
    engine->heat = heap_caps_calloc((size_t)engine->heat_width * engine->heat_height, sizeof(*engine->heat), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (engine->heat == NULL) {
        heap_caps_free(engine);
        return NULL;
    }
    engine->random_state = 0x6d2b79f5U;
    return engine;
}

void particle_engine_destroy(particle_engine_t* engine) {
    if (engine == NULL) {
        return;
    }
    heap_caps_free(engine->heat);
    heap_caps_free(engine);
}

void particle_engine_set_mask(particle_engine_t* engine, const clock_mask_t* mask, bool transition) {
    if (engine == NULL || !mask_bounds_valid(mask, false)) {
        return;
    }

    engine->mask = *mask;
    engine->mask_ready = true;
    engine->colon_flare = 255;

    if (!transition) {
        engine->mode = PARTICLE_MODE_FLAME;
        engine->transition_elapsed_ms = 0;
        for (size_t index = 0; index < PARTICLE_COUNT; index++) {
            reset_flame_particle(engine, &engine->particles[index], index, true);
        }
        return;
    }

    engine->mode = PARTICLE_MODE_DISINTEGRATE;
    engine->transition_elapsed_ms = 0;
    for (size_t index = 0; index < PARTICLE_COUNT; index++) {
        particle_t* particle = &engine->particles[index];
        set_particle_target(engine, particle, index);
        particle->velocity_x = (int16_t)(random_range(engine, -75, 75) * PARTICLE_POSITION_ONE);
        particle->velocity_y = (int16_t)(random_range(engine, -35, 12) * PARTICLE_POSITION_ONE);
    }
}

void particle_engine_pulse_colon(particle_engine_t* engine) {
    if (engine != NULL) {
        engine->colon_flare = 255;
    }
}

static void update_flame(particle_engine_t* engine, particle_t* particle, size_t index, uint32_t elapsed_ms) {
    const int32_t elapsed = (int32_t)elapsed_ms;
    particle->age_ms = (uint16_t)(particle->age_ms + elapsed_ms);
    if (particle->age_ms >= particle->life_ms || particle->y < -8 * PARTICLE_POSITION_ONE) {
        reset_flame_particle(engine, particle, index, false);
        return;
    }

    particle->phase = (uint8_t)(particle->phase + particle->phase_step);
    const int16_t turbulence = particle->phase < 128U ? (int16_t)particle->phase - 64 : 191 - (int16_t)particle->phase;
    const int32_t target_x = particle->target_x * PARTICLE_POSITION_ONE;
    particle->velocity_x += (int16_t)(((target_x - particle->x) * (int32_t)elapsed_ms) / 8000);
    particle->velocity_x += (int16_t)(turbulence * (int32_t)elapsed_ms / 12);
    particle->velocity_x = (int16_t)((int32_t)particle->velocity_x * 245 / 256);

    particle->x += (int32_t)particle->velocity_x * elapsed / 1000;
    particle->y += (int32_t)particle->velocity_y * elapsed / 1000;
}

static void update_disintegrate(particle_engine_t* engine, particle_t* particle, uint32_t elapsed_ms) {
    const int32_t elapsed = (int32_t)elapsed_ms;
    particle->velocity_y += (int16_t)(150 * PARTICLE_POSITION_ONE * elapsed / 1000);
    particle->x += (int32_t)particle->velocity_x * elapsed / 1000;
    particle->y += (int32_t)particle->velocity_y * elapsed / 1000;

    if (particle->x < -16 * PARTICLE_POSITION_ONE || particle->x > (engine->width + 16) * PARTICLE_POSITION_ONE) {
        particle->velocity_x = (int16_t)-particle->velocity_x;
    }
}

static void update_attract(particle_engine_t* engine, particle_t* particle, uint32_t elapsed_ms) {
    const int32_t target_x = particle->target_x * PARTICLE_POSITION_ONE;
    const int32_t target_y = particle->target_y * PARTICLE_POSITION_ONE;
    particle->phase = (uint8_t)(particle->phase + particle->phase_step);
    const int16_t turbulence = particle->phase < 128U ? (int16_t)particle->phase - 64 : 191 - (int16_t)particle->phase;

    particle->x += (target_x - particle->x) * (int32_t)elapsed_ms / 330;
    particle->y += (target_y - particle->y) * (int32_t)elapsed_ms / 330;
    particle->x += turbulence * (int32_t)elapsed_ms / 10;
    (void)engine;
}

static void spark_emit(particle_engine_t* engine) {
    const size_t first = particle_random(engine) % SPARK_COUNT;
    for (size_t offset = 0; offset < SPARK_COUNT; offset++) {
        flame_spark_t* spark = &engine->sparks[(first + offset) % SPARK_COUNT];
        if (spark->active) {
            continue;
        }

        int16_t x;
        int16_t y;
        sample_mask_upper_edge(engine, &x, &y);
        *spark = (flame_spark_t){
            .x = x * PARTICLE_POSITION_ONE,
            .y = y * PARTICLE_POSITION_ONE,
            .start_y = y * PARTICLE_POSITION_ONE,
            .velocity_x = (int16_t)(random_range(engine, -8, 8) * PARTICLE_POSITION_ONE),
            .velocity_y = (int16_t)(-random_range(engine, 75, 105) * PARTICLE_POSITION_ONE),
            .life_ms = (uint16_t)random_range(engine, 900, 1400),
            .active = true,
        };
        return;
    }
}

static void update_sparks(particle_engine_t* engine, uint32_t elapsed_ms) {
    const int32_t elapsed = (int32_t)elapsed_ms;
    for (size_t index = 0; index < SPARK_COUNT; index++) {
        flame_spark_t* spark = &engine->sparks[index];
        if (!spark->active) {
            continue;
        }

        spark->age_ms = (uint16_t)(spark->age_ms + elapsed_ms);
        if (spark->age_ms >= spark->life_ms || spark->y < -(int32_t)PARTICLE_HEAT_CELL_SIZE * PARTICLE_POSITION_ONE) {
            spark->active = false;
            continue;
        }
        spark->x += (int32_t)spark->velocity_x * elapsed / 1000;
        spark->y += (int32_t)spark->velocity_y * elapsed / 1000;
    }

    if (engine->mode != PARTICLE_MODE_FLAME) {
        engine->spark_emit_elapsed_ms = 0;
        return;
    }

    engine->spark_emit_elapsed_ms += elapsed_ms;
    while (engine->spark_emit_elapsed_ms >= SPARK_EMIT_INTERVAL_MS) {
        spark_emit(engine);
        engine->spark_emit_elapsed_ms -= SPARK_EMIT_INTERVAL_MS;
    }
}

void particle_engine_update(particle_engine_t* engine, uint32_t elapsed_ms) {
    if (engine == NULL || !engine->mask_ready) {
        return;
    }
    if (elapsed_ms > 80U) {
        elapsed_ms = 80U;
    }

    if (engine->colon_flare > elapsed_ms / 2U) {
        engine->colon_flare -= (uint8_t)(elapsed_ms / 2U);
    } else {
        engine->colon_flare = 0;
    }

    if (engine->mode != PARTICLE_MODE_FLAME) {
        engine->transition_elapsed_ms += elapsed_ms;
        if (engine->mode == PARTICLE_MODE_DISINTEGRATE && engine->transition_elapsed_ms >= PARTICLE_DISINTEGRATE_MS) {
            engine->mode = PARTICLE_MODE_ATTRACT;
        }
    }

    for (size_t index = 0; index < PARTICLE_COUNT; index++) {
        particle_t* particle = &engine->particles[index];
        if (engine->mode == PARTICLE_MODE_FLAME) {
            update_flame(engine, particle, index, elapsed_ms);
        } else if (engine->mode == PARTICLE_MODE_DISINTEGRATE) {
            update_disintegrate(engine, particle, elapsed_ms);
        } else {
            update_attract(engine, particle, elapsed_ms);
        }
    }

    if (engine->mode == PARTICLE_MODE_ATTRACT && engine->transition_elapsed_ms >= PARTICLE_TRANSITION_MS) {
        engine->mode = PARTICLE_MODE_FLAME;
        for (size_t index = 0; index < PARTICLE_COUNT; index++) {
            particle_t* particle = &engine->particles[index];
            particle->age_ms = (uint16_t)(particle_random(engine) % 260U);
            particle->life_ms = (uint16_t)random_range(engine, 750, 1450);
            set_flame_velocity(engine, particle);
        }
    }
    update_sparks(engine, elapsed_ms);
}

static uint16_t flame_color(uint8_t level) {
    uint8_t red;
    uint8_t green;
    uint8_t blue;

    if (level < 85U) {
        red = level * 3U;
        green = level / 5U;
        blue = 0;
    } else if (level < 180U) {
        red = 255;
        green = (uint8_t)((level - 85U) * 2U);
        blue = (uint8_t)((level - 85U) / 8U);
    } else {
        red = 255;
        green = (uint8_t)(190U + (level - 180U) * 65U / 75U);
        blue = (uint8_t)(12U + (level - 180U) * 185U / 75U);
    }

    return (uint16_t)(((uint16_t)(red >> 3) << 11) | ((uint16_t)(green >> 2) << 5) | (blue >> 3));
}

static uint8_t particle_level(const particle_engine_t* engine, const particle_t* particle) {
    uint32_t level;
    if (engine->mode == PARTICLE_MODE_DISINTEGRATE) {
        level = 210U - (engine->transition_elapsed_ms * 100U / PARTICLE_DISINTEGRATE_MS);
    } else if (engine->mode == PARTICLE_MODE_ATTRACT) {
        level = 130U + (engine->transition_elapsed_ms - PARTICLE_DISINTEGRATE_MS) * 110U / (PARTICLE_TRANSITION_MS - PARTICLE_DISINTEGRATE_MS);
    } else {
        const uint32_t progress = (uint32_t)particle->age_ms * 255U / particle->life_ms;
        if (progress < 42U) {
            level = 135U + progress * 120U / 42U;
        } else {
            level = 255U - (progress - 42U) * 180U / 213U;
        }
    }

    if (particle->colon && engine->colon_flare > 0U) {
        level += engine->colon_flare / 2U;
    }
    if (level > 255U) {
        level = 255U;
    }
    return (uint8_t)level;
}

static void heat_add(particle_engine_t* engine, int32_t x, int32_t y, uint8_t level) {
    if (x < 0 || y < 0 || x >= engine->width || y >= engine->height) {
        return;
    }

    uint8_t* heat = &engine->heat[(uint32_t)(y / PARTICLE_HEAT_CELL_SIZE) * engine->heat_width + (uint32_t)(x / PARTICLE_HEAT_CELL_SIZE)];
    if (level > *heat) {
        *heat = level;
    } else {
        const uint16_t combined = *heat + level / 24U;
        *heat = combined > UINT8_MAX ? UINT8_MAX : (uint8_t)combined;
    }
}

static void heat_decay(particle_engine_t* engine) {
    const size_t cell_count = (size_t)engine->heat_width * engine->heat_height;
    for (size_t index = 0; index < cell_count; index++) {
        const uint8_t heat = engine->heat[index];
        engine->heat[index] = heat > 18U ? heat - 18U : 0U;
    }
}

static void render_heat(const particle_engine_t* engine, void* pixels, uint32_t stride_bytes) {
    for (uint16_t heat_y = 0; heat_y < engine->heat_height; heat_y++) {
        for (uint16_t heat_x = 0; heat_x < engine->heat_width; heat_x++) {
            const uint8_t level = engine->heat[(uint32_t)heat_y * engine->heat_width + heat_x];
            if (level == 0U) {
                continue;
            }

            const uint16_t color = flame_color(level);
            const uint16_t x1 = heat_x * PARTICLE_HEAT_CELL_SIZE;
            const uint16_t y1 = heat_y * PARTICLE_HEAT_CELL_SIZE;
            const uint16_t x2 = (uint16_t)(x1 + PARTICLE_HEAT_CELL_SIZE) < engine->width ? x1 + PARTICLE_HEAT_CELL_SIZE : engine->width;
            const uint16_t y2 = (uint16_t)(y1 + PARTICLE_HEAT_CELL_SIZE) < engine->height ? y1 + PARTICLE_HEAT_CELL_SIZE : engine->height;

            for (uint16_t y = y1; y < y2; y++) {
                uint16_t* row = (uint16_t*)((uint8_t*)pixels + (uint32_t)y * stride_bytes);
                for (uint16_t x = x1; x < x2; x++) {
                    row[x] = color;
                }
            }
        }
    }
}

static void render_spark(const particle_engine_t* engine, void* pixels, uint32_t stride_bytes, const flame_spark_t* spark) {
    const int32_t x1 = spark->x >> PARTICLE_POSITION_SHIFT;
    const int32_t y1 = spark->y >> PARTICLE_POSITION_SHIFT;
    const uint32_t progress = (uint32_t)spark->age_ms * 255U / spark->life_ms;
    const uint16_t color = flame_color((uint8_t)(230U - progress * 80U / 255U));

    for (int32_t y = y1; y < y1 + PARTICLE_HEAT_CELL_SIZE; y++) {
        if (y < 0 || y >= engine->height) {
            continue;
        }
        uint16_t* row = (uint16_t*)((uint8_t*)pixels + (uint32_t)y * stride_bytes);
        for (int32_t x = x1; x < x1 + PARTICLE_HEAT_CELL_SIZE; x++) {
            if (x >= 0 && x < engine->width) {
                row[x] = color;
            }
        }
    }
}

void particle_engine_render_rgb565(particle_engine_t* engine, void* pixels, uint32_t stride_bytes) {
    if (engine == NULL || pixels == NULL || !engine->mask_ready || stride_bytes < engine->width * sizeof(uint16_t)) {
        return;
    }

    memset(pixels, 0, (size_t)stride_bytes * engine->height);
    heat_decay(engine);
    for (size_t index = 0; index < PARTICLE_COUNT; index++) {
        const particle_t* particle = &engine->particles[index];
        const int32_t x = particle->x >> PARTICLE_POSITION_SHIFT;
        const int32_t y = particle->y >> PARTICLE_POSITION_SHIFT;
        const uint8_t level = particle_level(engine, particle);
        heat_add(engine, x, y, level);
        if (level > 150U) {
            heat_add(engine, x - PARTICLE_HEAT_CELL_SIZE, y, level / 3U);
            heat_add(engine, x + PARTICLE_HEAT_CELL_SIZE, y, level / 3U);
        }
    }
    render_heat(engine, pixels, stride_bytes);
    for (size_t index = 0; index < SPARK_COUNT; index++) {
        const flame_spark_t* spark = &engine->sparks[index];
        if (spark->active) {
            render_spark(engine, pixels, stride_bytes, spark);
        }
    }
}

size_t particle_engine_count(const particle_engine_t* engine) {
    return engine == NULL ? 0 : PARTICLE_COUNT;
}

size_t particle_engine_spark_count(const particle_engine_t* engine) {
    if (engine == NULL) {
        return 0;
    }

    size_t count = 0;
    for (size_t index = 0; index < SPARK_COUNT; index++) {
        if (engine->sparks[index].active) {
            count++;
        }
    }
    return count;
}

int32_t particle_engine_spark_max_rise(const particle_engine_t* engine) {
    if (engine == NULL) {
        return 0;
    }

    int32_t maximum = 0;
    for (size_t index = 0; index < SPARK_COUNT; index++) {
        const flame_spark_t* spark = &engine->sparks[index];
        if (spark->active) {
            const int32_t rise = (spark->start_y - spark->y) / PARTICLE_POSITION_ONE;
            if (rise > maximum) {
                maximum = rise;
            }
        }
    }
    return maximum;
}

size_t particle_engine_memory_size(const particle_engine_t* engine) {
    if (engine == NULL) {
        return 0;
    }
    return sizeof(*engine) + (size_t)engine->heat_width * engine->heat_height;
}
