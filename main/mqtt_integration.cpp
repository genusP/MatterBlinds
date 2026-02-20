#include "mqtt_integration.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "controller.h"
#include <string.h>

static const char *TAG = "mqtt_integration";
static esp_mqtt_client_handle_t mqtt_client = NULL;
static SemaphoreHandle_t mqtt_mutex = NULL;
static bool mqtt_connected = false;

// Обработка MQTT команд
static void mqtt_handle_command(const char *payload, int payload_len)
{
    if (payload_len <= 0)
        return;

    // Создаем нуль-терминированную строку из payload
    char command[32];
    int copy_len = (payload_len < sizeof(command) - 1) ? payload_len : sizeof(command) - 1;
    memcpy(command, payload, copy_len);
    command[copy_len] = '\0';

    ESP_LOGI(TAG, "Processing MQTT command: %s", command);

    // Обрабатываем команды от Home Assistant
    if (strcmp(command, "OPEN") == 0)
    {
        controller_move_up();
    }
    else if (strcmp(command, "CLOSE") == 0)
    {
        controller_move_down();
    }
    else if (strcmp(command, "STOP") == 0)
    {
        controller_stop();
    }
    else
    {
        // Проверяем, является ли команда числом (позиция в процентах)
        char *endptr;
        long position = strtol(command, &endptr, 10);
        if (*endptr == '\0' && position >= 0 && position <= 100)
        {
            controller_set_position_percentage((float)position);
        }
        else
        {
            ESP_LOGW(TAG, "Unknown MQTT command: %s", command);
        }
    }
}

// Обработчик событий MQTT
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        mqtt_connected = true;
        mqtt_integration_subscribe_commands();
#ifdef CONFIG_MQTT_HA_DISCOVERY_ENABLED
        // Публикуем конфигурацию для Home Assistant
        mqtt_integration_publish_discovery_config();
#endif
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT disconnected");
        mqtt_connected = false;
#ifdef CONFIG_MQTT_HA_DISCOVERY_ENABLED
        // Публикуем статус недоступности для Home Assistant
        char availability_topic[256];
        snprintf(availability_topic, sizeof(availability_topic),
                 "%s/availability", CONFIG_MQTT_TOPIC_POSITION);
        // Публикуем статус offline с retain flag
        esp_mqtt_client_publish(mqtt_client, availability_topic, "offline", 0, 1, true);
#endif
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT data received, topic: %.*s", event->topic_len, event->topic);

        // Проверяем является ли это командным топиком
        if (event->topic_len == strlen(CONFIG_MQTT_TOPIC_COMMAND) &&
            memcmp(event->topic, CONFIG_MQTT_TOPIC_COMMAND, event->topic_len) == 0)
        {
            mqtt_handle_command(event->data, event->data_len);
        }
        break;

    default:
        break;
    }
}

esp_err_t mqtt_integration_init(void)
{
    if (mqtt_client != NULL)
    {
        ESP_LOGW(TAG, "MQTT already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Create mutex
    mqtt_mutex = xSemaphoreCreateMutex();
    if (mqtt_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Form broker URL from configuration
    char broker_url[128];
    if (CONFIG_MQTT_USE_SSL)
    {
        snprintf(broker_url, sizeof(broker_url), "mqtts://%s:%d", CONFIG_MQTT_BROKER_HOST, CONFIG_MQTT_BROKER_PORT);
    }
    else
    {
        snprintf(broker_url, sizeof(broker_url), "mqtt://%s:%d", CONFIG_MQTT_BROKER_HOST, CONFIG_MQTT_BROKER_PORT);
    }

    // Конфигурация MQTT клиента
    esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = broker_url,
        .credentials.client_id = CONFIG_MQTT_CLIENT_ID,
    };

    // Добавляем аутентификацию если настроена
    if (strlen(CONFIG_MQTT_USERNAME) > 0)
    {
        mqtt_config.credentials.username = CONFIG_MQTT_USERNAME;
        if (strlen(CONFIG_MQTT_PASSWORD) > 0)
        {
            mqtt_config.credentials.password = CONFIG_MQTT_PASSWORD;
        }
    }

    ESP_LOGI(TAG, "MQTT broker: %s, client ID: %s", broker_url, CONFIG_MQTT_CLIENT_ID);

    // Создаем MQTT клиент
    mqtt_client = esp_mqtt_client_init(&mqtt_config);
    if (mqtt_client == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        vSemaphoreDelete(mqtt_mutex);
        return ESP_FAIL;
    }

    // Регистрируем обработчик событий
    esp_err_t ret = esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register event handler");
        esp_mqtt_client_destroy(mqtt_client);
        vSemaphoreDelete(mqtt_mutex);
        return ret;
    }

    // Запускаем MQTT клиент
    ret = esp_mqtt_client_start(mqtt_client);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start MQTT client");
        esp_mqtt_client_destroy(mqtt_client);
        vSemaphoreDelete(mqtt_mutex);
        return ret;
    }

    ESP_LOGI(TAG, "MQTT integration initialized");
    return ESP_OK;
}

esp_err_t mqtt_integration_deinit(void)
{
    if (mqtt_client == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(mqtt_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to get mutex for deinitialization");
        return ESP_ERR_TIMEOUT;
    }

#ifdef CONFIG_MQTT_HA_DISCOVERY_ENABLED
    // Удаляем конфигурацию из Home Assistant
    mqtt_integration_remove_discovery_config();
#endif

    esp_mqtt_client_stop(mqtt_client);
    esp_mqtt_client_destroy(mqtt_client);
    mqtt_client = NULL;
    mqtt_connected = false;

    xSemaphoreGive(mqtt_mutex);
    vSemaphoreDelete(mqtt_mutex);
    mqtt_mutex = NULL;

    ESP_LOGI(TAG, "MQTT integration deinitialized");
    return ESP_OK;
}

esp_err_t mqtt_integration_publish_position(uint8_t position)
{
    if (!mqtt_connected || mqtt_client == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    char payload[16];
    snprintf(payload, sizeof(payload), "%d", position);

    int msg_id = esp_mqtt_client_publish(mqtt_client, CONFIG_MQTT_TOPIC_POSITION, payload, 0, 1, 0);
    if (msg_id == -1)
    {
        ESP_LOGE(TAG, "Failed to publish position");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Position published to topic %s: %s", CONFIG_MQTT_TOPIC_POSITION, payload);
    return ESP_OK;
}

esp_err_t mqtt_integration_publish_movement(bool is_moving, bool direction_up)
{
    if (!mqtt_connected || mqtt_client == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    char payload[32];
    if (is_moving)
    {
        snprintf(payload, sizeof(payload), "%s", direction_up ? "moving_up" : "moving_down");
    }
    else
    {
        snprintf(payload, sizeof(payload), "stopped");
    }

    int msg_id = esp_mqtt_client_publish(mqtt_client, CONFIG_MQTT_TOPIC_MOVEMENT, payload, 0, 1, 0);
    if (msg_id == -1)
    {
        ESP_LOGE(TAG, "Failed to publish movement");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Movement published to topic %s: %s", CONFIG_MQTT_TOPIC_MOVEMENT, payload);
    return ESP_OK;
}

esp_err_t mqtt_integration_publish_state(uint8_t position, bool is_moving, bool direction_up)
{
    if (!mqtt_connected || mqtt_client == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    char state_payload[32];

    // Определяем состояние согласно спецификации Home Assistant Cover
    if (is_moving)
    {
        if (direction_up)
        {
            snprintf(state_payload, sizeof(state_payload), "opening");
        }
        else
        {
            snprintf(state_payload, sizeof(state_payload), "closing");
        }
    }
    else
    {
        if (position == 0)
        {
            snprintf(state_payload, sizeof(state_payload), "closed");
        }
        else if (position == 100)
        {
            snprintf(state_payload, sizeof(state_payload), "open");
        }
        else
        {
            // Для промежуточных позиций используем "open" с позиционной информацией
            snprintf(state_payload, sizeof(state_payload), "open");
        }
    }

    int msg_id = esp_mqtt_client_publish(mqtt_client, CONFIG_MQTT_TOPIC_STATE, state_payload, 0, 1, true);
    if (msg_id == -1)
    {
        ESP_LOGE(TAG, "Failed to publish state");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "State published to topic %s: %s", CONFIG_MQTT_TOPIC_STATE, state_payload);
    return ESP_OK;
}

esp_err_t mqtt_integration_subscribe_commands(void)
{
    if (!mqtt_connected || mqtt_client == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_subscribe(mqtt_client, CONFIG_MQTT_TOPIC_COMMAND, 1);
    if (msg_id == -1)
    {
        ESP_LOGE(TAG, "Failed to subscribe to commands");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Subscribed to commands: %s", CONFIG_MQTT_TOPIC_COMMAND);
    return ESP_OK;
}

bool mqtt_integration_is_connected(void)
{
    return mqtt_connected;
}

#ifdef CONFIG_MQTT_HA_DISCOVERY_ENABLED
// Получение уникального идентификатора устройства
static void get_device_unique_id(char *buffer, size_t buffer_size)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(buffer, buffer_size, "%s_%02X%02X%02X",
             CONFIG_MQTT_HA_DEVICE_ID, mac[3], mac[4], mac[5]);
}

// Публикация конфигурации для Home Assistant MQTT Discovery
esp_err_t mqtt_integration_publish_discovery_config(void)
{
    if (!mqtt_connected || mqtt_client == NULL)
    {
        ESP_LOGW(TAG, "MQTT not connected, cannot publish discovery config");
        return ESP_ERR_INVALID_STATE;
    }

    char device_unique_id[32];
    get_device_unique_id(device_unique_id, sizeof(device_unique_id));

    // Формируем топик для конфигурации cover
    char discovery_topic[256];
    snprintf(discovery_topic, sizeof(discovery_topic),
             "%s/cover/%s/config",
             CONFIG_MQTT_HA_DISCOVERY_PREFIX, device_unique_id);

    // Формируем JSON конфигурацию для Home Assistant
    char config_payload[1024];
    snprintf(config_payload, sizeof(config_payload),
             "{"
             "\"~\":\"%s\","
             "\"name\":\"%s\","
             "\"unique_id\":\"%s_cover\","
             "\"device\":{"
             "\"identifiers\":[\"%s\"],"
             "\"name\":\"%s\","
             "\"model\":\"MatterBlinds ESP32\","
             "\"manufacturer\":\"MatterBlinds Project\""
             "},"
             "\"position_topic\":\"~/position\","
             "\"position_open\":100,"
             "\"position_closed\":0,"
             "\"set_position_topic\":\"~/command\","
             "\"command_topic\":\"~/command\","
             "\"state_topic\":\"~/state\","
             "\"payload_open\":\"OPEN\","
             "\"payload_close\":\"CLOSE\","
             "\"payload_stop\":\"STOP\","
             "\"state_open\":\"open\","
             "\"state_closed\":\"closed\","
             "\"state_closing\":\"closing\","
             "\"state_opening\":\"opening\","
             "\"availability_topic\":\"~/availability\","
             "\"payload_available\":\"online\","
             "\"payload_not_available\":\"offline\""
             "}",
             CONFIG_MQTT_TOPIC_POSITION,
             CONFIG_MQTT_HA_COVER_NAME,
             device_unique_id,
             device_unique_id,
             CONFIG_MQTT_HA_DEVICE_NAME);

    // Публикуем конфигурацию сretain flag
    int msg_id = esp_mqtt_client_publish(mqtt_client, discovery_topic, config_payload, 0, 1, true);
    if (msg_id == -1)
    {
        ESP_LOGE(TAG, "Failed to publish discovery config");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Published HA discovery config to %s", discovery_topic);

    // Публикуем статус доступности
    char availability_topic[256];
    snprintf(availability_topic, sizeof(availability_topic),
             "%s/availability", CONFIG_MQTT_TOPIC_POSITION);

    int avail_msg_id = esp_mqtt_client_publish(mqtt_client, availability_topic, "online", 0, 1, true);
    if (avail_msg_id == -1)
    {
        ESP_LOGE(TAG, "Failed to publish availability status");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Published HA availability status to %s", availability_topic);
    return ESP_OK;
}

// Удаление конфигурации из Home Assistant
esp_err_t mqtt_integration_remove_discovery_config(void)
{
    if (!mqtt_connected || mqtt_client == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    char device_unique_id[32];
    get_device_unique_id(device_unique_id, sizeof(device_unique_id));

    // Формируем топик для конфигурации cover
    char discovery_topic[256];
    snprintf(discovery_topic, sizeof(discovery_topic),
             "%s/cover/%s/config",
             CONFIG_MQTT_HA_DISCOVERY_PREFIX, device_unique_id);

    // Публикуем пустое сообщение для удаления конфигурации
    int msg_id = esp_mqtt_client_publish(mqtt_client, discovery_topic, "", 0, 1, true);
    if (msg_id == -1)
    {
        ESP_LOGE(TAG, "Failed to remove discovery config");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Removed HA discovery config from %s", discovery_topic);
    return ESP_OK;
}

#else
// Заглушки если HA Discovery отключен
esp_err_t mqtt_integration_publish_discovery_config(void)
{
    ESP_LOGD(TAG, "HA Discovery is disabled");
    return ESP_OK;
}

esp_err_t mqtt_integration_remove_discovery_config(void)
{
    ESP_LOGD(TAG, "HA Discovery is disabled");
    return ESP_OK;
}
#endif // CONFIG_MQTT_HA_DISCOVERY_ENABLED
