// components/motor_control/motor_control.h
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "include/controller.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        MOTOR_IDLE = 0,
        MOTOR_MOVING_FORWARD, // Направление к max_pos
        MOTOR_MOVING_REVERSE, // Направление к min_pos
        MOTOR_EMERGENCY_STOP
    } motor_status_t;

    typedef void (*motor_status_changed_cb_t)(motor_status_t new_status);

    void motor_init(uint32_t max_pos, uint32_t min_pos);
    void motor_control_set_status_callback(motor_status_changed_cb_t cb);
    void motor_move_to_position(uint32_t pos);
    motor_status_t motor_get_status(void);
    void motor_start_forward();
    void motor_start_reverce();
    void motor_stop();

#ifdef __cplusplus
}
#endif
