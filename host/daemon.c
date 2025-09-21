#include "config.h"
#include "daemon_state.h"
#include "logger.h"
#include "metrics.h"
#include "metrics_server.h"
#include "health_monitor.h"
#include "web_server.h"
#include "millennium_sdk.h"
#include "events.h"
#include "event_processor.h"
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <pthread.h>

#define EVENT_CATEGORIES 3
#define DISPLAY_WIDTH 20
#define MAX_STRING_LEN 256
#define MAX_KEYPAD_LEN 11

/* Global state management */
volatile int running = 1;
daemon_state_data_t* daemon_state = NULL;
millennium_client_t* client = NULL;
metrics_server_t *metrics_server = NULL;
struct web_server* web_server = NULL;
event_processor_t *event_processor = NULL;

/* Daemon start time for uptime calculation */
time_t daemon_start_time;

/* Display management */
char line1[MAX_STRING_LEN];
char line2[MAX_STRING_LEN];

/* Thread synchronization */
pthread_mutex_t running_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t daemon_state_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Simple validation macro */
#define VALIDATE_BASICS() do { \
    if (!client) { \
        logger_error_with_category("Event", "Client is null"); \
        return; \
    } \
    if (!daemon_state) { \
        logger_error_with_category("Event", "Daemon state is null"); \
        return; \
    } \
} while(0)

/* Function prototypes */
static void signal_handler(int signal);
static void check_and_call(void);
static void handle_coin_event(coin_event_t *coin_event);
static void handle_call_state_event(call_state_event_t *call_state_event);
static void handle_hook_event(hook_state_change_event_t *hook_event);
static void handle_keypad_event(keypad_event_t *keypad_event);
/* send_control_command is declared in web_server.h */
static void generate_display_bytes(char *output, size_t output_size);
static void format_number(const char* buffer, char *output);
static void generate_message(int inserted, char *output);
static void update_display(void);
static int is_phone_ready_for_operation(void);
static int keypad_has_space(void);
static health_status_t check_serial_connection(void);
static health_status_t check_sip_connection(void);
static health_status_t check_daemon_activity(void);
static void update_metrics(void);

/* C-compatible functions for web server */
time_t get_daemon_start_time(void) {
    return daemon_start_time;
}

struct daemon_state_info get_daemon_state_info(void) {
    struct daemon_state_info info;
    memset(&info, 0, sizeof(info));
    
    if (daemon_state) {
        pthread_mutex_lock(&daemon_state_mutex);
        info.current_state = (int)daemon_state->current_state;
        info.inserted_cents = daemon_state->inserted_cents;
        strncpy(info.keypad_buffer, daemon_state->keypad_buffer, sizeof(info.keypad_buffer) - 1);
        info.keypad_buffer[sizeof(info.keypad_buffer) - 1] = '\0';
        info.last_activity = daemon_state->last_activity;
        pthread_mutex_unlock(&daemon_state_mutex);
    } else {
        info.current_state = 0;
        info.inserted_cents = 0;
        strcpy(info.keypad_buffer, "");
        info.last_activity = time(NULL);
    }
    
    return info;
}

/* Display management functions */
void generate_display_bytes(char *output, size_t output_size) {
    size_t required_size;
    size_t pos;
    int i;
    
    if (!output || output_size == 0) {
        return;
    }
    
    /* Calculate required size: 2 lines * DISPLAY_WIDTH + 1 line feed + 1 null terminator */
    required_size = (2 * DISPLAY_WIDTH) + 2;
    
    if (output_size < required_size) {
        /* Buffer too small, truncate safely */
        output_size = required_size - 1;
    }
    
    pos = 0;
    
    /* Add line1, padded or truncated to DISPLAY_WIDTH */
    for (i = 0; i < DISPLAY_WIDTH && pos < output_size - 1; i++) {
        output[pos++] = (i < (int)strlen(line1)) ? line1[i] : ' ';
    }
    
    /* Add line feed */
    if (pos < output_size - 1) {
        output[pos++] = 0x0A;
    }
    
    /* Add line2, padded or truncated to DISPLAY_WIDTH */
    for (i = 0; i < DISPLAY_WIDTH && pos < output_size - 1; i++) {
        output[pos++] = (i < (int)strlen(line2)) ? line2[i] : ' ';
    }
    
    /* Null terminate */
    output[pos] = '\0';
}

void format_number(const char* buffer, char *output) {
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
    
    sprintf(output, "(%c%c%c) %c%c%c-%c%c%c%c",
            filled[0], filled[1], filled[2],
            filled[3], filled[4], filled[5],
            filled[6], filled[7], filled[8], filled[9]);
}

void generate_message(int inserted, char *output) {
    if (!output) {
        return;
    }
    
    int cost_cents = config_get_call_cost_cents(config_get_instance());
    int remaining = cost_cents - inserted;
    
    /* Ensure remaining is not negative */
    if (remaining < 0) {
        remaining = 0;
    }
    
    sprintf(output, "Insert %02d cents", remaining);
    
    char log_msg[MAX_STRING_LEN];
    strcpy(log_msg, "Generated message: ");
    strncat(log_msg, output, MAX_STRING_LEN - 20); /* Leave room for "Generated message: " */
    log_msg[MAX_STRING_LEN - 1] = '\0'; /* Ensure null termination */
    logger_debug_with_category("Display", log_msg);
}

/* Helper function to update display - consolidates repetitive code */
static void update_display(void) {
    if (!client || !daemon_state) {
        return;
    }
    
    char display_bytes[100];
    generate_display_bytes(display_bytes, sizeof(display_bytes));
    millennium_client_set_display(client, display_bytes);
}

/* Helper function to validate phone state for operations */
static int is_phone_ready_for_operation(void) {
    return (daemon_state && daemon_state->current_state == DAEMON_STATE_IDLE_UP);
}

/* Helper function to validate keypad buffer has space */
static int keypad_has_space(void) {
    return (daemon_state && daemon_state_get_keypad_length(daemon_state) < 10);
}

void check_and_call(void) {
    VALIDATE_BASICS();
    
    int cost_cents = config_get_call_cost_cents(config_get_instance());
    
    /* Quick check without mutex - if conditions aren't met, no need to lock */
    int keypad_ready = (daemon_state_get_keypad_length(daemon_state) == 10);
    int enough_money = (daemon_state->inserted_cents >= cost_cents);
    int phone_ready = is_phone_ready_for_operation();
    
    if (keypad_ready && enough_money && phone_ready) {
        
        char number[11];
        strncpy(number, daemon_state->keypad_buffer, 10);
        number[10] = '\0';
        
        char log_msg[MAX_STRING_LEN];
        sprintf(log_msg, "Dialing number: %s", number);
        logger_info_with_category("Call", log_msg);
        metrics_increment_counter("calls_initiated", 1);
        
        strcpy(line2, "Calling");
        update_display();
        
        millennium_client_call(client, number);
        
        /* Only lock for the state change */
        pthread_mutex_lock(&daemon_state_mutex);
        daemon_state->current_state = DAEMON_STATE_CALL_ACTIVE;
        daemon_state_update_activity(daemon_state);
        pthread_mutex_unlock(&daemon_state_mutex);
    }
}

void handle_coin_event(coin_event_t *coin_event) {
    if (!coin_event) {
        logger_error_with_category("Coin", "Received null coin event");
        return;
    }
    VALIDATE_BASICS();
    
    char *coin_code_str = coin_event_get_coin_code(coin_event);
    if (!coin_code_str) {
        logger_error_with_category("Coin", "Failed to get coin code");
        return;
    }
    
    int coin_value = 0;
    
    if (strcmp(coin_code_str, "COIN_6") == 0) {
        coin_value = 5;
    } else if (strcmp(coin_code_str, "COIN_7") == 0) {
        coin_value = 10;
    } else if (strcmp(coin_code_str, "COIN_8") == 0) {
        coin_value = 25;
    }
    
    if (coin_value > 0) {
        pthread_mutex_lock(&daemon_state_mutex);
        
        if (is_phone_ready_for_operation()) {
            daemon_state->inserted_cents += coin_value;
            daemon_state_update_activity(daemon_state);
            
            metrics_increment_counter("coins_inserted", 1);
            metrics_increment_counter("coins_value_cents", coin_value);
            
            char log_msg[MAX_STRING_LEN];
            sprintf(log_msg, 
                    "Coin inserted: %s, value: %d cents, total: %d cents",
                    coin_code_str, coin_value, daemon_state->inserted_cents);
            logger_info_with_category("Coin", log_msg);
            
            format_number(daemon_state->keypad_buffer, line1);
            generate_message(daemon_state->inserted_cents, line2);
            update_display();
        }
        pthread_mutex_unlock(&daemon_state_mutex);
        
        /* Check if we should initiate a call after coin insertion */
        if (coin_value > 0) {
            check_and_call();
        }
    }
    
    free(coin_code_str);
}

void handle_call_state_event(call_state_event_t *call_state_event) {
    if (!call_state_event) {
        logger_error_with_category("Call", "Received null call state event");
        return;
    }
    VALIDATE_BASICS();
    
    pthread_mutex_lock(&daemon_state_mutex);
    int is_incoming = (call_state_event_get_state(call_state_event) == EVENT_CALL_STATE_INCOMING);
    int phone_down = (daemon_state->current_state == DAEMON_STATE_IDLE_DOWN);
    
    if (is_incoming && phone_down) {
        
        logger_info_with_category("Call", "Incoming call received");
        metrics_increment_counter("calls_incoming", 1);
        
        strcpy(line1, "Call incoming...");
        update_display();
        
        daemon_state->current_state = DAEMON_STATE_CALL_INCOMING;
        daemon_state_update_activity(daemon_state);
        
    } else if (call_state_event_get_state(call_state_event) == EVENT_CALL_STATE_ACTIVE) {
        /* Handle call established (when baresip reports CALL_ESTABLISHED) */
        logger_info_with_category("Call", "Call established - audio should be working");
        metrics_increment_counter("calls_established", 1);
        
        strcpy(line1, "Call active");
        strcpy(line2, "Audio connected");
        update_display();
        
        daemon_state->current_state = DAEMON_STATE_CALL_ACTIVE;
        daemon_state_update_activity(daemon_state);
    }
    pthread_mutex_unlock(&daemon_state_mutex);
    
    /* Handle coin validator commands outside of mutex */
    if (call_state_event_get_state(call_state_event) == EVENT_CALL_STATE_INCOMING) {
        millennium_client_write_to_coin_validator(client, 'f');
        millennium_client_write_to_coin_validator(client, 'z');
    }
}

void handle_hook_event(hook_state_change_event_t *hook_event) {
    if (!hook_event) {
        logger_error_with_category("Hook", "Received null hook event");
        return;
    }
    VALIDATE_BASICS();
    
    pthread_mutex_lock(&daemon_state_mutex);
    int hook_up = (hook_state_change_event_get_direction(hook_event) == 'U');
    int hook_down = (hook_state_change_event_get_direction(hook_event) == 'D');
    
    if (hook_up) {
        int call_incoming = (daemon_state->current_state == DAEMON_STATE_CALL_INCOMING);
        int phone_down = (daemon_state->current_state == DAEMON_STATE_IDLE_DOWN);
        
        if (call_incoming) {
            logger_info_with_category("Call", "Call answered");
            metrics_increment_counter("calls_answered", 1);
            
            daemon_state->current_state = DAEMON_STATE_CALL_ACTIVE;
            daemon_state_update_activity(daemon_state);
            
        } else if (phone_down) {
            logger_info_with_category("Hook", "Hook lifted, transitioning to IDLE_UP");
            metrics_increment_counter("hook_lifted", 1);
            
            daemon_state->current_state = DAEMON_STATE_IDLE_UP;
            daemon_state_update_activity(daemon_state);
            
            daemon_state->inserted_cents = 0;
            daemon_state_clear_keypad(daemon_state);
            
            generate_message(daemon_state->inserted_cents, line2);
            format_number(daemon_state->keypad_buffer, line1);
            update_display();
        }
    } else if (hook_down) {
        logger_info_with_category("Hook", "Hook down, call ended");
        metrics_increment_counter("hook_down", 1);
        
        if (daemon_state->current_state == DAEMON_STATE_CALL_ACTIVE) {
            metrics_increment_counter("calls_ended", 1);
        }
        
        daemon_state_clear_keypad(daemon_state);
        daemon_state->inserted_cents = 0;
        
        generate_message(daemon_state->inserted_cents, line2);
        format_number(daemon_state->keypad_buffer, line1);
        update_display();
        
        /* Update state */
        daemon_state->current_state = DAEMON_STATE_IDLE_DOWN;
        daemon_state_update_activity(daemon_state);
    }
    pthread_mutex_unlock(&daemon_state_mutex);
    
    /* Handle external calls outside of mutex */
    if (hook_up) {
        /* Check what state we ended up in after the mutex section */
        pthread_mutex_lock(&daemon_state_mutex);
        if (daemon_state->current_state == DAEMON_STATE_CALL_ACTIVE) {
            pthread_mutex_unlock(&daemon_state_mutex);
            millennium_client_answer_call(client);
        } else if (daemon_state->current_state == DAEMON_STATE_IDLE_UP) {
            pthread_mutex_unlock(&daemon_state_mutex);
            millennium_client_write_to_coin_validator(client, 'a');
        } else {
            pthread_mutex_unlock(&daemon_state_mutex);
        }
    } else if (hook_down) {
        millennium_client_hangup(client);
        millennium_client_write_to_coin_validator(client, 'c'); /* Always 'c' for hook down */
        millennium_client_write_to_coin_validator(client, 'z');
    }
}

void handle_keypad_event(keypad_event_t *keypad_event) {
    if (!keypad_event) {
        logger_error_with_category("Keypad", "Received null keypad event");
        return;
    }
    VALIDATE_BASICS();
    
    char key = keypad_event_get_key(keypad_event);
    if (isdigit(key)) {
        pthread_mutex_lock(&daemon_state_mutex);
        
        if (keypad_has_space() && is_phone_ready_for_operation()) {
            
            char log_msg[MAX_STRING_LEN];
            sprintf(log_msg, "Key pressed: %c", key);
            logger_debug_with_category("Keypad", log_msg);
            metrics_increment_counter("keypad_presses", 1);
            
            daemon_state_add_key(daemon_state, key);
            daemon_state_update_activity(daemon_state);
            
            generate_message(daemon_state->inserted_cents, line2);
            format_number(daemon_state->keypad_buffer, line1);
            update_display();
        }
        pthread_mutex_unlock(&daemon_state_mutex);
        
        /* Check if we should initiate a call after keypad input */
        if (isdigit(key)) {
            check_and_call();
        }
    }
}

/* Implementation of sendControlCommand */
int send_control_command(const char* action) {
    if (!daemon_state) {
        logger_error_with_category("Control", "Daemon state is null");
        return 0;
    }
    
    if (!event_processor) {
        logger_error_with_category("Control", "Event processor is null");
        return 0;
    }
    
    if (!action) {
        return 0;
    }
    
    char log_msg[MAX_STRING_LEN];
    sprintf(log_msg, "Received control command: %s", action);
    logger_info_with_category("Control", log_msg);
    printf("[CONTROL] Received command: %s\n", action);
    
    /* Parse command and arguments */
    char command[MAX_STRING_LEN];
    char arg[MAX_STRING_LEN];
    const char *colon_pos = strchr(action, ':');
    
    if (colon_pos) {
        size_t cmd_len = colon_pos - action;
        if (cmd_len >= MAX_STRING_LEN) {
            cmd_len = MAX_STRING_LEN - 1;
        }
        strncpy(command, action, cmd_len);
        command[cmd_len] = '\0';
        
        /* Safely copy argument */
        strncpy(arg, colon_pos + 1, MAX_STRING_LEN - 1);
        arg[MAX_STRING_LEN - 1] = '\0';
    } else {
        strncpy(command, action, MAX_STRING_LEN - 1);
        command[MAX_STRING_LEN - 1] = '\0';
        strcpy(arg, "");
    }
    
    if (strcmp(command, "start_call") == 0) {
        /* Simulate starting a call by changing state */
        pthread_mutex_lock(&daemon_state_mutex);
        daemon_state->current_state = DAEMON_STATE_CALL_INCOMING;
        daemon_state_update_activity(daemon_state);
        metrics_set_gauge("current_state", (double)daemon_state->current_state);
        metrics_increment_counter("calls_initiated", 1);
        pthread_mutex_unlock(&daemon_state_mutex);
        logger_info_with_category("Control", "Call initiation requested via web portal");
        return 1;
        
    } else if (strcmp(command, "reset_system") == 0) {
        /* Reset the system state */
        pthread_mutex_lock(&daemon_state_mutex);
        daemon_state_reset(daemon_state);
        metrics_set_gauge("current_state", (double)daemon_state->current_state);
        metrics_set_gauge("inserted_cents", 0.0);
        metrics_increment_counter("system_resets", 1);
        pthread_mutex_unlock(&daemon_state_mutex);
        logger_info_with_category("Control", "System reset requested via web portal");
        return 1;
        
    } else if (strcmp(command, "emergency_stop") == 0) {
        /* Emergency stop - set to invalid state and stop running */
        pthread_mutex_lock(&daemon_state_mutex);
        daemon_state->current_state = DAEMON_STATE_INVALID;
        daemon_state_update_activity(daemon_state);
        metrics_set_gauge("current_state", (double)daemon_state->current_state);
        metrics_increment_counter("emergency_stops", 1);
        pthread_mutex_unlock(&daemon_state_mutex);
        logger_warn_with_category("Control", "Emergency stop activated via web portal");
        /* Note: We don't actually stop the daemon, just set it to invalid state */
        return 1;
        
    } else if (strcmp(command, "keypad_press") == 0) {
        /* Extract key from argument and inject as keypad event */
        if (isdigit(arg[0])) {
            keypad_event_t *keypad_event = keypad_event_create(arg[0]);
            if (keypad_event) {
                event_processor_process_event(event_processor, (event_t *)keypad_event);
                event_destroy((event_t *)keypad_event);
            }
            sprintf(log_msg, "Keypad key '%c' pressed via web portal", arg[0]);
            logger_info_with_category("Control", log_msg);
            return 1;
        } else {
            sprintf(log_msg, "Invalid keypad key: %c", arg[0]);
            logger_warn_with_category("Control", log_msg);
            return 0;
        }
        
    } else if (strcmp(command, "keypad_clear") == 0) {
        /* Only allow clear when handset is up (same as physical keypad logic) */
        pthread_mutex_lock(&daemon_state_mutex);
        if (is_phone_ready_for_operation()) {
            daemon_state_clear_keypad(daemon_state);
            daemon_state_update_activity(daemon_state);
            metrics_increment_counter("keypad_clears", 1);
            logger_info_with_category("Control", "Keypad cleared via web portal");
            
            /* Update physical display */
            format_number(daemon_state->keypad_buffer, line1);
            generate_message(daemon_state->inserted_cents, line2);
            update_display();
        } else {
            logger_warn_with_category("Control", "Keypad clear ignored - handset down");
        }
        int success = is_phone_ready_for_operation();
        pthread_mutex_unlock(&daemon_state_mutex);
        return success;
        
    } else if (strcmp(command, "keypad_backspace") == 0) {
        /* Only allow backspace when handset is up and buffer is not empty */
        pthread_mutex_lock(&daemon_state_mutex);
        int buffer_not_empty = (daemon_state_get_keypad_length(daemon_state) > 0);
        
        if (is_phone_ready_for_operation() && buffer_not_empty) {
            daemon_state_remove_last_key(daemon_state);
            daemon_state_update_activity(daemon_state);
            metrics_increment_counter("keypad_backspaces", 1);
            logger_info_with_category("Control", "Keypad backspace via web portal");
            
            /* Update physical display */
            format_number(daemon_state->keypad_buffer, line1);
            generate_message(daemon_state->inserted_cents, line2);
            update_display();
        } else {
            logger_warn_with_category("Control", "Keypad backspace ignored - handset down or buffer empty");
        }
        int success = (is_phone_ready_for_operation() && buffer_not_empty);
        pthread_mutex_unlock(&daemon_state_mutex);
        return success;
        
    } else if (strcmp(command, "coin_insert") == 0) {
        /* Extract cents from argument and inject as coin event */
        if (strlen(arg) == 0) {
            logger_warn_with_category("Control", "Coin insert command missing argument");
            return 0;
        }
        
        sprintf(log_msg, "Extracted cents string: '%s'", arg);
        logger_info_with_category("Control", log_msg);
        printf("[CONTROL] Extracted cents: '%s'\n", arg);
        
        int cents = atoi(arg);
        
        /* Validate coin value */
        if (cents <= 0) {
            sprintf(log_msg, "Invalid coin value: %d¢", cents);
            logger_warn_with_category("Control", log_msg);
            return 0;
        }
        
        /* Map cents to coin codes (same as physical coin reader) */
        uint8_t coin_code;
        switch (cents) {
            case 5:  coin_code = 0x36; break; /* COIN_6 */
            case 10: coin_code = 0x37; break; /* COIN_7 */
            case 25: coin_code = 0x38; break; /* COIN_8 */
            default:
                sprintf(log_msg, "Invalid coin value: %d¢", cents);
                logger_warn_with_category("Control", log_msg);
                return 0;
        }
        
        coin_event_t *coin_event = coin_event_create(coin_code);
        if (coin_event) {
            event_processor_process_event(event_processor, (event_t *)coin_event);
            event_destroy((event_t *)coin_event);
        }
        sprintf(log_msg, "Coin inserted: %d¢ via web portal", cents);
        logger_info_with_category("Control", log_msg);
        printf("[CONTROL] Coin inserted successfully: %d¢\n", cents);
        
        return 1;
        
    } else if (strcmp(command, "coin_return") == 0) {
        pthread_mutex_lock(&daemon_state_mutex);
        daemon_state->inserted_cents = 0;
        daemon_state_update_activity(daemon_state);
        metrics_set_gauge("inserted_cents", 0.0);
        metrics_increment_counter("coin_returns", 1);
        logger_info_with_category("Control", "Coins returned via web portal");
        
        /* Update physical display */
        format_number(daemon_state->keypad_buffer, line1);
        generate_message(daemon_state->inserted_cents, line2);
        char display_bytes[100];
        generate_display_bytes(display_bytes, sizeof(display_bytes));
        millennium_client_set_display(client, display_bytes);
        
        pthread_mutex_unlock(&daemon_state_mutex);
        return 1;
        
    } else if (strcmp(command, "handset_up") == 0) {
        /* Inject as hook event */
        hook_state_change_event_t *hook_event = hook_state_change_event_create('U');
        if (hook_event) {
            event_processor_process_event(event_processor, (event_t *)hook_event);
            event_destroy((event_t *)hook_event);
        }
        logger_info_with_category("Control", "Handset lifted via web portal");
        return 1;
        
    } else if (strcmp(command, "handset_down") == 0) {
        /* Inject as hook event */
        hook_state_change_event_t *hook_event = hook_state_change_event_create('D');
        if (hook_event) {
            event_processor_process_event(event_processor, (event_t *)hook_event);
            event_destroy((event_t *)hook_event);
        }
        logger_info_with_category("Control", "Handset placed down via web portal");
        return 1;
    }
    
    return 0;
}

/* Signal handler */
void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        char log_msg[MAX_STRING_LEN];
        sprintf(log_msg, "Received signal %d, shutting down gracefully...", signal);
        logger_info_with_category("Daemon", log_msg);
        pthread_mutex_lock(&running_mutex);
        running = 0;
        pthread_mutex_unlock(&running_mutex);
    }
}

/* Health check functions */
health_status_t check_serial_connection(void) {
    /* This would check if the serial connection is working */
    /* For now, we'll return healthy */
    return HEALTH_STATUS_HEALTHY;
}

health_status_t check_sip_connection(void) {
    /* This would check if the SIP connection is active */
    /* For now, we'll return healthy */
    return HEALTH_STATUS_HEALTHY;
}

health_status_t check_daemon_activity(void) {
    if (!daemon_state) {
        return HEALTH_STATUS_CRITICAL;
    }
    
    time_t now = time(NULL);
    time_t last_activity;
    
    pthread_mutex_lock(&daemon_state_mutex);
    last_activity = daemon_state->last_activity;
    pthread_mutex_unlock(&daemon_state_mutex);
    
    time_t time_since_activity = now - last_activity;
    
    if (time_since_activity > 3600) { /* No activity for more than 1 hour */
        return HEALTH_STATUS_WARNING;
    }
    
    return HEALTH_STATUS_HEALTHY;
}

/* Helper function to update metrics - consolidated from thread */
static void update_metrics(void) {
    if (!daemon_state) return;
    
    /* Update system metrics */
    time_t uptime = time(NULL) - daemon_start_time;
    metrics_set_gauge("daemon_uptime_seconds", (double)uptime);
    
    /* Quick snapshot of state without holding mutex too long */
    pthread_mutex_lock(&daemon_state_mutex);
    double current_state = (double)daemon_state->current_state;
    double inserted_cents = (double)daemon_state->inserted_cents;
    double keypad_size = (double)daemon_state_get_keypad_length(daemon_state);
    pthread_mutex_unlock(&daemon_state_mutex);
    
    /* Update metrics outside of mutex */
    metrics_set_gauge("current_state", current_state);
    metrics_set_gauge("inserted_cents", inserted_cents);
    metrics_set_gauge("keypad_buffer_size", keypad_size);
}

int main(int argc, char *argv[]) {
    config_data_t* config = config_get_instance();
    /* health_monitor_t* health_monitor = health_monitor_get_instance(); */
    int loop_count = 0;
    char config_file[MAX_STRING_LEN];
    char start_msg[MAX_STRING_LEN];
    
    /* Record daemon start time for uptime calculation */
    daemon_start_time = time(NULL);
    
    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Load configuration */
    strcpy(config_file, "/etc/millennium/daemon.conf");
    if (argc > 2 && strcmp(argv[1], "--config") == 0) {
        strcpy(config_file, argv[2]);
    }
    
    if (!config_load_from_file(config, config_file)) {
        char log_msg[MAX_STRING_LEN];
        sprintf(log_msg, "Could not load config file: %s, using environment variables", config_file);
        logger_warn_with_category("Config", log_msg);
        config_load_from_environment(config);
    }
    
    if (!config_validate(config)) {
        logger_error_with_category("Config", "Configuration validation failed");
        return 1;
    }
    
    /* Setup logging */
    logger_set_level(logger_parse_level(config_get_log_level(config)));
    if (config_get_log_to_file(config) && strlen(config_get_log_file(config)) > 0) {
        fprintf(stderr, "Logging to %s\n", config_get_log_file(config));
        logger_set_log_file(config_get_log_file(config));
        logger_set_log_to_file(1);
    }
    
    logger_info_with_category("Daemon", "Starting Millennium Daemon");
    
    /* Initialize daemon state */
    daemon_state = (daemon_state_data_t*)malloc(sizeof(daemon_state_data_t));
    if (!daemon_state) {
        logger_error_with_category("Daemon", "Failed to allocate daemon state memory");
        return 1;
    }
    daemon_state_init(daemon_state);
    
    /* Initialize metrics */
    if (metrics_init() != 0) {
        logger_error_with_category("Daemon", "Failed to initialize metrics");
        return 1;
    }
    
    /* Initialize client */
    client = millennium_client_create();
    
    /* Initialize health monitoring */
    health_monitor_register_check("serial_connection", check_serial_connection, 30);
    health_monitor_register_check("sip_connection", check_sip_connection, 60);
    health_monitor_register_check("daemon_activity", check_daemon_activity, 120);
    health_monitor_start_monitoring();
    
    /* Start metrics server if enabled */
    if (config_get_metrics_server_enabled(config)) {
        metrics_server = metrics_server_create(config_get_metrics_server_port(config));
        if (metrics_server) {
            metrics_server_start(metrics_server);
        }
        sprintf(start_msg, "Metrics server started on port %d", 
                config_get_metrics_server_port(config));
        logger_info_with_category("Daemon", start_msg);
    } else {
        logger_info_with_category("Daemon", "Metrics server disabled");
    }
    
    /* Start web server if enabled */
    if (config_get_web_server_enabled(config)) {
        web_server = web_server_create(config_get_web_server_port(config));
        
        /* Add the web portal as a file route (streaming) */
        web_server_add_file_route(web_server, "/", "web_portal.html", "text/html");
        
        web_server_start(web_server);
        sprintf(start_msg, "Web server started on port %d", 
                config_get_web_server_port(config));
        logger_info_with_category("Daemon", start_msg);
    } else {
        logger_info_with_category("Daemon", "Web server disabled");
    }
    
    /* Metrics collection is now handled in the main loop */
    
    /* Initialize display */
    pthread_mutex_lock(&daemon_state_mutex);
    format_number(daemon_state->keypad_buffer, line1);
    generate_message(daemon_state->inserted_cents, line2);
    update_display();
    pthread_mutex_unlock(&daemon_state_mutex);
    
    /* Initialize event processor */
    event_processor = event_processor_create();
    if (!event_processor) {
        logger_error_with_category("Control", "Failed to create event processor");
        return 1;
    }
    
    /* Register event handlers */
    event_processor_register_coin_handler(event_processor, handle_coin_event);
    event_processor_register_call_state_handler(event_processor, handle_call_state_event);
    event_processor_register_hook_handler(event_processor, handle_hook_event);
    event_processor_register_keypad_handler(event_processor, handle_keypad_event);
    
    logger_info_with_category("Daemon", "Daemon initialized successfully");
    
    /* Main event loop */
    while (1) {
        pthread_mutex_lock(&running_mutex);
        if (!running) {
            pthread_mutex_unlock(&running_mutex);
            break;
        }
        pthread_mutex_unlock(&running_mutex);
        
        millennium_client_update(client);
        
        event_t *event = (event_t *)millennium_client_next_event(client);
        if (event) {
            event_processor_process_event(event_processor, event);
            event_destroy(event);
        }
        
        /* Update metrics in main loop (every 1000 loops = ~1 second) */
        if (++loop_count % 1000 == 0) {
            update_metrics();
        }
        
        /* Log metrics summary every 10000 loops (about every 10 seconds) at DEBUG level */
        if (loop_count % 10000 == 0) {
            logger_debug_with_category("Metrics", "=== Metrics Summary ===");
            
            /* Log current state gauge (simplified for C89 version) */
            double current_state = metrics_get_gauge("current_state");
            char log_msg[MAX_STRING_LEN];
            sprintf(log_msg, "Current state: %.0f", current_state);
            logger_debug_with_category("Metrics", log_msg);
        }
    }
    
    /* Cleanup */
    logger_info_with_category("Daemon", "Shutting down daemon");
    
    /* Stop metrics server */
    if (metrics_server) {
        metrics_server_stop(metrics_server);
        metrics_server_destroy(metrics_server);
        metrics_server = NULL;
    }
    
    /* Stop running flag */
    pthread_mutex_lock(&running_mutex);
    running = 0;
    pthread_mutex_unlock(&running_mutex);
    
    /* Stop health monitoring */
    health_monitor_stop_monitoring();
    
    /* Stop web server */
    if (web_server) {
        web_server_stop(web_server);
        web_server_destroy(web_server);
        web_server = NULL;
    }
    
    /* Cleanup client */
    millennium_client_destroy(client);
    client = NULL;
    
    /* Cleanup metrics */
    metrics_cleanup();
    
    /* Cleanup event processor */
    if (event_processor) {
        event_processor_destroy(event_processor);
        event_processor = NULL;
    }
    
    /* Cleanup daemon state */
    if (daemon_state) {
        free(daemon_state);
        daemon_state = NULL;
    }
    
    logger_info_with_category("Daemon", "Daemon shutdown complete");
    
    return 0;
}
