#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // Инициализация MQTT клиента
    esp_err_t mqtt_integration_init(void);

    // Деинициализация MQTT клиента
    esp_err_t mqtt_integration_deinit(void);

    // Публикация состояния штор
    esp_err_t mqtt_integration_publish_position(uint8_t position);

    // Публикация статуса движения
    esp_err_t mqtt_integration_publish_movement(bool is_moving, bool direction_up);

    // Подписка на команды управления
    esp_err_t mqtt_integration_subscribe_commands(void);

    // Проверка подключения
    bool mqtt_integration_is_connected(void);

    // Home Assistant MQTT Discovery
    esp_err_t mqtt_integration_publish_discovery_config(void);
    esp_err_t mqtt_integration_remove_discovery_config(void);

    // Публикация состояния штор для Home Assistant
    esp_err_t mqtt_integration_publish_state(uint8_t position, bool is_moving, bool direction_up);

#ifdef __cplusplus
}
#endif
