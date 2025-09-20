#ifndef DAEMON_STATE_H
#define DAEMON_STATE_H

#include <time.h>

/* C equivalent of DaemonState class */
typedef enum {
    DAEMON_STATE_INVALID = 0,
    DAEMON_STATE_IDLE_DOWN,
    DAEMON_STATE_IDLE_UP,
    DAEMON_STATE_CALL_INCOMING,
    DAEMON_STATE_CALL_ACTIVE
} daemon_state_t;

typedef struct {
    daemon_state_t current_state;
    char keypad_buffer[11];  /* Fixed size: 10 digits + null terminator */
    int inserted_cents;
    time_t last_activity;  /* C89 compatible time representation */
} daemon_state_data_t;

/* Function declarations (C equivalent of class methods) */
void daemon_state_init(daemon_state_data_t* state);
void daemon_state_reset(daemon_state_data_t* state);
void daemon_state_update_activity(daemon_state_data_t* state);
const char* daemon_state_to_string(daemon_state_t state);

/* Utility functions */
int daemon_state_get_keypad_length(const daemon_state_data_t* state);
void daemon_state_clear_keypad(daemon_state_data_t* state);
void daemon_state_add_key(daemon_state_data_t* state, char key);
void daemon_state_remove_last_key(daemon_state_data_t* state);

#endif /* DAEMON_STATE_H */
