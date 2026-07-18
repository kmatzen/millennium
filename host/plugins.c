#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include "plugins.h"
#include "logger.h"
#include "millennium_sdk.h"
#include "metrics.h"
#include "plugin_sdk.h"

/* Maximum number of plugins. Generous headroom so experimenters can add
 * their own alongside the built-ins (8 ship by default). */
#define MAX_PLUGINS 32

/* Plugin registry */
static plugin_t plugins[MAX_PLUGINS];
static int plugin_count = 0;
static int active_plugin_index = -1;
static pthread_mutex_t plugins_mutex = PTHREAD_MUTEX_INITIALIZER;

/* External references */
extern daemon_state_data_t *daemon_state;
extern millennium_client_t *client;

void plugins_init(void) {
    plugin_count = 0;
    active_plugin_index = -1;
    
    /* Register built-in plugins */
    register_classic_phone_plugin();
    register_fortune_teller_plugin();
    register_jukebox_plugin();
    register_number_guess_plugin();
    register_simon_plugin();
    register_dial_a_joke_plugin();
    register_trivia_plugin();
    register_time_operator_plugin();

    /* Activate classic phone by default */
    plugins_activate("Classic Phone");
    
    logger_info_with_category("Plugins", "Plugin system initialized");
}

void plugins_cleanup(void) {
    plugin_count = 0;
    active_plugin_index = -1;
    logger_info_with_category("Plugins", "Plugin system cleaned up");
}

int plugins_register(const char *name, const char *description,
                    coin_handler_t coin_handler,
                    keypad_handler_t keypad_handler,
                    hook_handler_t hook_handler,
                    call_state_handler_t call_state_handler,
                    card_handler_t card_handler,
                    activation_handler_t activation_handler,
                    tick_handler_t tick_handler) {
    int i;
    if (plugin_count >= MAX_PLUGINS) {
        logger_error_with_category("Plugins", "Maximum number of plugins reached");
        return -1;
    }
    
    if (!name || !description) {
        logger_error_with_category("Plugins", "Plugin name and description required");
        return -1;
    }
    
    /* Check if plugin already exists */
    for (i = 0; i < plugin_count; i++) {
        if (strcmp(plugins[i].name, name) == 0) {
            logger_warnf_with_category("Plugins", "Plugin %s already registered", name);
            return -1;
        }
    }
    
    /* Register the plugin */
    plugins[plugin_count].name = name;
    plugins[plugin_count].description = description;
    plugins[plugin_count].handle_coin = coin_handler;
    plugins[plugin_count].handle_keypad = keypad_handler;
    plugins[plugin_count].handle_hook = hook_handler;
    plugins[plugin_count].handle_call_state = call_state_handler;
    plugins[plugin_count].handle_card = card_handler;
    plugins[plugin_count].handle_activation = activation_handler;
    plugins[plugin_count].handle_tick = tick_handler;
    
    plugin_count++;
    
    logger_infof_with_category("Plugins", "Plugin %s registered", name);
    return 0;
}

/* Bump the activation counters for a plugin that was just made active.
 *
 * Exposes which of the built-in experiences actually get used on a phone in
 * the field: a per-plugin counter (plugin_activations_<name>) plus an
 * aggregate (plugin_activations_total). Both are plain Prometheus counters, so
 * they ride out through the existing dynamic metrics export with no endpoint
 * changes. "Activation" here means "made the active plugin" — that includes the
 * boot-time default and a restore from persistence, not only deliberate user
 * switches, which is the honest count for a counter named this way.
 *
 * Called *after* releasing plugins_mutex: metrics_increment_counter takes its
 * own lock, and keeping the two mutexes strictly un-nested avoids any lock
 * ordering concern. Safe before metrics_init() too — the increment is a no-op
 * until the metrics subsystem is up. */
static void plugins_record_activation(const char *name) {
    char counter[128];
    metrics_increment_counter("plugin_activations_total", 1);
    snprintf(counter, sizeof(counter), "plugin_activations_%s", name);
    metrics_increment_counter(counter, 1);
}

int plugins_activate(const char *plugin_name) {
    int i;
    void (*activation)(void) = NULL;
    int found = 0;
    int previous = -1;

    if (!plugin_name) {
        logger_error_with_category("Plugins", "Plugin name required for activation");
        return -1;
    }

    /* Look the plugin up and snapshot its activation handler under the lock,
     * but CALL the handler after releasing it (#226).
     *
     * This is the same discipline plugins_snapshot_active() documents below:
     * plugins_mutex is not recursive, so a handler that re-enters the registry
     * -- plugins_activate, plugins_get_active_name, plugins_get_info,
     * plugins_to_json -- would deadlock against itself. This function used to
     * be the one place that broke that rule.
     *
     * The plugins[] table is a fixed static array whose entries are never moved
     * or unregistered, so the snapshotted function pointer stays valid. */
    pthread_mutex_lock(&plugins_mutex);
    previous = active_plugin_index;
    for (i = 0; i < plugin_count; i++) {
        if (strcmp(plugins[i].name, plugin_name) == 0) {
            activation = plugins[i].handle_activation;
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&plugins_mutex);

    /* Resolve the lookup BEFORE tearing anything down: a request naming a
     * plugin that does not exist must leave the current session untouched. */
    if (!found) {
        logger_warnf_with_category("Plugins", "Plugin %s not found", plugin_name);
        return -1;
    }

    /* #223: the plugin API has no handle_deactivation hook, so the outgoing
     * plugin never gets a chance to release what it was holding. Switching
     * mid-call used to leave the SIP call up, the audio running and the daemon
     * in CALL_ACTIVE, while the outgoing plugin's own activation handler later
     * reset is_in_call and forgot the call existed. The daemon releases the
     * resources it owns instead of obliging every plugin author to.
     *
     * Only on a real switch: re-activating the plugin that is already active
     * (which the web dashboard can do) must not drop a call in progress. */
    if (previous >= 0 && previous != i) {
        sdk_release_session();
    }

    /* Initialize BEFORE publishing the new active index. The old code assigned
     * active_plugin_index first and relied on holding plugins_mutex across the
     * handler to keep dispatch out; dispatching unlocked, that order would let
     * an event reach a half-initialized plugin. Publishing last means dispatch
     * sees the previous plugin until the new one is ready. */
    if (activation) {
        activation();
    }

    pthread_mutex_lock(&plugins_mutex);
    active_plugin_index = i;
    pthread_mutex_unlock(&plugins_mutex);

    logger_infof_with_category("Plugins", "Plugin %s activated", plugin_name);
    plugins_record_activation(plugin_name);
    return 0;
}

const char* plugins_get_active_name(void) {
    const char *name = NULL;
    pthread_mutex_lock(&plugins_mutex);
    if (active_plugin_index >= 0 && active_plugin_index < plugin_count) {
        name = plugins[active_plugin_index].name;
    }
    pthread_mutex_unlock(&plugins_mutex);
    return name;
}

int plugins_list(char *buffer, size_t buffer_size) {
    int pos = 0;
    int i;
    if (!buffer || buffer_size == 0) {
        return -1;
    }

    pthread_mutex_lock(&plugins_mutex);

    buffer[0] = '\0';

    for (i = 0; i < plugin_count && pos < (int)buffer_size - 1; i++) {
        char temp[256];
        int len;
        snprintf(temp, sizeof(temp), "%s: %s%s\n",
                plugins[i].name, plugins[i].description,
                (i == active_plugin_index) ? " (ACTIVE)" : "");

        len = strlen(temp);
        if (pos + len < (int)buffer_size - 1) {
            strncpy(buffer + pos, temp, buffer_size - pos - 1);
            buffer[buffer_size - 1] = '\0';
            pos += len;
        }
    }
    
    pthread_mutex_unlock(&plugins_mutex);
    return 0;
}

int plugins_get_count(void) {
    int count;
    pthread_mutex_lock(&plugins_mutex);
    count = plugin_count;
    pthread_mutex_unlock(&plugins_mutex);
    return count;
}

int plugins_get_info(int index, const char **name, const char **description,
                     int *is_active) {
    pthread_mutex_lock(&plugins_mutex);
    if (index < 0 || index >= plugin_count) {
        pthread_mutex_unlock(&plugins_mutex);
        return -1;
    }
    if (name) *name = plugins[index].name;
    if (description) *description = plugins[index].description;
    if (is_active) *is_active = (index == active_plugin_index) ? 1 : 0;
    pthread_mutex_unlock(&plugins_mutex);
    return 0;
}

/* Append src to dst (at *pos) with JSON-special characters escaped, never
 * overflowing cap. Advances *pos. */
static void json_append_escaped(char *dst, size_t cap, size_t *pos,
                                const char *src) {
    size_t i;
    if (!src) src = "";
    for (i = 0; src[i] != '\0'; i++) {
        char esc[8];
        const char *chunk;
        size_t len;
        unsigned char c = (unsigned char)src[i];
        switch (c) {
            case '"':  chunk = "\\\""; len = 2; break;
            case '\\': chunk = "\\\\"; len = 2; break;
            case '\n': chunk = "\\n";  len = 2; break;
            case '\r': chunk = "\\r";  len = 2; break;
            case '\t': chunk = "\\t";  len = 2; break;
            default:
                if (c < 0x20) {
                    snprintf(esc, sizeof(esc), "\\u%04x", c);
                    chunk = esc; len = 6;
                } else {
                    esc[0] = (char)c; esc[1] = '\0';
                    chunk = esc; len = 1;
                }
                break;
        }
        if (*pos + len >= cap) break; /* leave room for NUL */
        memcpy(dst + *pos, chunk, len);
        *pos += len;
    }
    dst[*pos] = '\0';
}

static void json_append_lit(char *dst, size_t cap, size_t *pos,
                            const char *lit) {
    size_t len = strlen(lit);
    if (*pos + len >= cap) return;
    memcpy(dst + *pos, lit, len);
    *pos += len;
    dst[*pos] = '\0';
}

int plugins_to_json(char *buffer, size_t buffer_size) {
    size_t pos = 0;
    int i;

    if (!buffer || buffer_size == 0) return -1;
    buffer[0] = '\0';

    pthread_mutex_lock(&plugins_mutex);

    json_append_lit(buffer, buffer_size, &pos, "{\"plugins\":[");
    for (i = 0; i < plugin_count; i++) {
        if (i > 0) json_append_lit(buffer, buffer_size, &pos, ",");
        json_append_lit(buffer, buffer_size, &pos, "{\"name\":\"");
        json_append_escaped(buffer, buffer_size, &pos, plugins[i].name);
        json_append_lit(buffer, buffer_size, &pos, "\",\"description\":\"");
        json_append_escaped(buffer, buffer_size, &pos, plugins[i].description);
        json_append_lit(buffer, buffer_size, &pos, "\",\"active\":");
        json_append_lit(buffer, buffer_size, &pos,
                        (i == active_plugin_index) ? "true" : "false");
        json_append_lit(buffer, buffer_size, &pos, "}");
    }
    json_append_lit(buffer, buffer_size, &pos, "],\"active_plugin\":\"");
    if (active_plugin_index >= 0 && active_plugin_index < plugin_count) {
        json_append_escaped(buffer, buffer_size, &pos,
                            plugins[active_plugin_index].name);
    }
    json_append_lit(buffer, buffer_size, &pos, "\"}");

    pthread_mutex_unlock(&plugins_mutex);
    return (int)pos;
}

/* Capture the active plugin's table entry under the registry mutex.
 *
 * Dispatch happens on the daemon event thread while activation
 * (plugins_activate) can run concurrently on the web-server thread, so
 * reading active_plugin_index without the lock is a data race. We snapshot
 * the pointer here under the lock and let the caller invoke the handler
 * *outside* the lock: the plugins[] table is a fixed static array whose
 * entries are never moved or unregistered, so the returned pointer stays
 * valid, and dispatching unlocked avoids holding the mutex across a plugin
 * callback (which may re-enter the registry and would otherwise deadlock).
 * Returns NULL when no plugin is active. */
static plugin_t *plugins_snapshot_active(void) {
    plugin_t *active = NULL;
    pthread_mutex_lock(&plugins_mutex);
    if (active_plugin_index >= 0 && active_plugin_index < plugin_count) {
        active = &plugins[active_plugin_index];
    }
    pthread_mutex_unlock(&plugins_mutex);
    return active;
}

int plugins_handle_coin(int coin_value, const char *coin_code) {
    plugin_t *active = plugins_snapshot_active();
    if (active && active->handle_coin) {
        return active->handle_coin(coin_value, coin_code);
    }
    return -1; /* No active plugin or handler */
}

int plugins_handle_keypad(char key) {
    plugin_t *active = plugins_snapshot_active();
    if (active && active->handle_keypad) {
        return active->handle_keypad(key);
    }
    return -1; /* No active plugin or handler */
}

int plugins_handle_hook(int hook_up, int hook_down) {
    plugin_t *active = plugins_snapshot_active();
    if (active && active->handle_hook) {
        return active->handle_hook(hook_up, hook_down);
    }
    return -1; /* No active plugin or handler */
}

int plugins_handle_call_state(int call_state) {
    plugin_t *active = plugins_snapshot_active();
    if (active && active->handle_call_state) {
        return active->handle_call_state(call_state);
    }
    return -1; /* No active plugin or handler */
}

int plugins_handle_card(const char *card_number) {
    plugin_t *active = plugins_snapshot_active();
    if (active && active->handle_card) {
        return active->handle_card(card_number);
    }
    return -1;
}

void plugins_tick(void) {
    plugin_t *active = plugins_snapshot_active();
    if (active && active->handle_tick) {
        active->handle_tick();
    }
}
