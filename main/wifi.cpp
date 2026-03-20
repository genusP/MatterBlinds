#include <esp_bit_defs.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <string.h>
#include <esp_netif.h>
#include <esp_event.h>
#include "sdkconfig.h"
#include "wifi.h"
#include "nvs_flash.h"

static EventGroupHandle_t s_wifi_event_group = xEventGroupCreate();
static const int WIFI_STARTED_BIT = BIT0;
static const int WIFI_CONNECTED_BIT = BIT1;
static const int WIFI_DISCONNECTED_BIT = BIT2;
static const int WIFI_FAIL_BIT = BIT3;
static const int WIFI_IP_GOT_BIT = BIT4;

static const char *TAG = "main";
static int s_retry_num = 0;
static const int MAX_RETRY_COUNT = 5;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi station started");
            xEventGroupSetBits(s_wifi_event_group, WIFI_STARTED_BIT);
            break;

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "Connected to AP");
            s_retry_num = 0;
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
        {
            wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGE(TAG, "Disconnected from AP. Reason: %u", disconnected->reason);

            xEventGroupSetBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);

            // Пытаемся переподключиться, если это не отказ в аутентификации
            if (disconnected->reason == WIFI_REASON_AUTH_EXPIRE ||
                disconnected->reason == WIFI_REASON_AUTH_FAIL ||
                disconnected->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT)
            {
                ESP_LOGE(TAG, "Authentication failed - check password and security settings");
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
            else if (s_retry_num < MAX_RETRY_COUNT)
            {
                s_retry_num++;
                ESP_LOGI(TAG, "Trying to reconnect... (%d/%d)", s_retry_num, MAX_RETRY_COUNT);
                esp_wifi_connect();
            }
            else
            {
                ESP_LOGE(TAG, "Max retry attempts reached");
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
            break;
        }

        default:
            ESP_LOGI(TAG, "Unhandled WiFi event: %lu", event_id);
            break;
        }
    }
    else if (event_base == IP_EVENT)
    {
        if (event_id == IP_EVENT_STA_GOT_IP)
        {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
            s_retry_num = 0;
            xEventGroupSetBits(s_wifi_event_group, WIFI_IP_GOT_BIT);
        }
    }
}

// Функция сканирования и получения параметров целевой WiFi сети
esp_err_t wifi_scan_and_get_network_params(wifi_ap_record_t *target_ap)
{
    if (!target_ap)
    {
        ESP_LOGE(TAG, "Target AP pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting WiFi scan to get network parameters...");

    wifi_scan_config_t scan_config = {
        .ssid = 0,
        .bssid = 0,
        .channel = 0,
        .show_hidden = true};

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start WiFi scan: %s", esp_err_to_name(err));
        return err;
    }

    uint16_t ap_count = 0;
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get AP count: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Found %u WiFi networks", ap_count);

    if (ap_count == 0)
    {
        return ESP_ERR_NOT_FOUND;
    }

    wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_records)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for AP records");
        return ESP_ERR_NO_MEM;
    }

    err = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get AP records: %s", esp_err_to_name(err));
        free(ap_records);
        return err;
    }

    // Ищем целевую сеть и получаем ее параметры
    bool target_found = false;
    for (uint16_t i = 0; i < ap_count; i++)
    {
        ESP_LOGI(TAG, "Network %u: SSID='%s', RSSI=%d, Channel=%u, Auth=%u",
                 i + 1, ap_records[i].ssid, ap_records[i].rssi,
                 ap_records[i].primary, ap_records[i].authmode);

        // Проверяем, это ли наша целевая сеть
        if (strcmp((char *)ap_records[i].ssid, CONFIG_DEFAULT_WIFI_SSID) == 0)
        {
            target_found = true;
            ESP_LOGI(TAG, "=== TARGET NETWORK FOUND ===");
            ESP_LOGI(TAG, "SSID: %s", ap_records[i].ssid);
            ESP_LOGI(TAG, "RSSI: %d dBm", ap_records[i].rssi);
            ESP_LOGI(TAG, "Channel: %u", ap_records[i].primary);
            ESP_LOGI(TAG, "Auth Mode: %u (%s)", ap_records[i].authmode,
                     ap_records[i].authmode == WIFI_AUTH_OPEN ? "OPEN" : ap_records[i].authmode == WIFI_AUTH_WEP        ? "WEP"
                                                                     : ap_records[i].authmode == WIFI_AUTH_WPA_PSK      ? "WPA_PSK"
                                                                     : ap_records[i].authmode == WIFI_AUTH_WPA2_PSK     ? "WPA2_PSK"
                                                                     : ap_records[i].authmode == WIFI_AUTH_WPA_WPA2_PSK ? "WPA_WPA2_PSK"
                                                                     : ap_records[i].authmode == WIFI_AUTH_WPA3_PSK     ? "WPA3_PSK"
                                                                                                                        : "UNKNOWN");
            ESP_LOGI(TAG, "BSSID: %02x:%02x:%02x:%02x:%02x:%02x",
                     ap_records[i].bssid[0], ap_records[i].bssid[1], ap_records[i].bssid[2],
                     ap_records[i].bssid[3], ap_records[i].bssid[4], ap_records[i].bssid[5]);
            ESP_LOGI(TAG, "Pairwise Cipher: %u", ap_records[i].pairwise_cipher);
            ESP_LOGI(TAG, "Group Cipher: %u", ap_records[i].group_cipher);
            ESP_LOGI(TAG, "=============================");

            // Копируем параметры целевой сети
            memcpy(target_ap, &ap_records[i], sizeof(wifi_ap_record_t));
            break;
        }
    }

    if (!target_found)
    {
        ESP_LOGW(TAG, "Target network '%s' not found in scan results", CONFIG_DEFAULT_WIFI_SSID);
    }

    free(ap_records);
    esp_wifi_scan_stop();

    return target_found ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t wifi_init(void)
{
    if (CONFIG_DEFAULT_WIFI_SSID[0] != '\0')
    {
        ESP_LOGI(TAG, "Initializing WiFi for SSID: %s", CONFIG_DEFAULT_WIFI_SSID);

        // Очищаем NVS от старых WiFi параметров для избежания конфликтов
        esp_err_t nvs_err = nvs_flash_erase_partition("nvs");
        if (nvs_err == ESP_OK)
        {
            ESP_LOGI(TAG, "Cleared NVS partition to avoid old WiFi parameter conflicts");
            // Переинициализируем NVS после очистки
            nvs_err = nvs_flash_init();
            if (nvs_err == ESP_OK)
            {
                ESP_LOGI(TAG, "NVS reinitialized successfully");
            }
            else
            {
                ESP_LOGE(TAG, "Failed to reinitialize NVS after erase: %s", esp_err_to_name(nvs_err));
            }
        }
        else
        {
            ESP_LOGW(TAG, "Failed to erase NVS partition: %s", esp_err_to_name(nvs_err));
        }

        // Создаем WiFi station interface
        esp_netif_t *wifi_netif = esp_netif_create_default_wifi_sta();
        if (wifi_netif == NULL)
        {
            ESP_LOGE(TAG, "Failed to create WiFi station interface");
            return ESP_FAIL;
        }

        // Инициализация WiFi
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t err = esp_wifi_init(&cfg);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(err));
            return err;
        }

        // Регистрация обработчиков событий WiFi
        err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to register WiFi event handler: %s", esp_err_to_name(err));
            return err;
        }

        err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(err));
            return err;
        }

        wifi_config_t wifi_config = {
            .sta = {
                .ssid = CONFIG_DEFAULT_WIFI_SSID,
                .password = CONFIG_DEFAULT_WIFI_PASSWORDs,
                /* Искать на всех каналах (важно для AX-роутеров и скрытых сетей) */
                .scan_method = WIFI_ALL_CHANNEL_SCAN,
                /* Настройки сканирования */
                // .scan_config = {
                //     .show_hidden = true, // Увидит скрытую сеть
                // },
                /* Позволяем роутеру самому диктовать правила безопасности */
                .threshold = {
                    .authmode = WIFI_AUTH_OPEN, // Минимальный порог (подключится к любой)
                },
                .pmf_cfg = {
                    .capable = true,   // Поддерживаем современные фишки (для AX роутеров)
                    .required = false, // Но не требуем их принудительно
                },
            },
        };

        ESP_LOGI(TAG, "WiFi configuration prepared for SSID: %s", CONFIG_DEFAULT_WIFI_SSID);
        ESP_LOGI(TAG, "Password length: %u, Auth mode: %d", strlen(CONFIG_DEFAULT_WIFI_PASSWORD), wifi_config.sta.threshold.authmode);

        // Устанавливаем режим WiFi station
        err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(err));
            return err;
        }

        // Устанавливаем конфигурацию WiFi
        err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to set WiFi config: %s", esp_err_to_name(err));
            return err;
        }

        // Запуск WiFi
        err = esp_wifi_start();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(err));
            return err;
        }

        // Отключаем энергосбережение для надежности
        err = esp_wifi_set_ps(WIFI_PS_NONE);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to disable power saving: %s", esp_err_to_name(err));
        }

        ESP_LOGI(TAG, "WiFi initialized, waiting for station start...");

        // Ожидаем запуска станции
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_STARTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(5000));
        if (!(bits & WIFI_STARTED_BIT))
        {
            ESP_LOGE(TAG, "WiFi station start timeout");
            return ESP_ERR_TIMEOUT;
        }

        ESP_LOGI(TAG, "WiFi station started, scanning networks before connection...");

        // Сканируем сети для получения параметров целевой сети
        // wifi_ap_record_t target_ap;
        // esp_err_t scan_result = wifi_scan_and_get_network_params(&target_ap);
        // if (scan_result == ESP_OK)
        // {
        //     // Адаптируем конфигурацию под параметры найденной сети
        //     ESP_LOGI(TAG, "Adapting WiFi configuration to target network parameters...");

        //     // Обновляем режим аутентификации согласно параметрам сети
        //     wifi_config.sta.threshold.authmode = target_ap.authmode;

        //     // Устанавливаем канал сети
        //     wifi_config.sta.channel = target_ap.primary;

        //     // Используем BSSID для более надежного подключения
        //     wifi_config.sta.bssid_set = 1;
        //     memcpy(wifi_config.sta.bssid, target_ap.bssid, 6);

        //     // Устанавливаем параметры шифрования на основе обнаруженной сети
        //     wifi_config.sta.pmf_cfg.capable = false;
        //     wifi_config.sta.pmf_cfg.required = false;

        //     // Уменьшаем listen_interval для более быстрого ответа
        //     wifi_config.sta.listen_interval = 1;

        //     ESP_LOGI(TAG, "Updated configuration - Auth mode: %u, Channel: %u",
        //              target_ap.authmode, target_ap.primary);

        //     // Обновляем конфигурацию WiFi с новыми параметрами
        //     err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        //     if (err != ESP_OK)
        //     {
        //         ESP_LOGE(TAG, "Failed to update WiFi config with scanned parameters: %s", esp_err_to_name(err));
        //     }
        //     else
        //     {
        //         ESP_LOGI(TAG, "WiFi configuration updated with scanned network parameters");
        //     }
        // }
        // else
        // {
        //     ESP_LOGW(TAG, "WiFi scan failed or target network not found, proceeding with default configuration");
        // }

        ESP_LOGI(TAG, "WiFi station started, connecting to AP...");

        // Начинаем подключение
        err = esp_wifi_connect();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to start WiFi connection: %s", esp_err_to_name(err));
            return err;
        }

        ESP_LOGI(TAG, "WiFi connection initiated");
        return ESP_OK;
    }
    else
    {
        ESP_LOGW(TAG, "WiFi SSID not configured, skipping WiFi initialization");
        return ESP_OK;
    }
}
