#include "matter_integration.h"

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

using namespace esp_matter;
using namespace esp_matter::cluster;
using namespace esp_matter::endpoint;

void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
}

esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                  uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    return ESP_OK;
}

esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                uint8_t effect_variant, void *priv_data)
{
    return ESP_OK;
}

void matter_integration_init(void)
{
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    /* Set OpenThread platform config */
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
#endif

    // 1. Инициализация ESP-IDF и стека
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);

    // 2. Создание эндпоинта Window Covering (ID для штор)
    window_covering_device::config_t window_config;
    // Тип 0x08 = Rollershade (рулонная штора)
    // window_config.window_covering.device_type_id = 0x08;
    endpoint_t *endpoint = window_covering_device::create(node, &window_config, ENDPOINT_FLAG_NONE, NULL);

    // 3. Запуск Matter
    esp_matter::start(app_event_cb);
}
