#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_wifi.h"

#ifdef __cplusplus
extern "C"
{
#endif

    esp_err_t wifi_init(void);
    esp_err_t wifi_scan_and_get_network_params(wifi_ap_record_t *target_ap);

#ifdef __cplusplus
}
#endif
