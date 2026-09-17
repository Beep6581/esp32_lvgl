#include "display.h"
#include "sdkconfig.h"
#include "ui.h"

#if CONFIG_APP_MODE_AIR_QUALITY
#include "air_quality.h"
#include "i2c_bus.h"
#include "touch.h"
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

void app_main(void) {
#if CONFIG_APP_MODE_AIR_QUALITY
    ESP_LOGI(TAG, "Starting air-quality mode");
    ESP_ERROR_CHECK(i2c_bus_init());

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

    ESP_ERROR_CHECK(touch_start());
#else
    ui_screen_diagnostics_init(disp);
    ESP_LOGI(TAG, "Screen diagnostics display ready");
#endif
}
