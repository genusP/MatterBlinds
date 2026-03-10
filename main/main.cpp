#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "controller.h"
#include "esp_log.h"
#include "esp_err.h"
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <string.h>
#include "nvs_flash.h"

// Условные включения интеграций
#if CONFIG_EXTERNAL_INTEGRATION == 1
#include "matter_integration.h"
#endif

#if CONFIG_EXTERNAL_INTEGRATION == 2
#include "mqtt_integration.h"
#endif

extern "C"
{
    static const char *TAG = "main";

    esp_err_t init_network()
    {
#if CONFIG_EXTERNAL_INTEGRATION != 0
        esp_err_t err = esp_event_loop_create_default();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Error init default event loop");
            return err;
        }
#if CONFIG_ESP_WIFI_ENABLED == 1
        err = esp_netif_init();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Error init TCP/IP");
            return err;
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
        err = esp_wifi_init(&cfg);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to initialize WiFi");
            return err;
        }

        // Конфигурация WiFi station mode
        wifi_config_t wifi_config = {};
        strncpy((char *)wifi_config.sta.ssid, CONFIG_DEFAULT_WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char *)wifi_config.sta.password, CONFIG_DEFAULT_WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);

        ESP_LOGI(TAG, "Setting WiFi configuration for SSID: %s", CONFIG_DEFAULT_WIFI_SSID);

        err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to set WiFi mode");
            return err;
        }

        err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to set WiFi config");
            return err;
        }

        // Запуск WiFi
        err = esp_wifi_start();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to start WiFi");
            return err;
        }

        ESP_LOGI(TAG, "WiFi initialized, connecting to AP...");

        err = esp_wifi_connect();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to connect to WiFi");
            return err;
        }
#endif
#endif
        return ESP_OK;
    }

    void app_main()
    {
        // Инициализация NVS (обязательно для хранения ключей Matter и других данных)
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            ESP_ERROR_CHECK(nvs_flash_erase());
            err = nvs_flash_init();
        }
        ESP_ERROR_CHECK(err);

        ESP_ERROR_CHECK(init_network());

        // Инициализация компонентов
        ESP_LOGI(TAG, "Initializing controller...");
        controller_init();

#if CONFIG_EXTERNAL_INTEGRATION == 1
        // Инициализация Matter интеграции
        ESP_LOGI(TAG, "Initializing Matter integration...");
        matter_integration_init();

#elif CONFIG_EXTERNAL_INTEGRATION == 2

        // Инициализация MQTT интеграции
        ESP_LOGI(TAG, "Initializing MQTT integration...");
        mqtt_integration_init();
#endif

        ESP_LOGI(TAG, "Rollershade driver ready");

        while (1)
        {
            // Интеграции обрабатываются через события
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}
