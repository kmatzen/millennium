#define _POSIX_C_SOURCE 200112L
#include "config.h"
#include "daemon_state.h"
#include "logger.h"
#include "metrics.h"
#include "metrics_server.h"
#include "call_metrics.h"
#include "health_monitor.h"
#include "web_server.h"
#include "millennium_sdk.h"
#include "events.h"
#include "event_processor.h"
#include "plugins.h"
#include "state_persistence.h"
#include "display_manager.h"
#include "audio_tones.h"
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>

#define EVENT_CATEGORIES 3
#define DISPLAY_WIDTH 20
#define MAX_STRING_LEN 256
#define MAX_KEYPAD_LEN 11
#define MAX_KEYPAD_DIGITS 10

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

/* State persistence */
static const char *state_file_path = NULL;

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
static void handle_coin_event(coin_event_t *coin_event);
static void handle_call_state_event(call_state_event_t *call_state_event);
static void handle_hook_event(hook_state_change_event_t *hook_event);
static void handle_keypad_event(keypad_event_t *keypad_event);
static void handle_card_event(card_event_t *card_event);
/* send_control_command is declared in web_server.h */
static void generate_display_bytes(char *output, size_t output_size);
static void update_display(void);
static int is_phone_ready_for_operation(void);
static int keypad_has_space(void);
static health_status_t check_serial_connection(void);
static void daemon_save_state(void);
static void daemon_broadcast_state(const char *event_type);
static health_status_t check_sip_connection(void);
static health_status_t check_daemon_activity(void);
static void update_metrics(void);

/* C-compatible functions for web server */
time_t get_daemon_start_time(void) {
    return daemon_start_time;
}

/* #109: Keep daemon_state.inserted_cents in sync when plugin deducts/refunds */
void plugins_adjust_inserted_cents(int delta) {
    if (!daemon_state) return;
    pthread_mutex_lock(&daemon_state_mutex);
    daemon_state->inserted_cents += delta;
    if (daemon_state->inserted_cents < 0) daemon_state->inserted_cents = 0;
    daemon_state_update_activity(daemon_state);
    pthread_mutex_unlock(&daemon_state_mutex);
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

    millennium_sdk_get_sip_status(&info.sip_registered, info.sip_last_error, sizeof(info.sip_last_error));
    
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

/* Helper function to safely copy strings with bounds checking */
static void safe_strcpy(char *dest, const char *src, size_t dest_size) {
    size_t n;
    if (!dest || dest_size == 0) return;
    if (!src) {
        dest[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n >= dest_size) n = dest_size - 1;
    memcpy(dest, src, n);
    dest[n] = '\0';
}

/* Helper function to update display - consolidates repetitive code */
static void update_display(void) {
    char display_bytes[100];
    if (!client || !daemon_state) {
        return;
    }

    generate_display_bytes(display_bytes, sizeof(display_bytes));
    millennium_client_set_display(client, display_bytes);
}

/* Helper function to update display with formatted content */
static void update_display_with_content(const char *line1_content, const char *line2_content) {
    if (!client || !daemon_state) {
        return;
    }
    
    safe_strcpy(line1, line1_content, sizeof(line1));
    safe_strcpy(line2, line2_content, sizeof(line2));
    update_display();
}

/* Helper function to validate phone state for operations */
static int is_phone_ready_for_operation(void) {
    return (daemon_state && daemon_state->current_state == DAEMON_STATE_IDLE_UP);
}

/* Helper function to validate keypad buffer has space */
static int keypad_has_space(void) {
    return (daemon_state && daemon_state_get_keypad_length(daemon_state) < MAX_KEYPAD_DIGITS);
}

/* Helper function to safely update daemon state with mutex protection */

static void daemon_save_state(void) {
    persisted_state_t ps;
    if (!state_file_path || !daemon_state) return;

    pthread_mutex_lock(&daemon_state_mutex);
    ps.inserted_cents = daemon_state->inserted_cents;
    ps.last_state = (int)daemon_state->current_state;
    pthread_mutex_unlock(&daemon_state_mutex);

    strncpy(ps.active_plugin, plugins_get_active_name() ? plugins_get_active_name() : "",
            sizeof(ps.active_plugin) - 1);
    ps.active_plugin[sizeof(ps.active_plugin) - 1] = '\0';

    state_persistence_save(&ps, state_file_path);
}

/* Minimal JSON string escaper for broadcast fields (display text may contain
 * quotes/backslashes). Writes a NUL-terminated, escaped copy into dst. */
static void daemon_json_escape(const char *src, char *dst, size_t dst_size) {
    size_t o = 0;
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    while (*src && o + 2 < dst_size) {
        unsigned char c = (unsigned char)*src++;
        if (c == '"' || c == '\\') {
            dst[o++] = '\\';
            dst[o++] = (char)c;
        } else if (c >= 0x20) {
            dst[o++] = (char)c;
        } /* drop control chars */
    }
    dst[o] = '\0';
}

static void daemon_broadcast_state(const char *event_type) {
    char msg[768];
    const char *state_str;
    const char *plugin_name;
    char line1[64], line2[64];
    char esc1[128], esc2[128];

    if (!web_server || !daemon_state) return;

    display_manager_get_text(line1, sizeof(line1), line2, sizeof(line2));
    daemon_json_escape(line1, esc1, sizeof(esc1));
    daemon_json_escape(line2, esc2, sizeof(esc2));

    pthread_mutex_lock(&daemon_state_mutex);
    state_str = daemon_state_to_string(daemon_state->current_state);
    plugin_name = plugins_get_active_name();
    snprintf(msg, sizeof(msg),
        "{\"event\":\"%s\",\"state\":\"%s\",\"coins\":%d,"
        "\"keypad\":\"%s\",\"plugin\":\"%s\","
        "\"line1\":\"%s\",\"line2\":\"%s\"}",
        event_type ? event_type : "update",
        state_str ? state_str : "UNKNOWN",
        daemon_state->inserted_cents,
        daemon_state->keypad_buffer,
        plugin_name ? plugin_name : "",
        esc1, esc2);
    pthread_mutex_unlock(&daemon_state_mutex);

    web_server_broadcast_to_websockets(web_server, msg);
}

void handle_coin_event(coin_event_t *coin_event) {
    char *coin_code_str;
    int coin_value = 0;
    if (!coin_event) {
        logger_error_with_category("Coin", "Received null coin event");
        return;
    }
    VALIDATE_BASICS();

    coin_code_str = coin_event_get_coin_code(coin_event);
    if (!coin_code_str) {
        logger_error_with_category("Coin", "Failed to get coin code");
        return;
    }


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
            
            logger_infof_with_category("Coin", 
                    "Coin inserted: %s, value: %d cents, total: %d cents",
                    coin_code_str, coin_value, daemon_state->inserted_cents);
            
            /* Let plugins handle display updates */
        }
        pthread_mutex_unlock(&daemon_state_mutex);
        
        /* Let active plugin handle the coin event */
        if (coin_value > 0) {
            plugins_handle_coin(coin_value, coin_code_str);
            daemon_save_state();
            daemon_broadcast_state("coin");
        }
    }
    
    free(coin_code_str);
}

void handle_call_state_event(call_state_event_t *call_state_event) {
    int need_hangup_sync = 0;
    int is_incoming;
    int phone_down;
    int phone_up;

    if (!call_state_event) {
        logger_error_with_category("Call", "Received null call state event");
        return;
    }
    VALIDATE_BASICS();

    pthread_mutex_lock(&daemon_state_mutex);
    is_incoming = (call_state_event_get_state(call_state_event) == EVENT_CALL_STATE_INCOMING);
    phone_down = (daemon_state->current_state == DAEMON_STATE_IDLE_DOWN);
    phone_up = (daemon_state->current_state == DAEMON_STATE_IDLE_UP);
    
    /* #92: Accept incoming when handset down (ring) or up (e.g. interrupt dialing) */
    if (is_incoming && (phone_down || phone_up)) {
        
        logger_info_with_category("Call", "Incoming call received");
        metrics_increment_counter("calls_incoming", 1);
        call_metrics_ringing_started();

        update_display_with_content("Call incoming...", line2);
        
        daemon_state->current_state = DAEMON_STATE_CALL_INCOMING;
        daemon_state_update_activity(daemon_state);
        
    } else if (call_state_event_get_state(call_state_event) == EVENT_CALL_STATE_ACTIVE) {
        /* Handle call established (when the SIP stack reports CALL_ESTABLISHED) */
        logger_info_with_category("Call", "Call established - audio should be working");
        metrics_increment_counter("calls_established", 1);
        
        update_display_with_content("Call active", "Audio connected");

        daemon_state->current_state = DAEMON_STATE_CALL_ACTIVE;
        daemon_state_update_activity(daemon_state);
        /* If this transition answered a ring (SIP-side answer), close it out;
         * a no-op for outbound calls that were never ringing. */
        call_metrics_ringing_answered();
        call_metrics_started();
    } else if (call_state_event_get_state(call_state_event) == EVENT_CALL_STATE_INVALID) {
        /* #90/#91: Call ended - remote hung up or call failed during dial */
        if (daemon_state->current_state == DAEMON_STATE_CALL_ACTIVE) {
            logger_info_with_category("Call", "Call ended by remote party");
            metrics_increment_counter("calls_ended", 1);
            call_metrics_ended();
            daemon_state_clear_keypad(daemon_state);
            daemon_state->inserted_cents = 0;
            daemon_state->current_state = DAEMON_STATE_IDLE_UP;
            daemon_state_update_activity(daemon_state);
            need_hangup_sync = 1;
        } else if (daemon_state->current_state == DAEMON_STATE_CALL_INCOMING) {
            /* #91: Call failed during dial - don't clear coins (refund via plugin) */
            logger_info_with_category("Call", "Call failed during dial");
            /* Resolve the ended CALL_INCOMING phase: a genuine inbound ring that
             * was never answered counts a miss (call_ring_seconds + calls_missed),
             * while the web-initiated outbound start_call path (CALL_INCOMING
             * without a ring) counts an outbound dial failure (calls_failed). */
            call_metrics_incoming_ended();
            daemon_state_clear_keypad(daemon_state);
            daemon_state->current_state = DAEMON_STATE_IDLE_UP;
            daemon_state_update_activity(daemon_state);
            need_hangup_sync = 1;
        }
    }
    pthread_mutex_unlock(&daemon_state_mutex);
    
    /* Let active plugin handle the call state event */
    plugins_handle_call_state(call_state_event_get_state(call_state_event));
    
    /* #90: Sync with SDK when remote hung up */
    if (need_hangup_sync) {
        millennium_client_hangup(client);
        millennium_client_write_to_coin_validator(client, 'c');
        millennium_client_write_to_coin_validator(client, 'z');
    }
    
    daemon_save_state();
    daemon_broadcast_state("call_state");

    /* Handle coin validator + ringer outside of mutex */
    {
        call_state_t st = call_state_event_get_state(call_state_event);
        if (st == EVENT_CALL_STATE_INCOMING) {
            millennium_client_write_to_coin_validator(client, 'f');
            millennium_client_write_to_coin_validator(client, 'z');
            /* Ring the bell until the call is answered (handset lift) or ends.
             * PJSIP (unlike baresip's audio_alert) plays no local ring, so the
             * daemon drives the ringer. Safe because the call audio device is
             * only opened by PJSIP once the call is answered. */
            audio_tones_play_ring();
        } else if (st == EVENT_CALL_STATE_ACTIVE || st == EVENT_CALL_STATE_INVALID) {
            audio_tones_stop();
        }
    }
}

void handle_hook_event(hook_state_change_event_t *hook_event) {
    int hook_up;
    int hook_down;
    int resulting_state;

    if (!hook_event) {
        logger_error_with_category("Hook", "Received null hook event");
        return;
    }
    VALIDATE_BASICS();

    pthread_mutex_lock(&daemon_state_mutex);
    hook_up = (hook_state_change_event_get_direction(hook_event) == 'U');
    hook_down = (hook_state_change_event_get_direction(hook_event) == 'D');
    
    if (hook_up) {
        int call_incoming = (daemon_state->current_state == DAEMON_STATE_CALL_INCOMING);
        int phone_down = (daemon_state->current_state == DAEMON_STATE_IDLE_DOWN);
        
        if (call_incoming) {
            logger_info_with_category("Call", "Call answered");
            metrics_increment_counter("calls_answered", 1);

            daemon_state->current_state = DAEMON_STATE_CALL_ACTIVE;
            daemon_state_update_activity(daemon_state);
            call_metrics_ringing_answered();
            call_metrics_started();

        } else if (phone_down) {
            logger_info_with_category("Hook", "Hook lifted, transitioning to IDLE_UP");
            metrics_increment_counter("hook_lifted", 1);
            
            daemon_state->current_state = DAEMON_STATE_IDLE_UP;
            daemon_state_update_activity(daemon_state);
            
            daemon_state->inserted_cents = 0;
            daemon_state_clear_keypad(daemon_state);
            
            /* Let plugins handle display updates */
        }
    } else if (hook_down) {
        logger_info_with_category("Hook", "Hook down, call ended");
        metrics_increment_counter("hook_down", 1);
        
        if (daemon_state->current_state == DAEMON_STATE_CALL_ACTIVE) {
            metrics_increment_counter("calls_ended", 1);
            call_metrics_ended();
        }

        daemon_state_clear_keypad(daemon_state);
        daemon_state->inserted_cents = 0;
        
        /* Let plugins handle display updates */
        
        /* Update state */
        daemon_state->current_state = DAEMON_STATE_IDLE_DOWN;
        daemon_state_update_activity(daemon_state);
    }
    resulting_state = (int)daemon_state->current_state;
    pthread_mutex_unlock(&daemon_state_mutex);

    /* Let active plugin handle the hook event */
    plugins_handle_hook(hook_up, hook_down);
    
    /* Handle external calls outside of mutex using snapshot taken above */
    if (hook_up) {
        if (resulting_state == DAEMON_STATE_CALL_ACTIVE) {
            millennium_client_answer_call(client);
        } else if (resulting_state == DAEMON_STATE_IDLE_UP) {
            millennium_client_write_to_coin_validator(client, 'a');
        }
    } else if (hook_down) {
        millennium_client_hangup(client);
        millennium_client_write_to_coin_validator(client, 'c'); /* Always 'c' for hook down */
        millennium_client_write_to_coin_validator(client, 'z');
    }

    daemon_save_state();
    daemon_broadcast_state("hook");
}

void handle_keypad_event(keypad_event_t *keypad_event) {
    char key;
    if (!keypad_event) {
        logger_error_with_category("Keypad", "Received null keypad event");
        return;
    }
    VALIDATE_BASICS();

    key = keypad_event_get_key(keypad_event);

    /* Digits feed the shared dial buffer (for Classic Phone dialing) only
     * while the phone is ready and the buffer has room. */
    if (isdigit(key)) {
        pthread_mutex_lock(&daemon_state_mutex);
        if (keypad_has_space() && is_phone_ready_for_operation()) {
            logger_debugf_with_category("Keypad", "Key pressed: %c", key);
            daemon_state_add_key(daemon_state, key);
            daemon_state_update_activity(daemon_state);
        }
        pthread_mutex_unlock(&daemon_state_mutex);
    }

    /* Every keypress — digit, *, #, or matrix letter — is delivered to the
     * active plugin in every state, so plugins can use the full keypad
     * (games, menus, IVR/DTMF during calls). Plugins ignore keys they don't
     * care about. */
    metrics_increment_counter("keypad_presses", 1);
    plugins_handle_keypad(key);
    daemon_broadcast_state("keypad");
}

void handle_card_event(card_event_t *card_event) {
    if (!card_event) {
        logger_error_with_category("Card", "Received null card event");
        return;
    }
    VALIDATE_BASICS();

    logger_infof_with_category("Card", "Card swiped: %.4s...", card_event->card_number);
    metrics_increment_counter("card_swipes", 1);

    plugins_handle_card(card_event->card_number);
    daemon_broadcast_state("card");
}

/* Implementation of sendControlCommand */
int send_control_command(const char* action) {
    char command[MAX_STRING_LEN];
    char arg[MAX_STRING_LEN];
    const char *colon_pos;
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
    
    logger_infof_with_category("Control", "Received control command: %s", action);
    printf("[CONTROL] Received command: %s\n", action);
    
    /* Parse command and arguments */
    colon_pos = strchr(action, ':');

    if (colon_pos) {
        size_t cmd_len = colon_pos - action;
        if (cmd_len >= MAX_STRING_LEN) {
            cmd_len = MAX_STRING_LEN - 1;
        }
        safe_strcpy(command, action, cmd_len + 1);
        command[cmd_len] = '\0';
        
        /* Safely copy argument */
        safe_strcpy(arg, colon_pos + 1, MAX_STRING_LEN);
    } else {
        safe_strcpy(command, action, MAX_STRING_LEN);
        arg[0] = '\0';
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
        /* Inject any keypad key: digits, * and #, or matrix letters A-D, so
         * the full keypad is usable from the web/REST API (games, menus). */
        char k = arg[0];
        if (isdigit((unsigned char)k) || k == '*' || k == '#' ||
            (k >= 'A' && k <= 'D')) {
            keypad_event_t *keypad_event = keypad_event_create(k);
            if (keypad_event) {
                event_processor_process_event(event_processor, (event_t *)keypad_event);
                event_destroy((event_t *)keypad_event);
            }
            logger_infof_with_category("Control", "Keypad key '%c' pressed via web portal", k);
            return 1;
        } else {
            logger_warnf_with_category("Control", "Invalid keypad key: %c", k);
            return 0;
        }
        
    } else if (strcmp(command, "keypad_clear") == 0) {
        int success;
        /* Only allow clear when handset is up (same as physical keypad logic) */
        pthread_mutex_lock(&daemon_state_mutex);
        if (is_phone_ready_for_operation()) {
            daemon_state_clear_keypad(daemon_state);
            daemon_state_update_activity(daemon_state);
            metrics_increment_counter("keypad_clears", 1);
            logger_info_with_category("Control", "Keypad cleared via web portal");

            /* Let plugins handle display updates */
        } else {
            logger_warn_with_category("Control", "Keypad clear ignored - handset down");
        }
        success = is_phone_ready_for_operation();
        pthread_mutex_unlock(&daemon_state_mutex);
        return success;
        
    } else if (strcmp(command, "keypad_backspace") == 0) {
        int buffer_not_empty;
        int success;
        /* Only allow backspace when handset is up and buffer is not empty */
        pthread_mutex_lock(&daemon_state_mutex);
        buffer_not_empty = (daemon_state_get_keypad_length(daemon_state) > 0);

        if (is_phone_ready_for_operation() && buffer_not_empty) {
            daemon_state_remove_last_key(daemon_state);
            daemon_state_update_activity(daemon_state);
            metrics_increment_counter("keypad_backspaces", 1);
            logger_info_with_category("Control", "Keypad backspace via web portal");

            /* Let plugins handle display updates */
        } else {
            logger_warn_with_category("Control", "Keypad backspace ignored - handset down or buffer empty");
        }
        success = (is_phone_ready_for_operation() && buffer_not_empty);
        pthread_mutex_unlock(&daemon_state_mutex);
        return success;
        
    } else if (strcmp(command, "coin_insert") == 0) {
        int cents;
        uint8_t coin_code;
        coin_event_t *coin_event;
        /* Extract cents from argument and inject as coin event */
        if (strlen(arg) == 0) {
            logger_warn_with_category("Control", "Coin insert command missing argument");
            return 0;
        }

        logger_infof_with_category("Control", "Extracted cents string: '%s'", arg);
        printf("[CONTROL] Extracted cents: '%s'\n", arg);

        cents = atoi(arg);

        /* Validate coin value */
        if (cents <= 0) {
            logger_warnf_with_category("Control", "Invalid coin value: %d¢", cents);
            return 0;
        }

        /* Map cents to coin codes (same as physical coin reader) */
        switch (cents) {
            case 5:  coin_code = 0x36; break; /* COIN_6 */
            case 10: coin_code = 0x37; break; /* COIN_7 */
            case 25: coin_code = 0x38; break; /* COIN_8 */
            default:
                logger_warnf_with_category("Control", "Invalid coin value: %d¢", cents);
                return 0;
        }
        
        coin_event = coin_event_create(coin_code);
        if (coin_event) {
            event_processor_process_event(event_processor, (event_t *)coin_event);
            event_destroy((event_t *)coin_event);
        }
        logger_infof_with_category("Control", "Coin inserted: %d¢ via web portal", cents);
        printf("[CONTROL] Coin inserted successfully: %d¢\n", cents);
        
        return 1;
        
    } else if (strcmp(command, "coin_return") == 0) {
        pthread_mutex_lock(&daemon_state_mutex);
        daemon_state->inserted_cents = 0;
        daemon_state_update_activity(daemon_state);
        metrics_set_gauge("inserted_cents", 0.0);
        metrics_increment_counter("coin_returns", 1);
        logger_info_with_category("Control", "Coins returned via web portal");
        
        /* Let plugins handle display updates */
        
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
        
    } else if (strcmp(command, "activate_plugin") == 0) {
        /* Activate a plugin */
        if (strlen(arg) > 0) {
            int result = plugins_activate(arg);
            if (result == 0) {
                logger_infof_with_category("Control", "Plugin %s activated via web portal", arg);
                return 1;
            } else {
                logger_warnf_with_category("Control", "Failed to activate plugin %s", arg);
                return 0;
            }
        } else {
            logger_warn_with_category("Control", "Plugin activation command missing plugin name");
            return 0;
        }
    }
    
    return 0;
}

/* Signal handler */
void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        logger_infof_with_category("Daemon", "Received signal %d, shutting down gracefully...", signal);
        pthread_mutex_lock(&running_mutex);
        running = 0;
        pthread_mutex_unlock(&running_mutex);
    } else {
        logger_warnf_with_category("Daemon", "Received unexpected signal %d", signal);
    }
}

/* Health check functions */
health_status_t check_serial_connection(void) {
    if (!client) return HEALTH_STATUS_UNKNOWN;
    return millennium_client_serial_is_healthy(client)
        ? HEALTH_STATUS_HEALTHY : HEALTH_STATUS_CRITICAL;
}

health_status_t check_sip_connection(void) {
    int registered = 0;
    millennium_sdk_get_sip_status(&registered, NULL, 0);
    if (registered == 1) return HEALTH_STATUS_HEALTHY;
    if (registered == -1) return HEALTH_STATUS_CRITICAL;
    return HEALTH_STATUS_UNKNOWN;
}

health_status_t check_daemon_activity(void) {
    time_t now;
    time_t last_activity;
    time_t time_since_activity;
    if (!daemon_state) {
        return HEALTH_STATUS_CRITICAL;
    }

    now = time(NULL);

    pthread_mutex_lock(&daemon_state_mutex);
    last_activity = daemon_state->last_activity;
    pthread_mutex_unlock(&daemon_state_mutex);

    time_since_activity = now - last_activity;

    if (time_since_activity > 3600) { /* No activity for more than 1 hour */
        return HEALTH_STATUS_WARNING;
    }
    
    return HEALTH_STATUS_HEALTHY;
}

/* Helper function to update metrics - consolidated from thread */
static void update_metrics(void) {
    time_t uptime;
    double current_state;
    double inserted_cents;
    double keypad_size;
    if (!daemon_state) return;

    /* Update system metrics */
    uptime = time(NULL) - daemon_start_time;
    metrics_set_gauge("daemon_uptime_seconds", (double)uptime);

    /* Quick snapshot of state without holding mutex too long */
    pthread_mutex_lock(&daemon_state_mutex);
    current_state = (double)daemon_state->current_state;
    inserted_cents = (double)daemon_state->inserted_cents;
    keypad_size = (double)daemon_state_get_keypad_length(daemon_state);
    pthread_mutex_unlock(&daemon_state_mutex);
    
    /* Update metrics outside of mutex */
    metrics_set_gauge("current_state", current_state);
    metrics_set_gauge("inserted_cents", inserted_cents);
    metrics_set_gauge("keypad_buffer_size", keypad_size);

    /* Async logger health (issue #123 follow-up). Publishes queue depth, the
     * high-water mark (capacity headroom), and a true counter of dropped log
     * lines so log loss is alertable out-of-band — the in-file drop notice is
     * unreachable exactly when a slow/full disk is the cause. Called from the
     * single main-loop tick, so the static delta tracker needs no lock. */
    {
        logger_queue_stats_t lstats;
        static unsigned long long last_dropped = 0;

        logger_get_queue_stats(&lstats);
        metrics_set_gauge("log_queue_depth", (double)lstats.depth);
        metrics_set_gauge("log_queue_high_water", (double)lstats.high_water);
        if (lstats.dropped_total > last_dropped) {
            metrics_increment_counter("log_lines_dropped",
                (uint64_t)(lstats.dropped_total - last_dropped));
            last_dropped = lstats.dropped_total;
        }
    }
}

int main(int argc, char *argv[]) {
    config_data_t* config;
    /* health_monitor_t* health_monitor = health_monitor_get_instance(); */
    char config_file[MAX_STRING_LEN];

    /* Daemon has no controlling terminal: point stdin at /dev/null so nothing
     * accidentally polls fd 0 (which under systemd can be a non-pollable fd). */
    {
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            if (devnull != STDIN_FILENO) close(devnull);
        }
    }

    config = config_get_instance();

    /* Record daemon start time for uptime calculation */
    daemon_start_time = time(NULL);
    
    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Load configuration */
    strcpy(config_file, "/etc/millennium/daemon.conf");
    if (argc > 2 && strcmp(argv[1], "--config") == 0) {
        strncpy(config_file, argv[2], MAX_STRING_LEN - 1);
        config_file[MAX_STRING_LEN - 1] = '\0';
    }
    
    if (!config_load_from_file(config, config_file)) {
        logger_warnf_with_category("Config", "Could not load config file: %s, using environment/defaults (#113)", config_file);
        config_load_from_environment(config);
    } else {
        logger_infof_with_category("Config", "Config loaded from %s", config_file);
    }
    
    if (!config_validate(config)) {
        logger_error_with_category("Config", "Configuration validation failed");
        return 1;
    }
    
    /* Setup logging */
    logger_set_level(logger_parse_level(config_get_log_level(config)));
    logger_set_rotation(
        (long)config_get_log_max_size_bytes(config),
        config_get_log_max_files(config));
    if (config_get_log_to_file(config) && strlen(config_get_log_file(config)) > 0) {
        fprintf(stderr, "Logging to %s\n", config_get_log_file(config));
        logger_set_log_file(config_get_log_file(config));
        logger_set_log_to_file(1);
    }
    
    logger_info_with_category("Daemon", "Starting Millennium Daemon");

    state_file_path = config_get_state_file(config);
    
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
    display_manager_init(client);
    
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
        logger_infof_with_category("Daemon", "Metrics server started on port %d", 
                config_get_metrics_server_port(config));
    } else {
        logger_info_with_category("Daemon", "Metrics server disabled");
    }
    
    /* Start web server if enabled */
    if (config_get_web_server_enabled(config)) {
        web_server = web_server_create(config_get_web_server_port(config));
        
        /* Add the web portal as a file route (streaming) */
        web_server_add_file_route(web_server, "/", "/usr/local/share/millennium/web_portal.html", "text/html");
        
        web_server_start(web_server);
        logger_infof_with_category("Daemon", "Web server started on port %d", 
                config_get_web_server_port(config));
    } else {
        logger_info_with_category("Daemon", "Web server disabled");
    }
    
    /* Metrics collection is now handled in the main loop */
    
    /* Initialize display - plugins will handle their own display */
    pthread_mutex_lock(&daemon_state_mutex);
    /* Let plugins handle display updates */
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
    event_processor_register_card_handler(event_processor, handle_card_event);
    
    /* Initialize audio tones */
    audio_tones_init();

    /* Initialize plugin system */
    plugins_init();

    /* Restore persisted state */
    {
        persisted_state_t ps;
        if (state_persistence_load(&ps, state_file_path) == 0) {
            logger_infof_with_category("Daemon",
                "Restored state: coins=%d, plugin=%s, last_state=%d",
                ps.inserted_cents, ps.active_plugin, ps.last_state);

            pthread_mutex_lock(&daemon_state_mutex);
            daemon_state->inserted_cents = ps.inserted_cents;
            pthread_mutex_unlock(&daemon_state_mutex);

            if (strlen(ps.active_plugin) > 0) {
                plugins_activate(ps.active_plugin);
            }

            if (ps.last_state == (int)DAEMON_STATE_CALL_ACTIVE ||
                ps.last_state == (int)DAEMON_STATE_CALL_INCOMING) {
                logger_warn_with_category("Daemon",
                    "Unclean shutdown detected: resetting to IDLE_DOWN (#96)");
                metrics_increment_counter("unclean_shutdowns", 1);
                /* No SIP call exists after restart; ensure state is IDLE_DOWN */
                pthread_mutex_lock(&daemon_state_mutex);
                daemon_state->current_state = DAEMON_STATE_IDLE_DOWN;
                daemon_state_update_activity(daemon_state);
                pthread_mutex_unlock(&daemon_state_mutex);
            }
        }
    }

    logger_info_with_category("Daemon", "Daemon initialized successfully");
    
    /* Main event loop */
    while (1) {
        event_t *event;
        pthread_mutex_lock(&running_mutex);
        if (!running) {
            pthread_mutex_unlock(&running_mutex);
            break;
        }
        pthread_mutex_unlock(&running_mutex);

        millennium_client_update(client);

        event = (event_t *)millennium_client_next_event(client);
        if (event) {
            event_processor_process_event(event_processor, event);
            event_destroy(event);
        } else {
            /* No event: yield ~10ms to keep idle CPU low (the OS buffers serial,
             * so input latency stays imperceptible) — frees the single core for
             * call audio (#115). */
            {
                struct timeval tv;
                tv.tv_sec = 0;
                tv.tv_usec = 10000;
                select(0, NULL, NULL, NULL, &tv);
            }
        }
        
        /* Periodic work on a wall-clock schedule. The loop now idles at ~10ms
         * (and runs faster when events flow), so we can't count iterations —
         * gate on elapsed time instead. ~300ms keeps display scrolling/game
         * animation smooth while leaving the CPU free for call audio. */
        {
            static struct timespec last_tick = {0, 0};
            static struct timespec last_summary = {0, 0};
            static char last_display[128] = "";
            struct timespec now_ts;
            long tick_ms;

            clock_gettime(CLOCK_MONOTONIC, &now_ts);
            tick_ms = (now_ts.tv_sec - last_tick.tv_sec) * 1000L +
                      (now_ts.tv_nsec - last_tick.tv_nsec) / 1000000L;
            if (tick_ms >= 300) {
                char cur1[64], cur2[64], cur[128];
                long summary_ms;
                last_tick = now_ts;

                update_metrics();
                plugins_tick();
                display_manager_tick();
                millennium_client_check_serial(client);

                /* Broadcast tick-driven display changes (game animations,
                 * fortune reveals) so the dashboard VFD stays live without a
                 * user event. Compares full text, so scrolling won't spam. */
                display_manager_get_text(cur1, sizeof(cur1), cur2, sizeof(cur2));
                snprintf(cur, sizeof(cur), "%s\n%s", cur1, cur2);
                if (strcmp(cur, last_display) != 0) {
                    safe_strcpy(last_display, cur, sizeof(last_display));
                    daemon_broadcast_state("display");
                }

                summary_ms = (now_ts.tv_sec - last_summary.tv_sec) * 1000L +
                             (now_ts.tv_nsec - last_summary.tv_nsec) / 1000000L;
                if (summary_ms >= 10000) {
                    last_summary = now_ts;
                    logger_debug_with_category("Metrics", "=== Metrics Summary ===");
                    {
                        double current_state = metrics_get_gauge("current_state");
                        logger_debugf_with_category("Metrics", "Current state: %.0f", current_state);
                    }
                }
            }
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
    
    /* Cleanup audio tones */
    audio_tones_cleanup();

    /* Cleanup plugin system */
    plugins_cleanup();
    
    /* Cleanup daemon state */
    if (daemon_state) {
        free(daemon_state);
        daemon_state = NULL;
    }
    
    logger_info_with_category("Daemon", "Daemon shutdown complete");

    /* Drain the async log queue and stop the writer thread so the final
     * lines reach disk before we exit (issue #123). */
    logger_shutdown();

    return 0;
}
