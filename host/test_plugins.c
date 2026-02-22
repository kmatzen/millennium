#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

/* Mock the required structures and functions for testing */
typedef struct {
    int inserted_cents;
    char keypad_buffer[11];
    int keypad_length;
} mock_daemon_state_t;

typedef struct {
    int dummy;
} mock_millennium_client_t;

static mock_daemon_state_t *daemon_state = NULL;
static mock_millennium_client_t *client = NULL;

/* Mock logger functions */
void logger_info_with_category(const char *category, const char *message) {
    printf("[%s] %s\n", category, message);
}

void logger_infof_with_category(const char *category, const char *format, ...) {
    printf("[%s] ", category);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}

void logger_error_with_category(const char *category, const char *message) {
    printf("[ERROR %s] %s\n", category, message);
}

void logger_warn_with_category(const char *category, const char *message) {
    printf("[WARN %s] %s\n", category, message);
}

void logger_warnf_with_category(const char *category, const char *format, ...) {
    printf("[WARN %s] ", category);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}

/* Mock millennium client functions */
void millennium_client_set_display(mock_millennium_client_t *client, const char *display_bytes) {
    printf("DISPLAY: %s\n", display_bytes);
}

void millennium_client_make_call(mock_millennium_client_t *client, const char *number) {
    printf("CALLING: %s\n", number);
}

void millennium_client_answer_call(mock_millennium_client_t *client) {
    printf("ANSWERING CALL\n");
}

void millennium_client_hangup(mock_millennium_client_t *client) {
    printf("HANGING UP\n");
}

/* Mock sleep function */
void sleep(int seconds) {
    printf("SLEEP: %d seconds\n", seconds);
}

/* Include the plugin system */
#include "plugins.c"
#include "plugins/classic_phone.c"
#include "plugins/fortune_teller.c"
#include "plugins/jukebox.c"

int main() {
    printf("=== Millennium Plugin System Test ===\n\n");
    
    /* Initialize mock data */
    daemon_state = malloc(sizeof(mock_daemon_state_t));
    daemon_state->inserted_cents = 0;
    daemon_state->keypad_length = 0;
    memset(daemon_state->keypad_buffer, 0, sizeof(daemon_state->keypad_buffer));
    
    client = malloc(sizeof(mock_millennium_client_t));
    
    /* Initialize plugin system */
    plugins_init();
    
    /* List available plugins */
    printf("Available plugins:\n");
    char buffer[1024];
    plugins_list(buffer, sizeof(buffer));
    printf("%s\n", buffer);
    
    /* Test Classic Phone plugin */
    printf("=== Testing Classic Phone Plugin ===\n");
    plugins_activate("Classic Phone");
    printf("Active plugin: %s\n\n", plugins_get_active_name());
    
    printf("Inserting 25 cents...\n");
    plugins_handle_coin(25, "COIN_8");
    
    printf("Dialing number 1234567890...\n");
    for (int i = 0; i < 10; i++) {
        plugins_handle_keypad('0' + (i + 1) % 10);
    }
    
    printf("Lifting handset...\n");
    plugins_handle_hook(1, 0);
    
    printf("Placing handset down...\n");
    plugins_handle_hook(0, 1);
    
    /* Test Fortune Teller plugin */
    printf("\n=== Testing Fortune Teller Plugin ===\n");
    plugins_activate("Fortune Teller");
    printf("Active plugin: %s\n\n", plugins_get_active_name());
    
    printf("Inserting 25 cents...\n");
    plugins_handle_coin(25, "COIN_8");
    
    printf("Selecting Love fortune (key 1)...\n");
    plugins_handle_keypad('1');
    
    /* Test Jukebox plugin */
    printf("\n=== Testing Jukebox Plugin ===\n");
    plugins_activate("Jukebox");
    printf("Active plugin: %s\n\n", plugins_get_active_name());
    
    printf("Inserting 25 cents...\n");
    plugins_handle_coin(25, "COIN_8");
    
    printf("Selecting song 1 (Bohemian Rhapsody)...\n");
    plugins_handle_keypad('1');
    
    printf("Stopping song with * key...\n");
    plugins_handle_keypad('*');
    
    /* Cleanup */
    plugins_cleanup();
    free(daemon_state);
    free(client);
    
    printf("\n=== Test Complete ===\n");
    return 0;
}
