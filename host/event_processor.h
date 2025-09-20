#ifndef EVENT_PROCESSOR_H
#define EVENT_PROCESSOR_H

#include "events.h"

/* Forward declarations */
typedef struct event_processor event_processor_t;

/* Event handler function pointer types */
typedef void (*coin_handler_func_t)(coin_event_t *event);
typedef void (*call_state_handler_func_t)(call_state_event_t *event);
typedef void (*hook_handler_func_t)(hook_state_change_event_t *event);
typedef void (*keypad_handler_func_t)(keypad_event_t *event);

/* Event processor structure */
struct event_processor {
    /* Event handler callbacks */
    coin_handler_func_t coin_handler;
    call_state_handler_func_t call_state_handler;
    hook_handler_func_t hook_handler;
    keypad_handler_func_t keypad_handler;
};

/* Function declarations */
event_processor_t *event_processor_create(void);
void event_processor_destroy(event_processor_t *processor);
void event_processor_process_event(event_processor_t *processor, event_t *event);

/* Register event handler callbacks */
void event_processor_register_coin_handler(event_processor_t *processor, coin_handler_func_t handler);
void event_processor_register_call_state_handler(event_processor_t *processor, call_state_handler_func_t handler);
void event_processor_register_hook_handler(event_processor_t *processor, hook_handler_func_t handler);
void event_processor_register_keypad_handler(event_processor_t *processor, keypad_handler_func_t handler);

#endif /* EVENT_PROCESSOR_H */
