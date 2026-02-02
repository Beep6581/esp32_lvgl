#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ntc_adc_init(void);
esp_err_t ntc_adc_read_raw(int *out_raw);

#ifdef __cplusplus
}
#endif
