// components/position_sensor/position_sensor.h
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"

// Пины конфигурации из Kconfig
#define POSITION_SENSOR_POWER_PIN (gpio_num_t) CONFIG_POSITION_SENSOR_POWER_PIN
#define POSITION_SENSOR_ADC_UNIT (adc_unit_t)(CONFIG_POSITION_SENSOR_ADC_UNIT - 1) // ADC1 = 0, ADC2 = 1
#define POSITION_SENSOR_ADC_CHANNEL (adc_channel_t) CONFIG_POSITION_SENSOR_ADC_CHANNEL
#define POSITION_SENSOR_ADC_ATTENUATION (adc_atten_t) CONFIG_POSITION_SENSOR_ADC_ATTENUATION
#define POSITION_SENSOR_STABILIZATION_MS CONFIG_POSITION_SENSOR_STABILIZATION_MS

#ifdef __cplusplus
extern "C"
{
#endif

    void position_sensor_init(void);
    void position_sensor_power(bool on);
    bool position_sensor_is_power();
    uint32_t position_sensor_read(void);
    uint32_t position_sensor_read_raw(void);

#ifdef __cplusplus
}
#endif
