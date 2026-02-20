// components/shade_controller/shade_controller.h
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "position_sensor.h"
#include "motor_control.h"
#include "button_handler.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        IDLE,
        MOVING_UP,
        MOVING_DOWN,
        CALIBRATING,
        EMERGENCY_STOP
    } state_t;

    typedef struct
    {
        state_t state;
        position_config_t position;
        bool auto_calibrate;
    } config_t;

    void controller_init(void);
    void controller_move_to_position(uint32_t position);
    void controller_move_up(void);
    void controller_move_down(void);
    void controller_stop(void);
    void controller_calibrate(void);
    void controller_goto_top(void);
    void controller_goto_bottom(void);
    void controller_set_position_percentage(float percentage);
    state_t controller_get_state(void);
    bool controller_is_moving(void);

#ifdef __cplusplus
}
#endif
