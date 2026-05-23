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

/* Maximum number of plugins. Generous headroom so experimenters can add
 * their own alongside the built-ins (7 ship by default). */
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
    if (plugin_count >= MAX_PLUGINS) {
        logger_error_with_category("Plugins", "Maximum number of plugins reached");
        return -1;
    }
    
    if (!name || !description) {
        logger_error_with_category("Plugins", "Plugin name and description required");
        return -1;
    }
    
    /* Check if plugin already exists */
    int i;
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

int plugins_activate(const char *plugin_name) {
    if (!plugin_name) {
        logger_error_with_category("Plugins", "Plugin name required for activation");
        return -1;
    }
    
    pthread_mutex_lock(&plugins_mutex);
    
    /* Find the plugin */
    int i;
    for (i = 0; i < plugin_count; i++) {
        if (strcmp(plugins[i].name, plugin_name) == 0) {
            active_plugin_index = i;
            
            /* Call the plugin's activation handler if it exists */
            if (plugins[i].handle_activation) {
                plugins[i].handle_activation();
            }
            
            logger_infof_with_category("Plugins", "Plugin %s activated", plugin_name);
            pthread_mutex_unlock(&plugins_mutex);
            return 0;
        }
    }
    
    logger_warnf_with_category("Plugins", "Plugin %s not found", plugin_name);
    pthread_mutex_unlock(&plugins_mutex);
    return -1;
}

const char* plugins_get_active_name(void) {
    pthread_mutex_lock(&plugins_mutex);
    const char *name = NULL;
    if (active_plugin_index >= 0 && active_plugin_index < plugin_count) {
        name = plugins[active_plugin_index].name;
    }
    pthread_mutex_unlock(&plugins_mutex);
    return name;
}

int plugins_list(char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return -1;
    }
    
    pthread_mutex_lock(&plugins_mutex);
    
    buffer[0] = '\0';
    int pos = 0;
    
    int i;
    for (i = 0; i < plugin_count && pos < (int)buffer_size - 1; i++) {
        char temp[256];
        snprintf(temp, sizeof(temp), "%s: %s%s\n",
                plugins[i].name, plugins[i].description,
                (i == active_plugin_index) ? " (ACTIVE)" : "");
        
        int len = strlen(temp);
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

int plugins_handle_coin(int coin_value, const char *coin_code) {
    if (active_plugin_index >= 0 && active_plugin_index < plugin_count) {
        plugin_t *active_plugin = &plugins[active_plugin_index];
        if (active_plugin->handle_coin) {
            return active_plugin->handle_coin(coin_value, coin_code);
        }
    }
    return -1; /* No active plugin or handler */
}

int plugins_handle_keypad(char key) {
    if (active_plugin_index >= 0 && active_plugin_index < plugin_count) {
        plugin_t *active_plugin = &plugins[active_plugin_index];
        if (active_plugin->handle_keypad) {
            return active_plugin->handle_keypad(key);
        }
    }
    return -1; /* No active plugin or handler */
}

int plugins_handle_hook(int hook_up, int hook_down) {
    if (active_plugin_index >= 0 && active_plugin_index < plugin_count) {
        plugin_t *active_plugin = &plugins[active_plugin_index];
        if (active_plugin->handle_hook) {
            return active_plugin->handle_hook(hook_up, hook_down);
        }
    }
    return -1; /* No active plugin or handler */
}

int plugins_handle_call_state(int call_state) {
    if (active_plugin_index >= 0 && active_plugin_index < plugin_count) {
        plugin_t *active_plugin = &plugins[active_plugin_index];
        if (active_plugin->handle_call_state) {
            return active_plugin->handle_call_state(call_state);
        }
    }
    return -1; /* No active plugin or handler */
}

int plugins_handle_card(const char *card_number) {
    if (active_plugin_index >= 0 && active_plugin_index < plugin_count) {
        plugin_t *active_plugin = &plugins[active_plugin_index];
        if (active_plugin->handle_card) {
            return active_plugin->handle_card(card_number);
        }
    }
    return -1;
}

void plugins_tick(void) {
    if (active_plugin_index >= 0 && active_plugin_index < plugin_count) {
        plugin_t *active_plugin = &plugins[active_plugin_index];
        if (active_plugin->handle_tick) {
            active_plugin->handle_tick();
        }
    }
}
