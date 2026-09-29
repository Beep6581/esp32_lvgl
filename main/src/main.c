#include "display.h"
#include "i2c_bus.h"
#include "sdkconfig.h"
#include "touch.h"
#include "ui.h"

#if CONFIG_APP_MODE_AIR_QUALITY
#include "air_quality.h"
#endif

#include "esp_log.h"

/*
// UART-485 pins
#define RS485_IO_RTS GPIO_NUM_NC
#define RS485_IO_RX GPIO_NUM_1
#define RS485_IO_TX GPIO_NUM_2

// USB pins
#define USB_IO_DP GPIO_NUM_20
#define USB_IO_DN GPIO_NUM_19

// SHT20 pins
#define SHT20_IO_I2C_SDA BOARD_IO_I2C_SDA
#define SHT20_IO_I2C_SCL BOARD_IO_I2C_SCL
*/

static const char* TAG = "main";

#if CONFIG_SPIRAM_MODE_OCT
#define APP_PSRAM_MODE "octal"
#elif CONFIG_SPIRAM_MODE_QUAD
#define APP_PSRAM_MODE "quad"
#else
#define APP_PSRAM_MODE "unknown"
#endif

#if CONFIG_SPIRAM_XIP_FROM_PSRAM
#define APP_PSRAM_XIP_ENABLED 1
#else
#define APP_PSRAM_XIP_ENABLED 0
#endif

void app_main(void) {
    ESP_LOGI(TAG,
             "Build config: LV_DEF_REFR_PERIOD=%d ms, LV_USE_SYSMON=%d, LV_USE_PERF_MONITOR=%d, PSRAM=%s/%d MHz, D-cache=%d KiB/%d B line, PSRAM XIP=%d",
             LV_DEF_REFR_PERIOD, LV_USE_SYSMON, LV_USE_PERF_MONITOR, APP_PSRAM_MODE, CONFIG_SPIRAM_SPEED, CONFIG_ESP32S3_DATA_CACHE_SIZE / 1024,
             CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE, APP_PSRAM_XIP_ENABLED);

    ESP_ERROR_CHECK(i2c_bus_init());

#if CONFIG_APP_MODE_AIR_QUALITY
    ESP_LOGI(TAG, "Starting air-quality mode");
    ESP_ERROR_CHECK(air_quality_start());
#else
    ESP_LOGI(TAG, "Starting screen-diagnostics mode");
#endif

    lv_display_t* disp = display_init();
    if (disp == NULL) {
        ESP_LOGE(TAG, "display_init failed");
        return;
    }

#if CONFIG_APP_MODE_AIR_QUALITY
    ui_init(disp);
#else
    ui_screen_diagnostics_init(disp);
    ESP_LOGI(TAG, "Screen diagnostics display ready");
#endif

    ESP_ERROR_CHECK(touch_start());
}
