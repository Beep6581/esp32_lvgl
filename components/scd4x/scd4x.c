#include "scd4x.h"

#include "i2c_bus.h"

#include "esp_log.h"

// Sensirion embedded driver
#include "scd4x_i2c.h"
#include "sensirion_common.h"
#include "sensirion_i2c_hal.h"

static const char* TAG = "scd4x";
