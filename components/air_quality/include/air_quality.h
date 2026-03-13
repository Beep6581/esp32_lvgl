#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // "Primary" values used by the UI.
    // If SCD4x is present, these come from SCD4x.
    // If SCD4x is absent and STCC4 is present, these may come from STCC4.
    bool has_co2;
    bool has_rht;

    uint16_t co2_ppm;
    int32_t temperature_m_deg_c;      // milli-degrees Celsius
    int32_t humidity_m_percent_rh;    // milli-%RH

    // Timestamp (ms since boot) when we last updated the primary values.
    uint32_t last_co2_ms;

    // Optional: raw STCC4 values (if present).
    bool has_stcc4;

    uint16_t stcc4_co2_ppm;
    int32_t stcc4_temperature_m_deg_c;      // milli-degrees Celsius
    int32_t stcc4_humidity_m_percent_rh;    // milli-%RH

    // Timestamp (ms since boot) when we last updated from STCC4.
    uint32_t last_stcc4_ms;
} air_quality_data_t;

esp_err_t air_quality_start(void);

air_quality_data_t air_quality_get_latest(void);

uint8_t air_quality_get_scd4x_addr(void);

uint8_t air_quality_get_stcc4_addr(void);

#ifdef __cplusplus
}
#endif
