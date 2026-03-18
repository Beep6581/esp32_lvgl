#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // SCD41 (SCD4x family) at 0x62
    bool scd41_detected;
    bool scd41_asc_enabled;
    bool scd41_has_co2;
    bool scd41_has_rht;
    uint16_t scd41_co2_ppm;
    int32_t scd41_temperature_m_deg_c;      // milli-degrees Celsius
    int32_t scd41_humidity_m_percent_rh;    // milli-%RH
    uint32_t scd41_last_ms;

    // STCC4 at 0x64 (paired with SHT4x via SDA_C/SCL_C on the same PCB)
    bool stcc4_detected;
    bool stcc4_has_co2;
    bool stcc4_has_rht;
    uint16_t stcc4_co2_ppm;
    int32_t stcc4_temperature_m_deg_c;      // milli-degrees Celsius
    int32_t stcc4_humidity_m_percent_rh;    // milli-%RH
    uint32_t stcc4_last_ms;

    // SHT20 at 0x40 (temperature + humidity only)
    bool sht20_detected;
    bool sht20_has_rht;
    int32_t sht20_temperature_m_deg_c;      // milli-degrees Celsius
    int32_t sht20_humidity_m_percent_rh;    // milli-%RH
    uint32_t sht20_last_ms;
} air_quality_data_t;

/** Start the air quality sampling task. */
esp_err_t air_quality_start(void);

/** Get latest cached values (thread-safe copy). */
air_quality_data_t air_quality_get_latest(void);

#ifdef __cplusplus
}
#endif
