/* Version: 2026-10-07 */

#include "system_ui.h"
#include "system_gesture.h"

#include <stdint.h>
#include <string.h>

#include "board.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "wifi_manager.h"

#define SYSTEM_UI_QR_SIZE 290
#define SYSTEM_UI_BUTTON_WIDTH 188
#define SYSTEM_UI_BUTTON_HEIGHT 42
#define SYSTEM_UI_BUTTON_GAP 10
#define SYSTEM_UI_STATUS_PANEL_X 16
#define SYSTEM_UI_STATUS_PANEL_Y 52
#define SYSTEM_UI_STATUS_PANEL_WIDTH (BOARD_LCD_HRES - (SYSTEM_UI_STATUS_PANEL_X * 2))
#define SYSTEM_UI_STATUS_PANEL_HEIGHT 154

#define SYSTEM_UI_COLOR_BG 0x101010
#define SYSTEM_UI_COLOR_PANEL 0x202020
#define SYSTEM_UI_COLOR_TEXT 0xE0E0E0
#define SYSTEM_UI_COLOR_MUTED 0x909090
#define SYSTEM_UI_COLOR_BUTTON 0x303030
#define SYSTEM_UI_COLOR_DANGER 0x802020

typedef enum {
    SYSTEM_UI_ACTION_RETRY,
    SYSTEM_UI_ACTION_CONFIGURE,
    SYSTEM_UI_ACTION_CONTINUE_OFFLINE,
    SYSTEM_UI_ACTION_CANCEL,
    SYSTEM_UI_ACTION_DISCONNECT,
    SYSTEM_UI_ACTION_FORGET,
    SYSTEM_UI_ACTION_REFRESH,
} system_ui_action_t;

static const char* TAG = "system_ui";

static lv_display_t* s_display;
static lv_indev_t* s_pointer_indev;
static lv_obj_t* s_settings_screen;
static lv_obj_t* s_previous_screen;
static lv_obj_t* s_status_panel;
static lv_obj_t* s_state_label;
static lv_obj_t* s_network_label;
static lv_obj_t* s_ip_label;
static lv_obj_t* s_internet_label;
static lv_obj_t* s_external_ip_label;
static lv_obj_t* s_qr;
static lv_obj_t* s_button_panel;
static lv_obj_t* s_forget_dialog;
static lv_timer_t* s_update_timer;
static wifi_manager_status_t s_last_status;
static lv_point_t s_press_point;
static char s_last_qr_uri[WIFI_MANAGER_DPP_URI_CAPACITY];
static bool s_settings_open;
static bool s_initial_setup_open;
static system_ui_close_callback_t s_close_callback;

static const char* state_text(wifi_manager_state_t state) {
    switch (state) {
        case WIFI_MANAGER_STATE_NOT_STARTED:
            return "Starting Wi-Fi";
        case WIFI_MANAGER_STATE_NO_SAVED_CONFIG:
            return "No saved network";
        case WIFI_MANAGER_STATE_DISCONNECTED:
            return "Disconnected";
        case WIFI_MANAGER_STATE_OFFLINE:
            return "Offline";
        case WIFI_MANAGER_STATE_CONNECTING:
            return "Connecting";
        case WIFI_MANAGER_STATE_PROVISIONING:
            return "Scan QR to configure Wi-Fi";
        case WIFI_MANAGER_STATE_PROVISIONING_FAILED:
            return "Provisioning failed";
        case WIFI_MANAGER_STATE_TESTING_CANDIDATE:
            return "Testing new network";
        case WIFI_MANAGER_STATE_CANDIDATE_FAILED:
            return "New network failed";
        case WIFI_MANAGER_STATE_CONNECTED_CHECKING_INTERNET:
            return "Connected";
        case WIFI_MANAGER_STATE_CONNECTED_INTERNET_AVAILABLE:
            return "Connected";
        case WIFI_MANAGER_STATE_CONNECTED_INTERNET_UNAVAILABLE:
            return "Connected";
        default:
            return "Unknown";
    }
}

static bool state_is_connected(wifi_manager_state_t state) {
    return state == WIFI_MANAGER_STATE_CONNECTED_CHECKING_INTERNET || state == WIFI_MANAGER_STATE_CONNECTED_INTERNET_AVAILABLE ||
           state == WIFI_MANAGER_STATE_CONNECTED_INTERNET_UNAVAILABLE;
}

static bool state_is_configuration_active(wifi_manager_state_t state) {
    return state == WIFI_MANAGER_STATE_PROVISIONING || state == WIFI_MANAGER_STATE_PROVISIONING_FAILED ||
           state == WIFI_MANAGER_STATE_TESTING_CANDIDATE || state == WIFI_MANAGER_STATE_CANDIDATE_FAILED;
}

static void close_settings(void) {
    if (!s_settings_open || s_previous_screen == NULL) {
        return;
    }
    s_settings_open = false;
    s_initial_setup_open = false;
    if (s_close_callback != NULL) {
        s_close_callback();
        return;
    }
    lv_screen_load(s_previous_screen);
}

static void open_settings(bool initial_setup) {
    if (s_settings_open) {
        return;
    }
    s_previous_screen = lv_display_get_screen_active(s_display);
    s_settings_open = true;
    s_initial_setup_open = initial_setup;
    lv_screen_load(s_settings_screen);
}

static void leave_settings(void) {
    wifi_manager_status_t status;
    wifi_manager_get_status(&status);

    esp_err_t err = ESP_OK;
    if (state_is_configuration_active(status.state)) {
        err = status.reconfiguration ? wifi_manager_cancel_configuration() : wifi_manager_continue_offline();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to leave Wi-Fi configuration cleanly: %s", esp_err_to_name(err));
        return;
    }
    close_settings();
}

static void close_forget_dialog(void) {
    if (s_forget_dialog == NULL) {
        return;
    }
    lv_msgbox_close_async(s_forget_dialog);
    s_forget_dialog = NULL;
}

static void forget_cancel_event_cb(lv_event_t* event) {
    (void)event;
    close_forget_dialog();
}

static void forget_confirm_event_cb(lv_event_t* event) {
    (void)event;

    const esp_err_t err = wifi_manager_forget_network();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to forget Wi-Fi network: %s", esp_err_to_name(err));
    }
    close_forget_dialog();
}

static void show_forget_confirmation(void) {
    if (s_forget_dialog != NULL) {
        return;
    }

    s_forget_dialog = lv_msgbox_create(NULL);
    lv_obj_set_size(s_forget_dialog, 380, 190);
    lv_obj_set_style_bg_color(s_forget_dialog, lv_color_hex(SYSTEM_UI_COLOR_PANEL), 0);
    lv_obj_set_style_text_color(s_forget_dialog, lv_color_hex(SYSTEM_UI_COLOR_TEXT), 0);

    lv_msgbox_add_title(s_forget_dialog, "Forget network?");
    lv_msgbox_add_text(s_forget_dialog, "Are you sure?\nSaved Wi-Fi credentials will be erased.");

    lv_obj_t* cancel_button = lv_msgbox_add_footer_button(s_forget_dialog, "Cancel");
    lv_obj_set_style_bg_color(cancel_button, lv_color_hex(SYSTEM_UI_COLOR_BUTTON), 0);
    lv_obj_add_event_cb(cancel_button, forget_cancel_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* forget_button = lv_msgbox_add_footer_button(s_forget_dialog, "Forget");
    lv_obj_set_style_bg_color(forget_button, lv_color_hex(SYSTEM_UI_COLOR_DANGER), 0);
    lv_obj_add_event_cb(forget_button, forget_confirm_event_cb, LV_EVENT_CLICKED, NULL);
}

static void action_event_cb(lv_event_t* event) {
    const system_ui_action_t action = (system_ui_action_t)(uintptr_t)lv_event_get_user_data(event);
    esp_err_t err = ESP_OK;

    switch (action) {
        case SYSTEM_UI_ACTION_RETRY:
            err = wifi_manager_retry();
            break;
        case SYSTEM_UI_ACTION_CONFIGURE:
            err = wifi_manager_start_provisioning();
            break;
        case SYSTEM_UI_ACTION_CONTINUE_OFFLINE:
            err = wifi_manager_continue_offline();
            if (err == ESP_OK) {
                close_settings();
            }
            break;
        case SYSTEM_UI_ACTION_CANCEL:
            err = wifi_manager_cancel_configuration();
            if (err == ESP_OK) {
                close_settings();
            }
            break;
        case SYSTEM_UI_ACTION_DISCONNECT:
            err = wifi_manager_disconnect();
            break;
        case SYSTEM_UI_ACTION_FORGET:
            show_forget_confirmation();
            break;
        case SYSTEM_UI_ACTION_REFRESH:
            err = wifi_manager_refresh_internet_status();
            break;
        default:
            err = ESP_ERR_INVALID_ARG;
            break;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi action %d failed: %s", (int)action, esp_err_to_name(err));
    }
}

static void create_action_button(const char* text, system_ui_action_t action, bool destructive, uint32_t index) {
    lv_obj_t* button = lv_button_create(s_button_panel);
    lv_obj_set_size(button, SYSTEM_UI_BUTTON_WIDTH, SYSTEM_UI_BUTTON_HEIGHT);
    const int32_t column = (int32_t)(index % 2U);
    const int32_t row = (int32_t)(index / 2U);
    lv_obj_set_pos(button, column * (SYSTEM_UI_BUTTON_WIDTH + SYSTEM_UI_BUTTON_GAP), row * (SYSTEM_UI_BUTTON_HEIGHT + SYSTEM_UI_BUTTON_GAP));
    lv_obj_set_style_bg_color(button, lv_color_hex(destructive ? SYSTEM_UI_COLOR_DANGER : SYSTEM_UI_COLOR_BUTTON), 0);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_add_event_cb(button, action_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)action);

    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(SYSTEM_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_center(label);
}

static void update_actions(const wifi_manager_status_t* status) {
    lv_obj_clean(s_button_panel);
    uint32_t count = 0U;

    switch (status->state) {
        case WIFI_MANAGER_STATE_CONNECTED_CHECKING_INTERNET:
        case WIFI_MANAGER_STATE_CONNECTED_INTERNET_AVAILABLE:
        case WIFI_MANAGER_STATE_CONNECTED_INTERNET_UNAVAILABLE:
            create_action_button("Disconnect", SYSTEM_UI_ACTION_DISCONNECT, false, count++);
            create_action_button("Configure Wi-Fi", SYSTEM_UI_ACTION_CONFIGURE, false, count++);
            create_action_button("Refresh Status", SYSTEM_UI_ACTION_REFRESH, false, count++);
            create_action_button("Forget Network", SYSTEM_UI_ACTION_FORGET, true, count++);
            break;

        case WIFI_MANAGER_STATE_PROVISIONING:
        case WIFI_MANAGER_STATE_TESTING_CANDIDATE:
            if (status->reconfiguration) {
                create_action_button("Cancel", SYSTEM_UI_ACTION_CANCEL, false, count++);
            } else {
                create_action_button("Continue Offline", SYSTEM_UI_ACTION_CONTINUE_OFFLINE, false, count++);
            }
            break;

        case WIFI_MANAGER_STATE_PROVISIONING_FAILED:
        case WIFI_MANAGER_STATE_CANDIDATE_FAILED:
            create_action_button("Retry", SYSTEM_UI_ACTION_RETRY, false, count++);
            if (status->reconfiguration) {
                create_action_button("Cancel", SYSTEM_UI_ACTION_CANCEL, false, count++);
            } else {
                create_action_button("Continue Offline", SYSTEM_UI_ACTION_CONTINUE_OFFLINE, false, count++);
            }
            break;

        case WIFI_MANAGER_STATE_CONNECTING:
            create_action_button("Configure Wi-Fi", SYSTEM_UI_ACTION_CONFIGURE, false, count++);
            create_action_button("Continue Offline", SYSTEM_UI_ACTION_CONTINUE_OFFLINE, false, count++);
            break;

        case WIFI_MANAGER_STATE_DISCONNECTED:
            create_action_button("Retry", SYSTEM_UI_ACTION_RETRY, false, count++);
            create_action_button("Configure Wi-Fi", SYSTEM_UI_ACTION_CONFIGURE, false, count++);
            create_action_button("Continue Offline", SYSTEM_UI_ACTION_CONTINUE_OFFLINE, false, count++);
            if (status->has_saved_config) {
                create_action_button("Forget Network", SYSTEM_UI_ACTION_FORGET, true, count++);
            }
            break;

        case WIFI_MANAGER_STATE_OFFLINE:
            create_action_button("Retry", SYSTEM_UI_ACTION_RETRY, false, count++);
            create_action_button("Configure Wi-Fi", SYSTEM_UI_ACTION_CONFIGURE, false, count++);
            if (status->has_saved_config) {
                // Reserve the left column so the destructive action remains on the right.
                count++;
                create_action_button("Forget Network", SYSTEM_UI_ACTION_FORGET, true, count++);
            }
            break;

        case WIFI_MANAGER_STATE_NO_SAVED_CONFIG:
        case WIFI_MANAGER_STATE_NOT_STARTED:
        default:
            create_action_button("Configure Wi-Fi", SYSTEM_UI_ACTION_CONFIGURE, false, count++);
            create_action_button("Continue Offline", SYSTEM_UI_ACTION_CONTINUE_OFFLINE, false, count++);
            break;
    }

    const uint32_t rows = (count + 1U) / 2U;
    lv_obj_set_size(s_button_panel, (SYSTEM_UI_BUTTON_WIDTH * 2) + SYSTEM_UI_BUTTON_GAP,
                    rows * SYSTEM_UI_BUTTON_HEIGHT + (rows > 0U ? (rows - 1U) * SYSTEM_UI_BUTTON_GAP : 0U));
    lv_obj_align(s_button_panel, LV_ALIGN_BOTTOM_MID, 0, -34);
}

static void update_status_labels(const wifi_manager_status_t* status) {
    lv_label_set_text_fmt(s_state_label, "Status: %s", state_text(status->state));
    lv_label_set_text_fmt(s_network_label, "Network: %s", status->ssid[0] != '\0' ? status->ssid : "-");
    lv_label_set_text_fmt(s_ip_label, "Internal IP: %s", status->internal_ip[0] != '\0' ? status->internal_ip : "-");

    const char* internet = "-";
    if (status->state == WIFI_MANAGER_STATE_CONNECTED_CHECKING_INTERNET) {
        internet = "checking";
    } else if (status->state == WIFI_MANAGER_STATE_CONNECTED_INTERNET_AVAILABLE) {
        internet = "yes";
    } else if (status->state == WIFI_MANAGER_STATE_CONNECTED_INTERNET_UNAVAILABLE) {
        internet = "no";
    }
    lv_label_set_text_fmt(s_internet_label, "Internet: %s", internet);
    lv_label_set_text_fmt(s_external_ip_label, "External IP: %s", status->external_ip[0] != '\0' ? status->external_ip : "-");
}

static void update_qr(const wifi_manager_status_t* status) {
    if (status->dpp_uri[0] == '\0') {
        s_last_qr_uri[0] = '\0';
        lv_obj_set_hidden(s_qr, true);
        return;
    }

    if (strcmp(status->dpp_uri, s_last_qr_uri) != 0) {
        const lv_result_t result = lv_qrcode_update(s_qr, status->dpp_uri, strlen(status->dpp_uri));
        if (result != LV_RESULT_OK) {
            ESP_LOGE(TAG, "failed to encode DPP QR URI");
            lv_obj_set_hidden(s_qr, true);
            return;
        }
        memcpy(s_last_qr_uri, status->dpp_uri, sizeof(s_last_qr_uri));
    }
    lv_obj_set_hidden(s_qr, false);
}

static void update_timer_cb(lv_timer_t* timer) {
    (void)timer;

    wifi_manager_status_t status;
    wifi_manager_get_status(&status);
    update_status_labels(&status);
    update_qr(&status);

    if (status.state != s_last_status.state || status.has_saved_config != s_last_status.has_saved_config ||
        status.reconfiguration != s_last_status.reconfiguration) {
        update_actions(&status);
    }

    if (s_initial_setup_open && state_is_connected(status.state)) {
        close_settings();
    }
    s_last_status = status;
}

static void input_event_cb(lv_event_t* event) {
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(s_pointer_indev, &s_press_point);
        return;
    }

    if (s_forget_dialog != NULL) {
        return;
    }

    if (code == LV_EVENT_GESTURE_DOWN && !s_settings_open && s_press_point.y <= SYSTEM_SETTINGS_TOP_EDGE_HEIGHT) {
        open_settings(false);
    } else if (code == LV_EVENT_GESTURE_UP && s_settings_open) {
        leave_settings();
    }
}

static lv_indev_t* find_pointer_input(lv_display_t* display) {
    lv_indev_t* input = lv_indev_get_next(NULL);
    while (input != NULL) {
        if (lv_indev_get_type(input) == LV_INDEV_TYPE_POINTER && lv_indev_get_display(input) == display) {
            return input;
        }
        input = lv_indev_get_next(input);
    }
    return NULL;
}

static lv_obj_t* create_label(lv_obj_t* parent, int32_t y, uint32_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_width(label, BOARD_LCD_HRES - 32);
    lv_obj_set_pos(label, 16, y);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return label;
}

static lv_obj_t* create_status_label(int32_t y, uint32_t color) {
    lv_obj_t* label = lv_label_create(s_status_panel);
    lv_obj_set_width(label, SYSTEM_UI_STATUS_PANEL_WIDTH - 32);
    lv_obj_set_pos(label, 16, y);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    return label;
}

esp_err_t system_ui_init(lv_display_t* display, bool open_initially, system_ui_close_callback_t close_callback) {
    if (display == NULL || s_display != NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lvgl_port_lock(0);
    s_pointer_indev = find_pointer_input(display);
    if (s_pointer_indev == NULL) {
        lvgl_port_unlock();
        return ESP_ERR_NOT_FOUND;
    }

    s_display = display;
    s_close_callback = close_callback;
    s_settings_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_settings_screen, lv_color_hex(SYSTEM_UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_settings_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_settings_screen, 0, 0);
    lv_obj_set_style_pad_all(s_settings_screen, 0, 0);
    lv_obj_set_scrollable(s_settings_screen, false);

    lv_obj_t* title = create_label(s_settings_screen, 12, SYSTEM_UI_COLOR_TEXT);
    lv_label_set_text(title, "Wi-Fi Configuration");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

    s_status_panel = lv_obj_create(s_settings_screen);
    lv_obj_set_size(s_status_panel, SYSTEM_UI_STATUS_PANEL_WIDTH, SYSTEM_UI_STATUS_PANEL_HEIGHT);
    lv_obj_set_pos(s_status_panel, SYSTEM_UI_STATUS_PANEL_X, SYSTEM_UI_STATUS_PANEL_Y);
    lv_obj_set_style_bg_color(s_status_panel, lv_color_hex(SYSTEM_UI_COLOR_PANEL), 0);
    lv_obj_set_style_border_width(s_status_panel, 0, 0);
    lv_obj_set_style_pad_all(s_status_panel, 0, 0);
    lv_obj_set_style_radius(s_status_panel, 4, 0);
    lv_obj_set_scrollable(s_status_panel, false);

    s_state_label = create_status_label(10, SYSTEM_UI_COLOR_TEXT);
    s_network_label = create_status_label(37, SYSTEM_UI_COLOR_MUTED);
    s_ip_label = create_status_label(64, SYSTEM_UI_COLOR_MUTED);
    s_internet_label = create_status_label(91, SYSTEM_UI_COLOR_MUTED);
    s_external_ip_label = create_status_label(118, SYSTEM_UI_COLOR_MUTED);

    s_qr = lv_qrcode_create(s_settings_screen);
    lv_qrcode_set_size(s_qr, SYSTEM_UI_QR_SIZE);
    lv_qrcode_set_dark_color(s_qr, lv_color_black());
    lv_qrcode_set_light_color(s_qr, lv_color_white());
    lv_qrcode_set_quiet_zone(s_qr, true);
    lv_obj_align(s_qr, LV_ALIGN_CENTER, 0, 6);
    lv_obj_set_hidden(s_qr, true);

    s_button_panel = lv_obj_create(s_settings_screen);
    lv_obj_set_style_bg_opa(s_button_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_button_panel, 0, 0);
    lv_obj_set_style_pad_all(s_button_panel, 0, 0);
    lv_obj_set_scrollable(s_button_panel, false);

    lv_obj_t* instruction = create_label(s_settings_screen, BOARD_LCD_VRES - 23, SYSTEM_UI_COLOR_MUTED);
    lv_label_set_text(instruction, "Swipe UP to return");
    lv_obj_set_style_text_font(instruction, &lv_font_montserrat_12, 0);

    s_last_status.state = (wifi_manager_state_t)-1;
    s_update_timer = lv_timer_create(update_timer_cb, 250, NULL);
    lv_indev_add_event_cb(s_pointer_indev, input_event_cb, LV_EVENT_PRESSED, NULL);
    lv_indev_add_event_cb(s_pointer_indev, input_event_cb, LV_EVENT_GESTURE_DOWN, NULL);
    lv_indev_add_event_cb(s_pointer_indev, input_event_cb, LV_EVENT_GESTURE_UP, NULL);

    update_timer_cb(NULL);
    if (open_initially) {
        // A close callback identifies the Clock configuration shell, which must
        // remain open after its existing network reconnects.
        open_settings(close_callback == NULL);
    }
    lvgl_port_unlock();

    ESP_LOGI(TAG, "global top-edge settings gesture ready");
    return ESP_OK;
}
