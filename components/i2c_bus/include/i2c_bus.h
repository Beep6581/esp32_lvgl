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

bool i2c_bus_is_initialized(void);

i2c_master_bus_handle_t i2c_bus_get_handle(void);

esp_err_t i2c_bus_probe(uint8_t addr_7bit, int timeout_ms);

esp_err_t i2c_bus_scan(uint8_t* out_addrs, size_t max_addrs, size_t* out_count);

#ifdef __cplusplus
}
#endif
