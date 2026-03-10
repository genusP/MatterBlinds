// components/shade_controller/shade_controller.h
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "position_sensor.h"
#include "motor_control.h"

// Maximum number of simultaneous subscribers
#define CONTROLLER_MAX_SUBSCRIBERS 5

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        IDLE,
        MOVING_UP,
        MOVING_DOWN,
        EMERGENCY_STOP,
        CALIBRATING_INIT,
        CALIBRATING,
    } controller_state_t;

    // Callback function type for controller state change events
    typedef void (*controller_state_changed_cb_t)(controller_state_t old_state, controller_state_t new_state, void *user_data);

    const char *controller_state_text(controller_state_t state);
    void controller_init(void);
    void controller_move_to_position(uint32_t position);
    void controller_move_up(void);
    void controller_move_down(void);
    void controller_calibrate(void);
    void controller_goto_top(void);
    void controller_goto_bottom(void);
    void controller_set_position_percentage(float percentage);
    float controller_get_position_percentage();
    void controller_stop(void);
    controller_state_t controller_get_state(void);
    bool controller_subscribe_state_changes(controller_state_changed_cb_t callback, void *user_data);

#ifdef __cplusplus
}
#endif
