// components/button_handler/button_handler.h
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <iot_button.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        BUTTON_EVENT_SHORT_PRESS_UP,
        BUTTON_EVENT_SHORT_PRESS_DOWN,
        BUTTON_EVENT_DOUBLE_PRESS_UP,
        BUTTON_EVENT_DOUBLE_PRESS_DOWN,
        BUTTON_EVENT_LONG_PRESS_UP,
        BUTTON_EVENT_LONG_PRESS_DOWN,
        BUTTON_EVENT_SIMULTANEOUS_PRESS,
        BUTTON_EVENT_RELEASE
    } button_event_t;

    typedef void (*button_callback_t)(button_event_t event, void *user_data);

    void button_handler_init(void);
    void button_handler_set_callback(button_callback_t callback, void *user_data);
    void button_handler_task(void *arg);

#ifdef __cplusplus
}
#endif
