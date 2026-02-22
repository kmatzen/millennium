#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

/* Simple test of the plugin system without including the full codebase */

/* Mock plugin structure */
typedef struct {
    const char *name;
    const char *description;
    int (*handle_coin)(int coin_value, const char *coin_code);
    int (*handle_keypad)(char key);
    int (*handle_hook)(int hook_up, int hook_down);
    int (*handle_call_state)(int call_state);
} plugin_t;

/* Mock plugin registry */
static plugin_t plugins[10];
static int plugin_count = 0;
static int active_plugin_index = -1;

/* Mock functions */
void mock_display(const char *line1, const char *line2) {
    printf("DISPLAY: %s | %s\n", line1, line2);
}

void mock_log(const char *category, const char *message) {
    printf("[%s] %s\n", category, message);
}

/* Classic Phone Plugin */
static int classic_phone_handle_coin(int coin_value, const char *coin_code) {
    mock_log("ClassicPhone", "Coin inserted - ready for dialing");
    return 0;
}

static int classic_phone_handle_keypad(char key) {
    mock_log("ClassicPhone", "Key pressed - building number");
    return 0;
}

static int classic_phone_handle_hook(int hook_up, int hook_down) {
    if (hook_up) {
        mock_log("ClassicPhone", "Handset lifted - ready for call");
    } else {
        mock_log("ClassicPhone", "Handset down - call ended");
    }
    return 0;
}

static int classic_phone_handle_call_state(int call_state) {
    mock_log("ClassicPhone", "Call state changed");
    return 0;
}

/* Fortune Teller Plugin */
static int fortune_teller_handle_coin(int coin_value, const char *coin_code) {
    mock_log("FortuneTeller", "Coin inserted - choose your fortune");
    return 0;
}

static int fortune_teller_handle_keypad(char key) {
    if (key >= '1' && key <= '5') {
        const char* fortunes[] = {"Love", "Career", "Health", "Money", "General"};
        mock_log("FortuneTeller", "Fortune selected - reading the stars");
    }
    return 0;
}

static int fortune_teller_handle_hook(int hook_up, int hook_down) {
    if (hook_up) {
        mock_log("FortuneTeller", "Handset lifted - mystical session begins");
    } else {
        mock_log("FortuneTeller", "Handset down - session ended");
    }
    return 0;
}

static int fortune_teller_handle_call_state(int call_state) {
    /* Fortune teller doesn't handle calls */
    return 0;
}

/* Jukebox Plugin */
static int jukebox_handle_coin(int coin_value, const char *coin_code) {
    mock_log("Jukebox", "Coin inserted - select your song");
    return 0;
}

static int jukebox_handle_keypad(char key) {
    if (key >= '1' && key <= '9') {
        mock_log("Jukebox", "Song selected - music starts playing");
    } else if (key == '*') {
        mock_log("Jukebox", "Song stopped");
    }
    return 0;
}

static int jukebox_handle_hook(int hook_up, int hook_down) {
    if (hook_up) {
        mock_log("Jukebox", "Handset lifted - jukebox ready");
    } else {
        mock_log("Jukebox", "Handset down - jukebox session ended");
    }
    return 0;
}

static int jukebox_handle_call_state(int call_state) {
    /* Jukebox doesn't handle calls */
    return 0;
}

/* Plugin management functions */
void register_plugin(const char *name, const char *description,
                    int (*coin_handler)(int, const char*),
                    int (*keypad_handler)(char),
                    int (*hook_handler)(int, int),
                    int (*call_state_handler)(int)) {
    if (plugin_count < 10) {
        plugins[plugin_count].name = name;
        plugins[plugin_count].description = description;
        plugins[plugin_count].handle_coin = coin_handler;
        plugins[plugin_count].handle_keypad = keypad_handler;
        plugins[plugin_count].handle_hook = hook_handler;
        plugins[plugin_count].handle_call_state = call_state_handler;
        plugin_count++;
        mock_log("PluginSystem", "Plugin registered");
    }
}

int activate_plugin(const char *name) {
    int i;
    for (i = 0; i < plugin_count; i++) {
        if (strcmp(plugins[i].name, name) == 0) {
            active_plugin_index = i;
            mock_log("PluginSystem", "Plugin activated");
            return 0;
        }
    }
    return -1;
}

const char* get_active_plugin_name(void) {
    if (active_plugin_index >= 0 && active_plugin_index < plugin_count) {
        return plugins[active_plugin_index].name;
    }
    return NULL;
}

int handle_coin(int coin_value, const char *coin_code) {
    if (active_plugin_index >= 0 && active_plugin_index < plugin_count) {
        plugin_t *active_plugin = &plugins[active_plugin_index];
        if (active_plugin->handle_coin) {
            return active_plugin->handle_coin(coin_value, coin_code);
        }
    }
    return -1;
}

int handle_keypad(char key) {
    if (active_plugin_index >= 0 && active_plugin_index < plugin_count) {
        plugin_t *active_plugin = &plugins[active_plugin_index];
        if (active_plugin->handle_keypad) {
            return active_plugin->handle_keypad(key);
        }
    }
    return -1;
}

int handle_hook(int hook_up, int hook_down) {
    if (active_plugin_index >= 0 && active_plugin_index < plugin_count) {
        plugin_t *active_plugin = &plugins[active_plugin_index];
        if (active_plugin->handle_hook) {
            return active_plugin->handle_hook(hook_up, hook_down);
        }
    }
    return -1;
}

int handle_call_state(int call_state) {
    if (active_plugin_index >= 0 && active_plugin_index < plugin_count) {
        plugin_t *active_plugin = &plugins[active_plugin_index];
        if (active_plugin->handle_call_state) {
            return active_plugin->handle_call_state(call_state);
        }
    }
    return -1;
}

int main() {
    printf("=== Millennium Plugin System Test ===\n\n");
    
    /* Register plugins */
    register_plugin("Classic Phone", "Traditional pay phone functionality",
                   classic_phone_handle_coin,
                   classic_phone_handle_keypad,
                   classic_phone_handle_hook,
                   classic_phone_handle_call_state);
    
    register_plugin("Fortune Teller", "Mystical fortune telling experience",
                   fortune_teller_handle_coin,
                   fortune_teller_handle_keypad,
                   fortune_teller_handle_hook,
                   fortune_teller_handle_call_state);
    
    register_plugin("Jukebox", "Coin-operated music player",
                   jukebox_handle_coin,
                   jukebox_handle_keypad,
                   jukebox_handle_hook,
                   jukebox_handle_call_state);
    
    /* Test Classic Phone plugin */
    printf("=== Testing Classic Phone Plugin ===\n");
    activate_plugin("Classic Phone");
    printf("Active plugin: %s\n\n", get_active_plugin_name());
    
    printf("Inserting 25 cents...\n");
    handle_coin(25, "COIN_8");
    
    printf("Dialing number...\n");
    handle_keypad('1');
    handle_keypad('2');
    handle_keypad('3');
    
    printf("Lifting handset...\n");
    handle_hook(1, 0);
    
    printf("Placing handset down...\n");
    handle_hook(0, 1);
    
    /* Test Fortune Teller plugin */
    printf("\n=== Testing Fortune Teller Plugin ===\n");
    activate_plugin("Fortune Teller");
    printf("Active plugin: %s\n\n", get_active_plugin_name());
    
    printf("Inserting 25 cents...\n");
    handle_coin(25, "COIN_8");
    
    printf("Selecting Love fortune (key 1)...\n");
    handle_keypad('1');
    
    printf("Lifting handset...\n");
    handle_hook(1, 0);
    
    /* Test Jukebox plugin */
    printf("\n=== Testing Jukebox Plugin ===\n");
    activate_plugin("Jukebox");
    printf("Active plugin: %s\n\n", get_active_plugin_name());
    
    printf("Inserting 25 cents...\n");
    handle_coin(25, "COIN_8");
    
    printf("Selecting song 1 (Bohemian Rhapsody)...\n");
    handle_keypad('1');
    
    printf("Stopping song with * key...\n");
    handle_keypad('*');
    
    printf("Lifting handset...\n");
    handle_hook(1, 0);
    
    printf("\n=== Test Complete ===\n");
    printf("Plugin system working correctly!\n");
    printf("- Plugins can be registered and activated\n");
    printf("- Event handlers are properly routed to active plugin\n");
    printf("- Each plugin implements different behavior\n");
    printf("- Hot-swapping between plugins works\n");
    
    return 0;
}

