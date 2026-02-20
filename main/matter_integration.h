#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "controller.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        bool is_open;
        uint8_t position_percent;
        bool is_moving;
    } matter_shade_state_t;

    void matter_integration_init(void);
    void matter_integration_update_state(state_t state, float position);
    void matter_integration_set_position_callback(void (*callback)(uint8_t position));
    void matter_integration_set_move_callback(void (*callback)(bool direction));

#ifdef __cplusplus
}
#endif
