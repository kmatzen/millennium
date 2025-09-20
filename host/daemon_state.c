#include "daemon_state.h"
#include <string.h>
#include <time.h>
#include <ctype.h>

void daemon_state_init(daemon_state_data_t* state) {
    if (!state) return;
    
    state->current_state = DAEMON_STATE_IDLE_DOWN;
    memset(state->keypad_buffer, 0, sizeof(state->keypad_buffer));
    state->inserted_cents = 0;
    state->last_activity = time(NULL);
}

void daemon_state_reset(daemon_state_data_t* state) {
    if (!state) return;
    
    state->current_state = DAEMON_STATE_IDLE_DOWN;
    daemon_state_clear_keypad(state);
    state->inserted_cents = 0;
    state->last_activity = time(NULL);
}

void daemon_state_update_activity(daemon_state_data_t* state) {
    if (!state) return;
    state->last_activity = time(NULL);
}

const char* daemon_state_to_string(daemon_state_t state) {
    switch (state) {
        case DAEMON_STATE_INVALID: return "INVALID";
        case DAEMON_STATE_IDLE_DOWN: return "IDLE_DOWN";
        case DAEMON_STATE_IDLE_UP: return "IDLE_UP";
        case DAEMON_STATE_CALL_INCOMING: return "CALL_INCOMING";
        case DAEMON_STATE_CALL_ACTIVE: return "CALL_ACTIVE";
        default: return "UNKNOWN";
    }
}

int daemon_state_get_keypad_length(const daemon_state_data_t* state) {
    if (!state) return 0;
    return (int)strlen(state->keypad_buffer);
}

void daemon_state_clear_keypad(daemon_state_data_t* state) {
    if (!state) return;
    memset(state->keypad_buffer, 0, sizeof(state->keypad_buffer));
}

void daemon_state_add_key(daemon_state_data_t* state, char key) {
    int len;
    
    if (!state || !isdigit(key)) return;
    
    len = daemon_state_get_keypad_length(state);
    if (len < 10) {  /* Max 10 digits */
        state->keypad_buffer[len] = key;
        state->keypad_buffer[len + 1] = '\0';
    }
}

void daemon_state_remove_last_key(daemon_state_data_t* state) {
    int len;
    
    if (!state) return;
    
    len = daemon_state_get_keypad_length(state);
    if (len > 0) {
        state->keypad_buffer[len - 1] = '\0';
    }
}
