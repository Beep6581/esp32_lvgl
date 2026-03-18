#include "air_quality.h"

#include <stddef.h>

#include "i2c_bus.h"
#include "scd4x.h"
#include "stcc4.h"
#include "sht20.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char* TAG = "air_quality";

#define SCD41_ADDR 0x62
#define STCC4_ADDR 0x64
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
static air_quality_data_t s_latest = {0};

static sensor_sm_t s_scd41_sm = SENSOR_SM_PROBE;
static sensor_sm_t s_stcc4_sm = SENSOR_SM_PROBE;
static sensor_sm_t s_sht20_sm = SENSOR_SM_PROBE;
static uint32_t s_stcc4_wait_deadline_ms = 0;

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

static air_quality_data_t get_latest_no_lock(void) {
    return s_latest;
}

static void clear_latest(void) {
    air_quality_data_t d = {0};
    set_latest(&d);
}

static void log_i2c_scan_once(void) {
    uint8_t addrs[32] = {0};
    size_t count = 0;

    if (i2c_bus_scan(addrs, 32, &count) != ESP_OK) {
        ESP_LOGW(TAG, "I2C scan failed");
        return;
    }

    ESP_LOGI(TAG, "I2C scan: %u device(s) found", (unsigned)count);

    const size_t n = (count > 32) ? 32 : count;
    for (size_t i = 0; i < n; i++) {
        ESP_LOGI(TAG, "  probe OK: 0x%02X", (unsigned)addrs[i]);
    }
}

static bool i2c_addr_present(uint8_t addr_7bit) {
    return (i2c_bus_probe(addr_7bit, 50) == ESP_OK);
}

static void reset_scd41(air_quality_data_t* d) {
    s_scd41_sm = SENSOR_SM_PROBE;
    if (d) {
        d->scd41_detected = false;
        d->scd41_asc_enabled = false;
        d->scd41_has_co2 = false;
        d->scd41_has_rht = false;
        d->scd41_last_ms = 0;
    }
}

static void reset_stcc4(air_quality_data_t* d) {
    s_stcc4_sm = SENSOR_SM_PROBE;
    s_stcc4_wait_deadline_ms = 0;
    if (d) {
        d->stcc4_detected = false;
        d->stcc4_has_co2 = false;
        d->stcc4_has_rht = false;
        d->stcc4_last_ms = 0;
    }
}

static void reset_sht20(air_quality_data_t* d) {
    s_sht20_sm = SENSOR_SM_PROBE;
    if (d) {
        d->sht20_detected = false;
        d->sht20_has_rht = false;
        d->sht20_last_ms = 0;
    }
}

static void run_scd41_sm(air_quality_data_t* d, bool present, uint32_t now_ms) {
    (void)now_ms;
    if (d == NULL) {
        return;
    }

    d->scd41_detected = present;
    if (!present) {
        reset_scd41(d);
        return;
    }

    switch (s_scd41_sm) {
    case SENSOR_SM_PROBE:
        s_scd41_sm = SENSOR_SM_IDENTIFY;
        break;

    case SENSOR_SM_IDENTIFY: {
        const esp_err_t err = scd4x_esp_init(SCD41_ADDR);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SCD41 init failed (%s)", esp_err_to_name(err));
            break;
        }

        uint16_t serial[3] = {0};
        const esp_err_t ser_err = scd4x_esp_get_serial(serial);
        if (ser_err != ESP_OK) {
            ESP_LOGW(TAG, "SCD41 get serial failed (%s)", esp_err_to_name(ser_err));
            break;
        }

        ESP_LOGI(TAG, "SCD41 detected at 0x%02X serial=%04X-%04X-%04X",
                 (unsigned)SCD41_ADDR,
                 (unsigned)serial[0], (unsigned)serial[1], (unsigned)serial[2]);
        s_scd41_sm = SENSOR_SM_START;
        break;
    }

    case SENSOR_SM_START: {
        const esp_err_t asc_set_err = scd4x_esp_set_asc_enabled(true);
        if (asc_set_err != ESP_OK) {
            ESP_LOGW(TAG, "SCD41 enable ASC failed (%s)", esp_err_to_name(asc_set_err));
        }

        bool asc_enabled = false;
        const esp_err_t asc_get_err = scd4x_esp_get_asc_enabled(&asc_enabled);
        if (asc_get_err != ESP_OK) {
            ESP_LOGW(TAG, "SCD41 get ASC failed (%s)", esp_err_to_name(asc_get_err));
            asc_enabled = false;
        }
        d->scd41_asc_enabled = asc_enabled;

        const esp_err_t err = scd4x_esp_start_periodic();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SCD41 start periodic failed (%s)", esp_err_to_name(err));
            s_scd41_sm = SENSOR_SM_IDENTIFY;
            break;
        }

        ESP_LOGI(TAG, "Initialized SCD41 at 0x%02X", (unsigned)SCD41_ADDR);
        s_scd41_sm = SENSOR_SM_RUN;
        break;
    }

    case SENSOR_SM_RUN: {
        bool ready = false;
        const esp_err_t ready_err = scd4x_esp_get_data_ready(&ready);
        if (ready_err != ESP_OK) {
            ESP_LOGW(TAG, "SCD41 get_data_ready failed (%s)", esp_err_to_name(ready_err));
            s_scd41_sm = SENSOR_SM_IDENTIFY;
            d->scd41_has_co2 = false;
            d->scd41_has_rht = false;
            d->scd41_last_ms = 0;
            break;
        }

        if (!ready) {
            break;
        }

        scd4x_sample_t s = {0};
        const esp_err_t read_err = scd4x_esp_read_measurement(&s);
        if (read_err != ESP_OK) {
            ESP_LOGW(TAG, "SCD41 read failed (%s)", esp_err_to_name(read_err));
            s_scd41_sm = SENSOR_SM_IDENTIFY;
            d->scd41_has_co2 = false;
            d->scd41_has_rht = false;
            d->scd41_last_ms = 0;
            break;
        }

        d->scd41_has_co2 = true;
        d->scd41_has_rht = true;
        d->scd41_co2_ppm = s.co2_ppm;
        d->scd41_temperature_m_deg_c = s.temperature_m_deg_c;
        d->scd41_humidity_m_percent_rh = s.humidity_m_percent_rh;
        d->scd41_last_ms = now_ms;
        break;
    }

    case SENSOR_SM_WAIT_FIRST:
    default:
        s_scd41_sm = SENSOR_SM_IDENTIFY;
        break;
    }
}

static void run_stcc4_sm(air_quality_data_t* d, bool present, uint32_t now_ms) {
    if (d == NULL) {
        return;
    }

    d->stcc4_detected = present;
    if (!present) {
        reset_stcc4(d);
        return;
    }

    switch (s_stcc4_sm) {
    case SENSOR_SM_PROBE:
        s_stcc4_sm = SENSOR_SM_IDENTIFY;
        break;

    case SENSOR_SM_IDENTIFY: {
        const esp_err_t init_err = stcc4_init(STCC4_ADDR);
        if (init_err != ESP_OK) {
            ESP_LOGW(TAG, "STCC4 init failed (%s)", esp_err_to_name(init_err));
            break;
        }

        stcc4_identity_t id = {0};
        const esp_err_t id_err = stcc4_get_identity(&id);
        if (id_err == ESP_OK) {
            ESP_LOGI(TAG,
                     "STCC4 detected at 0x%02X product_id=%08lX serial=%04X-%04X-%04X-%04X",
                     (unsigned)STCC4_ADDR,
                     (unsigned long)id.product_id,
                     (unsigned)id.serial_words[0],
                     (unsigned)id.serial_words[1],
                     (unsigned)id.serial_words[2],
                     (unsigned)id.serial_words[3]);
            s_stcc4_sm = SENSOR_SM_START;
            break;
        }

        ESP_LOGW(TAG, "STCC4 get_product_id failed (%s), trying stop+retry",
                 esp_err_to_name(id_err));
        const esp_err_t stop_err = stcc4_stop_continuous_measurement();
        if (stop_err != ESP_OK) {
            ESP_LOGW(TAG, "STCC4 stop continuous failed (%s)", esp_err_to_name(stop_err));
        }
        s_stcc4_sm = SENSOR_SM_IDENTIFY;
        break;
    }

    case SENSOR_SM_START: {
        const esp_err_t err = stcc4_start_continuous_measurement();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "STCC4 start continuous failed (%s), trying stop+retry",
                     esp_err_to_name(err));
            const esp_err_t stop_err = stcc4_stop_continuous_measurement();
            if (stop_err != ESP_OK) {
                ESP_LOGW(TAG, "STCC4 stop continuous failed (%s)", esp_err_to_name(stop_err));
            }
            s_stcc4_sm = SENSOR_SM_IDENTIFY;
            break;
        }

        s_stcc4_wait_deadline_ms = now_ms + 1000;
        s_stcc4_sm = SENSOR_SM_WAIT_FIRST;
        break;
    }

    case SENSOR_SM_WAIT_FIRST:
        if (now_ms >= s_stcc4_wait_deadline_ms) {
            ESP_LOGI(TAG, "Initialized STCC4 at 0x%02X", (unsigned)STCC4_ADDR);
            s_stcc4_sm = SENSOR_SM_RUN;
        }
        break;

    case SENSOR_SM_RUN: {
        stcc4_sample_t s = {0};
        const esp_err_t err = stcc4_read_measurement(&s);
        if (err == ESP_OK) {
            d->stcc4_has_co2 = s.has_co2;
            d->stcc4_has_rht = s.has_rht;
            if (s.has_co2) {
                d->stcc4_co2_ppm = (uint16_t)s.co2_ppm;
            }
            if (s.has_rht) {
                d->stcc4_temperature_m_deg_c = s.temperature_m_deg_c;
                d->stcc4_humidity_m_percent_rh = s.humidity_m_percent_rh;
            }
            d->stcc4_last_ms = now_ms;
            break;
        }

        if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_TIMEOUT || err == ESP_ERR_INVALID_STATE) {
            break;
        }

        ESP_LOGW(TAG, "STCC4 read failed (%s), re-identifying", esp_err_to_name(err));
        d->stcc4_has_co2 = false;
        d->stcc4_has_rht = false;
        d->stcc4_last_ms = 0;
        s_stcc4_sm = SENSOR_SM_IDENTIFY;
        break;
    }

    default:
        s_stcc4_sm = SENSOR_SM_IDENTIFY;
        break;
    }
}

static void run_sht20_sm(air_quality_data_t* d, bool present, uint32_t now_ms) {
    if (d == NULL) {
        return;
    }

    d->sht20_detected = present;
    if (!present) {
        reset_sht20(d);
        return;
    }

    switch (s_sht20_sm) {
    case SENSOR_SM_PROBE:
        s_sht20_sm = SENSOR_SM_IDENTIFY;
        break;

    case SENSOR_SM_IDENTIFY: {
        const esp_err_t init_err = sht20_init(SHT20_ADDR);
        if (init_err != ESP_OK) {
            ESP_LOGW(TAG, "SHT20 init failed (%s)", esp_err_to_name(init_err));
            break;
        }

        sht20_identity_t id = {0};
        const esp_err_t id_err = sht20_get_identity(&id);
        if (id_err != ESP_OK) {
            ESP_LOGW(TAG, "SHT20 get electronic ID failed (%s)", esp_err_to_name(id_err));
            break;
        }

        ESP_LOGI(TAG,
                 "SHT20 detected at 0x%02X otp=%02X%02X%02X%02X%02X%02X%02X%02X metal=%04X-%04X-%04X",
                 (unsigned)SHT20_ADDR,
                 (unsigned)id.otp_bytes[0],
                 (unsigned)id.otp_bytes[1],
                 (unsigned)id.otp_bytes[2],
                 (unsigned)id.otp_bytes[3],
                 (unsigned)id.otp_bytes[4],
                 (unsigned)id.otp_bytes[5],
                 (unsigned)id.otp_bytes[6],
                 (unsigned)id.otp_bytes[7],
                 (unsigned)id.metal_rom_words[0],
                 (unsigned)id.metal_rom_words[1],
                 (unsigned)id.metal_rom_words[2]);
        ESP_LOGI(TAG, "Initialized SHT20 at 0x%02X", (unsigned)SHT20_ADDR);
        s_sht20_sm = SENSOR_SM_RUN;
        break;
    }

    case SENSOR_SM_RUN: {
        sht20_sample_t s = {0};
        const esp_err_t err = sht20_read_rht(&s);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SHT20 read failed (%s)", esp_err_to_name(err));
            d->sht20_has_rht = false;
            d->sht20_last_ms = 0;
            s_sht20_sm = SENSOR_SM_IDENTIFY;
            break;
        }

        d->sht20_has_rht = s.has_rht;
        d->sht20_temperature_m_deg_c = s.temperature_m_deg_c;
        d->sht20_humidity_m_percent_rh = s.humidity_m_percent_rh;
        d->sht20_last_ms = now_ms;
        break;
    }

    case SENSOR_SM_START:
    case SENSOR_SM_WAIT_FIRST:
    default:
        s_sht20_sm = SENSOR_SM_IDENTIFY;
        break;
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
    log_i2c_scan_once();

    uint32_t last_scan_ms = 0;

    for (;;) {
        air_quality_data_t d = get_latest_no_lock();
        const uint32_t now_ms = (uint32_t)esp_log_timestamp();

        const bool scd41_present = i2c_addr_present(SCD41_ADDR);
        const bool stcc4_present = i2c_addr_present(STCC4_ADDR);
        const bool sht20_present = i2c_addr_present(SHT20_ADDR);

        if (!scd41_present && !stcc4_present && !sht20_present) {
            if (last_scan_ms == 0 || (now_ms - last_scan_ms) > 5000) {
                last_scan_ms = now_ms;
                log_i2c_scan_once();
            }
        }

        run_scd41_sm(&d, scd41_present, now_ms);
        run_stcc4_sm(&d, stcc4_present, now_ms);
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
