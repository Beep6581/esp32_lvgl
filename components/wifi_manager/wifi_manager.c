/* Version: 2026-10-07 */

#include "wifi_manager.h"

#include <string.h>

#include "esp_dpp.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

#define WIFI_MANAGER_DPP_LISTEN_CHANNELS "6"

#define WIFI_MANAGER_URI_READY_BIT BIT0
#define WIFI_MANAGER_CONNECTED_BIT BIT1
#define WIFI_MANAGER_FAILED_BIT BIT2

static const char* TAG = "wifi_manager";

static EventGroupHandle_t s_events;
static char s_dpp_uri[WIFI_MANAGER_DPP_URI_CAPACITY];
static bool s_provisioning;
static bool s_dpp_initialized;
static bool s_dpp_config_received;

static bool wifi_config_is_usable(const wifi_config_t* config) {
    return config != NULL && config->sta.ssid[0] != '\0';
}

static bool dpp_akm_uses_connector(uint8_t akm) {
    return akm == ESP_DPP_AKM_DPP || akm == ESP_DPP_AKM_SAE_DPP || akm == ESP_DPP_AKM_PSK_SAE_DPP;
}

static esp_err_t apply_dpp_config(const wifi_event_dpp_config_received_t* event) {
    if (event == NULL || event->total_conf == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_dpp_config_data_t* dpp_config = &event->configs[0];
    esp_err_t err = esp_supp_dpp_set_config(dpp_akm_uses_connector(dpp_config->akm) ? dpp_config : NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_supp_dpp_set_config failed: %s", esp_err_to_name(err));
        return err;
    }

    wifi_config_t wifi_config = event->wifi_cfg;
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "DPP configuration received for SSID: %.*s", (int)dpp_config->ssid_len, (const char*)dpp_config->ssid);
    s_dpp_config_received = true;
    return esp_wifi_connect();
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    (void)arg;

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t* event = event_data;
        ESP_LOGI(TAG, "connected, IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_events, WIFI_MANAGER_CONNECTED_BIT);
        return;
    }

    if (event_base != WIFI_EVENT) {
        return;
    }

    switch (event_id) {
        case WIFI_EVENT_STA_START:
            if (s_provisioning) {
                esp_err_t err = esp_supp_dpp_start_listen();
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "DPP listening started on channel %s", WIFI_MANAGER_DPP_LISTEN_CHANNELS);
                } else {
                    ESP_LOGE(TAG, "esp_supp_dpp_start_listen failed: %s", esp_err_to_name(err));
                    xEventGroupSetBits(s_events, WIFI_MANAGER_FAILED_BIT);
                }
            } else {
                esp_err_t err = esp_wifi_connect();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
                }
            }
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            if (!s_provisioning || s_dpp_config_received) {
                esp_err_t err = esp_wifi_connect();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Wi-Fi reconnect failed: %s", esp_err_to_name(err));
                }
            }
            break;

        case WIFI_EVENT_DPP_URI_READY: {
            const wifi_event_dpp_uri_ready_t* event = event_data;
            if (event == NULL || event->uri_data_len < 2U || event->uri_data_len > sizeof(s_dpp_uri) || event->uri[event->uri_data_len - 1U] != '\0') {
                ESP_LOGE(TAG, "invalid or oversized DPP URI");
                xEventGroupSetBits(s_events, WIFI_MANAGER_FAILED_BIT);
                break;
            }
            memcpy(s_dpp_uri, event->uri, event->uri_data_len);
            xEventGroupSetBits(s_events, WIFI_MANAGER_URI_READY_BIT);
            ESP_LOGI(TAG, "DPP URI ready (%lu bytes)", (unsigned long)(event->uri_data_len - 1U));
            break;
        }

        case WIFI_EVENT_DPP_CFG_RECVD: {
            esp_err_t err = apply_dpp_config(event_data);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "failed to apply DPP configuration: %s", esp_err_to_name(err));
                xEventGroupSetBits(s_events, WIFI_MANAGER_FAILED_BIT);
            }
            break;
        }

        case WIFI_EVENT_DPP_FAILED: {
            const wifi_event_dpp_failed_t* event = event_data;
            const esp_err_t reason = event != NULL ? (esp_err_t)event->failure_reason : ESP_ERR_DPP_FAILURE;
            if (s_dpp_config_received) {
                ESP_LOGW(TAG, "ignoring DPP failure after configuration was received: %s", esp_err_to_name(reason));
            } else {
                ESP_LOGE(TAG, "DPP failed: %s", esp_err_to_name(reason));
                xEventGroupSetBits(s_events, WIFI_MANAGER_FAILED_BIT);
            }
            break;
        }

        default:
            break;
    }
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

esp_err_t wifi_manager_prepare(bool* provisioning_required) {
    if (provisioning_required == NULL || s_events != NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_events = xEventGroupCreate();
    if (s_events == NULL) {
        return ESP_ERR_NO_MEM;
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

    wifi_config_t stored_config = {0};
    err = esp_wifi_get_config(WIFI_IF_STA, &stored_config);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_deinit();
    if (err != ESP_OK) {
        return err;
    }

    s_provisioning = !wifi_config_is_usable(&stored_config);
    *provisioning_required = s_provisioning;
    return ESP_OK;
}

esp_err_t wifi_manager_start(void) {
    if (s_events == NULL) {
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

    if (s_provisioning) {
        err = esp_supp_dpp_init();
        if (err != ESP_OK) {
            return err;
        }
        s_dpp_initialized = true;
        err = esp_supp_dpp_bootstrap_gen(WIFI_MANAGER_DPP_LISTEN_CHANNELS, DPP_BOOTSTRAP_QR_CODE, NULL, NULL);
        if (err != ESP_OK) {
            return err;
        }
        ESP_LOGI(TAG, "no stored Wi-Fi configuration; starting DPP provisioning");
    } else {
        ESP_LOGI(TAG, "stored Wi-Fi configuration found; connecting normally");
    }

    return esp_wifi_start();
}

esp_err_t wifi_manager_wait_for_dpp_uri(char* uri, size_t uri_capacity) {
    if (!s_provisioning || uri == NULL || uri_capacity == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    const EventBits_t bits = xEventGroupWaitBits(s_events, WIFI_MANAGER_URI_READY_BIT | WIFI_MANAGER_FAILED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    if ((bits & WIFI_MANAGER_URI_READY_BIT) == 0U) {
        return ESP_FAIL;
    }

    const size_t uri_length = strlen(s_dpp_uri) + 1U;
    if (uri_length > uri_capacity) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(uri, s_dpp_uri, uri_length);
    return ESP_OK;
}

esp_err_t wifi_manager_wait_for_connection(void) {
    if (!s_provisioning) {
        return ESP_ERR_INVALID_STATE;
    }

    const EventBits_t bits = xEventGroupWaitBits(s_events, WIFI_MANAGER_CONNECTED_BIT | WIFI_MANAGER_FAILED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    return (bits & WIFI_MANAGER_CONNECTED_BIT) != 0U ? ESP_OK : ESP_FAIL;
}

esp_err_t wifi_manager_finish_provisioning(void) {
    if (!s_dpp_initialized) {
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
    s_provisioning = false;
    return ESP_OK;
}
