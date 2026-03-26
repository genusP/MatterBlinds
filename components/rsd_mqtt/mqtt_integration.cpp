#include "include/mqtt_integration.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_netif.h"
#include "controller.h"
#include <string.h>
#include "sdkconfig.h"

static const char *TAG = "mqtt_integration";
static esp_mqtt_client_handle_t mqtt_client = NULL;
static SemaphoreHandle_t mqtt_mutex = NULL;
static TaskHandle_t position_task_handle = NULL;
static bool mqtt_connected = false;
static char broker_url[128];
static float lastPositionPerc = 0.0f;

// MQTT topic variables
static char mqtt_topic_prefix[128];
static char mqtt_topic_position[256];
static char mqtt_topic_command[256];
static char mqtt_topic_state[256];
static char mqtt_topic_availability[256];
static char mqtt_topic_error[256];

esp_err_t mqtt_integration_publish_availabilty(bool online);
esp_err_t mqtt_integration_publish_error(const char *error);
static void controller_state_handler(controller_state_t old_state, controller_state_t new_state, void *user_data);

// Получение уникального идентификатора устройства
static void get_device_unique_id(char *buffer, size_t buffer_size)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(buffer, buffer_size, "%s_%02X%02X%02X",
             CONFIG_MQTT_HA_DEVICE_ID, mac[3], mac[4], mac[5]);
}

// Инициализация MQTT топиков на основе префикса и ID устройства
static void mqtt_init_topics(void)
{
    char device_unique_id[32];
    get_device_unique_id(device_unique_id, sizeof(device_unique_id));

    // Формируем префикс топика из CONFIG_MQTT_TOPIC и device_unique_id
    snprintf(mqtt_topic_prefix, sizeof(mqtt_topic_prefix),
             "%s/%s", CONFIG_MQTT_TOPIC, device_unique_id);

    // Формируем имена топиков на основании префикса
    snprintf(mqtt_topic_position, sizeof(mqtt_topic_position),
             "%s/position", mqtt_topic_prefix);
    snprintf(mqtt_topic_command, sizeof(mqtt_topic_command),
             "%s/command", mqtt_topic_prefix);
    snprintf(mqtt_topic_state, sizeof(mqtt_topic_state),
             "%s/state", mqtt_topic_prefix);
    snprintf(mqtt_topic_availability, sizeof(mqtt_topic_availability),
             "%s/availability", mqtt_topic_prefix);
    snprintf(mqtt_topic_error, sizeof(mqtt_topic_error),
             "%s/error", mqtt_topic_prefix);

    // Выводим имена топиков в информационный лог
    ESP_LOGI(TAG, "MQTT topics initialized:");
    ESP_LOGI(TAG, "  Prefix: %s", mqtt_topic_prefix);
    ESP_LOGI(TAG, "  Position: %s", mqtt_topic_position);
    ESP_LOGI(TAG, "  Command: %s", mqtt_topic_command);
    ESP_LOGI(TAG, "  State: %s", mqtt_topic_state);
    ESP_LOGI(TAG, "  Availabilty: %s", mqtt_topic_availability);
    ESP_LOGI(TAG, "  Error: %s", mqtt_topic_error);
}

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
            auto newPos = 100 - (float)position; // преобразуем из формата ХА
            controller_set_position_percentage(newPos);
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
        // сбрасываем ошибку
        mqtt_integration_publish_error("0");
        // пробрасываем текущее состояние
        controller_state_handler(IDLE, controller_get_state(), nullptr); //
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT disconnected");
        mqtt_connected = false;
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT data received, topic: %.*s", event->topic_len, event->topic);

        // Проверяем является ли это командным топиком
        if (event->topic_len == strlen(mqtt_topic_command) &&
            memcmp(event->topic, mqtt_topic_command, event->topic_len) == 0)
        {
            mqtt_handle_command(event->data, event->data_len);
        }
        break;

    default:
        break;
    }
}

// Обработчик изменения состояния контроллера
static void controller_state_handler(controller_state_t old_state, controller_state_t new_state, void *user_data)
{
    ESP_LOGD(TAG, "New state: %s", controller_state_text(new_state));
    switch (new_state)
    {
    case IDLE:
    {
        auto perc = controller_get_position_percentage();
        mqtt_integration_publish_state(perc, false, false);
        mqtt_integration_publish_position(perc);
        break;
    }
    case MOVING_UP:
        mqtt_integration_publish_state(0, true, true);
        break;
    case MOVING_DOWN:
        mqtt_integration_publish_state(0, true, false);
        break;

    // здесь приостанавливаем мониторинг, т.к. переход из этих состояний требует перезагрузки
    case CALIBRATING:
    case CALIBRATING_INIT:
        vTaskSuspend(position_task_handle);
        mqtt_integration_publish_error("Не откалиброван.");
        mqtt_integration_publish_availabilty(false);
        return;
    case EMERGENCY_STOP:
        vTaskSuspend(position_task_handle);
        mqtt_integration_publish_error("Ошибка.");
        mqtt_integration_publish_availabilty(false);
        return;
    default:
        vTaskSuspend(position_task_handle);
        mqtt_integration_publish_availabilty(false);
        return;
    }
}

// задача обновления позиции. запускается обработчиком изменения статуса
static void position_update_task(void *args)
{
    while (1)
    {
        auto state = controller_get_state();
        auto perc = controller_get_position_percentage();
        if (lastPositionPerc != perc)
        {
            mqtt_integration_publish_position(perc);
            mqtt_integration_publish_state(perc, false, false);
        }
        lastPositionPerc = perc;

        // если идет движение обновляем каждую секунду иначе на основании конфига
        vTaskDelay(pdMS_TO_TICKS(
            (state == MOVING_DOWN || MOVING_UP)
                ? 1000
                : CONFIG_POSITION_UPDATE_PERIOD_MS));
    }
}

// Обработчик события подключения к сети
static void network_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP && mqtt_client)
    {
        // Запускаем MQTT клиент
        auto ret = esp_mqtt_client_start(mqtt_client);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to start MQTT client");
            esp_mqtt_client_destroy(mqtt_client);
        }
        ESP_LOGI(TAG, "MQTT client connected!");
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

    // Initialize MQTT topics
    mqtt_init_topics();

    // Form broker URL from configuration
#ifdef CONFIG_MQTT_USE_SSL
    snprintf(broker_url, sizeof(broker_url), "mqtts://%s:%d", CONFIG_MQTT_BROKER_HOST, CONFIG_MQTT_BROKER_PORT);
#else
    snprintf(broker_url, sizeof(broker_url), "mqtt://%s:%d", CONFIG_MQTT_BROKER_HOST, CONFIG_MQTT_BROKER_PORT);
#endif

    // Конфигурация MQTT клиента
    esp_mqtt_client_config_t mqtt_config = {};
    mqtt_config.broker.address.uri = broker_url;
    mqtt_config.credentials.client_id = CONFIG_MQTT_CLIENT_ID;

#ifdef CONFIG_MQTT_HA_DISCOVERY_ENABLED
    mqtt_config.session.last_will = {
        .topic = mqtt_topic_availability,
        .msg = "offline",
        .msg_len = 7,
        .qos = 1,
        .retain = true};
#endif

    // Добавляем аутентификацию если настроена
    if (strlen(CONFIG_MQTT_USERNAME) > 0)
    {
        mqtt_config.credentials.username = CONFIG_MQTT_USERNAME;
        if (strlen(CONFIG_MQTT_PASSWORD) > 0)
        {
            mqtt_config.credentials.authentication.password = CONFIG_MQTT_PASSWORD;
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
    esp_err_t ret = esp_mqtt_client_register_event(mqtt_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register event handler");
        esp_mqtt_client_destroy(mqtt_client);
        vSemaphoreDelete(mqtt_mutex);
        return ret;
    }

    // Регистрируем обработчик событий сети для запуска MQTT при получении IP
    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &network_event_handler, NULL, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register network event handler");
        esp_mqtt_client_destroy(mqtt_client);
        vSemaphoreDelete(mqtt_mutex);
        return ret;
    }

    xTaskCreate(position_update_task, "mqtt_pos_upd_task", 4096, nullptr, 5, &position_task_handle);

    controller_subscribe_state_changes(controller_state_handler, nullptr);

    ESP_LOGI(TAG, "MQTT integration initialized - waiting for network connection");
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

    // Отрегистрируем обработчик событий сети
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, NULL);

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

    auto newPos = 100 - position; // в HA процент показывает на сколько штора открыта, а у нас на оборот
    char payload[16];
    snprintf(payload, sizeof(payload), "%d", newPos);

    int msg_id = esp_mqtt_client_publish(mqtt_client, mqtt_topic_position, payload, 0, 1, 0);
    if (msg_id == -1)
    {
        ESP_LOGE(TAG, "Failed to publish position");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Position published to topic %s: %s (%u)", mqtt_topic_position, payload, position);
    return ESP_OK;
}

esp_err_t mqtt_integration_publish_error(const char *error)
{
    if (!mqtt_connected || mqtt_client == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    int avail_msg_id = esp_mqtt_client_publish(mqtt_client, mqtt_topic_error, error, 0, 1, true);
    if (avail_msg_id == -1)
    {
        ESP_LOGE(TAG, "Failed to publish error status");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Published HA error status to %s", mqtt_topic_error);
    return ESP_OK;
}

esp_err_t mqtt_integration_publish_availabilty(bool online)
{
    if (!mqtt_connected || mqtt_client == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    int avail_msg_id = esp_mqtt_client_publish(mqtt_client, mqtt_topic_availability, online ? "online" : "offline", 0, 1, true);
    if (avail_msg_id == -1)
    {
        ESP_LOGE(TAG, "Failed to publish availability status");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Published HA availability status to %s", mqtt_topic_availability);
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
        if (position == 100) // в отличии от ХА позиция показывает процент закрытия
        {
            snprintf(state_payload, sizeof(state_payload), "closed");
        }
        else if (position == 0)
        {
            snprintf(state_payload, sizeof(state_payload), "open");
        }
        else
        {
            // Для промежуточных позиций используем "open" с позиционной информацией
            snprintf(state_payload, sizeof(state_payload), "open");
        }
    }

    int msg_id = esp_mqtt_client_publish(mqtt_client, mqtt_topic_state, state_payload, 0, 1, true);
    if (msg_id == -1)
    {
        ESP_LOGE(TAG, "Failed to publish state");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "State published to topic %s: %s", mqtt_topic_state, state_payload);
    return ESP_OK;
}

esp_err_t mqtt_integration_subscribe_commands(void)
{
    if (!mqtt_connected || mqtt_client == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_subscribe(mqtt_client, mqtt_topic_command, 1);
    if (msg_id == -1)
    {
        ESP_LOGE(TAG, "Failed to subscribe to commands");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Subscribed to commands: %s", mqtt_topic_command);
    return ESP_OK;
}

bool mqtt_integration_is_connected(void)
{
    return mqtt_connected;
}

#ifdef CONFIG_MQTT_HA_DISCOVERY_ENABLED
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
             "\"manufacturer\":\"DYI Project\""
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
             mqtt_topic_prefix,
             CONFIG_MQTT_HA_COVER_NAME,
             device_unique_id,
             device_unique_id,
             CONFIG_MQTT_HA_DEVICE_NAME);

    // Публикуем конфигурацию с retain flag
    int msg_id = esp_mqtt_client_publish(mqtt_client, discovery_topic, config_payload, 0, 1, true);
    if (msg_id == -1)
    {
        ESP_LOGE(TAG, "Failed to publish discovery config");
        return ESP_FAIL;
    }

    // Формируем топик для конфигурации сенсора ошибки
    snprintf(discovery_topic, sizeof(discovery_topic),
             "%s/binary_sensor/%s/config",
             CONFIG_MQTT_HA_DISCOVERY_PREFIX, device_unique_id);

    snprintf(config_payload, sizeof(config_payload),
             "{"
             "\"~\":\"%s\","
             "\"name\":\"Error\","
             "\"unique_id\":\"%s_cover_errors\","
             "\"device\":{"
             "\"identifiers\":[\"%s\"],"
             "\"name\":\"%s\","
             "\"model\":\"MatterBlinds ESP32\","
             "\"manufacturer\":\"DYI Project\""
             "},"
             "\"device_class\":\"problem\","
             "\"value_template\": \"{{ 'ON' if value != '0' else 'OFF' }}\","
             "\"state_topic\":\"~/error\""
             "}",
             mqtt_topic_prefix,
             device_unique_id,
             device_unique_id,
             CONFIG_MQTT_HA_DEVICE_NAME);

    // Публикуем конфигурацию с retain flag
    msg_id = esp_mqtt_client_publish(mqtt_client, discovery_topic, config_payload, 0, 1, true);
    if (msg_id == -1)
    {
        ESP_LOGE(TAG, "Failed to publish discovery config for error binary sensor");
        return ESP_FAIL;
    }

    // Формируем топик для конфигурации сенсора текста ошибки
    snprintf(discovery_topic, sizeof(discovery_topic),
             "%s/sensor/%s/config",
             CONFIG_MQTT_HA_DISCOVERY_PREFIX, device_unique_id);

    snprintf(config_payload, sizeof(config_payload),
             "{"
             "\"~\":\"%s\","
             "\"name\":\"Error Message\","
             "\"unique_id\":\"%s_cover_error_text\","
             "\"device\":{"
             "\"identifiers\":[\"%s\"],"
             "\"name\":\"%s\","
             "\"model\":\"MatterBlinds ESP32\","
             "\"manufacturer\":\"DYI Project\""
             "},"
             "\"icon\":\"mdi:alert-circle-outline\","
             "\"value_template\": \"{%% if value == '0' %%}ОК{%% else %%}{{ value }}{%% endif %%}\","
             "\"state_topic\":\"~/error\""
             "}",
             mqtt_topic_prefix,
             device_unique_id,
             device_unique_id,
             CONFIG_MQTT_HA_DEVICE_NAME);

    // Публикуем конфигурацию с retain flag
    msg_id = esp_mqtt_client_publish(mqtt_client, discovery_topic, config_payload, 0, 1, true);
    if (msg_id == -1)
    {
        ESP_LOGE(TAG, "Failed to publish discovery config for error text sensor");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Published HA discovery config to %s", discovery_topic);

    // Публикуем статус доступности
    mqtt_integration_publish_availabilty(true);
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
