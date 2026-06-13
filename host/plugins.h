#ifndef PLUGINS_H
#define PLUGINS_H

#include "events.h"
#include "daemon_state.h"

/* Plugin event handler function types */
typedef int (*coin_handler_t)(int coin_value, const char *coin_code);
typedef int (*keypad_handler_t)(char key);
typedef int (*hook_handler_t)(int hook_up, int hook_down);
typedef int (*call_state_handler_t)(int call_state);
typedef int (*card_handler_t)(const char *card_number);
typedef void (*activation_handler_t)(void);
typedef void (*tick_handler_t)(void);

/* Plugin structure */
typedef struct {
    const char *name;
    const char *description;
    coin_handler_t handle_coin;
    keypad_handler_t handle_keypad;
    hook_handler_t handle_hook;
    call_state_handler_t handle_call_state;
    card_handler_t handle_card;
    activation_handler_t handle_activation;
    tick_handler_t handle_tick;
} plugin_t;

/* Plugin management functions */
void plugins_init(void);
void plugins_cleanup(void);
int plugins_register(const char *name, const char *description,
                    coin_handler_t coin_handler,
                    keypad_handler_t keypad_handler,
                    hook_handler_t hook_handler,
                    call_state_handler_t call_state_handler,
                    card_handler_t card_handler,
                    activation_handler_t activation_handler,
                    tick_handler_t tick_handler);
int plugins_activate(const char *plugin_name);
const char* plugins_get_active_name(void);
int plugins_list(char *buffer, size_t buffer_size);

/* Dynamic enumeration: lets the web API / dashboard discover plugins at
 * runtime instead of hard-coding them. Returns the number of registered
 * plugins. plugins_get_info() fills the (optional) out-params for the plugin
 * at index [0, count); returns 0 on success, -1 if index is out of range. */
int plugins_get_count(void);
int plugins_get_info(int index, const char **name, const char **description,
                     int *is_active);

/* Render the plugin registry as a JSON object into buffer:
 *   {"plugins":[{"name":..,"description":..,"active":bool},..],
 *    "active_plugin":".."}
 * Strings are JSON-escaped. Returns the number of bytes written (excluding
 * the NUL), or -1 on bad arguments. */
int plugins_to_json(char *buffer, size_t buffer_size);

/* #109: Sync inserted_cents when plugin deducts/refunds (e.g. call cost). */
void plugins_adjust_inserted_cents(int delta);

/* Plugin event handler functions */
int plugins_handle_coin(int coin_value, const char *coin_code);
int plugins_handle_keypad(char key);
int plugins_handle_hook(int hook_up, int hook_down);
int plugins_handle_call_state(int call_state);
int plugins_handle_card(const char *card_number);
void plugins_tick(void);

/* Built-in plugin registration functions */
void register_classic_phone_plugin(void);
void register_fortune_teller_plugin(void);
void register_jukebox_plugin(void);
void register_number_guess_plugin(void);
void register_simon_plugin(void);
void register_dial_a_joke_plugin(void);
void register_trivia_plugin(void);
void register_time_operator_plugin(void);

#endif /* PLUGINS_H */
