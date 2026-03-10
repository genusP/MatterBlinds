// main/calibration.h
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "position_sensor.h"
#include "motor_control.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        NONE,
        PREPARE,
        SETUP_TOP,
        SETUP_BOTTOM,
        SETUP_ZEBRA,
        CALIBRATED
    } calib_state_t;

    typedef struct
    {
        calib_state_t state = NONE;
        uint16_t top_limit = 0;    // Значение потенциометра вверху
        uint16_t bottom_limit = 0; // Значение внизу
        uint16_t zebra_offset = 0; // Смещение для зебры (если нужно)
    } calibration_data_t;

    calibration_data_t calibration_start();
    calibration_data_t calibration_next();
    void calibration_cancel();
    void save_calibration_data(calibration_data_t calib);
    bool read_calibration_data(calibration_data_t *calib_);

#ifdef __cplusplus
}
#endif
