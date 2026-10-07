/* Version: 2026-10-07 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define WIFI_MANAGER_DPP_URI_CAPACITY 512U

esp_err_t wifi_manager_prepare(bool* provisioning_required);
esp_err_t wifi_manager_start(void);
esp_err_t wifi_manager_wait_for_dpp_uri(char* uri, size_t uri_capacity);
esp_err_t wifi_manager_wait_for_connection(void);
esp_err_t wifi_manager_finish_provisioning(void);
