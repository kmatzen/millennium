#define _POSIX_C_SOURCE 200112L
#include "test_framework.h"
#include "../config.h"
#include "../daemon_state.h"
#include "../plugins.h"
#include "../logger.h"
#include "../metrics.h"
#include "../millennium_sdk.h"
#include <stdlib.h>

/* ── Stubs for linker (plugins.c references these) ──────────────── */

daemon_state_data_t *daemon_state = NULL;
millennium_client_t *client = NULL;

millennium_client_t *millennium_client_create(void) {
    millennium_client_t *c = calloc(1, sizeof(millennium_client_t));
    return c;
}
void millennium_client_destroy(millennium_client_t *c) { free(c); }
void millennium_client_close(millennium_client_t *c) { (void)c; }
void millennium_client_set_display(millennium_client_t *c, const char *m) { (void)c; (void)m; }
void millennium_client_call(millennium_client_t *c, const char *n) { (void)c; (void)n; }
void millennium_client_answer_call(millennium_client_t *c) { (void)c; }
void millennium_client_hangup(millennium_client_t *c) { (void)c; }
void millennium_client_update(millennium_client_t *c) { (void)c; }
void *millennium_client_next_event(millennium_client_t *c) { (void)c; return NULL; }
void millennium_client_write_to_display(millennium_client_t *c, const char *m) { (void)c; (void)m; }
void millennium_client_write_to_coin_validator(millennium_client_t *c, uint8_t d) { (void)c; (void)d; }
void millennium_client_write_command(millennium_client_t *c, uint8_t cmd, const uint8_t *d, size_t sz) {
    (void)c; (void)cmd; (void)d; (void)sz;
}
void millennium_client_set_ua(millennium_client_t *c, void *ua) { (void)c; (void)ua; }
void millennium_client_process_event_buffer(millennium_client_t *c) { (void)c; }
char *millennium_client_extract_payload(millennium_client_t *c, char t, size_t s) { (void)c; (void)t; (void)s; return NULL; }
void millennium_client_create_and_queue_event_char(millennium_client_t *c, char t, const char *p) { (void)c; (void)t; (void)p; }
void millennium_client_create_and_queue_event_ptr(millennium_client_t *c, void *e) { (void)c; (void)e; }
int millennium_client_serial_is_healthy(millennium_client_t *c) { (void)c; return 1; }
void millennium_client_check_serial(millennium_client_t *c) { (void)c; }
void millennium_client_serial_activity(millennium_client_t *c) { (void)c; }
void list_audio_devices(void) {}

/* Audio tone stubs */
void audio_tones_init(void) {}
void audio_tones_cleanup(void) {}
void audio_tones_play_dial_tone(void) {}
void audio_tones_play_dtmf(char k) { (void)k; }
void audio_tones_play_ringback(void) {}
void audio_tones_play_busy_tone(void) {}
void audio_tones_play_coin_tone(void) {}
void audio_tones_stop(void) {}
int audio_tones_is_playing(void) { return 0; }

/* ── Config tests ───────────────────────────────────────────────── */

static void test_config_defaults(void) {
    config_data_t cfg;
    cfg.count = 0;
    config_set_default_values(&cfg);

    TEST_ASSERT_EQ_INT(config_get_call_cost_cents(&cfg), 50);
    TEST_ASSERT_EQ_INT(config_get_call_timeout_seconds(&cfg), 300);
    TEST_ASSERT_EQ_INT(config_get_baud_rate(&cfg), 9600);
    TEST_ASSERT_EQ_INT(config_get_update_interval_ms(&cfg), 33);
    TEST_ASSERT_EQ_INT(config_get_max_retries(&cfg), 3);
    TEST_ASSERT_EQ_STR(config_get_log_level(&cfg), "INFO");
    TEST_ASSERT_EQ_INT(config_get_log_to_file(&cfg), 0);
}

static void test_config_set_and_get(void) {
    config_data_t cfg;
    cfg.count = 0;
    config_set_default_values(&cfg);

    config_set_value(&cfg, "call.cost_cents", "75");
    TEST_ASSERT_EQ_INT(config_get_call_cost_cents(&cfg), 75);

    config_set_value(&cfg, "logging.level", "DEBUG");
    TEST_ASSERT_EQ_STR(config_get_log_level(&cfg), "DEBUG");
}

static void test_config_get_string_default(void) {
    config_data_t cfg;
    cfg.count = 0;

    TEST_ASSERT_EQ_STR(config_get_string(&cfg, "nonexistent", "fallback"), "fallback");
}

static void test_config_get_int_default(void) {
    config_data_t cfg;
    cfg.count = 0;

    TEST_ASSERT_EQ_INT(config_get_int(&cfg, "nonexistent", 42), 42);
}

static void test_config_get_bool_variants(void) {
    config_data_t cfg;
    cfg.count = 0;

    config_set_value(&cfg, "a", "true");
    config_set_value(&cfg, "b", "1");
    config_set_value(&cfg, "c", "yes");
    config_set_value(&cfg, "d", "false");
    config_set_value(&cfg, "e", "0");

    TEST_ASSERT_EQ_INT(config_get_bool(&cfg, "a", 0), 1);
    TEST_ASSERT_EQ_INT(config_get_bool(&cfg, "b", 0), 1);
    TEST_ASSERT_EQ_INT(config_get_bool(&cfg, "c", 0), 1);
    TEST_ASSERT_EQ_INT(config_get_bool(&cfg, "d", 1), 0);
    TEST_ASSERT_EQ_INT(config_get_bool(&cfg, "e", 1), 0);
}

static void test_config_validate_good(void) {
    config_data_t cfg;
    cfg.count = 0;
    config_set_default_values(&cfg);

    TEST_ASSERT_EQ_INT(config_validate(&cfg), 1);
}

static void test_config_validate_bad_cost(void) {
    config_data_t cfg;
    cfg.count = 0;
    config_set_default_values(&cfg);

    config_set_value(&cfg, "call.cost_cents", "0");
    TEST_ASSERT_EQ_INT(config_validate(&cfg), 0);
}

static void test_config_trim(void) {
    char result[64];

    config_trim("  hello  ", result, sizeof(result));
    TEST_ASSERT_EQ_STR(result, "hello");

    config_trim("no_trim", result, sizeof(result));
    TEST_ASSERT_EQ_STR(result, "no_trim");

    config_trim("   ", result, sizeof(result));
    TEST_ASSERT_EQ_STR(result, "");
}

static void test_config_null_safety(void) {
    TEST_ASSERT_EQ_STR(config_get_string(NULL, "key", "safe"), "safe");
    TEST_ASSERT_EQ_INT(config_get_int(NULL, "key", 99), 99);
    TEST_ASSERT_EQ_INT(config_get_bool(NULL, "key", 1), 1);
    TEST_ASSERT_EQ_INT(config_set_value(NULL, "k", "v"), 0);
}

static void test_config_overwrite_value(void) {
    config_data_t cfg;
    cfg.count = 0;

    config_set_value(&cfg, "key", "original");
    TEST_ASSERT_EQ_STR(config_get_string(&cfg, "key", ""), "original");

    config_set_value(&cfg, "key", "updated");
    TEST_ASSERT_EQ_STR(config_get_string(&cfg, "key", ""), "updated");
}

static void test_config_load_from_file(void) {
    FILE *f;
    config_data_t cfg;
    cfg.count = 0;

    f = fopen("/tmp/millennium_test.cfg", "w");
    TEST_ASSERT_NOT_NULL(f);
    fprintf(f, "# comment\n");
    fprintf(f, "call.cost_cents = 25\n");
    fprintf(f, "logging.level = DEBUG\n");
    fprintf(f, "\n");
    fprintf(f, "call.timeout_seconds=120\n");
    fclose(f);

    TEST_ASSERT_EQ_INT(config_load_from_file(&cfg, "/tmp/millennium_test.cfg"), 1);
    TEST_ASSERT_EQ_INT(config_get_call_cost_cents(&cfg), 25);
    TEST_ASSERT_EQ_STR(config_get_log_level(&cfg), "DEBUG");
    TEST_ASSERT_EQ_INT(config_get_call_timeout_seconds(&cfg), 120);

    remove("/tmp/millennium_test.cfg");
}

/* ── Daemon state tests ─────────────────────────────────────────── */

static void test_state_init(void) {
    daemon_state_data_t state;
    daemon_state_init(&state);

    TEST_ASSERT_EQ_INT((int)state.current_state, (int)DAEMON_STATE_IDLE_DOWN);
    TEST_ASSERT_EQ_INT(state.inserted_cents, 0);
    TEST_ASSERT_EQ_INT(daemon_state_get_keypad_length(&state), 0);
}

static void test_state_to_string(void) {
    TEST_ASSERT_EQ_STR(daemon_state_to_string(DAEMON_STATE_IDLE_DOWN), "IDLE_DOWN");
    TEST_ASSERT_EQ_STR(daemon_state_to_string(DAEMON_STATE_IDLE_UP), "IDLE_UP");
    TEST_ASSERT_EQ_STR(daemon_state_to_string(DAEMON_STATE_CALL_ACTIVE), "CALL_ACTIVE");
    TEST_ASSERT_EQ_STR(daemon_state_to_string(DAEMON_STATE_INVALID), "INVALID");
}

static void test_state_keypad_add(void) {
    daemon_state_data_t state;
    daemon_state_init(&state);

    daemon_state_add_key(&state, '1');
    daemon_state_add_key(&state, '2');
    daemon_state_add_key(&state, '3');

    TEST_ASSERT_EQ_INT(daemon_state_get_keypad_length(&state), 3);
    TEST_ASSERT_EQ_STR(state.keypad_buffer, "123");
}

static void test_state_keypad_max_digits(void) {
    daemon_state_data_t state;
    int i;
    daemon_state_init(&state);

    for (i = 0; i < 15; i++) {
        daemon_state_add_key(&state, '5');
    }

    TEST_ASSERT_EQ_INT(daemon_state_get_keypad_length(&state), 10);
}

static void test_state_keypad_remove(void) {
    daemon_state_data_t state;
    daemon_state_init(&state);

    daemon_state_add_key(&state, '1');
    daemon_state_add_key(&state, '2');
    daemon_state_remove_last_key(&state);

    TEST_ASSERT_EQ_INT(daemon_state_get_keypad_length(&state), 1);
    TEST_ASSERT_EQ_STR(state.keypad_buffer, "1");
}

static void test_state_keypad_clear(void) {
    daemon_state_data_t state;
    daemon_state_init(&state);

    daemon_state_add_key(&state, '9');
    daemon_state_clear_keypad(&state);

    TEST_ASSERT_EQ_INT(daemon_state_get_keypad_length(&state), 0);
}

static void test_state_keypad_rejects_non_digit(void) {
    daemon_state_data_t state;
    daemon_state_init(&state);

    daemon_state_add_key(&state, 'A');
    daemon_state_add_key(&state, '#');
    daemon_state_add_key(&state, ' ');

    TEST_ASSERT_EQ_INT(daemon_state_get_keypad_length(&state), 0);
}

static void test_state_reset(void) {
    daemon_state_data_t state;
    daemon_state_init(&state);

    state.current_state = DAEMON_STATE_CALL_ACTIVE;
    state.inserted_cents = 100;
    daemon_state_add_key(&state, '5');

    daemon_state_reset(&state);

    TEST_ASSERT_EQ_INT((int)state.current_state, (int)DAEMON_STATE_IDLE_DOWN);
    TEST_ASSERT_EQ_INT(state.inserted_cents, 0);
    TEST_ASSERT_EQ_INT(daemon_state_get_keypad_length(&state), 0);
}

static void test_state_null_safety(void) {
    daemon_state_init(NULL);
    daemon_state_reset(NULL);
    daemon_state_add_key(NULL, '1');
    daemon_state_remove_last_key(NULL);
    daemon_state_clear_keypad(NULL);
    TEST_ASSERT_EQ_INT(daemon_state_get_keypad_length(NULL), 0);
    /* If we get here without crashing, null safety is working */
    TEST_ASSERT(1);
}

/* ── Plugin tests ───────────────────────────────────────────────── */

static int test_coin_handler(int v, const char *c) { (void)v; (void)c; return 0; }
static int test_key_handler(char k) { (void)k; return 0; }
static int test_hook_handler(int u, int d) { (void)u; (void)d; return 0; }
static int test_call_handler(int s) { (void)s; return 0; }
static int g_activated = 0;
static void test_activation_handler(void) { g_activated = 1; }
static void test_tick_handler(void) {}

static void test_plugins_register_and_activate(void) {
    daemon_state_data_t ds;
    daemon_state_init(&ds);
    daemon_state = &ds;
    client = millennium_client_create();

    plugins_init();

    TEST_ASSERT_EQ_STR(plugins_get_active_name(), "Classic Phone");

    TEST_ASSERT_EQ_INT(plugins_activate("Fortune Teller"), 0);
    TEST_ASSERT_EQ_STR(plugins_get_active_name(), "Fortune Teller");

    millennium_client_destroy(client);
    client = NULL;
    daemon_state = NULL;
    plugins_cleanup();
}

static void test_plugins_activate_nonexistent(void) {
    daemon_state_data_t ds;
    daemon_state_init(&ds);
    daemon_state = &ds;
    client = millennium_client_create();

    plugins_init();

    TEST_ASSERT_EQ_INT(plugins_activate("Nonexistent"), -1);
    TEST_ASSERT_EQ_STR(plugins_get_active_name(), "Classic Phone");

    millennium_client_destroy(client);
    client = NULL;
    daemon_state = NULL;
    plugins_cleanup();
}

static void test_plugins_register_custom(void) {
    daemon_state_data_t ds;
    daemon_state_init(&ds);
    daemon_state = &ds;
    client = millennium_client_create();

    plugins_init();
    g_activated = 0;

    TEST_ASSERT_EQ_INT(plugins_register("Test Plugin", "A test",
        test_coin_handler, test_key_handler, test_hook_handler,
        test_call_handler, test_activation_handler, test_tick_handler), 0);

    TEST_ASSERT_EQ_INT(plugins_activate("Test Plugin"), 0);
    TEST_ASSERT_EQ_INT(g_activated, 1);
    TEST_ASSERT_EQ_STR(plugins_get_active_name(), "Test Plugin");

    millennium_client_destroy(client);
    client = NULL;
    daemon_state = NULL;
    plugins_cleanup();
}

static void test_plugins_list(void) {
    daemon_state_data_t ds;
    char buf[1024];
    daemon_state_init(&ds);
    daemon_state = &ds;
    client = millennium_client_create();

    plugins_init();

    TEST_ASSERT_EQ_INT(plugins_list(buf, sizeof(buf)), 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "Classic Phone"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Fortune Teller"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "(ACTIVE)"));

    millennium_client_destroy(client);
    client = NULL;
    daemon_state = NULL;
    plugins_cleanup();
}

static void test_plugins_duplicate_register(void) {
    daemon_state_data_t ds;
    daemon_state_init(&ds);
    daemon_state = &ds;
    client = millennium_client_create();

    plugins_init();

    TEST_ASSERT_EQ_INT(plugins_register("Classic Phone", "Duplicate",
        NULL, NULL, NULL, NULL, NULL, NULL), -1);

    millennium_client_destroy(client);
    client = NULL;
    daemon_state = NULL;
    plugins_cleanup();
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void) {
    logger_set_level(LOG_LEVEL_ERROR);

    TEST_SUITE_BEGIN("Config");
    TEST_SUITE_RUN(test_config_defaults);
    TEST_SUITE_RUN(test_config_set_and_get);
    TEST_SUITE_RUN(test_config_get_string_default);
    TEST_SUITE_RUN(test_config_get_int_default);
    TEST_SUITE_RUN(test_config_get_bool_variants);
    TEST_SUITE_RUN(test_config_validate_good);
    TEST_SUITE_RUN(test_config_validate_bad_cost);
    TEST_SUITE_RUN(test_config_trim);
    TEST_SUITE_RUN(test_config_null_safety);
    TEST_SUITE_RUN(test_config_overwrite_value);
    TEST_SUITE_RUN(test_config_load_from_file);

    TEST_SUITE_BEGIN("Daemon State");
    TEST_SUITE_RUN(test_state_init);
    TEST_SUITE_RUN(test_state_to_string);
    TEST_SUITE_RUN(test_state_keypad_add);
    TEST_SUITE_RUN(test_state_keypad_max_digits);
    TEST_SUITE_RUN(test_state_keypad_remove);
    TEST_SUITE_RUN(test_state_keypad_clear);
    TEST_SUITE_RUN(test_state_keypad_rejects_non_digit);
    TEST_SUITE_RUN(test_state_reset);
    TEST_SUITE_RUN(test_state_null_safety);

    TEST_SUITE_BEGIN("Plugins");
    TEST_SUITE_RUN(test_plugins_register_and_activate);
    TEST_SUITE_RUN(test_plugins_activate_nonexistent);
    TEST_SUITE_RUN(test_plugins_register_custom);
    TEST_SUITE_RUN(test_plugins_list);
    TEST_SUITE_RUN(test_plugins_duplicate_register);

    TEST_REPORT();
}
