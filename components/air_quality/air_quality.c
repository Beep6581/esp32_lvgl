#include "air_quality.h"

#include "i2c_bus.h"
#include "scd4x.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char* TAG = "air_quality";

static TaskHandle_t s_task = NULL;
static SemaphoreHandle_t s_lock = NULL;

static air_quality_data_t s_latest = {0};
static uint8_t s_scd4x_addr = 0;

static void set_addr(uint8_t addr) {
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_scd4x_addr = addr;
        xSemaphoreGive(s_lock);
    } else {
        s_scd4x_addr = addr;
    }
}

static uint8_t get_addr(void) {
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

static bool try_find_scd4x(uint8_t* out_addr) {
    if (out_addr == NULL) {
        return false;
    }

    uint8_t found[16] = {0};
    size_t found_count = 0;

    const esp_err_t err = i2c_bus_scan(found, 16, &found_count);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C scan failed: %s", esp_err_to_name(err));
        return false;
    }

    if (found_count == 0) {
        ESP_LOGI(TAG, "I2C scan: no devices found");
    } else {
        ESP_LOGI(TAG, "I2C scan: %u device(s) found", (unsigned)found_count);

        const size_t n = (found_count > 16) ? 16 : found_count;
        bool saw_62 = false;

        for (size_t i = 0; i < n; i++) {
            const uint8_t addr = found[i];
            ESP_LOGI(TAG, "  probe OK: 0x%02X", (unsigned)addr);
            if (addr == 0x62) {
                saw_62 = true;
            }
        }

        if (!saw_62) {
            ESP_LOGI(TAG, "I2C scan did not report 0x62; not probing other addresses to avoid disturbing other devices");
        }
    }

    // SCD4x uses a fixed 7-bit address (0x62). Try only that address.
    if (scd4x_esp_init(0x62) == ESP_OK) {
        *out_addr = 0x62;
        return true;
    }

    return false;
}

static void air_quality_task(void* arg) {
    (void)arg;

    if (i2c_bus_init() != ESP_OK) {
        ESP_LOGE(TAG, "i2c_bus_init failed");
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        // If we don't know the address yet, keep scanning.
        uint8_t addr = get_addr();
        if (addr == 0) {
            clear_latest();

            uint8_t detected = 0;
            if (try_find_scd4x(&detected)) {
                set_addr(detected);
                addr = detected;

                // Use periodic mode; ASC is allowed in periodic mode.
                (void)scd4x_esp_set_asc_enabled(true);

                const esp_err_t start_err = scd4x_esp_start_periodic();
                if (start_err != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to start periodic measurement");
                    set_addr(0);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    continue;
                }

                ESP_LOGI(TAG, "SCD4x periodic measurement started");
            } else {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
        }

        // 1 Hz sampling tick (UI also updates at 1 Hz).
        // In periodic mode, the SCD4x only produces new data every 5 seconds.
        bool ready = false;
        const esp_err_t ready_err = scd4x_esp_get_data_ready(&ready);
        if (ready_err != ESP_OK) {
            ESP_LOGW(TAG, "get_data_ready failed, will re-scan");
            set_addr(0);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (ready) {
            scd4x_sample_t s = {0};
            const esp_err_t read_err = scd4x_esp_read_measurement(&s);
            if (read_err == ESP_OK) {
                air_quality_data_t d = {0};
                d.has_co2 = true;
                d.has_rht = true;
                d.co2_ppm = s.co2_ppm;
                d.temperature_m_deg_c = s.temperature_m_deg_c;
                d.humidity_m_percent_rh = s.humidity_m_percent_rh;
                d.last_co2_ms = (uint32_t)esp_log_timestamp();
                set_latest(&d);

                ESP_LOGI(TAG, "CO2=%u ppm T=%0.2f C RH=%0.2f %%",
                         (unsigned)d.co2_ppm,
                         (double)d.temperature_m_deg_c / 1000.0,
                         (double)d.humidity_m_percent_rh / 1000.0);
            } else {
                // If we got here with ready==true but read failed, treat it as
                // a bus/device issue and fall back to scanning.
                ESP_LOGW(TAG, "read_measurement failed, will re-scan");
                set_addr(0);
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
    set_addr(0);

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
    return get_addr();
}
