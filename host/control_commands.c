#define _POSIX_C_SOURCE 200112L
#include "control_commands.h"
#include "daemon_state.h"
#include "events.h"
#include "event_processor.h"
#include "plugins.h"
#include "metrics.h"
#include "logger.h"
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_STRING_LEN 256

/* Each binary that links this file (daemon.c, simulator.c, unit_tests.c)
 * defines its own copy of these globals -- same convention as plugins.c /
 * plugin_sdk.c, which take the same externs. */
extern daemon_state_data_t *daemon_state;
extern event_processor_t *event_processor;
extern pthread_mutex_t daemon_state_mutex;

/* Helper function to safely copy strings with bounds checking. A private
 * copy of daemon.c's own safe_strcpy() -- small enough that duplicating it
 * beats coupling this file to daemon.c's other statics. */
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

/* Helper function to validate phone state for operations. A private copy of
 * daemon.c's own is_phone_ready_for_operation(). */
static int is_phone_ready_for_operation(void) {
    return (daemon_state && daemon_state->current_state == DAEMON_STATE_IDLE_UP);
}

int dispatch_control_command(const char *action) {
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
        int returned_cents;
        pthread_mutex_lock(&daemon_state_mutex);
        returned_cents = daemon_state->inserted_cents;
        daemon_state->inserted_cents = 0;
        daemon_state_update_activity(daemon_state);
        metrics_set_gauge("inserted_cents", 0.0);
        metrics_increment_counter("coin_returns", 1);
        /* Track the value handed back so net revenue is observable:
         * net = coins_value_cents - coins_returned_cents */
        if (returned_cents > 0) {
            metrics_increment_counter("coins_returned_cents",
                                      (uint64_t)returned_cents);
        }
        logger_infof_with_category("Control",
                "Coins returned via web portal: %d cents", returned_cents);

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
