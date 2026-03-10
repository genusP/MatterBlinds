// main.controller.cpp

#include "controller.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "motor_control.h"
#include <iot_button.h>
#include "calibration.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "controller";

// Кэш конфигурации
static controller_state_t g_state = IDLE;
static calibration_data_t g_calibration_data = {.top_limit = 100, .bottom_limit = 4000};

// Система подписки на события изменения состояния
static struct
{
    controller_state_changed_cb_t callbacks[CONTROLLER_MAX_SUBSCRIBERS];
    void *user_data[CONTROLLER_MAX_SUBSCRIBERS];
    uint8_t count;
} g_state_subscribers = {0};

// кнопки
static button_handle_t g_button_up = NULL;
static button_handle_t g_button_down = NULL;

// Объявления функций
static void controller_button_callback(void *button_handle, void *user_data);
static void controller_button_press_up(void *arg, void *user_data);
static void button_handler_init(void);
static void motor_control_status_changed(motor_status_t status);
#ifdef CONFIG_ZEBRA_BLINDS_SUPPORT
static void controller_handle_zebra_offset(void);
#endif

const char *controller_state_text(controller_state_t state)
{
    switch (state)
    {
    case IDLE:
        return "IDLE";
    case MOVING_UP:
        return "MOVING_UP";
    case MOVING_DOWN:
        return "MOVING_DOWN";
    case EMERGENCY_STOP:
        return "EMERGENCY_STOP";
    case CALIBRATING_INIT:
        return "CALIBRATING_INIT";
    case CALIBRATING:
        return "CALIBRATING";
    default:
        return "UNKNOWN";
    }
}

bool is_calibrated() { return g_calibration_data.state == CALIBRATED; }
bool is_calibrating() { return g_state == CALIBRATING || g_state == CALIBRATING_INIT; }

void update_state(controller_state_t state)
{
    if (g_state != state)
    {
        controller_state_t old_state = g_state;
        ESP_LOGD(TAG, "Change controller state: %s -> %s", controller_state_text(old_state), controller_state_text(state));
        g_state = state;

        // Уведомляем всех подписчиков об изменении состояния
        for (uint8_t i = 0; i < g_state_subscribers.count; i++)
        {
            if (g_state_subscribers.callbacks[i] != NULL)
            {
                g_state_subscribers.callbacks[i](old_state, state, g_state_subscribers.user_data[i]);
            }
        }
    }
}
void controller_init(void)
{
    ESP_LOGI(TAG, "Initializing controller");

    // Инициализация подсистем
    read_calibration_data(&g_calibration_data);
    position_sensor_init();
    motor_init(g_calibration_data.bottom_limit, g_calibration_data.top_limit);
    motor_control_set_status_callback(motor_control_status_changed);
    button_handler_init();

    update_state(
        is_calibrated()
            ? controller_state_t::IDLE
            // переводим в готовность к калибровке чтоб перейти при любом нажатии перейтив калибровку
            : controller_state_t::CALIBRATING_INIT);

    ESP_LOGI(TAG, "Controller initialized. Calibrated: %s",
             is_calibrated() ? "Yes" : "No");
}

void button_handler_init(void)
{
    ESP_LOGI(TAG, "Initializing button handler");

    // Инициализация кнопки вверх
    button_config_t up_button_config = {
        .type = BUTTON_TYPE_GPIO,
        .short_press_time = 50,
        .gpio_button_config = {
            .gpio_num = CONFIG_BUTTON_UP_PIN,
            .active_level = 0, // Активный низкий уровень (кнопка замыкает на GND)
        },
    };

    g_button_up = iot_button_create(&up_button_config);
    if (g_button_up == NULL)
    {
        ESP_LOGE(TAG, "Failed to create up button");
        return;
    }

    // Инициализация кнопки вниз
    button_config_t down_button_config = {
        .type = BUTTON_TYPE_GPIO,
        .short_press_time = 50,
        .gpio_button_config = {
            .gpio_num = CONFIG_BUTTON_DOWN_PIN,
            .active_level = 0, // Активный низкий уровень
        },
    };

    g_button_down = iot_button_create(&down_button_config);
    if (g_button_down == NULL)
    {
        ESP_LOGE(TAG, "Failed to create down button");
        return;
    }

    // Регистрация обработчиков для кнопки вверх
    iot_button_register_cb(g_button_up, BUTTON_SINGLE_CLICK, controller_button_callback, NULL);
    iot_button_register_cb(g_button_up, BUTTON_DOUBLE_CLICK, controller_button_callback, NULL);
    iot_button_register_cb(g_button_up, BUTTON_LONG_PRESS_START, controller_button_callback, NULL);
    iot_button_register_cb(g_button_up, BUTTON_LONG_PRESS_UP, controller_button_press_up, NULL);

    // Регистрация обработчиков для кнопки вниз
    iot_button_register_cb(g_button_down, BUTTON_SINGLE_CLICK, controller_button_callback, NULL);
    iot_button_register_cb(g_button_down, BUTTON_DOUBLE_CLICK, controller_button_callback, NULL);
    iot_button_register_cb(g_button_down, BUTTON_LONG_PRESS_START, controller_button_callback, NULL);
    iot_button_register_cb(g_button_down, BUTTON_LONG_PRESS_UP, controller_button_press_up, NULL);

    ESP_LOGI(TAG, "Button handler initialized successfully");
}

bool is_moving_allowed()
{
    if (is_calibrating())
    {
        ESP_LOGW(TAG, "Cannot move during calibration");
        return false;
    }
    if (!is_calibrated())
    {
        ESP_LOGW(TAG, "Not calinrated, cannot move.");
        return false;
    }
    return true;
}

void controller_move_to_position(uint32_t position)
{
    if (is_moving_allowed())
    {
        ESP_LOGI(TAG, "Moving to position %lu", position);

        motor_move_to_position(position);
    }
}

void controller_move_up(void)
{
    if (is_moving_allowed())
    {
        ESP_LOGI(TAG, "Moving up");
        motor_start_forward();
    }
}

void controller_move_down(void)
{
    if (is_moving_allowed())
    {
        ESP_LOGI(TAG, "Moving down");
        motor_start_reverce();
    }
}

void controller_goto_top(void)
{
    if (is_moving_allowed())
    {
        // Получаем реальную минимальную позицию из position_sensor
        uint32_t min_pos = g_calibration_data.top_limit;
        controller_move_to_position(min_pos);
    }
}

void controller_goto_bottom(void)
{
    if (is_moving_allowed())
    {
        // Получаем реальную максимальную позицию из position_sensor
        uint32_t max_pos = g_calibration_data.bottom_limit;
        controller_move_to_position(max_pos);
    }
}

controller_state_t controller_get_state(void)
{
    return g_state;
}

void controller_set_position_percentage(float percentage)
{
    if (is_moving_allowed())
    {
        if (percentage < 0.0f)
            percentage = 0.0f;
        if (percentage > 100.0f)
            percentage = 100.0f;

        // Получаем реальные границы из position_sensor
        uint32_t min_pos = g_calibration_data.top_limit;
        uint32_t max_pos = g_calibration_data.bottom_limit;
        uint32_t range = max_pos - min_pos;
        uint32_t target_position = min_pos + (uint32_t)(range * (100.0f - percentage) / 100.0f);

        ESP_LOGI(TAG, "Setting position %.1f%% (ADC: %lu, range: %lu-%lu)",
                 percentage, target_position, min_pos, max_pos);

        controller_move_to_position(target_position);
    }
}

float controller_get_position_percentage()
{
    auto pos = position_sensor_is_power()
                   ? position_sensor_read_raw()
                   : position_sensor_read();
    auto perc = pos > g_calibration_data.bottom_limit
                    ? 100.0f
                : pos < g_calibration_data.top_limit
                    ? 0.0f
                    : (g_calibration_data.bottom_limit - g_calibration_data.top_limit) / (float)pos;
    return 100.0f - perc;
}

void controller_stop(void)
{
    motor_stop();
}

void controller_calibrate(void)
{
    g_calibration_data = calibration_start();
}

static void controller_button_press_up(void *arg, void *user_data)
{
    if (g_state == MOVING_UP || g_state == MOVING_DOWN)
    {
        motor_stop();
    }
    else if (g_state == CALIBRATING_INIT)
    {
        button_handle_t btn_handle = (button_handle_t)arg;
        auto other_btn = btn_handle == g_button_up ? g_button_down : g_button_up;
        if (iot_button_get_key_level(other_btn) == 0)
        {
            controller_calibrate();
        }
    }
}

static void controller_button_handler_on_calibrating(button_handle_t btn_handle, button_event_t event)
{
    switch (event)
    {
    case BUTTON_SINGLE_CLICK:
        g_calibration_data = calibration_next();
        if (g_calibration_data.state == CALIBRATED)
        {
            save_calibration_data(g_calibration_data);
            esp_restart();
        }
        break;
    case BUTTON_LONG_PRESS_START:
        // нажаты обе кнопки
        if (iot_button_get_key_level(g_button_down) == 1 && iot_button_get_key_level(g_button_up) == 1)
        {
            calibration_cancel();
            esp_restart();
        }
        break;
    default:
        break;
    }
}

static void controller_button_callback(void *arg, void *user_data)
{
    button_handle_t btn_handle = (button_handle_t)arg;
    button_event_t event = iot_button_get_event(btn_handle);

    ESP_LOGD(TAG, "Button event: %s, button_id: %s, state: %d", iot_button_get_event_str(event), btn_handle == g_button_up ? "UP" : "DOWN", g_state);

    if (is_calibrating())
    {
        return controller_button_handler_on_calibrating(btn_handle, event);
    }
    else if (g_state != IDLE)
    {
        ESP_LOGD(TAG, "Current state: %s. Button event skip.", controller_state_text(g_state));
    }

    switch (event)
    {
    case BUTTON_SINGLE_CLICK:
        // Одиночное нажатие - переход в крайнее положение
        if (btn_handle == g_button_up)
        {
            controller_goto_top();
        }
        else if (btn_handle == g_button_down)
        {
            controller_goto_bottom();
        }
        break;

    case BUTTON_DOUBLE_CLICK:
        // Двойное нажатие
#ifdef CONFIG_ZEBRA_BLINDS_SUPPORT
        if (position_sensor_is_calibrated())
        {
            uint32_t zebra_offset = position_sensor_get_zebra_offset();
            if (zebra_offset > 0)
            {
                // Выполнение откалиброванного смещения для штор зебра
                controller_handle_zebra_offset(btn_handle);
                break;
            }
        }
#else
        // Переход на позицию 50%
        controller_set_position_percentage(50.0f);
#endif

        break;

    case BUTTON_LONG_PRESS_START:
        // нажаты обе кнопки
        if (iot_button_get_key_level(g_button_down) == 1 && iot_button_get_key_level(g_button_up) == 1)
        {
            // подтверждаем старт калибровки и ждем отпускания кнопок
            update_state(CALIBRATING_INIT);
            break;
        }
        else
        {
            // Движение пока кнопка удерживается
            if (btn_handle == g_button_up)
            {
                controller_move_up();
            }
            else if (btn_handle == g_button_down)
            {
                controller_move_down();
            }
        }
        break;

    default:
        break;
    }
}

#ifdef CONFIG_ZEBRA_BLINDS_SUPPORT
static void controller_handle_zebra_offset(button_handle_t btn_handle)
{
    if (is_moving_allowed())
    {

        auto offset = btn_handle == g_button_down
                          ? (int32_t)g_calibration_data.zebra_offset
                          : -(int32_t)g_calibration_data.zebra_offset;
        // Вычисляем целевую позицию с учетом смещения
        uint32_t current_pos = position_sensor_read();
        int32_t target_pos = (int32_t)current_pos + offset;

        // получам целевую позицию с учетом границ
        target_pos = target_pos < g_calibration_data.top_limit
                         ? g_calibration_data.top_limit
                     : target_pos > g_calibration_data.bottom_limit
                         ? g_calibration_data.bottom_limit
                         : target_pos;

        ESP_LOGI(TAG, "Moving to zebra offset position: %li", target_pos);
        controller_move_to_position((uint32_t)target_pos);
    }
}
#endif

void motor_control_status_changed(motor_status_t status)
{
    if (g_state < CALIBRATING_INIT)
    {
        controller_state_t r = IDLE;
        switch (status)
        {
        case motor_status_t::MOTOR_IDLE:
            r = controller_state_t::IDLE;
            break;
        case motor_status_t::MOTOR_MOVING_FORWARD:
            r = controller_state_t::MOVING_UP;
            break;
        case motor_status_t::MOTOR_MOVING_REVERSE:
            r = controller_state_t::MOVING_DOWN;
            break;
        case motor_status_t::MOTOR_EMERGENCY_STOP:
            r = controller_state_t::EMERGENCY_STOP;
            break;

        default:
            ESP_LOGW(TAG, "Motor status %d not mapped. (motor_control_status_changed)", status);
            return;
        }
        update_state(r);
    }
}

bool controller_subscribe_state_changes(controller_state_changed_cb_t callback, void *user_data)
{
    if (callback == NULL)
    {
        ESP_LOGE(TAG, "Callback function cannot be NULL");
        return false;
    }

    if (g_state_subscribers.count >= CONTROLLER_MAX_SUBSCRIBERS)
    {
        ESP_LOGE(TAG, "Maximum number of subscribers reached (%d)", CONTROLLER_MAX_SUBSCRIBERS);
        return false;
    }

    // Проверяем, что такой callback уже не зарегистрирован
    for (uint8_t i = 0; i < g_state_subscribers.count; i++)
    {
        if (g_state_subscribers.callbacks[i] == callback)
        {
            ESP_LOGW(TAG, "Callback already registered");
            return false;
        }
    }

    // Добавляем нового подписчика
    g_state_subscribers.callbacks[g_state_subscribers.count] = callback;
    g_state_subscribers.user_data[g_state_subscribers.count] = user_data;
    g_state_subscribers.count++;

    ESP_LOGD(TAG, "State change subscriber added. Total subscribers: %d", g_state_subscribers.count);

    return true;
}
