// components/motor_control/motor_control.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        MOTOR_DIR_UP,
        MOTOR_DIR_DOWN,
        MOTOR_DIR_STOP
    } motor_direction_t;

    void motor_control_init(void);
    void motor_set_direction(motor_direction_t direction);
    void motor_set_speed(uint32_t speed);
    void motor_step(uint32_t steps);
    bool motor_is_moving(void);
    void motor_stop(void);

    void motor_set_step_mode(bool half_step);
    uint32_t motor_get_position_steps(void);
    void motor_move_degrees(float degrees);
    void motor_move_rotations(float rotations);

#ifdef __cplusplus
}
#endif
