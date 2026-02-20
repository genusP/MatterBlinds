#include "motor_control.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "motor_control";

// Конфигурация GPIO пинов для ULN2003 из Kconfig
#define MOTOR_PIN_1 CONFIG_MOTOR_PIN_1
#define MOTOR_PIN_2 CONFIG_MOTOR_PIN_2
#define MOTOR_PIN_3 CONFIG_MOTOR_PIN_3
#define MOTOR_PIN_4 CONFIG_MOTOR_PIN_4
#define MOTOR_ENABLE_PIN CONFIG_MOTOR_ENABLE_PIN

// Параметры шагового двигателя из Kconfig
#define STEPS_PER_REVOLUTION CONFIG_MOTOR_STEPS_PER_REVOLUTION
#define MICROSECONDS_PER_STEP_MIN 800 // Минимальная задержка между шагами

// Последовательности шагов для полношагового режима
static const uint8_t step_sequence_full[][4] = {
    {1, 0, 0, 0},
    {0, 1, 0, 0},
    {0, 0, 1, 0},
    {0, 0, 0, 1}};

// Последовательности шагов для полушагового режима (более плавный)
static const uint8_t step_sequence_half[][4] = {
    {1, 0, 0, 0},
    {1, 1, 0, 0},
    {0, 1, 0, 0},
    {0, 1, 1, 0},
    {0, 0, 1, 0},
    {0, 0, 1, 1},
    {0, 0, 0, 1},
    {1, 0, 0, 1}};

// Структура состояния двигателя
typedef struct
{
    bool is_moving;
    motor_direction_t current_direction;
    uint32_t current_speed;
    uint32_t remaining_steps;
    uint32_t current_step;
    bool use_half_step;
    bool enable_pin_active;
    esp_timer_handle_t step_timer;
    TaskHandle_t motor_task_handle;
} motor_state_t;

static motor_state_t motor_state = {0};

// Прототипы внутренних функций
static void motor_set_gpio_mode(void);
static void motor_write_step(uint8_t step_index);
static void motor_step_callback(void *arg);
static void motor_control_task(void *parameter);
static uint32_t calculate_delay_from_speed(uint32_t speed);
static void motor_enable(bool enable);

void motor_control_init(void)
{
    ESP_LOGI(TAG, "Initializing motor control for ULN2003");

    // Инициализация состояния
    memset(&motor_state, 0, sizeof(motor_state));
    motor_state.current_direction = MOTOR_DIR_STOP;
    motor_state.current_speed = CONFIG_MOTOR_DEFAULT_SPEED;
    motor_state.use_half_step = CONFIG_MOTOR_USE_HALF_STEP;

    // Настройка GPIO
    motor_set_gpio_mode();

    // Создание таймера для шагов
    esp_timer_create_args_t timer_args = {
        .callback = &motor_step_callback,
        .name = "motor_step_timer"};

    esp_err_t ret = esp_timer_create(&timer_args, &motor_state.step_timer);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create step timer: %s", esp_err_to_name(ret));
        return;
    }

    // Установка всех пинов в LOW
    motor_write_step(0);

    // Включение двигателя
    motor_enable(true);

    // Создание задачи управления
    xTaskCreate(motor_control_task, "motor_control", 2048, NULL, 5, &motor_state.task_handle);

    ESP_LOGI(TAG, "Motor control initialized. Pins: IN1=%d, IN2=%d, IN3=%d, IN4=%d, EN=%d",
             MOTOR_PIN_1, MOTOR_PIN_2, MOTOR_PIN_3, MOTOR_PIN_4, MOTOR_ENABLE_PIN);
}

static void motor_set_gpio_mode(void)
{
    // Настройка пинов управления катушками
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MOTOR_PIN_1) | (1ULL << MOTOR_PIN_2) |
                        (1ULL << MOTOR_PIN_3) | (1ULL << MOTOR_PIN_4),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);

    // Настройка пина управления питанием (если используется)
    if (MOTOR_ENABLE_PIN >= 0)
    {
        gpio_config_t enable_conf = {
            .pin_bit_mask = (1ULL << MOTOR_ENABLE_PIN),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE};
        gpio_config(&enable_conf);
    }
}

static void motor_enable(bool enable)
{
    if (MOTOR_ENABLE_PIN >= 0)
    {
        gpio_set_level(MOTOR_ENABLE_PIN, enable ? 1 : 0);
        motor_state.enable_pin_active = enable;

        // Небольшая задержка для стабилизации
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void motor_write_step(uint8_t step_index)
{
    const uint8_t *sequence;
    uint8_t sequence_size;

    if (motor_state.use_half_step)
    {
        sequence = step_sequence_half[0];
        sequence_size = 8;
    }
    else
    {
        sequence = step_sequence_full[0];
        sequence_size = 4;
    }

    if (step_index >= sequence_size)
    {
        step_index = 0;
    }

    // Копируем нужный шаг из последовательности
    uint8_t step[4];
    if (motor_state.use_half_step)
    {
        memcpy(step, step_sequence_half[step_index], 4);
    }
    else
    {
        memcpy(step, step_sequence_full[step_index], 4);
    }

    // Устанавливаем уровни на пины
    gpio_set_level(MOTOR_PIN_1, step[0]);
    gpio_set_level(MOTOR_PIN_2, step[1]);
    gpio_set_level(MOTOR_PIN_3, step[2]);
    gpio_set_level(MOTOR_PIN_4, step[3]);
}

static uint32_t calculate_delay_from_speed(uint32_t speed)
{
    // speed: 1-100, где 1 - медленно, 100 - быстро
    if (speed == 0)
        speed = 1;
    if (speed > 100)
        speed = 100;

    // Конвертируем скорость в задержку
    // Чем выше скорость, тем меньше задержка
    uint32_t max_delay = 5000;                      // 5ms для самой медленной скорости
    uint32_t min_delay = MICROSECONDS_PER_STEP_MIN; // 0.8ms для самой быстрой скорости

    uint32_t delay = max_delay - (speed * (max_delay - min_delay) / 100);

    return delay;
}

static void motor_step_callback(void *arg)
{
    if (!motor_state.is_moving || motor_state.remaining_steps == 0)
    {
        return;
    }

    // Вычисляем следующий шаг
    uint8_t sequence_size = motor_state.use_half_step ? 8 : 4;

    if (motor_state.current_direction == MOTOR_DIR_UP)
    {
        motor_state.current_step = (motor_state.current_step + 1) % sequence_size;
    }
    else if (motor_state.current_direction == MOTOR_DIR_DOWN)
    {
        motor_state.current_step = (motor_state.current_step == 0) ? (sequence_size - 1) : (motor_state.current_step - 1);
    }

    // Выводим шаг на пины
    motor_write_step(motor_state.current_step);

    // Уменьшаем количество оставшихся шагов
    motor_state.remaining_steps--;

    // Если шаги закончились, останавливаем двигатель
    if (motor_state.remaining_steps == 0)
    {
        motor_stop();
    }
}

void motor_set_direction(motor_direction_t direction)
{
    if (direction == motor_state.current_direction)
    {
        return;
    }

    ESP_LOGI(TAG, "Setting motor direction: %d", direction);

    motor_state.current_direction = direction;

    // Если двигатель движется, перезапускаем с новым направлением
    if (motor_state.is_moving && motor_state.remaining_steps > 0)
    {
        // Останавливаем текущий таймер
        esp_timer_stop(motor_state.step_timer);

        // Перезапускаем с новым направлением
        uint32_t delay = calculate_delay_from_speed(motor_state.current_speed);
        esp_timer_start_periodic(motor_state.step_timer, delay);
    }
}

void motor_set_speed(uint32_t speed)
{
    if (speed == motor_state.current_speed)
    {
        return;
    }

    ESP_LOGI(TAG, "Setting motor speed: %lu", speed);

    motor_state.current_speed = speed;

    // Если двигатель движется, обновляем задержку таймера
    if (motor_state.is_moving && motor_state.remaining_steps > 0)
    {
        uint32_t delay = calculate_delay_from_speed(speed);

        // Перезапускаем таймер с новой задержкой
        esp_timer_stop(motor_state.step_timer);
        esp_timer_start_periodic(motor_state.step_timer, delay);
    }
}

void motor_step(uint32_t steps)
{
    if (steps == 0)
    {
        motor_stop();
        return;
    }

    ESP_LOGI(TAG, "Starting motor for %lu steps", steps);

    // Останавливаем текущее движение
    esp_timer_stop(motor_state.step_timer);

    // Устанавливаем параметры движения
    motor_state.remaining_steps = steps;
    motor_state.is_moving = true;

    // Включаем двигатель
    motor_enable(true);

    // Запускаем таймер
    uint32_t delay = calculate_delay_from_speed(motor_state.current_speed);
    esp_err_t ret = esp_timer_start_periodic(motor_state.step_timer, delay);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start step timer: %s", esp_err_to_name(ret));
        motor_state.is_moving = false;
        return;
    }
}

bool motor_is_moving(void)
{
    return motor_state.is_moving;
}

void motor_stop(void)
{
    if (!motor_state.is_moving)
    {
        return;
    }

    ESP_LOGI(TAG, "Stopping motor");

    // Останавливаем таймер
    esp_timer_stop(motor_state.step_timer);

    // Сбрасываем состояние
    motor_state.is_moving = false;
    motor_state.remaining_steps = 0;
    motor_state.current_direction = MOTOR_DIR_STOP;

    // Устанавливаем все пины в LOW для экономии энергии
    motor_write_step(0);

// Выключаем питание двигателя (если есть пин включения)
#ifdef CONFIG_MOTOR_DISABLE_ON_STOP
    motor_enable(false);
#endif
}

static void motor_control_task(void *parameter)
{
    ESP_LOGI(TAG, "Motor control task started");

    while (1)
    {
        // Задача для мониторинга состояния и обработки крайних случаев
        vTaskDelay(pdMS_TO_TICKS(100));

        // Проверка на зависание таймера
        if (motor_state.is_moving && motor_state.remaining_steps > 0)
        {
            // Можно добавить проверку таймаута здесь
        }
    }
}

// Дополнительные функции для расширенного управления

void motor_set_step_mode(bool half_step)
{
    motor_state.use_half_step = half_step;
    ESP_LOGI(TAG, "Step mode set to: %s", half_step ? "half-step" : "full-step");
}

uint32_t motor_get_position_steps(void)
{
    // Возвращает текущую позицию в шагах от начала
    // Для этого нужно добавить счетчик абсолютной позиции
    return motor_state.current_step;
}

void motor_move_degrees(float degrees)
{
    // Конвертируем градусы в шаги
    // 360 градусов = STEPS_PER_REVOLUTION шагов
    float steps_f = (degrees * STEPS_PER_REVOLUTION) / 360.0f;
    uint32_t steps = (uint32_t)steps_f;

    motor_step(steps);
}

void motor_move_rotations(float rotations)
{
    // Конвертируем обороты в шаги
    uint32_t steps = (uint32_t)(rotations * STEPS_PER_REVOLUTION);
    motor_step(steps);
}
