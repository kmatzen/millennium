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
    time_t call_active_since;  /* mclock_now() when the call became active, 0 if no active call */
} daemon_state_data_t;

/* Function declarations (C equivalent of class methods) */
void daemon_state_init(daemon_state_data_t* state);
void daemon_state_reset(daemon_state_data_t* state);
void daemon_state_update_activity(daemon_state_data_t* state);
const char* daemon_state_to_string(daemon_state_t state);

/* Call-duration accounting. daemon_state_call_begin() stamps the moment a call
 * becomes active; daemon_state_call_end() observes the elapsed time into the
 * call_duration_seconds histogram (and last_call_duration_seconds gauge) and
 * clears the stamp. Both read the clock seam (mclock_now) so the simulator's
 * advanceable clock yields real durations in scenario tests. call_end() returns
 * the duration in seconds, or -1 (recording nothing) if no call was active. */
void daemon_state_call_begin(daemon_state_data_t* state);
long daemon_state_call_end(daemon_state_data_t* state);

/* Utility functions */
int daemon_state_get_keypad_length(const daemon_state_data_t* state);
void daemon_state_clear_keypad(daemon_state_data_t* state);
void daemon_state_add_key(daemon_state_data_t* state, char key);
void daemon_state_remove_last_key(daemon_state_data_t* state);

#endif /* DAEMON_STATE_H */
