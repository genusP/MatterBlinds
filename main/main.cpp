#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "controller.h"
#include "esp_log.h"
#include "esp_err.h"
#include <esp_netif.h>
#include <esp_event.h>
#include <string.h>
#include "nvs_flash.h"

// условное вкдючение WiFi
// #if defined(CONFIG_EXTERNAL_INTEGRATION_MQTT) || (defined(CONFIG_EXTERNAL_INTEGRATION_MATTER) && defined(CONFIG_ESP_WIFI_ENABLED))
#define WIFI_INIT
#include "wifi.h"
// #endif

// Условные включения интеграций
#ifdef CONFIG_EXTERNAL_INTEGRATION_MATTER
#include "matter_integration.h"
#endif

#ifdef CONFIG_EXTERNAL_INTEGRATION_MQTT
#include "mqtt_integration.h"
#endif

extern "C"
{
    static const char *TAG = "main";

    esp_err_t init_network()
    {
        esp_err_t err = esp_netif_init();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Error init TCP/IP");
            return err;
        }
        // #ifndef CONFIG_EXTERNAL_INTEGRATION_NONE
        err = esp_event_loop_create_default();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Error init default event loop");
            return err;
        }

        // #endif
        // #ifdef WIFI_INIT
        err = wifi_init();
        // #endif
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
        // ESP_LOGI(TAG, "Initializing controller...");
        // controller_init();

#ifdef CONFIG_EXTERNAL_INTEGRATION_MATTER
        // Инициализация Matter интеграции
        // ESP_LOGI(TAG, "Initializing Matter integration...");
        // matter_integration_init();
#endif
#ifdef CONFIG_EXTERNAL_INTEGRATION_MQTT
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
