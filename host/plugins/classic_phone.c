#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include "../plugins.h"
#include "../logger.h"
#include "../millennium_sdk.h"
#include "../config.h"
#include "../metrics.h"
#include "../display_manager.h"

/* Classic phone plugin data */
typedef struct {
    int inserted_cents;
    int call_cost_cents;
    char keypad_buffer[11];
    int keypad_length;
    int is_dialing;
    int is_in_call;
    time_t last_activity;
    char current_number[11];
    int call_timeout_seconds;
    time_t call_start_time;
} classic_phone_data_t;

static classic_phone_data_t classic_phone_data = {0};

/* External references */
extern daemon_state_data_t *daemon_state;
extern millennium_client_t *client;

/* Seconds remaining at which the countdown appears on the display */
#define TIMEOUT_WARNING_SECONDS 60

/* Internal functions */
static void classic_phone_update_display(void);
static void classic_phone_clear_keypad(void);
static void classic_phone_add_key(char key);
static void classic_phone_remove_last_key(void);
static int classic_phone_has_enough_money(void);
static int classic_phone_has_complete_number(void);
static void classic_phone_check_and_call(void);
static void classic_phone_start_call(void);
static void classic_phone_end_call(void);
static void classic_phone_format_number(const char* buffer, char *output);
static void classic_phone_tick(void);

/* Classic phone event handlers */
static int classic_phone_handle_coin(int coin_value, const char *coin_code) {
    /* Only accept coins when phone is ready for operation (IDLE_UP state) */
    if (coin_value > 0 && daemon_state && daemon_state->current_state == DAEMON_STATE_IDLE_UP) {
        classic_phone_data.inserted_cents += coin_value;
        classic_phone_data.last_activity = time(NULL);
        
        classic_phone_update_display();
        classic_phone_check_and_call();
        
        logger_infof_with_category("ClassicPhone", 
                "Coin inserted: %s, value: %d cents, total: %d cents",
                coin_code, coin_value, classic_phone_data.inserted_cents);
    }
    return 0;
}

static int classic_phone_handle_keypad(char key) {
    if (classic_phone_data.is_in_call) {
        /* In call - could handle DTMF here */
        return 0;
    }
    
    /* Only accept keypad input when phone is ready for operation (IDLE_UP state) and has space */
    if (isdigit(key) && daemon_state && daemon_state->current_state == DAEMON_STATE_IDLE_UP && 
        classic_phone_data.keypad_length < 10) {
        classic_phone_add_key(key);
        classic_phone_data.last_activity = time(NULL);
        classic_phone_update_display();
        classic_phone_check_and_call();
    }
    return 0;
}

static int classic_phone_handle_hook(int hook_up, int hook_down) {
    if (hook_up) {
        if (classic_phone_data.is_in_call) {
            /* Incoming call - daemon will handle answering, just update our state */
            logger_info_with_category("ClassicPhone", "Handset lifted during incoming call");
            classic_phone_data.call_start_time = time(NULL);
            classic_phone_update_display();
        } else {
            /* Start new call session */
            classic_phone_data.inserted_cents = 0;
            classic_phone_data.keypad_length = 0;
            classic_phone_data.last_activity = time(NULL);
            classic_phone_update_display();
        }
    } else if (hook_down) {
        /* Always clear keypad and reset coins on hook down, like original daemon */
        classic_phone_data.keypad_length = 0;
        classic_phone_data.inserted_cents = 0;
        
        if (classic_phone_data.is_in_call) {
            classic_phone_end_call();
        } else {
            classic_phone_update_display();
        }
    }
    return 0;
}

static int classic_phone_handle_call_state(int call_state) {
    char log_msg[128];
    snprintf(log_msg, sizeof(log_msg), "Call state changed to: %d (dialing=%d, in_call=%d)", 
            call_state, classic_phone_data.is_dialing, classic_phone_data.is_in_call);
    logger_info_with_category("ClassicPhone", log_msg);
    
    if (call_state == EVENT_CALL_STATE_INCOMING) {
        /* Handle incoming call - show "Call incoming..." like original daemon */
        classic_phone_data.is_in_call = 1;
        classic_phone_data.call_start_time = time(NULL);
        classic_phone_update_display();
        logger_info_with_category("ClassicPhone", "Incoming call received");
    } else if (call_state == EVENT_CALL_STATE_ACTIVE) {
        /* Handle call established - show "Call active" and "Audio connected" like original daemon */
        classic_phone_data.is_in_call = 1;
        classic_phone_data.call_start_time = time(NULL);
        classic_phone_update_display();
        logger_info_with_category("ClassicPhone", "Call established");
    } else if (call_state == EVENT_CALL_STATE_INVALID) {
        /* Completely ignore INVALID events during active calls - let Baresip handle call termination */
        if (classic_phone_data.is_dialing || classic_phone_data.is_in_call) {
            logger_info_with_category("ClassicPhone", "Call state invalid during active call - ignoring (let Baresip handle termination)");
        } else {
            logger_info_with_category("ClassicPhone", "Call state invalid but not in call - ignoring");
        }
    }
    return 0;
}

/* Internal function implementations */
static void classic_phone_on_activation(void) {
    /* Restore coins from daemon_state (may have been loaded from persisted state) */
    classic_phone_data.inserted_cents = daemon_state ? daemon_state->inserted_cents : 0;
    classic_phone_data.keypad_length = 0;
    classic_phone_data.is_dialing = 0;
    classic_phone_data.is_in_call = 0;
    classic_phone_data.last_activity = time(NULL);
    classic_phone_update_display();
}





static void classic_phone_update_display(void) {
    char line1[21];
    char line2[21];
    
    /* Simple state machine for display */
    if (classic_phone_data.is_in_call) {
        /* Call is active */
        strcpy(line1, "Call active");
        strcpy(line2, "Hang up to end");
    } else if (classic_phone_data.is_dialing) {
        /* Dialing a number */
        classic_phone_format_number(classic_phone_data.keypad_buffer, line1);
        strcpy(line2, "Dialing...");
    } else if (daemon_state && daemon_state->current_state == DAEMON_STATE_IDLE_DOWN) {
        /* Receiver is down */
        strcpy(line1, "Lift receiver");
        strcpy(line2, "to make a call");
    } else {
        /* Receiver is up - show phone number and coin status */
        if (classic_phone_data.keypad_length > 0) {
            classic_phone_format_number(classic_phone_data.keypad_buffer, line1);
        } else {
            classic_phone_format_number("", line1);
        }
        
        if (classic_phone_data.inserted_cents > 0) {
            snprintf(line2, sizeof(line2), "Have: %dc", classic_phone_data.inserted_cents);
        } else {
            snprintf(line2, sizeof(line2), "Insert %dc", classic_phone_data.call_cost_cents);
        }
    }
    
    display_manager_set_text(line1, line2);
}

/* Helper function for future keypad functionality - kept for extensibility */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
static void classic_phone_clear_keypad(void) {
    classic_phone_data.keypad_length = 0;
    memset(classic_phone_data.keypad_buffer, 0, sizeof(classic_phone_data.keypad_buffer));
}
#pragma GCC diagnostic pop

static void classic_phone_add_key(char key) {
    if (classic_phone_data.keypad_length < 10) {
        classic_phone_data.keypad_buffer[classic_phone_data.keypad_length] = key;
        classic_phone_data.keypad_length++;
        classic_phone_data.keypad_buffer[classic_phone_data.keypad_length] = '\0';
    }
}

/* Helper function for future keypad functionality - kept for extensibility */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
static void classic_phone_remove_last_key(void) {
    if (classic_phone_data.keypad_length > 0) {
        classic_phone_data.keypad_length--;
        classic_phone_data.keypad_buffer[classic_phone_data.keypad_length] = '\0';
    }
}
#pragma GCC diagnostic pop

static int classic_phone_has_enough_money(void) {
    return classic_phone_data.inserted_cents >= classic_phone_data.call_cost_cents;
}

static int classic_phone_has_complete_number(void) {
    return classic_phone_data.keypad_length >= 10; /* Require 10-digit number */
}

static void classic_phone_check_and_call(void) {
    if (classic_phone_has_enough_money() && 
        classic_phone_has_complete_number() && 
        !classic_phone_data.is_dialing && !classic_phone_data.is_in_call) {
        
        classic_phone_start_call();
    }
}

static void classic_phone_start_call(void) {
    classic_phone_data.is_dialing = 1;
    strncpy(classic_phone_data.current_number, classic_phone_data.keypad_buffer, sizeof(classic_phone_data.current_number) - 1);
    classic_phone_data.current_number[sizeof(classic_phone_data.current_number) - 1] = '\0';
    
    classic_phone_update_display();
    
    /* Consume coins */
    classic_phone_data.inserted_cents -= classic_phone_data.call_cost_cents;
    
    /* Make the actual call */
    millennium_client_call(client, classic_phone_data.current_number);
    
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "Starting call to %s", classic_phone_data.current_number);
    logger_info_with_category("ClassicPhone", log_msg);
}

static void classic_phone_end_call(void) {
    classic_phone_data.is_dialing = 0;
    classic_phone_data.is_in_call = 0;
    classic_phone_data.keypad_length = 0;
    classic_phone_data.inserted_cents = 0;
    
    millennium_client_hangup(client);
    classic_phone_update_display();
    
    logger_info_with_category("ClassicPhone", "Call ended");
}

static void classic_phone_format_number(const char* buffer, char *output) {
    char filled[11] = "__________";  /* 10 underscores + null terminator */
    int len;
    int i;
    
    if (!buffer || !output) {
        return;
    }
    
    len = strlen(buffer);
    
    for (i = 0; i < len && i < 10; i++) {
        filled[i] = buffer[i];
    }
    
    snprintf(output, 21, "(%c%c%c) %c%c%c-%c%c%c%c",
            filled[0], filled[1], filled[2],
            filled[3], filled[4], filled[5],
            filled[6], filled[7], filled[8], filled[9]);
}


static void classic_phone_tick(void) {
    int remaining;
    int minutes;
    int seconds;
    char line1[21];
    char line2[21];

    if (!classic_phone_data.is_in_call) {
        return;
    }
    if (classic_phone_data.call_timeout_seconds <= 0) {
        return;
    }
    if (classic_phone_data.call_start_time == 0) {
        return;
    }

    remaining = classic_phone_data.call_timeout_seconds
              - (int)(time(NULL) - classic_phone_data.call_start_time);

    if (remaining <= 0) {
        logger_info_with_category("ClassicPhone",
                                  "Call timeout reached, ending call");
        metrics_increment_counter("calls_timed_out", 1);
        classic_phone_end_call();
        return;
    }

    if (remaining > TIMEOUT_WARNING_SECONDS) {
        return;
    }

    minutes = remaining / 60;
    seconds = remaining % 60;

    strcpy(line1, "Call active");
    snprintf(line2, sizeof(line2), "%d:%02d remaining", minutes, seconds);

    display_manager_set_text(line1, line2);
}

/* Plugin registration function */
void register_classic_phone_plugin(void) {
    /* Initialize plugin data */
    classic_phone_data.inserted_cents = 0;
    classic_phone_data.call_cost_cents = config_get_call_cost_cents(config_get_instance()); /* Use configurable cost like original daemon */
    classic_phone_data.keypad_length = 0;
    classic_phone_data.is_dialing = 0;
    classic_phone_data.is_in_call = 0;
    classic_phone_data.call_timeout_seconds = config_get_call_timeout_seconds(config_get_instance()); /* Use configurable timeout */
    classic_phone_data.last_activity = time(NULL);
    
    plugins_register("Classic Phone",
                    "Traditional pay phone functionality with VoIP calling",
                    classic_phone_handle_coin,
                    classic_phone_handle_keypad,
                    classic_phone_handle_hook,
                    classic_phone_handle_call_state,
                    classic_phone_on_activation,
                    classic_phone_tick);
}
