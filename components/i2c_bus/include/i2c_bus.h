#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t i2c_bus_init(void);

i2c_master_bus_handle_t i2c_bus_get_handle(void);

esp_err_t i2c_bus_probe(uint8_t addr_7bit, int timeout_ms);

#ifdef __cplusplus
}
#endif
