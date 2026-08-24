#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t temperature_m_deg_c;
    int32_t humidity_m_percent_rh;
} xy_md0x_sample_t;

/** Initialize the XY-MD03/XY-MD04 Modbus RTU connection on RS485 CONN2. */
esp_err_t xy_md0x_init(uint8_t slave_address);

/** Read the temperature and humidity input registers. */
esp_err_t xy_md0x_read(xy_md0x_sample_t* out);

#ifdef __cplusplus
}
#endif
