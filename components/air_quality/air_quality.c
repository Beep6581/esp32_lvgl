#include "air_quality.h"

#include "i2c_bus.h"
#include "scd4x.h"
#include "stcc4.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char* TAG = "air_quality";

static TaskHandle_t s_task = NULL;
static SemaphoreHandle_t s_lock = NULL;

static air_quality_data_t s_latest = {0};

static uint8_t s_scd4x_addr = 0;
static uint8_t s_stcc4_addr = 0;
static uint32_t s_last_missing_log_ms = 0;



static void set_scd4x_addr(uint8_t addr) {
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_scd4x_addr = addr;
        xSemaphoreGive(s_lock);
    } else {
        s_scd4x_addr = addr;
    }
}

static uint8_t get_scd4x_addr(void) {
    uint8_t addr = 0;
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        addr = s_scd4x_addr;
        xSemaphoreGive(s_lock);
    } else {
        addr = s_scd4x_addr;
    }
    return addr;
}

static void set_stcc4_addr(uint8_t addr) {
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_stcc4_addr = addr;
        xSemaphoreGive(s_lock);
    } else {
        s_stcc4_addr = addr;
    }
}

static uint8_t get_stcc4_addr(void) {
    uint8_t addr = 0;
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        addr = s_stcc4_addr;
        xSemaphoreGive(s_lock);
    } else {
        addr = s_stcc4_addr;
    }
    return addr;
}

static void set_latest(const air_quality_data_t* d) {
    if (d == NULL) {
        return;
    }
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_latest = *d;
        xSemaphoreGive(s_lock);
    } else {
        s_latest = *d;
    }
}

static void clear_latest(void) {
    air_quality_data_t d = {0};
    set_latest(&d);
}

static void log_i2c_scan(void) {
    uint8_t found[16] = {0};
    size_t found_count = 0;

    const esp_err_t err = i2c_bus_scan(found, 16, &found_count);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C scan failed: %s", esp_err_to_name(err));
        return;
    }

    if (found_count == 0) {
        ESP_LOGI(TAG, "I2C scan: no devices found");
        return;
    }

    ESP_LOGI(TAG, "I2C scan: %u device(s) found", (unsigned)found_count);

    const size_t n = (found_count > 16) ? 16 : found_count;
    for (size_t i = 0; i < n; i++) {
        const uint8_t addr = found[i];
        ESP_LOGI(TAG, "  probe OK: 0x%02X", (unsigned)addr);
    }
}

static bool try_init_scd4x(void) {
    // SCD4x uses a fixed 7-bit address (0x62).
    // Only attempt init if the device ACKs a probe, to avoid spamming the bus with SCD4x commands.
    if (i2c_bus_probe(0x62, 50) != ESP_OK) {
        return false;
    }

    if (scd4x_esp_init(0x62) != ESP_OK) {
        return false;
    }

    // Use periodic mode; ASC is allowed in periodic mode.
    (void)scd4x_esp_set_asc_enabled(true);

    if (scd4x_esp_start_periodic() != ESP_OK) {
        return false;
    }

    return true;
}

static bool try_init_stcc4(void) {
    // STCC4 address selection:
    //  - ADDR = GND -> 0x64
    //  - ADDR = VDD -> 0x65
    //
    // On this PCB, ADDR is strapped to GND, so the address is fixed at 0x64.
    // Do not run self tests or compensation logic here; just start continuous measurement.
    if (i2c_bus_probe(0x64, 50) != ESP_OK) {
        return false;
    }

    if (stcc4_init(0x64) != ESP_OK) {
        return false;
    }

    const esp_err_t start_err = stcc4_start_continuous_measurement();
    if (start_err != ESP_OK) {
        ESP_LOGW(TAG, "STCC4 start_continuous_measurement failed (%s)", esp_err_to_name(start_err));
        return false;
    }

    // Recommended: wait 1 sec for the first sample.
    vTaskDelay(pdMS_TO_TICKS(1000));

    return true;
}


static void air_quality_task(void* arg) {
    (void)arg;

    if (i2c_bus_init() != ESP_OK) {
        ESP_LOGE(TAG, "i2c_bus_init failed");
        vTaskDelete(NULL);
        return;
    }

    // One scan at startup for visibility/debug.
    log_i2c_scan();

    for (;;) {
        // Attempt init if needed.
        if (get_scd4x_addr() == 0) {
            if (try_init_scd4x()) {
                set_scd4x_addr(0x62);
                ESP_LOGI(TAG, "SCD4x periodic measurement started");
            }
        }

        if (get_stcc4_addr() == 0) {
            if (try_init_stcc4()) {
                set_stcc4_addr(stcc4_addr());
                ESP_LOGI(TAG, "STCC4 continuous measurement started (addr=0x%02X)", (unsigned)stcc4_addr());
            }
        }

        // If nothing is detected yet, print a slow heartbeat so lack of logs is not confusing.
        const uint32_t now_ms = (uint32_t)esp_log_timestamp();
        if ((get_scd4x_addr() == 0) && (get_stcc4_addr() == 0)) {
            if ((now_ms - s_last_missing_log_ms) >= 3000) {
                const esp_err_t p62 = i2c_bus_probe(0x62, 50);
                const esp_err_t p64 = i2c_bus_probe(0x64, 50);
                ESP_LOGW(TAG, "No sensors detected yet (probe 0x62=%s probe 0x64=%s)",
                         esp_err_to_name(p62), esp_err_to_name(p64));
                log_i2c_scan();
                s_last_missing_log_ms = now_ms;
            }
        }

        // ---- SCD4x sampling (periodic, new data about every 5 seconds) ----
        if (get_scd4x_addr() != 0) {
            bool ready = false;
            const esp_err_t ready_err = scd4x_esp_get_data_ready(&ready);
            if (ready_err != ESP_OK) {
                ESP_LOGW(TAG, "SCD4x get_data_ready failed, will re-init");
                set_scd4x_addr(0);
            } else if (ready) {
                scd4x_sample_t s = {0};
                const esp_err_t read_err = scd4x_esp_read_measurement(&s);
                if (read_err == ESP_OK) {
                    air_quality_data_t d = air_quality_get_latest();

                    d.has_co2 = true;
                    d.has_rht = true;
                    d.co2_ppm = s.co2_ppm;
                    d.temperature_m_deg_c = s.temperature_m_deg_c;
                    d.humidity_m_percent_rh = s.humidity_m_percent_rh;
                    d.last_co2_ms = (uint32_t)esp_log_timestamp();

                    set_latest(&d);
                } else {
                    ESP_LOGW(TAG, "SCD4x read_measurement failed, will re-init");
                    set_scd4x_addr(0);
                }
            }
        }

        // ---- STCC4 sampling (continuous, 1 second interval) ----
        if (get_stcc4_addr() != 0) {
            stcc4_sample_t s = {0};
            const esp_err_t err = stcc4_read_measurement(&s);

            if (err == ESP_OK) {
                air_quality_data_t d = air_quality_get_latest();

                d.has_stcc4 = true;

                // CO2 is i16 in the datasheet. Clamp negatives to 0 for display/caching.
                const int32_t co2_ppm = (s.co2_ppm < 0) ? 0 : (int32_t)s.co2_ppm;

                d.stcc4_co2_ppm = (uint16_t)co2_ppm;
                d.stcc4_temperature_m_deg_c = s.temperature_m_deg_c;
                d.stcc4_humidity_m_percent_rh = s.humidity_m_percent_rh;
                d.last_stcc4_ms = (uint32_t)esp_log_timestamp();

                // If SCD4x is not present, use STCC4 as the primary source.
                if (get_scd4x_addr() == 0) {
                    d.has_co2 = true;
                    d.co2_ppm = d.stcc4_co2_ppm;
                    d.has_rht = s.has_rht;
                    d.temperature_m_deg_c = d.stcc4_temperature_m_deg_c;
                    d.humidity_m_percent_rh = d.stcc4_humidity_m_percent_rh;
                    d.last_co2_ms = d.last_stcc4_ms;
                }

                set_latest(&d);
            } else if (err != ESP_ERR_NOT_FOUND) {
                ESP_LOGW(TAG, "STCC4 read_measurement failed (%s), will re-init", esp_err_to_name(err));
                set_stcc4_addr(0);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t air_quality_start(void) {
    if (s_task) {
        return ESP_OK;
    }

    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    clear_latest();
    set_scd4x_addr(0);
    set_stcc4_addr(0);

    const BaseType_t ok = xTaskCreate(air_quality_task, "air_quality", 4096, NULL,
                                     tskIDLE_PRIORITY + 2, &s_task);
    return (ok == pdPASS) ? ESP_OK : ESP_FAIL;
}

air_quality_data_t air_quality_get_latest(void) {
    air_quality_data_t d = {0};

    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        d = s_latest;
        xSemaphoreGive(s_lock);
    } else {
        d = s_latest;
    }

    return d;
}

uint8_t air_quality_get_scd4x_addr(void) {
    return get_scd4x_addr();
}

uint8_t air_quality_get_stcc4_addr(void) {
    return get_stcc4_addr();
}
