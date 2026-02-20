#include "controller.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"

// Условные включения интеграций
#ifdef CONFIG_ENABLE_MATTER_INTEGRATION
#include "matter_integration.h"
#endif

#ifdef CONFIG_ENABLE_MQTT_INTEGRATION
#include "mqtt_integration.h"
#endif

extern "C" void app_main()
{
    // Инициализация NVS (обязательно для хранения ключей Matter и других данных)
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Инициализация компонентов
    ESP_LOGI("main", "Initializing controller...");
    controller_init();

#ifdef CONFIG_ENABLE_MATTER_INTEGRATION
    // Инициализация Matter интеграции
    ESP_LOGI("main", "Initializing Matter integration...");
    matter_integration_init();
#endif

#ifdef CONFIG_ENABLE_MQTT_INTEGRATION
    // Инициализация MQTT интеграции
    ESP_LOGI("main", "Initializing MQTT integration...");
    mqtt_integration_init();
#endif

    ESP_LOGI("main", "Shade ready");

    while (1)
    {
        // Интеграции обрабатываются через события
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
