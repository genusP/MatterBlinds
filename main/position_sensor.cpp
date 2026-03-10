#include "position_sensor.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_POSITION_SENSOR_POWER_PIN > 0
#define SENSOR_POWER
#endif

static const char *TAG = "position_sensor";
static uint32_t current_position = 0;
static bool sensor_initialized = false;

// ADC Oneshot handle
static adc_oneshot_unit_handle_t adc_handle = NULL;

#ifdef SENSOR_POWER
// Внутренние функции для управления питанием
static void position_sensor_power_on(void)
{
    gpio_set_level(POSITION_SENSOR_POWER_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(POSITION_SENSOR_STABILIZATION_MS));
}

static void position_sensor_power_off(void)
{
    gpio_set_level(POSITION_SENSOR_POWER_PIN, 0);
}
#endif

void position_sensor_init(void)
{
    ESP_LOGI(TAG, "Инициализация датчика положения");

    // Инициализация GPIO для питания потенциометра
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << POSITION_SENSOR_POWER_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);

#ifdef SENSOR_POWER
    // Изначально выключаем питание
    position_sensor_power_off();
#endif

    // Инициализация ADC Oneshot
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = POSITION_SENSOR_ADC_UNIT,
    };

    esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Ошибка инициализации ADC Oneshot: %s", esp_err_to_name(ret));
        return;
    }

    // Конфигурация канала ADC
    adc_oneshot_chan_cfg_t config = {
        .atten = POSITION_SENSOR_ADC_ATTENUATION,
        .bitwidth = ADC_BITWIDTH_12,
    };

    ret = adc_oneshot_config_channel(adc_handle, POSITION_SENSOR_ADC_CHANNEL, &config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Ошибка конфигурации канала ADC: %s", esp_err_to_name(ret));
        return;
    }

    sensor_initialized = true;
    ESP_LOGI(TAG, "Датчик положения инициализирован");
    ESP_LOGI(TAG, "ADC канал: %d, пин питания: %d", POSITION_SENSOR_ADC_CHANNEL, POSITION_SENSOR_POWER_PIN);
}

void position_sensor_power(bool on)
{
#ifdef SENSOR_POWER
    if (on)
    {
        position_sensor_power_on();
    }
    else
    {
        position_sensor_power_off();
    }
#endif
}

bool position_sensor_is_power()
{
#ifdef SENSOR_POWER
    return gpio_get_level(POSITION_SENSOR_POWER_PIN) == 1;

#else
    return true;
#endif
}

uint32_t position_sensor_read_raw(void)
{
    int raw_value;
    adc_oneshot_read(adc_handle, POSITION_SENSOR_ADC_CHANNEL, &raw_value);
    return raw_value;
}

uint32_t position_sensor_read(void)
{
    if (!sensor_initialized || adc_handle == NULL)
        return 0;

#ifdef SENSOR_POWER
    position_sensor_power_on();
    vTaskDelay(pdMS_TO_TICKS(5)); // Даем питанию стабилизироваться
#endif

    uint32_t sum = 0;
    int raw_value;
    const int SAMPLES = 8; // Возьмем 8 для более плавного значения

    for (int i = 0; i < SAMPLES; i++)
    {
        if (adc_oneshot_read(adc_handle, (adc_channel_t)POSITION_SENSOR_ADC_CHANNEL, &raw_value) == ESP_OK)
        {
            sum += raw_value;
        }
        else
        {
            sum += current_position; // Если ошибка, берем старое значение
        }
        // Небольшая пауза между выборками для исключения шума
        esp_rom_delay_us(50);
    }

#ifdef SENSOR_POWER
    position_sensor_power_off();
#endif

    current_position = sum / SAMPLES;
    ESP_LOGD(TAG, "ADC Averaged: %lu", current_position);

    return current_position;
}
