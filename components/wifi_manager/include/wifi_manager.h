/* Version: 2026-10-07 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define WIFI_MANAGER_DPP_URI_CAPACITY 512U
#define WIFI_MANAGER_SSID_CAPACITY 33U
#define WIFI_MANAGER_IPV4_CAPACITY 16U

typedef enum {
    WIFI_MANAGER_STATE_NOT_STARTED,
    WIFI_MANAGER_STATE_NO_SAVED_CONFIG,
    WIFI_MANAGER_STATE_DISCONNECTED,
    WIFI_MANAGER_STATE_OFFLINE,
    WIFI_MANAGER_STATE_CONNECTING,
    WIFI_MANAGER_STATE_PROVISIONING,
    WIFI_MANAGER_STATE_PROVISIONING_FAILED,
    WIFI_MANAGER_STATE_TESTING_CANDIDATE,
    WIFI_MANAGER_STATE_CANDIDATE_FAILED,
    WIFI_MANAGER_STATE_CONNECTED_CHECKING_INTERNET,
    WIFI_MANAGER_STATE_CONNECTED_INTERNET_AVAILABLE,
    WIFI_MANAGER_STATE_CONNECTED_INTERNET_UNAVAILABLE,
} wifi_manager_state_t;

typedef struct {
    wifi_manager_state_t state;
    bool has_saved_config;
    bool reconfiguration;
    char ssid[WIFI_MANAGER_SSID_CAPACITY];
    char internal_ip[WIFI_MANAGER_IPV4_CAPACITY];
    char external_ip[WIFI_MANAGER_IPV4_CAPACITY];
    char dpp_uri[WIFI_MANAGER_DPP_URI_CAPACITY];
} wifi_manager_status_t;

esp_err_t wifi_manager_prepare(bool* provisioning_required);
esp_err_t wifi_manager_start(void);
void wifi_manager_prepare_for_restart(void);
void wifi_manager_get_status(wifi_manager_status_t* status);

esp_err_t wifi_manager_retry(void);
esp_err_t wifi_manager_start_provisioning(void);
esp_err_t wifi_manager_continue_offline(void);
esp_err_t wifi_manager_cancel_configuration(void);
esp_err_t wifi_manager_disconnect(void);
esp_err_t wifi_manager_forget_network(void);
esp_err_t wifi_manager_refresh_internet_status(void);
