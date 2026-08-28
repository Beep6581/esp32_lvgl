#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AIR_QUALITY_SOURCE_SCD41 = 0,
    AIR_QUALITY_SOURCE_SHT20,
    AIR_QUALITY_SOURCE_XY_MD0X,
    AIR_QUALITY_SOURCE_COUNT,
} air_quality_source_t;

typedef enum {
    AIR_QUALITY_METRIC_TEMPERATURE = 0,
    AIR_QUALITY_METRIC_HUMIDITY,
    AIR_QUALITY_METRIC_CO2,
    AIR_QUALITY_METRIC_COUNT,
} air_quality_metric_t;

typedef struct {
    bool supported;
    bool valid;
    int32_t value; // Temperature and RH use milli-units; CO2 uses ppm.
} air_quality_metric_data_t;

typedef struct {
    bool configured;
    bool online;
    uint32_t last_update_ms;
    air_quality_metric_data_t metric[AIR_QUALITY_METRIC_COUNT];
} air_quality_source_data_t;

typedef struct {
    uint32_t timestamp_ms;
    air_quality_source_data_t source[AIR_QUALITY_SOURCE_COUNT];
} air_quality_snapshot_t;

/** Start the air quality sampling task. */
esp_err_t air_quality_start(void);

/** Get latest cached values (thread-safe copy). */
air_quality_snapshot_t air_quality_get_latest(void);

#ifdef __cplusplus
}
#endif
