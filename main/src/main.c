#include "display.h"
#include "i2c_bus.h"
#include "sdkconfig.h"
#include "touch.h"
#include "wifi_manager.h"

#if CONFIG_APP_MODE_AIR_QUALITY
#include "air_quality.h"
#include "ui.h"
#elif CONFIG_APP_MODE_SCREEN_DIAGNOSTICS
#include "system_ui.h"
#include "ui.h"
#elif CONFIG_APP_MODE_CLOCK
#include "clock.h"
#include "wifi_qr_ui.h"
#endif

#if CONFIG_APP_MODE_AIR_QUALITY
#include "system_ui.h"
#endif

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

    // Allocate the RGB DMA bounce buffers before Wi-Fi fragments internal RAM.
    ESP_ERROR_CHECK(display_prepare());

    bool provisioning_required = false;
    ESP_ERROR_CHECK(wifi_manager_prepare(&provisioning_required));

#if CONFIG_APP_MODE_CLOCK
    if (provisioning_required) {
        lv_display_t* provisioning_display = display_init();
        if (provisioning_display == NULL) {
            ESP_LOGE(TAG, "display_init failed during Wi-Fi provisioning");
            return;
        }

        ESP_ERROR_CHECK(wifi_manager_start());
        char dpp_uri[WIFI_MANAGER_DPP_URI_CAPACITY];
        ESP_ERROR_CHECK(wifi_manager_wait_for_dpp_uri(dpp_uri, sizeof(dpp_uri)));
        ESP_ERROR_CHECK(wifi_qr_ui_show(provisioning_display, dpp_uri));
        ESP_ERROR_CHECK(wifi_manager_wait_for_connection());
        ESP_ERROR_CHECK(wifi_manager_finish_provisioning());

        ESP_LOGI(TAG, "Wi-Fi provisioning complete; restarting into Clock mode");
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    }
#endif

#if !CONFIG_APP_MODE_CLOCK
    ESP_ERROR_CHECK(i2c_bus_init());
#endif

#if CONFIG_APP_MODE_AIR_QUALITY
    ESP_LOGI(TAG, "Starting air-quality mode");
    ESP_ERROR_CHECK(air_quality_start());
#elif CONFIG_APP_MODE_SCREEN_DIAGNOSTICS
    ESP_LOGI(TAG, "Starting screen-diagnostics mode");
#else
    ESP_LOGI(TAG, "Starting particle-clock mode");
#endif

#if CONFIG_APP_MODE_CLOCK
    esp_lcd_panel_handle_t panel = display_init_direct();
    if (panel == NULL) {
        ESP_LOGE(TAG, "display_init_direct failed");
        return;
    }
    ESP_ERROR_CHECK(clock_start(panel));
    ESP_ERROR_CHECK(display_backlight_on());
#else
    lv_display_t* disp = display_init();
    if (disp == NULL) {
        ESP_LOGE(TAG, "display_init failed");
        return;
    }

#if CONFIG_APP_MODE_AIR_QUALITY
    ui_init(disp);
    touch_set_point_callback(ui_touch_set_point);
#else
    ui_screen_diagnostics_init(disp);
    ESP_LOGI(TAG, "Screen diagnostics display ready");
#endif

    ESP_ERROR_CHECK(touch_start());
    ESP_ERROR_CHECK(system_ui_init(disp, provisioning_required));
#endif

    ESP_ERROR_CHECK(wifi_manager_start());
}
