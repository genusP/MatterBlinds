#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <driver/ledc.h>
#include <driver/gpio.h>
#include "motor_control.h"
#include "position_sensor.h"
#include "sdkconfig.h"

#define SPEED_MAX 8191 // 13-bit PWM
#define THRESHOLD 50   // Погрешность АЦП

static const char *TAG = "motor_control";

typedef struct
{
    uint32_t target_pos = 0;
    uint32_t max_pos = 0;
    uint32_t min_pos = 0;
    int32_t speed = 0;
    motor_status_t status = MOTOR_IDLE;
} controller_state_t;

volatile static controller_state_t state = {};
static TaskHandle_t motor_task_handle = NULL;
static motor_status_changed_cb_t status_cb = NULL;

static void motor_control_task(void *pvParameters);

void motor_init(uint32_t max_pos, uint32_t min_pos)
{
    // 1. Конфигурация таймера ШИМ
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_13_BIT, // Тот самый 8191
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000, // 5 кГц — стандарт для моторов
        .clk_cfg = LEDC_AUTO_CLK};
    ledc_timer_config(&ledc_timer);

    // 2. Конфигурация канала для FORWARD (Pin 12)
    ledc_channel_config_t channel_fwd = {
        .gpio_num = CONFIG_MOTOR_PIN_FWD,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0};
    ledc_channel_config(&channel_fwd);

    // 3. Конфигурация канала для REVERSE (Pin 13)
    ledc_channel_config_t channel_rev = {
        .gpio_num = CONFIG_MOTOR_PIN_REV,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0};
    ledc_channel_config(&channel_rev);

    state.max_pos = max_pos;
    state.min_pos = min_pos;
    if (motor_task_handle == NULL)
    {
        xTaskCreate(motor_control_task, "motor_ctrl", 4096, NULL, 5, &motor_task_handle);
    }
}

void motor_control_set_status_callback(motor_status_changed_cb_t cb)
{
    status_cb = cb;
}

static void update_status(motor_status_t new_status)
{
    if (state.status != new_status)
    {
        state.status = new_status;
        if (status_cb)
        {
            status_cb(new_status); // Уведомляем контроллер
        }
    }
}

void motor_move_to_position(uint32_t pos)
{
    if (pos > state.max_pos)
    {
        ESP_LOGW(TAG, "Target pos '%lu' great to max value '%lu'", pos, state.max_pos);
        state.target_pos = state.max_pos;
    }
    else if (pos < state.min_pos)
    {
        ESP_LOGW(TAG, "Target pos '%lu' less to min value '%lu'", pos, state.min_pos);
        state.target_pos = state.min_pos;
    }
    else
    {
        state.target_pos = pos;
    }

    position_sensor_power(true);
    auto curPos = position_sensor_read_raw();
    ESP_LOGD(TAG, "Curret pos '%lu'. Target pos '%lu'", curPos, state.target_pos);

    state.speed = curPos < pos ? SPEED_MAX : -SPEED_MAX;
    update_status(curPos < pos ? MOTOR_MOVING_FORWARD : MOTOR_MOVING_REVERSE);
}

motor_status_t motor_get_status(void)
{
    return state.status;
}

void motor_start_forward()
{
    state.speed = SPEED_MAX;
    state.target_pos = state.max_pos;
    update_status(MOTOR_MOVING_FORWARD);
    position_sensor_power(true);
}

void motor_start_reverce()
{
    state.speed = -SPEED_MAX;
    state.target_pos = state.min_pos;
    update_status(MOTOR_MOVING_REVERSE);
    position_sensor_power(true);
}

void motor_stop()
{
    state.target_pos = 0;
    update_status(MOTOR_IDLE);
    // position_sensor_power(false); отключение питания в установки скорости
}

// Плавная установка скорости (-8191 до 8191)
void set_motor_speed_smooth(int target_speed)
{
    static int current_speed = 0;
    const int step = 400; // Шаг изменения скорости за 10мс

    if (current_speed < target_speed)
    {
        current_speed = (current_speed + step > target_speed) ? target_speed : current_speed + step;
    }
    else if (current_speed > target_speed)
    {
        current_speed = (current_speed - step < target_speed) ? target_speed : current_speed - step;
    }

    if (current_speed >= 0)
    {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, current_speed);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
    }
    else
    {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, -current_speed);
    }
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);

    if (current_speed == 0)
    {
        position_sensor_power(false);
    }
}

static void motor_control_task(void *pvParameters)
{
    uint32_t stall_counter = 0;
    int last_adc = 0;
    const uint32_t STALL_TIMEOUT_MS = 1500; // 1.5 секунды на заклинивание
    const int STALL_THRESHOLD = 5;          // Минимальное изменение АЦП
    const int ITERATION_DELAY = 10;

    while (1)
    {
        if (state.status == MOTOR_MOVING_FORWARD || state.status == MOTOR_MOVING_REVERSE)
        {
            int cur_adc = position_sensor_read_raw();
            int diff = (int)state.target_pos - cur_adc;

            // --- ЗАЩИТА ОТ ЗАКЛИНИВАНИЯ ---
            // if (abs(cur_adc - last_adc) < STALL_THRESHOLD)
            // {
            //     stall_counter += ITERATION_DELAY; // Добавляем время цикла (10мс)
            // }
            // else
            // {
            //     stall_counter = 0; // Движение есть, сбрасываем счетчик
            //     last_adc = cur_adc;
            // }

            // if (stall_counter >= STALL_TIMEOUT_MS)
            // {
            //     ESP_LOGE(TAG, "Motor STALL detected! Emergency stop.");
            //     motor_stop();
            //     stall_counter = 0;
            //     update_status(MOTOR_EMERGENCY_STOP);
            // }
            // ------------------------------

            // Логика остановки по достижению цели
            if (abs(diff) < THRESHOLD)
            {
                motor_stop();
            }
            else
            {
                int current_target_speed = state.speed;
                if (abs(diff) < 500)
                    current_target_speed /= 2;
                set_motor_speed_smooth(current_target_speed);
            }
        }
        else
        {
            set_motor_speed_smooth(0);
            stall_counter = 0; // Сбрасываем в простое
        }
        vTaskDelay(pdMS_TO_TICKS(ITERATION_DELAY));
    }
}
