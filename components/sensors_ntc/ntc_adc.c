#include "ntc_adc.h"

#include "esp_err.h"
#include "esp_log.h"

#include "esp_adc/adc_oneshot.h"

static const char *TAG = "ntc_adc";

// WT32-S3: GPIO19 = ADC2_CH8
#define NTC_ADC_UNIT    ADC_UNIT_2
#define NTC_ADC_CH      ADC_CHANNEL_8

static adc_oneshot_unit_handle_t s_adc = NULL;
static bool s_inited = false;

esp_err_t ntc_adc_init(void)
{
    if (s_inited) return ESP_OK;

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = NTC_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_11,
    };

    err = adc_oneshot_config_channel(s_adc, NTC_ADC_CH, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    s_inited = true;
    ESP_LOGI(TAG, "NTC ADC init OK (GPIO19 => ADC2_CH8)");
    return ESP_OK;
}

esp_err_t ntc_adc_read_raw(int *out_raw)
{
    if (!out_raw) return ESP_ERR_INVALID_ARG;
    if (!s_inited) {
        esp_err_t err = ntc_adc_init();
        if (err != ESP_OK) return err;
    }

    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_adc, NTC_ADC_CH, &raw);
    if (err != ESP_OK) return err;

    *out_raw = raw;
    return ESP_OK;
}

