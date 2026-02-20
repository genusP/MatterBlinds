// components/position_sensor/position_sensor.h
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_log.h"
#include "driver/adc.h"
#include "driver/gpio.h"

// Пины конфигурации из Kconfig
#define POSITION_SENSOR_ADC_PIN CONFIG_POSITION_SENSOR_ADC_PIN
#define POSITION_SENSOR_POWER_PIN CONFIG_POSITION_SENSOR_POWER_PIN
#define POSITION_SENSOR_ADC_UNIT (CONFIG_POSITION_SENSOR_ADC_UNIT - 1) // ADC1 = 0, ADC2 = 1
#define POSITION_SENSOR_ADC_CHANNEL CONFIG_POSITION_SENSOR_ADC_CHANNEL
#define POSITION_SENSOR_ADC_ATTENUATION CONFIG_POSITION_SENSOR_ADC_ATTENUATION
#define POSITION_SENSOR_STABILIZATION_MS CONFIG_POSITION_SENSOR_STABILIZATION_MS

#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct
    {
        uint32_t min_position;
        uint32_t max_position;
        uint32_t current_position;
        bool calibrated;
    } position_config_t;

    typedef enum
    {
        CALIBRATION_STEP_UPPER,
        CALIBRATION_STEP_LOWER,
        CALIBRATION_STEP_ZEBRA_OFFSET,
        CALIBRATION_STEP_COMPLETE
    } calibration_step_t;

    typedef const char *(*calibration_step_callback_t)(calibration_step_t step);

    void position_sensor_init(void);
    uint32_t position_sensor_read(void);
    void position_sensor_set_calibration(uint32_t min_pos, uint32_t max_pos);
    void position_sensor_calibrate_start(void);
    bool position_sensor_is_calibrated(void);
    float position_sensor_get_percentage(void);

    // Новые функции для пошаговой калибровки
    calibration_step_callback_t position_sensor_start_calibration(void);
    calibration_step_t position_sensor_next_calibration_step(void);
    void position_sensor_save_calibration_step(uint32_t position);
    uint32_t position_sensor_get_zebra_offset(void);
    uint32_t position_sensor_get_min_position(void);
    uint32_t position_sensor_get_max_position(void);
    static void position_sensor_save_calibration_data(void);

#ifdef __cplusplus
}
#endif
