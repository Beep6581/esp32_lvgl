/* Version: 2026-10-07 */

#include "wifi_manager.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_attr.h"
#include "esp_crt_bundle.h"
#include "esp_dpp.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#define WIFI_MANAGER_DPP_LISTEN_CHANNELS "1,6,11"
#define WIFI_MANAGER_MAX_CONNECT_ATTEMPTS 3U
#define WIFI_MANAGER_IPIFY_URL "https://api.ipify.org"
#define WIFI_MANAGER_IPIFY_TIMEOUT_MS 5000
#define WIFI_MANAGER_TIME_SYNC_TIMEOUT_MS 5000
#define WIFI_MANAGER_PROBE_STACK_SIZE 8192U
#define WIFI_MANAGER_PROBE_TASK_PRIORITY 4U
#define WIFI_MANAGER_DPP_CLEANUP_STACK_SIZE 3072U
#define WIFI_MANAGER_DPP_CLEANUP_TASK_PRIORITY 4U
#define WIFI_MANAGER_EARLIEST_VALID_TIME 1704067200
#define WIFI_MANAGER_NVS_NAMESPACE "wifi_manager"
#define WIFI_MANAGER_NVS_CONFIG_KEY "network"
#define WIFI_MANAGER_NVS_CONFIG_VERSION 1U

typedef struct {
    char body[32];
    size_t length;
    bool overflow;
} ipify_response_t;

typedef struct {
    uint32_t version;
    bool uses_dpp;
    wifi_config_t wifi_config;
    esp_dpp_config_data_t dpp_config;
} saved_network_t;

static const char* TAG = "wifi_manager";

static SemaphoreHandle_t s_lock;
static wifi_manager_status_t s_status;
static wifi_config_t s_saved_config;
static EXT_RAM_BSS_ATTR esp_dpp_config_data_t s_saved_dpp_config;
static EXT_RAM_BSS_ATTR saved_network_t s_saved_network_record;
static wifi_config_t s_candidate_config;
static esp_dpp_config_data_t s_candidate_dpp_config;
static bool s_saved_uses_dpp;
static bool s_saved_has_dpp_config;
static bool s_started;
static bool s_dpp_initialized;
static bool s_dpp_bootstrap_ready;
static bool s_dpp_config_received;
static bool s_candidate_valid;
static bool s_candidate_uses_dpp;
static bool s_intentional_disconnect;
static bool s_station_started;
static bool s_dpp_listening;
static bool s_dpp_cleanup_running;
static bool s_probe_running;
static bool s_sntp_initialized;
static uint8_t s_connect_attempts;
static uint32_t s_connection_generation;
static uint32_t s_probe_generation;
static esp_err_t start_internet_probe_locked(void);

static void lock(void) {
    xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
}

static void unlock(void) {
    xSemaphoreGiveRecursive(s_lock);
}

static bool wifi_config_is_usable(const wifi_config_t* config) {
    return config != NULL && config->sta.ssid[0] != '\0';
}

static esp_err_t load_saved_network(bool* found) {
    if (found == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *found = false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_MANAGER_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    s_saved_network_record = (saved_network_t){0};
    size_t size = sizeof(s_saved_network_record);
    err = nvs_get_blob(handle, WIFI_MANAGER_NVS_CONFIG_KEY, &s_saved_network_record, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (size != sizeof(s_saved_network_record) || s_saved_network_record.version != WIFI_MANAGER_NVS_CONFIG_VERSION ||
        !wifi_config_is_usable(&s_saved_network_record.wifi_config)) {
        return ESP_ERR_INVALID_VERSION;
    }

    s_saved_config = s_saved_network_record.wifi_config;
    s_saved_dpp_config = s_saved_network_record.dpp_config;
    s_saved_uses_dpp = s_saved_network_record.uses_dpp;
    s_saved_has_dpp_config = true;
    *found = true;
    return ESP_OK;
}

static esp_err_t save_candidate_network(void) {
    s_saved_network_record = (saved_network_t){
        .version = WIFI_MANAGER_NVS_CONFIG_VERSION,
        .uses_dpp = s_candidate_uses_dpp,
        .wifi_config = s_candidate_config,
        .dpp_config = s_candidate_dpp_config,
    };

    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_MANAGER_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(handle, WIFI_MANAGER_NVS_CONFIG_KEY, &s_saved_network_record, sizeof(s_saved_network_record));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static esp_err_t erase_saved_network(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_MANAGER_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_key(handle, WIFI_MANAGER_NVS_CONFIG_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    } else if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static bool state_is_connected(wifi_manager_state_t state) {
    return state == WIFI_MANAGER_STATE_CONNECTED_CHECKING_INTERNET || state == WIFI_MANAGER_STATE_CONNECTED_INTERNET_AVAILABLE ||
           state == WIFI_MANAGER_STATE_CONNECTED_INTERNET_UNAVAILABLE;
}

static bool dpp_akm_uses_connector(uint8_t akm) {
    return akm == ESP_DPP_AKM_DPP || akm == ESP_DPP_AKM_SAE_DPP || akm == ESP_DPP_AKM_PSK_SAE_DPP;
}

static esp_err_t make_wifi_config_from_dpp(const esp_dpp_config_data_t* dpp, wifi_config_t* wifi_config) {
    if (dpp == NULL || wifi_config == NULL || dpp->ssid_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    *wifi_config = (wifi_config_t){0};

    size_t ssid_length = dpp->ssid_len;
    if (ssid_length > sizeof(wifi_config->sta.ssid)) {
        ssid_length = sizeof(wifi_config->sta.ssid);
    }
    memcpy(wifi_config->sta.ssid, dpp->ssid, ssid_length);

    size_t password_length = dpp->password_len;
    if (password_length > sizeof(wifi_config->sta.password)) {
        password_length = sizeof(wifi_config->sta.password);
    }
    memcpy(wifi_config->sta.password, dpp->password, password_length);

    switch ((esp_dpp_akm_t)dpp->akm) {
        case ESP_DPP_AKM_DPP:
            wifi_config->sta.threshold.authmode = WIFI_AUTH_DPP;
            break;
        case ESP_DPP_AKM_SAE_DPP:
        case ESP_DPP_AKM_SAE:
            wifi_config->sta.threshold.authmode = WIFI_AUTH_WPA3_PSK;
            break;
        case ESP_DPP_AKM_PSK_SAE_DPP:
        case ESP_DPP_AKM_PSK_SAE:
            wifi_config->sta.threshold.authmode = WIFI_AUTH_WPA2_WPA3_PSK;
            break;
        case ESP_DPP_AKM_PSK:
        default:
            wifi_config->sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
            break;
    }

    // The DPP exchange channel is not necessarily the AP's channel. Keep channel zero so the station scans all 2.4 GHz channels.
    return ESP_OK;
}

static void copy_ssid_from_wifi_config(char* destination, const wifi_config_t* config) {
    size_t length = 0U;
    if (config != NULL) {
        length = strnlen((const char*)config->sta.ssid, sizeof(config->sta.ssid));
    }
    if (length >= WIFI_MANAGER_SSID_CAPACITY) {
        length = WIFI_MANAGER_SSID_CAPACITY - 1U;
    }
    if (length > 0U) {
        memcpy(destination, config->sta.ssid, length);
    }
    destination[length] = '\0';
}

static void clear_connection_status_locked(void) {
    s_status.internal_ip[0] = '\0';
    s_status.external_ip[0] = '\0';
    s_connection_generation++;
}

static esp_err_t init_wifi_driver(void) {
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&init_config);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err != ESP_OK) {
        return err;
    }
    return esp_wifi_set_mode(WIFI_MODE_STA);
}

static esp_err_t stop_dpp_locked(void) {
    if (!s_dpp_initialized) {
        return ESP_OK;
    }
    if (s_dpp_cleanup_running) {
        return ESP_OK;
    }

    esp_err_t result = esp_supp_dpp_stop_listen();
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "esp_supp_dpp_stop_listen failed: %s", esp_err_to_name(result));
    }

    const esp_err_t deinit_result = esp_supp_dpp_deinit();
    if (deinit_result != ESP_OK) {
        ESP_LOGE(TAG, "esp_supp_dpp_deinit failed: %s", esp_err_to_name(deinit_result));
        return deinit_result;
    }

    s_dpp_initialized = false;
    s_dpp_bootstrap_ready = false;
    s_dpp_listening = false;
    s_dpp_config_received = false;
    s_status.dpp_uri[0] = '\0';
    return ESP_OK;
}

static void dpp_cleanup_task(void* argument) {
    (void)argument;

    const esp_err_t stop_result = esp_supp_dpp_stop_listen();
    const esp_err_t deinit_result = esp_supp_dpp_deinit();

    lock();
    if (deinit_result == ESP_OK) {
        s_dpp_initialized = false;
        s_dpp_bootstrap_ready = false;
        s_dpp_listening = false;
        s_dpp_config_received = false;
        s_status.dpp_uri[0] = '\0';
    }
    s_dpp_cleanup_running = false;
    unlock();

    if (stop_result != ESP_OK) {
        ESP_LOGW(TAG, "esp_supp_dpp_stop_listen failed during deferred cleanup: %s", esp_err_to_name(stop_result));
    }
    if (deinit_result != ESP_OK) {
        ESP_LOGE(TAG, "esp_supp_dpp_deinit failed during deferred cleanup: %s", esp_err_to_name(deinit_result));
    } else {
        ESP_LOGI(TAG, "DPP cleanup complete");
    }

    vTaskDeleteWithCaps(NULL);
}

static esp_err_t start_dpp_cleanup_locked(void) {
    if (!s_dpp_initialized || s_dpp_cleanup_running) {
        return s_dpp_cleanup_running ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    s_dpp_cleanup_running = true;
    if (xTaskCreateWithCaps(dpp_cleanup_task, "dpp_cleanup", WIFI_MANAGER_DPP_CLEANUP_STACK_SIZE, NULL,
                            WIFI_MANAGER_DPP_CLEANUP_TASK_PRIORITY, NULL, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_dpp_cleanup_running = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t start_dpp_listening_locked(void) {
    if (!s_dpp_initialized || !s_station_started || s_status.dpp_uri[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_dpp_listening) {
        return ESP_OK;
    }

    const esp_err_t err = esp_supp_dpp_start_listen();
    if (err == ESP_OK) {
        s_dpp_listening = true;
        ESP_LOGI(TAG, "DPP listening started on channel %s", WIFI_MANAGER_DPP_LISTEN_CHANNELS);
    }
    return err;
}

static esp_err_t restore_saved_config_locked(bool connect) {
    esp_err_t err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t config = s_saved_config;
    err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err != ESP_OK) {
        return err;
    }
    if (s_dpp_initialized && s_saved_has_dpp_config) {
        err = esp_supp_dpp_set_config(s_saved_uses_dpp ? &s_saved_dpp_config : NULL);
        if (err != ESP_OK) {
            return err;
        }
    }

    copy_ssid_from_wifi_config(s_status.ssid, &s_saved_config);
    if (!connect) {
        s_status.state = s_status.has_saved_config ? WIFI_MANAGER_STATE_DISCONNECTED : WIFI_MANAGER_STATE_NO_SAVED_CONFIG;
        return ESP_OK;
    }

    s_intentional_disconnect = false;
    s_connect_attempts = 1U;
    s_status.state = WIFI_MANAGER_STATE_CONNECTING;
    return esp_wifi_connect();
}

static esp_err_t start_provisioning_locked(void) {
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_dpp_cleanup_running) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_dpp_initialized) {
        esp_err_t err = stop_dpp_locked();
        if (err != ESP_OK) {
            return err;
        }
    }

    if (state_is_connected(s_status.state) || s_status.state == WIFI_MANAGER_STATE_CONNECTING || s_status.state == WIFI_MANAGER_STATE_TESTING_CANDIDATE) {
        s_intentional_disconnect = true;
        esp_wifi_disconnect();
    }

    clear_connection_status_locked();
    s_status.reconfiguration = s_status.has_saved_config;
    s_status.state = WIFI_MANAGER_STATE_PROVISIONING;
    s_status.dpp_uri[0] = '\0';
    s_candidate_valid = false;
    s_dpp_bootstrap_ready = false;
    s_dpp_config_received = false;
    s_connect_attempts = 0U;

    esp_err_t err = esp_supp_dpp_init();
    if (err != ESP_OK) {
        s_status.state = WIFI_MANAGER_STATE_PROVISIONING_FAILED;
        return err;
    }
    s_dpp_initialized = true;

    err = esp_supp_dpp_bootstrap_gen(WIFI_MANAGER_DPP_LISTEN_CHANNELS, DPP_BOOTSTRAP_QR_CODE, NULL, NULL);
    if (err != ESP_OK) {
        stop_dpp_locked();
        s_status.state = WIFI_MANAGER_STATE_PROVISIONING_FAILED;
        return err;
    }

    ESP_LOGI(TAG, "%s DPP provisioning started", s_status.reconfiguration ? "replacement-network" : "initial");
    return ESP_OK;
}

static esp_err_t apply_candidate_locked(void) {
    if (!s_candidate_valid || !s_dpp_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_supp_dpp_set_config(s_candidate_uses_dpp ? &s_candidate_dpp_config : NULL);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t config = s_candidate_config;
    err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err != ESP_OK) {
        return err;
    }

    copy_ssid_from_wifi_config(s_status.ssid, &s_candidate_config);
    s_connect_attempts = 1U;
    s_intentional_disconnect = false;
    s_status.state = WIFI_MANAGER_STATE_TESTING_CANDIDATE;
    clear_connection_status_locked();
    return esp_wifi_connect();
}

static bool parse_ipv4_response(ipify_response_t* response, char output[WIFI_MANAGER_IPV4_CAPACITY]) {
    if (response->overflow) {
        return false;
    }

    size_t begin = 0U;
    while (begin < response->length && isspace((unsigned char)response->body[begin])) {
        begin++;
    }
    size_t end = response->length;
    while (end > begin && isspace((unsigned char)response->body[end - 1U])) {
        end--;
    }
    response->body[end] = '\0';

    unsigned a;
    unsigned b;
    unsigned c;
    unsigned d;
    int consumed = 0;
    if (sscanf(&response->body[begin], "%u.%u.%u.%u%n", &a, &b, &c, &d, &consumed) != 4 || a > 255U || b > 255U || c > 255U || d > 255U ||
        begin + (size_t)consumed != end) {
        return false;
    }

    snprintf(output, WIFI_MANAGER_IPV4_CAPACITY, "%u.%u.%u.%u", a, b, c, d);
    return true;
}

static esp_err_t ipify_event_handler(esp_http_client_event_t* event) {
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data == NULL || event->data_len <= 0) {
        return ESP_OK;
    }

    ipify_response_t* response = event->user_data;
    const size_t available = sizeof(response->body) - 1U - response->length;
    const size_t received = (size_t)event->data_len;
    if (received > available) {
        response->overflow = true;
        return ESP_OK;
    }

    memcpy(&response->body[response->length], event->data, received);
    response->length += received;
    response->body[response->length] = '\0';
    return ESP_OK;
}

static esp_err_t ensure_system_time(void) {
    time_t now;
    time(&now);
    if (now >= WIFI_MANAGER_EARLIEST_VALID_TIME) {
        return ESP_OK;
    }

    if (!s_sntp_initialized) {
        const esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        const esp_err_t err = esp_netif_sntp_init(&config);
        if (err != ESP_OK) {
            return err;
        }
        s_sntp_initialized = true;
    }

    const esp_err_t err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(WIFI_MANAGER_TIME_SYNC_TIMEOUT_MS));
    if (err != ESP_OK) {
        return err;
    }

    time(&now);
    return now >= WIFI_MANAGER_EARLIEST_VALID_TIME ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static void internet_probe_task(void* argument) {
    (void)argument;

    ipify_response_t response = {0};
    const esp_http_client_config_t config = {
        .url = WIFI_MANAGER_IPIFY_URL,
        .timeout_ms = WIFI_MANAGER_IPIFY_TIMEOUT_MS,
        .disable_auto_redirect = true,
        .event_handler = ipify_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_data = &response,
        .buffer_size = 128,
        .buffer_size_tx = 256,
    };

    bool internet_available = false;
    char external_ip[WIFI_MANAGER_IPV4_CAPACITY] = {0};
    ESP_LOGI(TAG, "starting Internet probe: internal_free=%u internal_largest=%u psram_free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    const esp_err_t time_result = ensure_system_time();
    if (time_result == ESP_OK) {
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client != NULL) {
            const esp_err_t err = esp_http_client_perform(client);
            const int status_code = esp_http_client_get_status_code(client);
            internet_available = err == ESP_OK && status_code == 200 && parse_ipv4_response(&response, external_ip);
            if (!internet_available) {
                ESP_LOGW(TAG, "ipify probe failed: request=%s HTTP=%d valid_body=%d", esp_err_to_name(err), status_code,
                         response.length > 0U && !response.overflow);
            }
            esp_http_client_cleanup(client);
        } else {
            ESP_LOGE(TAG, "esp_http_client_init failed");
        }
    } else {
        ESP_LOGW(TAG, "cannot verify HTTPS certificate before time synchronization: %s", esp_err_to_name(time_result));
    }

    lock();
    const bool probe_is_current = s_probe_generation == s_connection_generation;
    if (probe_is_current && state_is_connected(s_status.state)) {
        if (internet_available) {
            memcpy(s_status.external_ip, external_ip, sizeof(s_status.external_ip));
            s_status.state = WIFI_MANAGER_STATE_CONNECTED_INTERNET_AVAILABLE;
            ESP_LOGI(TAG, "Internet available, external IP: %s", s_status.external_ip);
        } else {
            s_status.external_ip[0] = '\0';
            s_status.state = WIFI_MANAGER_STATE_CONNECTED_INTERNET_UNAVAILABLE;
        }
    }
    s_probe_running = false;
    if (!probe_is_current && state_is_connected(s_status.state)) {
        const esp_err_t err = start_internet_probe_locked();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "failed to restart Internet probe after reconnection: %s", esp_err_to_name(err));
        }
    }
    unlock();

    vTaskDeleteWithCaps(NULL);
}

static esp_err_t start_internet_probe_locked(void) {
    if (s_probe_running || !state_is_connected(s_status.state)) {
        return s_probe_running ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    s_status.external_ip[0] = '\0';
    s_status.state = WIFI_MANAGER_STATE_CONNECTED_CHECKING_INTERNET;
    s_probe_generation = s_connection_generation;
    s_probe_running = true;
    if (xTaskCreateWithCaps(internet_probe_task, "ipify", WIFI_MANAGER_PROBE_STACK_SIZE, NULL, WIFI_MANAGER_PROBE_TASK_PRIORITY, NULL,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_probe_running = false;
        s_status.state = WIFI_MANAGER_STATE_CONNECTED_INTERNET_UNAVAILABLE;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t persist_candidate_locked(void) {
    esp_err_t err = save_candidate_network();
    if (err != ESP_OK) {
        return err;
    }

    s_saved_config = s_candidate_config;
    s_saved_dpp_config = s_candidate_dpp_config;
    s_saved_uses_dpp = s_candidate_uses_dpp;
    s_saved_has_dpp_config = true;
    s_status.has_saved_config = true;
    s_status.reconfiguration = false;
    s_candidate_valid = false;
    ESP_LOGI(TAG, "candidate Wi-Fi configuration obtained an IP and is now persistent");
    return ESP_OK;
}

static void handle_got_ip_locked(const ip_event_got_ip_t* event) {
    bool candidate_committed = false;
    if (s_status.state == WIFI_MANAGER_STATE_TESTING_CANDIDATE) {
        const esp_err_t err = persist_candidate_locked();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "failed to persist successful candidate: %s", esp_err_to_name(err));
            s_intentional_disconnect = true;
            esp_wifi_disconnect();
            const esp_err_t restore_result = restore_saved_config_locked(false);
            if (restore_result != ESP_OK) {
                ESP_LOGE(TAG, "failed to restore saved configuration: %s", esp_err_to_name(restore_result));
            }
            s_status.state = WIFI_MANAGER_STATE_CANDIDATE_FAILED;
            return;
        }
        candidate_committed = true;
    }

    snprintf(s_status.internal_ip, sizeof(s_status.internal_ip), IPSTR, IP2STR(&event->ip_info.ip));
    s_connection_generation++;
    s_connect_attempts = 0U;
    s_status.state = WIFI_MANAGER_STATE_CONNECTED_CHECKING_INTERNET;
    ESP_LOGI(TAG, "connected, internal IP: %s", s_status.internal_ip);
    if (candidate_committed) {
        const esp_err_t cleanup_result = start_dpp_cleanup_locked();
        if (cleanup_result != ESP_OK) {
            ESP_LOGE(TAG, "failed to start deferred DPP cleanup: %s", esp_err_to_name(cleanup_result));
        }
    }
    const esp_err_t err = start_internet_probe_locked();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to start Internet probe: %s", esp_err_to_name(err));
    }
}

static void handle_disconnect_locked(const wifi_event_sta_disconnected_t* event) {
    clear_connection_status_locked();

    ESP_LOGW(TAG, "Wi-Fi disconnected: reason=%u state=%d attempt=%u/%u", event != NULL ? (unsigned)event->reason : 0U,
             (int)s_status.state, (unsigned)s_connect_attempts, WIFI_MANAGER_MAX_CONNECT_ATTEMPTS);

    if (s_intentional_disconnect) {
        s_intentional_disconnect = false;
        return;
    }

    const bool candidate = s_status.state == WIFI_MANAGER_STATE_TESTING_CANDIDATE;
    const bool reconnect = candidate || s_status.state == WIFI_MANAGER_STATE_CONNECTING || state_is_connected(s_status.state);
    if (!reconnect) {
        return;
    }

    if (s_connect_attempts < WIFI_MANAGER_MAX_CONNECT_ATTEMPTS) {
        s_connect_attempts++;
        s_status.state = candidate ? WIFI_MANAGER_STATE_TESTING_CANDIDATE : WIFI_MANAGER_STATE_CONNECTING;
        ESP_LOGI(TAG, "Wi-Fi connection retry %u/%u", (unsigned)s_connect_attempts, WIFI_MANAGER_MAX_CONNECT_ATTEMPTS);
        const esp_err_t err = esp_wifi_connect();
        if (err == ESP_OK) {
            return;
        }
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
    }

    if (candidate) {
        const esp_err_t err = restore_saved_config_locked(false);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "failed to restore saved configuration after candidate failure: %s", esp_err_to_name(err));
        }
        s_status.state = WIFI_MANAGER_STATE_CANDIDATE_FAILED;
    } else {
        s_status.state = s_status.has_saved_config ? WIFI_MANAGER_STATE_DISCONNECTED : WIFI_MANAGER_STATE_NO_SAVED_CONFIG;
    }
}

static esp_err_t receive_dpp_config_locked(const wifi_event_dpp_config_received_t* event) {
    if (event == NULL || event->total_conf == 0U || !s_dpp_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    s_candidate_dpp_config = event->configs[0];
    const esp_err_t config_result = make_wifi_config_from_dpp(&s_candidate_dpp_config, &s_candidate_config);
    if (config_result != ESP_OK) {
        return config_result;
    }
    s_candidate_uses_dpp = dpp_akm_uses_connector(s_candidate_dpp_config.akm);
    s_candidate_valid = true;
    s_dpp_config_received = true;

    ESP_LOGI(TAG, "DPP candidate received: SSID=%.*s AKM=%u password_len=%u connector_len=%u dpp_channel=%u",
             (int)s_candidate_dpp_config.ssid_len, (const char*)s_candidate_dpp_config.ssid, (unsigned)s_candidate_dpp_config.akm,
             (unsigned)s_candidate_dpp_config.password_len, (unsigned)s_candidate_dpp_config.connector_len,
             (unsigned)s_candidate_dpp_config.curr_chan);
    return apply_candidate_locked();
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    (void)arg;

    lock();
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        handle_got_ip_locked(event_data);
        unlock();
        return;
    }

    if (event_base != WIFI_EVENT) {
        unlock();
        return;
    }

    switch (event_id) {
        case WIFI_EVENT_STA_START: {
            s_station_started = true;
            esp_err_t err;
            if (s_status.has_saved_config) {
                s_connect_attempts = 1U;
                s_status.state = WIFI_MANAGER_STATE_CONNECTING;
                ESP_LOGI(TAG, "stored Wi-Fi configuration found; connecting to %s", s_status.ssid);
                err = esp_wifi_connect();
            } else {
                err = s_status.dpp_uri[0] != '\0' ? start_dpp_listening_locked() : ESP_OK;
            }
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "failed to start Wi-Fi operation: %s", esp_err_to_name(err));
                if (!s_status.has_saved_config) {
                    const esp_err_t cleanup_result = start_dpp_cleanup_locked();
                    if (cleanup_result != ESP_OK) {
                        ESP_LOGE(TAG, "failed to defer DPP cleanup: %s", esp_err_to_name(cleanup_result));
                    }
                }
                s_status.state = s_status.has_saved_config ? WIFI_MANAGER_STATE_DISCONNECTED : WIFI_MANAGER_STATE_PROVISIONING_FAILED;
            }
            break;
        }

        case WIFI_EVENT_STA_DISCONNECTED:
            handle_disconnect_locked(event_data);
            break;

        case WIFI_EVENT_DPP_URI_READY: {
            const wifi_event_dpp_uri_ready_t* event = event_data;
            if (event == NULL || event->uri_data_len < 2U || event->uri_data_len > sizeof(s_status.dpp_uri) ||
                event->uri[event->uri_data_len - 1U] != '\0') {
                ESP_LOGE(TAG, "invalid or oversized DPP URI");
                const esp_err_t cleanup_result = start_dpp_cleanup_locked();
                if (cleanup_result != ESP_OK) {
                    ESP_LOGE(TAG, "failed to defer DPP cleanup: %s", esp_err_to_name(cleanup_result));
                }
                s_status.state = WIFI_MANAGER_STATE_PROVISIONING_FAILED;
                break;
            }
            memcpy(s_status.dpp_uri, event->uri, event->uri_data_len);
            s_dpp_bootstrap_ready = true;
            ESP_LOGI(TAG, "DPP URI ready");
            if (s_station_started) {
                const esp_err_t err = start_dpp_listening_locked();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "esp_supp_dpp_start_listen failed: %s", esp_err_to_name(err));
                    const esp_err_t cleanup_result = start_dpp_cleanup_locked();
                    if (cleanup_result != ESP_OK) {
                        ESP_LOGE(TAG, "failed to defer DPP cleanup: %s", esp_err_to_name(cleanup_result));
                    }
                    s_status.state = WIFI_MANAGER_STATE_PROVISIONING_FAILED;
                }
            }
            break;
        }

        case WIFI_EVENT_DPP_CFG_RECVD: {
            const esp_err_t err = receive_dpp_config_locked(event_data);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "failed to apply DPP candidate: %s", esp_err_to_name(err));
                s_status.state = WIFI_MANAGER_STATE_CANDIDATE_FAILED;
            }
            break;
        }

        case WIFI_EVENT_DPP_FAILED: {
            const wifi_event_dpp_failed_t* event = event_data;
            const esp_err_t reason = event != NULL ? (esp_err_t)event->failure_reason : ESP_ERR_DPP_FAILURE;
            if (s_dpp_config_received) {
                ESP_LOGW(TAG, "ignoring DPP failure after candidate receipt: %s", esp_err_to_name(reason));
            } else if (s_dpp_bootstrap_ready) {
                s_dpp_listening = false;
                ESP_LOGW(TAG, "DPP authentication failed: %s; bootstrap retained for Retry", esp_err_to_name(reason));
                s_status.state = WIFI_MANAGER_STATE_PROVISIONING_FAILED;
            } else {
                ESP_LOGE(TAG, "DPP bootstrap failed: %s", esp_err_to_name(reason));
                const esp_err_t cleanup_result = start_dpp_cleanup_locked();
                if (cleanup_result != ESP_OK) {
                    ESP_LOGE(TAG, "failed to defer DPP cleanup: %s", esp_err_to_name(cleanup_result));
                }
                s_status.state = WIFI_MANAGER_STATE_PROVISIONING_FAILED;
            }
            break;
        }

        default:
            break;
    }
    unlock();
}

esp_err_t wifi_manager_prepare(bool* provisioning_required) {
    if (provisioning_required == NULL || s_lock != NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_lock = xSemaphoreCreateRecursiveMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        return err;
    }
    err = init_wifi_driver();
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t legacy_config = {0};
    err = esp_wifi_get_config(WIFI_IF_STA, &legacy_config);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_deinit();
    if (err != ESP_OK) {
        return err;
    }

    bool manager_config_found = false;
    err = load_saved_network(&manager_config_found);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to load saved network: %s", esp_err_to_name(err));
        manager_config_found = false;
    }
    if (!manager_config_found) {
        s_saved_config = legacy_config;
        s_saved_dpp_config = (esp_dpp_config_data_t){0};
        s_saved_uses_dpp = false;
        s_saved_has_dpp_config = false;
    }

    s_status.has_saved_config = wifi_config_is_usable(&s_saved_config);
    s_status.state = s_status.has_saved_config ? WIFI_MANAGER_STATE_DISCONNECTED : WIFI_MANAGER_STATE_NO_SAVED_CONFIG;
    copy_ssid_from_wifi_config(s_status.ssid, s_status.has_saved_config ? &s_saved_config : NULL);
    *provisioning_required = !s_status.has_saved_config;
    return ESP_OK;
}

esp_err_t wifi_manager_start(void) {
    if (s_lock == NULL || s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = init_wifi_driver();
    if (err != ESP_OK) {
        return err;
    }
    if (s_status.has_saved_config) {
        err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        if (err != ESP_OK) {
            return err;
        }
        wifi_config_t config = s_saved_config;
        err = esp_wifi_set_config(WIFI_IF_STA, &config);
        if (err != ESP_OK) {
            return err;
        }
        if (s_saved_uses_dpp && s_saved_has_dpp_config) {
            err = esp_supp_dpp_init();
            if (err != ESP_OK) {
                return err;
            }
            s_dpp_initialized = true;
            err = esp_supp_dpp_set_config(&s_saved_dpp_config);
            if (err != ESP_OK) {
                return err;
            }
        }
    }
    s_started = true;
    if (!s_status.has_saved_config) {
        lock();
        err = start_provisioning_locked();
        unlock();
        if (err != ESP_OK) {
            s_started = false;
            return err;
        }
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        lock();
        stop_dpp_locked();
        unlock();
        s_started = false;
        return err;
    }

    return ESP_OK;
}

void wifi_manager_prepare_for_restart(void) {
    if (s_lock == NULL) {
        return;
    }

    lock();
    // A software restart stops Wi-Fi. Suppress reconnect handling for the
    // resulting disconnect event without changing credentials or UI state.
    s_intentional_disconnect = true;
    unlock();
}

void wifi_manager_get_status(wifi_manager_status_t* status) {
    if (status == NULL) {
        return;
    }
    if (s_lock == NULL) {
        *status = (wifi_manager_status_t){0};
        return;
    }

    lock();
    *status = s_status;
    unlock();
}

esp_err_t wifi_manager_retry(void) {
    lock();
    esp_err_t err;
    if (s_status.state == WIFI_MANAGER_STATE_CANDIDATE_FAILED) {
        err = s_candidate_valid && s_dpp_initialized ? apply_candidate_locked() : start_provisioning_locked();
    } else if (s_status.state == WIFI_MANAGER_STATE_PROVISIONING_FAILED && s_dpp_initialized && s_dpp_bootstrap_ready) {
        s_dpp_config_received = false;
        s_status.state = WIFI_MANAGER_STATE_PROVISIONING;
        err = start_dpp_listening_locked();
        if (err != ESP_OK) {
            s_status.state = WIFI_MANAGER_STATE_PROVISIONING_FAILED;
        }
    } else if (s_status.state == WIFI_MANAGER_STATE_PROVISIONING_FAILED || !s_status.has_saved_config) {
        err = start_provisioning_locked();
    } else if (!state_is_connected(s_status.state)) {
        err = restore_saved_config_locked(true);
    } else {
        err = start_internet_probe_locked();
    }
    unlock();
    return err;
}

esp_err_t wifi_manager_start_provisioning(void) {
    lock();
    const esp_err_t err = start_provisioning_locked();
    unlock();
    return err;
}

esp_err_t wifi_manager_continue_offline(void) {
    lock();
    s_intentional_disconnect = true;
    esp_wifi_disconnect();
    const esp_err_t dpp_result = stop_dpp_locked();
    const esp_err_t restore_result = restore_saved_config_locked(false);
    s_status.reconfiguration = false;
    s_candidate_valid = false;
    clear_connection_status_locked();
    if (restore_result == ESP_OK) {
        s_status.state = WIFI_MANAGER_STATE_OFFLINE;
    }
    unlock();
    return dpp_result != ESP_OK ? dpp_result : restore_result;
}

esp_err_t wifi_manager_cancel_configuration(void) {
    lock();
    if (!s_status.reconfiguration) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }

    s_intentional_disconnect = true;
    esp_wifi_disconnect();
    const esp_err_t dpp_result = stop_dpp_locked();
    s_candidate_valid = false;
    s_status.reconfiguration = false;
    const esp_err_t restore_result = restore_saved_config_locked(true);
    unlock();
    return dpp_result != ESP_OK ? dpp_result : restore_result;
}

esp_err_t wifi_manager_disconnect(void) {
    lock();
    const esp_err_t dpp_result = stop_dpp_locked();
    s_intentional_disconnect = true;
    const esp_err_t disconnect_result = esp_wifi_disconnect();
    clear_connection_status_locked();
    s_status.state = s_status.has_saved_config ? WIFI_MANAGER_STATE_DISCONNECTED : WIFI_MANAGER_STATE_NO_SAVED_CONFIG;
    s_status.reconfiguration = false;
    unlock();
    return dpp_result != ESP_OK ? dpp_result : disconnect_result;
}

esp_err_t wifi_manager_forget_network(void) {
    lock();
    const esp_err_t dpp_result = stop_dpp_locked();
    s_intentional_disconnect = true;
    esp_wifi_disconnect();

    esp_err_t err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err == ESP_OK) {
        wifi_config_t empty_config = {0};
        err = esp_wifi_set_config(WIFI_IF_STA, &empty_config);
    }
    if (err == ESP_OK) {
        err = erase_saved_network();
    }
    if (err == ESP_OK) {
        s_saved_config = (wifi_config_t){0};
        s_saved_dpp_config = (esp_dpp_config_data_t){0};
        s_saved_uses_dpp = false;
        s_saved_has_dpp_config = false;
        s_candidate_valid = false;
        s_status.has_saved_config = false;
        s_status.reconfiguration = false;
        s_status.ssid[0] = '\0';
        clear_connection_status_locked();
        s_status.state = WIFI_MANAGER_STATE_NO_SAVED_CONFIG;
        ESP_LOGI(TAG, "stored Wi-Fi configuration erased by explicit user action");
    }
    unlock();
    return dpp_result != ESP_OK ? dpp_result : err;
}

esp_err_t wifi_manager_refresh_internet_status(void) {
    lock();
    const esp_err_t err = start_internet_probe_locked();
    unlock();
    return err;
}
