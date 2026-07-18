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
    /* Physical handset position, tracked separately from current_state.
     *
     * current_state cannot stand in for this: CALL_INCOMING is reachable both
     * on-hook (a phone ringing in its cradle) and off-hook (#92 interrupt
     * dialing), so inferring the handset from the state machine is unsound.
     * Call-state events racing hook events could then strand the daemon in
     * CALL_ACTIVE with the handset cradled, or in IDLE_UP after an unanswered
     * on-hook ring. Found by tests/EventOrdering.tla; see docs/EVENT_ORDERING.md.
     *
     * Not persisted: the keypad firmware never reports the hook position at
     * boot (it only emits on a transition), so a restart cannot know it. 0 is
     * the safe assumption and matches the existing unclean-shutdown coercion
     * to IDLE_DOWN in daemon.c. */
    int handset_up;
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
