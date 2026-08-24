#include "air_quality.h"

#include <stddef.h>

#include "i2c_bus.h"
#include "scd4x.h"
#include "sht20.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char* TAG = "air_quality";

#define SCD41_ADDR 0x62
#define SHT20_ADDR 0x40

typedef enum {
    SENSOR_SM_PROBE = 0,
    SENSOR_SM_IDENTIFY,
    SENSOR_SM_START,
    SENSOR_SM_WAIT_FIRST,
    SENSOR_SM_RUN,
} sensor_sm_t;

static TaskHandle_t s_task = NULL;
static SemaphoreHandle_t s_lock = NULL;
static air_quality_snapshot_t s_latest = {0};

static sensor_sm_t s_scd41_sm = SENSOR_SM_PROBE;
static sensor_sm_t s_sht20_sm = SENSOR_SM_PROBE;

static uint32_t s_scd41_ready_fail_count = 0;
static uint32_t s_scd41_read_fail_count = 0;
static uint32_t s_scd41_last_sample_ms = 0;

static void set_latest(const air_quality_snapshot_t* d) {
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

static air_quality_snapshot_t get_latest_no_lock(void) {
    return s_latest;
}

static void init_snapshot(air_quality_snapshot_t* d) {
    if (d == NULL) {
        return;
    }

    *d = (air_quality_snapshot_t){0};

    air_quality_source_data_t* scd41 = &d->source[AIR_QUALITY_SOURCE_SCD41];
    scd41->configured = true;
    scd41->metric[AIR_QUALITY_METRIC_TEMPERATURE].supported = true;
    scd41->metric[AIR_QUALITY_METRIC_HUMIDITY].supported = true;
    scd41->metric[AIR_QUALITY_METRIC_CO2].supported = true;

    air_quality_source_data_t* sht20 = &d->source[AIR_QUALITY_SOURCE_SHT20];
    sht20->configured = true;
    sht20->metric[AIR_QUALITY_METRIC_TEMPERATURE].supported = true;
    sht20->metric[AIR_QUALITY_METRIC_HUMIDITY].supported = true;
}

static void clear_latest(void) {
    air_quality_snapshot_t d;
    init_snapshot(&d);
    set_latest(&d);
}

static bool i2c_addr_present(uint8_t addr_7bit) {
    return (i2c_bus_probe(addr_7bit, 50) == ESP_OK);
}

static void reset_scd41(air_quality_snapshot_t* d) {
    s_scd41_sm = SENSOR_SM_PROBE;
    s_scd41_ready_fail_count = 0;
    s_scd41_read_fail_count = 0;
    s_scd41_last_sample_ms = 0;
    if (d) {
        air_quality_source_data_t* source = &d->source[AIR_QUALITY_SOURCE_SCD41];
        source->online = false;
        source->last_update_ms = 0;
        source->metric[AIR_QUALITY_METRIC_TEMPERATURE].valid = false;
        source->metric[AIR_QUALITY_METRIC_HUMIDITY].valid = false;
        source->metric[AIR_QUALITY_METRIC_CO2].valid = false;
    }
}

static void reset_sht20(air_quality_snapshot_t* d) {
    s_sht20_sm = SENSOR_SM_PROBE;
    if (d) {
        air_quality_source_data_t* source = &d->source[AIR_QUALITY_SOURCE_SHT20];
        source->online = false;
        source->last_update_ms = 0;
        source->metric[AIR_QUALITY_METRIC_TEMPERATURE].valid = false;
        source->metric[AIR_QUALITY_METRIC_HUMIDITY].valid = false;
    }
}

static void run_scd41_sm(air_quality_snapshot_t* d, bool present, uint32_t now_ms) {
    if (d == NULL) {
        return;
    }

    air_quality_source_data_t* source = &d->source[AIR_QUALITY_SOURCE_SCD41];
    source->online = present;
    if (!present) {
        reset_scd41(d);
        return;
    }

    for (;;) {
        switch (s_scd41_sm) {
        case SENSOR_SM_PROBE:
            s_scd41_sm = SENSOR_SM_IDENTIFY;
            continue;

        case SENSOR_SM_IDENTIFY: {
            const esp_err_t err = scd4x_esp_init(SCD41_ADDR);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "SCD41 init failed (%s)", esp_err_to_name(err));
                return;
            }

            uint16_t serial[3] = {0};
            const esp_err_t ser_err = scd4x_esp_get_serial(serial);
            if (ser_err != ESP_OK) {
                ESP_LOGW(TAG, "SCD41 get serial failed (%s)", esp_err_to_name(ser_err));
                return;
            }

            ESP_LOGI(TAG, "SCD41 detected at 0x%02X serial=%04X-%04X-%04X", (unsigned)SCD41_ADDR, (unsigned)serial[0], (unsigned)serial[1], (unsigned)serial[2]);
            s_scd41_sm = SENSOR_SM_START;
            continue;
        }

        case SENSOR_SM_START: {
            const esp_err_t asc_set_err = scd4x_esp_set_asc_enabled(true);
            if (asc_set_err != ESP_OK) {
                ESP_LOGW(TAG, "SCD41 enable ASC failed (%s)", esp_err_to_name(asc_set_err));
            }

            const esp_err_t err = scd4x_esp_start_periodic();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "SCD41 start periodic failed (%s)", esp_err_to_name(err));
                s_scd41_sm = SENSOR_SM_IDENTIFY;
                return;
            }

            ESP_LOGI(TAG, "Initialized SCD41 at 0x%02X", (unsigned)SCD41_ADDR);
            s_scd41_sm = SENSOR_SM_RUN;
            continue;
        }

        case SENSOR_SM_RUN: {
            bool ready = false;
            const esp_err_t ready_err = scd4x_esp_get_data_ready(&ready);
            if (ready_err != ESP_OK) {
                s_scd41_ready_fail_count++;
                const uint32_t age_ms = (s_scd41_last_sample_ms == 0U) ? 0U : (now_ms - s_scd41_last_sample_ms);

                ESP_LOGW(TAG, "SCD41 get_data_ready failed (%s), consecutive=%lu, last_sample_age_ms=%lu", esp_err_to_name(ready_err), (unsigned long)s_scd41_ready_fail_count, (unsigned long)age_ms);

                if (s_scd41_ready_fail_count < 3U) {
                    return;
                }

                ESP_LOGW(TAG, "SCD41 restarting after %lu consecutive get_data_ready failures", (unsigned long)s_scd41_ready_fail_count);

                s_scd41_ready_fail_count = 0;
                s_scd41_sm = SENSOR_SM_IDENTIFY;
                source->metric[AIR_QUALITY_METRIC_TEMPERATURE].valid = false;
                source->metric[AIR_QUALITY_METRIC_HUMIDITY].valid = false;
                source->metric[AIR_QUALITY_METRIC_CO2].valid = false;
                source->last_update_ms = 0;
                return;
            }

            s_scd41_ready_fail_count = 0;

            if (!ready) {
                if (s_scd41_last_sample_ms != 0U && (now_ms - s_scd41_last_sample_ms) > 15000U) {
                    ESP_LOGW(TAG, "SCD41 not ready for %lu ms", (unsigned long)(now_ms - s_scd41_last_sample_ms));
                }
                return;
            }

            scd4x_sample_t s = {0};
            const esp_err_t read_err = scd4x_esp_read_measurement(&s);
            if (read_err != ESP_OK) {
                s_scd41_read_fail_count++;
                ESP_LOGW(TAG, "SCD41 read failed (%s), consecutive=%lu", esp_err_to_name(read_err), (unsigned long)s_scd41_read_fail_count);

                if (s_scd41_read_fail_count < 3U) {
                    return;
                }

                ESP_LOGW(TAG, "SCD41 restarting after %lu consecutive read failures", (unsigned long)s_scd41_read_fail_count);

                s_scd41_read_fail_count = 0;
                s_scd41_sm = SENSOR_SM_IDENTIFY;
                source->metric[AIR_QUALITY_METRIC_TEMPERATURE].valid = false;
                source->metric[AIR_QUALITY_METRIC_HUMIDITY].valid = false;
                source->metric[AIR_QUALITY_METRIC_CO2].valid = false;
                source->last_update_ms = 0;
                return;
            }

            source->metric[AIR_QUALITY_METRIC_TEMPERATURE].valid = true;
            source->metric[AIR_QUALITY_METRIC_TEMPERATURE].value = s.temperature_m_deg_c;
            source->metric[AIR_QUALITY_METRIC_HUMIDITY].valid = true;
            source->metric[AIR_QUALITY_METRIC_HUMIDITY].value = s.humidity_m_percent_rh;
            source->metric[AIR_QUALITY_METRIC_CO2].valid = true;
            source->metric[AIR_QUALITY_METRIC_CO2].value = s.co2_ppm;
            source->last_update_ms = now_ms;

            ESP_LOGI(TAG, "SCD41 measurement T_mC=%ld RH_mpercent=%ld CO2_ppm=%u", (long)s.temperature_m_deg_c, (long)s.humidity_m_percent_rh, (unsigned)s.co2_ppm);

            s_scd41_ready_fail_count = 0;
            s_scd41_read_fail_count = 0;
            s_scd41_last_sample_ms = now_ms;

            return;
        }

        case SENSOR_SM_WAIT_FIRST:
        default:
            s_scd41_sm = SENSOR_SM_IDENTIFY;
            continue;
        }
    }
}

static void run_sht20_sm(air_quality_snapshot_t* d, bool present, uint32_t now_ms) {
    if (d == NULL) {
        return;
    }

    air_quality_source_data_t* source = &d->source[AIR_QUALITY_SOURCE_SHT20];
    source->online = present;
    if (!present) {
        reset_sht20(d);
        return;
    }

    for (;;) {
        switch (s_sht20_sm) {
        case SENSOR_SM_PROBE:
            s_sht20_sm = SENSOR_SM_IDENTIFY;
            continue;

        case SENSOR_SM_IDENTIFY: {
            const esp_err_t init_err = sht20_init(SHT20_ADDR);
            if (init_err != ESP_OK) {
                ESP_LOGW(TAG, "SHT20 init failed (%s)", esp_err_to_name(init_err));
                return;
            }

            sht20_identity_t id = {0};
            const esp_err_t id_err = sht20_get_identity(&id);
            if (id_err != ESP_OK) {
                ESP_LOGW(TAG, "SHT20 get electronic ID failed (%s)", esp_err_to_name(id_err));
                return;
            }

            ESP_LOGI(TAG, "SHT20 detected at 0x%02X otp=%02X%02X%02X%02X%02X%02X%02X%02X metal=%04X-%04X-%04X", (unsigned)SHT20_ADDR, (unsigned)id.otp_bytes[0], (unsigned)id.otp_bytes[1],
                     (unsigned)id.otp_bytes[2], (unsigned)id.otp_bytes[3], (unsigned)id.otp_bytes[4], (unsigned)id.otp_bytes[5], (unsigned)id.otp_bytes[6], (unsigned)id.otp_bytes[7],
                     (unsigned)id.metal_rom_words[0], (unsigned)id.metal_rom_words[1], (unsigned)id.metal_rom_words[2]);
            ESP_LOGI(TAG, "Initialized SHT20 at 0x%02X", (unsigned)SHT20_ADDR);
            s_sht20_sm = SENSOR_SM_RUN;
            continue;
        }

        case SENSOR_SM_RUN: {
            sht20_sample_t s = {0};
            const esp_err_t err = sht20_read_rht(&s);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "SHT20 read failed (%s)", esp_err_to_name(err));
                source->metric[AIR_QUALITY_METRIC_TEMPERATURE].valid = false;
                source->metric[AIR_QUALITY_METRIC_HUMIDITY].valid = false;
                source->last_update_ms = 0;
                s_sht20_sm = SENSOR_SM_IDENTIFY;
                return;
            }

            source->metric[AIR_QUALITY_METRIC_TEMPERATURE].valid = s.has_rht;
            source->metric[AIR_QUALITY_METRIC_TEMPERATURE].value = s.temperature_m_deg_c;
            source->metric[AIR_QUALITY_METRIC_HUMIDITY].valid = s.has_rht;
            source->metric[AIR_QUALITY_METRIC_HUMIDITY].value = s.humidity_m_percent_rh;
            source->last_update_ms = now_ms;

            ESP_LOGI(TAG, "SHT20 measurement T_mC=%ld RH_mpercent=%ld", (long)s.temperature_m_deg_c, (long)s.humidity_m_percent_rh);
            return;
        }

        case SENSOR_SM_START:
        case SENSOR_SM_WAIT_FIRST:
        default:
            s_sht20_sm = SENSOR_SM_IDENTIFY;
            continue;
        }
    }
}

static void air_quality_task(void* arg) {
    (void)arg;

    if (i2c_bus_init() != ESP_OK) {
        ESP_LOGE(TAG, "i2c_bus_init failed");
        vTaskDelete(NULL);
        return;
    }

    clear_latest();

    const bool scd41_present = i2c_addr_present(SCD41_ADDR);
    const bool sht20_present = i2c_addr_present(SHT20_ADDR);

    ESP_LOGI(TAG, "Sensors present: SCD41=%d SHT20=%d", scd41_present ? 1 : 0, sht20_present ? 1 : 0);

    for (;;) {
        air_quality_snapshot_t d = get_latest_no_lock();
        const uint32_t now_ms = (uint32_t)esp_log_timestamp();
        d.timestamp_ms = now_ms;

        run_scd41_sm(&d, scd41_present, now_ms);
        run_sht20_sm(&d, sht20_present, now_ms);

        set_latest(&d);
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

    const BaseType_t ok = xTaskCreate(air_quality_task, "air_quality", 4096, NULL, tskIDLE_PRIORITY + 2, &s_task);
    return (ok == pdPASS) ? ESP_OK : ESP_FAIL;
}

air_quality_snapshot_t air_quality_get_latest(void) {
    air_quality_snapshot_t d = {0};

    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        d = s_latest;
        xSemaphoreGive(s_lock);
    } else {
        d = s_latest;
    }

    return d;
}
