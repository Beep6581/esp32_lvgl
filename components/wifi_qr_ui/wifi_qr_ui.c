/* Version: 2026-10-07 */

#include "wifi_qr_ui.h"

#include <string.h>

#include "esp_lvgl_port.h"

#define WIFI_QR_SIZE 360

esp_err_t wifi_qr_ui_show(lv_display_t* display, const char* dpp_uri) {
    if (display == NULL || dpp_uri == NULL || dpp_uri[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    lvgl_port_lock(0);

    lv_obj_t* screen = lv_display_get_screen_active(display);
    lv_obj_clean(screen);
    lv_obj_set_scrollable(screen, false);
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t* label = lv_label_create(screen);
    lv_label_set_text(label, "Scan to configure Wi-Fi");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t* qr = lv_qrcode_create(screen);
    lv_qrcode_set_size(qr, WIFI_QR_SIZE);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_set_quiet_zone(qr, true);
    const lv_result_t result = lv_qrcode_update(qr, dpp_uri, strlen(dpp_uri));
    lv_obj_align(qr, LV_ALIGN_BOTTOM_MID, 0, -24);

    lvgl_port_unlock();
    return result == LV_RESULT_OK ? ESP_OK : ESP_FAIL;
}
