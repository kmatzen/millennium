#define _POSIX_C_SOURCE 200112L
#include "test_framework.h"
#include "../config.h"
#include "../cli.h"
#include "../daemon_state.h"
#include "../plugins.h"
#include "../logger.h"
#include "../metrics.h"
#include "../call_metrics.h"
#include "../millennium_sdk.h"
#include "../updater.h"
#include "../plugin_sdk.h"
#include "../clock_source.h"
#include "../state_persistence.h"
#include "../conn_queue.h"
#include "../health_monitor.h"
#include "../display_manager.h"
#include "../wav.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>

/* ── Stubs for linker (plugins.c references these) ──────────────── */

daemon_state_data_t *daemon_state = NULL;
millennium_client_t *client = NULL;

millennium_client_t *millennium_client_create(void) {
    millennium_client_t *c = calloc(1, sizeof(millennium_client_t));
    return c;
}
void millennium_client_destroy(millennium_client_t *c) { free(c); }
void millennium_client_close(millennium_client_t *c) { (void)c; }
/* Capture the most recent display payload so display_manager tests can assert
 * on what would actually be shown on the VFD. */
static char g_last_display[128];
void millennium_client_set_display(millennium_client_t *c, const char *m) {
    (void)c;
    if (m) {
        strncpy(g_last_display, m, sizeof(g_last_display) - 1);
        g_last_display[sizeof(g_last_display) - 1] = '\0';
    }
}
static int display_shows(const char *needle) {
    return strstr(g_last_display, needle) != NULL;
}
void millennium_client_call(millennium_client_t *c, const char *n) { (void)c; (void)n; }
void millennium_client_answer_call(millennium_client_t *c) { (void)c; }
void millennium_client_hangup(millennium_client_t *c) { (void)c; }
int millennium_client_send_dtmf(millennium_client_t *c, char key) { (void)c; (void)key; return 0; }
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

/* #109: Keep daemon_state in sync when plugin deducts/refunds */
void plugins_adjust_inserted_cents(int delta) {
    if (daemon_state) {
        daemon_state->inserted_cents += delta;
        if (daemon_state->inserted_cents < 0) daemon_state->inserted_cents = 0;
    }
}

/* SIP status stub (classic_phone calls this for paid-call pre-check) */
void millennium_sdk_get_sip_status(int *registered, char *last_error, size_t last_error_size) {
    if (registered) *registered = 1;
    if (last_error && last_error_size > 0) last_error[0] = '\0';
    (void)last_error_size;
}

/* Audio tone stubs */
void audio_tones_init(void) {}
void audio_tones_cleanup(void) {}
void audio_tones_play_dial_tone(void) {}
void audio_tones_play_dtmf(char k) { (void)k; }
void audio_tones_play_ringback(void) {}
void audio_tones_play_busy_tone(void) {}
void audio_tones_play_coin_tone(void) {}
void audio_tones_play_clip(const char *p) { (void)p; }
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

/* config_validate_ex fills a descriptive reason on failure and clears it on
   success, so the operator learns which setting is wrong. */
static void test_config_validate_ex_reports_reason(void) {
    config_data_t cfg;
    char err[256];

    cfg.count = 0;
    config_set_default_values(&cfg);
    err[0] = 'x';
    TEST_ASSERT_EQ_INT(config_validate_ex(&cfg, err, sizeof(err)), 1);
    TEST_ASSERT_EQ_STR(err, "");

    config_set_value(&cfg, "call.cost_cents", "-5");
    TEST_ASSERT_EQ_INT(config_validate_ex(&cfg, err, sizeof(err)), 0);
    TEST_ASSERT_NOT_NULL(strstr(err, "call.cost_cents"));
}

/* A NULL error buffer must be tolerated (config_validate() relies on this). */
static void test_config_validate_ex_null_buffer(void) {
    config_data_t cfg;
    cfg.count = 0;
    config_set_default_values(&cfg);
    TEST_ASSERT_EQ_INT(config_validate_ex(&cfg, NULL, 0), 1);

    config_set_value(&cfg, "call.cost_cents", "0");
    TEST_ASSERT_EQ_INT(config_validate_ex(&cfg, NULL, 0), 0);
}

/* Out-of-range / nonsensical numeric settings are rejected. */
static void test_config_validate_ranges(void) {
    config_data_t cfg;

    cfg.count = 0;
    config_set_default_values(&cfg);
    config_set_value(&cfg, "call.timeout_seconds", "0");
    TEST_ASSERT_EQ_INT(config_validate(&cfg), 0);

    cfg.count = 0;
    config_set_default_values(&cfg);
    config_set_value(&cfg, "call.idle_timeout_seconds", "-1");
    TEST_ASSERT_EQ_INT(config_validate(&cfg), 0);

    cfg.count = 0;
    config_set_default_values(&cfg);
    config_set_value(&cfg, "hardware.baud_rate", "0");
    TEST_ASSERT_EQ_INT(config_validate(&cfg), 0);

    cfg.count = 0;
    config_set_default_values(&cfg);
    config_set_value(&cfg, "logging.max_files", "0");
    TEST_ASSERT_EQ_INT(config_validate(&cfg), 0);
}

/* Web and metrics ports must be in range. */
static void test_config_validate_ports(void) {
    config_data_t cfg;

    cfg.count = 0;
    config_set_default_values(&cfg);
    config_set_value(&cfg, "web_server.port", "99999");
    TEST_ASSERT_EQ_INT(config_validate(&cfg), 0);

    cfg.count = 0;
    config_set_default_values(&cfg);
    config_set_value(&cfg, "metrics_server.port", "0");
    TEST_ASSERT_EQ_INT(config_validate(&cfg), 0);

    cfg.count = 0;
    config_set_default_values(&cfg);
    config_set_value(&cfg, "web_server.port", "8080");
    TEST_ASSERT_EQ_INT(config_validate(&cfg), 1);
}

/* Both servers enabled on the same port is a misconfiguration. */
static void test_config_validate_port_conflict(void) {
    config_data_t cfg;
    cfg.count = 0;
    config_set_default_values(&cfg);

    config_set_value(&cfg, "web_server.enabled", "true");
    config_set_value(&cfg, "metrics_server.enabled", "true");
    config_set_value(&cfg, "web_server.port", "9000");
    config_set_value(&cfg, "metrics_server.port", "9000");
    TEST_ASSERT_EQ_INT(config_validate(&cfg), 0);

    /* Same port but metrics disabled: no conflict. */
    config_set_value(&cfg, "metrics_server.enabled", "false");
    TEST_ASSERT_EQ_INT(config_validate(&cfg), 1);
}

/* Logging level and SIP transport accept only known tokens. */
static void test_config_validate_enums(void) {
    config_data_t cfg;

    cfg.count = 0;
    config_set_default_values(&cfg);
    config_set_value(&cfg, "logging.level", "INF");
    TEST_ASSERT_EQ_INT(config_validate(&cfg), 0);

    cfg.count = 0;
    config_set_default_values(&cfg);
    config_set_value(&cfg, "logging.level", "warn"); /* case-insensitive */
    TEST_ASSERT_EQ_INT(config_validate(&cfg), 1);

    cfg.count = 0;
    config_set_default_values(&cfg);
    config_set_value(&cfg, "sip.transport", "sctp");
    TEST_ASSERT_EQ_INT(config_validate(&cfg), 0);

    cfg.count = 0;
    config_set_default_values(&cfg);
    config_set_value(&cfg, "sip.transport", "TLS"); /* case-insensitive */
    TEST_ASSERT_EQ_INT(config_validate(&cfg), 1);

    /* Unset sip.transport must not fail validation. */
    cfg.count = 0;
    config_set_default_values(&cfg);
    TEST_ASSERT_EQ_INT(config_validate(&cfg), 1);
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

/* ── Display manager tests ──────────────────────────────────────── */

static void test_display_short_line_static(void) {
    millennium_client_t *c = millennium_client_create();
    int i;
    display_manager_init(c);

    /* A line that fits never scrolls, even after many ticks. */
    display_manager_set_text("Short", "Also short");
    for (i = 0; i < 20; i++) display_manager_tick();
    TEST_ASSERT_EQ_INT(display_shows("Short"), 1);
    TEST_ASSERT_EQ_INT(display_shows("Also short"), 1);

    millennium_client_destroy(c);
}

static void test_display_scroll_holds_at_start(void) {
    millennium_client_t *c = millennium_client_create();
    int i;
    display_manager_init(c);

    /* A line longer than the 20-char display starts held at the beginning. */
    display_manager_set_text("Header", "This is a very long fortune message");
    TEST_ASSERT_EQ_INT(display_shows("This is a very long"), 1);

    /* During the hold window the start stays in view; it does not scroll. */
    for (i = 0; i < DISPLAY_SCROLL_HOLD_TICKS - 1; i++) display_manager_tick();
    TEST_ASSERT_EQ_INT(display_shows("This is a very long"), 1);

    /* The tick that spends the last hold doesn't move; the next one scrolls. */
    display_manager_tick();  /* hold 1 -> 0 */
    display_manager_tick();  /* first real scroll step */
    TEST_ASSERT_EQ_INT(display_shows("his is a very long"), 1);

    millennium_client_destroy(c);
}

static void test_display_scroll_rearms_hold_on_wrap(void) {
    millennium_client_t *c = millennium_client_create();
    int i;
    /* 21-char line: one column too wide, so its scroll period is short. */
    const char *line = "123456789012345678901";
    int period = (int)strlen(line) + DISPLAY_SCROLL_GAP;
    display_manager_init(c);

    display_manager_set_text(line, NULL);
    /* Spend the initial hold, then a full lap back to column 0, which re-arms
     * the hold so the start is readable again on the next loop. */
    for (i = 0; i < DISPLAY_SCROLL_HOLD_TICKS + period; i++) display_manager_tick();
    TEST_ASSERT_EQ_INT(display_shows("12345678901234567890"), 1);

    /* Re-armed: this tick holds rather than advancing past the start. */
    display_manager_tick();
    TEST_ASSERT_EQ_INT(display_shows("12345678901234567890"), 1);

    millennium_client_destroy(c);
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

static void test_plugins_activation_metrics(void) {
    daemon_state_data_t ds;
    daemon_state_init(&ds);
    daemon_state = &ds;
    client = millennium_client_create();

    /* Start from a clean metrics slate so counts are deterministic even if an
     * earlier test already created the metrics instance. */
    metrics_init();
    metrics_reset_all();

    /* plugins_init activates the default "Classic Phone" once. */
    plugins_init();
    TEST_ASSERT_EQ_INT((int)metrics_get_counter("plugin_activations_total"), 1);
    TEST_ASSERT_EQ_INT(
        (int)metrics_get_counter("plugin_activations_Classic Phone"), 1);

    /* Each activation bumps the aggregate and the per-plugin counter. */
    TEST_ASSERT_EQ_INT(plugins_activate("Fortune Teller"), 0);
    TEST_ASSERT_EQ_INT(plugins_activate("Fortune Teller"), 0);
    TEST_ASSERT_EQ_INT((int)metrics_get_counter("plugin_activations_total"), 3);
    TEST_ASSERT_EQ_INT(
        (int)metrics_get_counter("plugin_activations_Fortune Teller"), 2);

    /* A failed activation must not move any counter. */
    TEST_ASSERT_EQ_INT(plugins_activate("Nonexistent"), -1);
    TEST_ASSERT_EQ_INT((int)metrics_get_counter("plugin_activations_total"), 3);

    millennium_client_destroy(client);
    client = NULL;
    daemon_state = NULL;
    plugins_cleanup();
    metrics_cleanup();
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
        test_call_handler, NULL, test_activation_handler, test_tick_handler), 0);

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
        NULL, NULL, NULL, NULL, NULL, NULL, NULL), -1);

    millennium_client_destroy(client);
    client = NULL;
    daemon_state = NULL;
    plugins_cleanup();
}

/* ── Plugin dispatch (plugins_handle_*) ─────────────────────────────── */

/* Counting handlers that record their last argument so we can verify the
 * active plugin actually receives each event and its return value is
 * propagated back to the caller. */
static int g_disp_coin_calls, g_disp_coin_value;
static int g_disp_key_calls; static char g_disp_key_char;
static int g_disp_hook_calls, g_disp_hook_up, g_disp_hook_down;
static int g_disp_call_calls, g_disp_call_state;
static int g_disp_card_calls; static char g_disp_card_last[32];
static int g_disp_tick_calls;

static int disp_coin_handler(int v, const char *c) {
    (void)c; g_disp_coin_calls++; g_disp_coin_value = v; return 42;
}
static int disp_key_handler(char k) {
    g_disp_key_calls++; g_disp_key_char = k; return 0;
}
static int disp_hook_handler(int u, int d) {
    g_disp_hook_calls++; g_disp_hook_up = u; g_disp_hook_down = d; return 0;
}
static int disp_call_handler(int s) {
    g_disp_call_calls++; g_disp_call_state = s; return 0;
}
static int disp_card_handler(const char *n) {
    g_disp_card_calls++;
    strncpy(g_disp_card_last, n ? n : "", sizeof(g_disp_card_last) - 1);
    g_disp_card_last[sizeof(g_disp_card_last) - 1] = '\0';
    return 0;
}
static void disp_tick_handler(void) { g_disp_tick_calls++; }

static void test_plugins_dispatch_to_active(void) {
    daemon_state_data_t ds;
    daemon_state_init(&ds);
    daemon_state = &ds;
    client = millennium_client_create();

    plugins_init();
    g_disp_coin_calls = g_disp_key_calls = g_disp_hook_calls = 0;
    g_disp_call_calls = g_disp_card_calls = g_disp_tick_calls = 0;

    TEST_ASSERT_EQ_INT(plugins_register("Dispatch Test", "Counts events",
        disp_coin_handler, disp_key_handler, disp_hook_handler,
        disp_call_handler, disp_card_handler, NULL, disp_tick_handler), 0);
    TEST_ASSERT_EQ_INT(plugins_activate("Dispatch Test"), 0);

    /* Each dispatch reaches the active plugin and propagates its return. */
    TEST_ASSERT_EQ_INT(plugins_handle_coin(25, "COIN_8"), 42);
    TEST_ASSERT_EQ_INT(g_disp_coin_calls, 1);
    TEST_ASSERT_EQ_INT(g_disp_coin_value, 25);

    plugins_handle_keypad('7');
    TEST_ASSERT_EQ_INT(g_disp_key_calls, 1);
    TEST_ASSERT_EQ_INT((int)g_disp_key_char, (int)'7');

    plugins_handle_hook(1, 0);
    TEST_ASSERT_EQ_INT(g_disp_hook_calls, 1);
    TEST_ASSERT_EQ_INT(g_disp_hook_up, 1);
    TEST_ASSERT_EQ_INT(g_disp_hook_down, 0);

    plugins_handle_call_state(3);
    TEST_ASSERT_EQ_INT(g_disp_call_calls, 1);
    TEST_ASSERT_EQ_INT(g_disp_call_state, 3);

    plugins_handle_card("4111111111111111");
    TEST_ASSERT_EQ_INT(g_disp_card_calls, 1);
    TEST_ASSERT_EQ_STR(g_disp_card_last, "4111111111111111");

    plugins_tick();
    TEST_ASSERT_EQ_INT(g_disp_tick_calls, 1);

    millennium_client_destroy(client);
    client = NULL;
    daemon_state = NULL;
    plugins_cleanup();
}

static void test_plugins_dispatch_no_active(void) {
    /* With no plugin active, dispatch is a safe no-op returning -1 and never
     * touches a stale handler. */
    plugins_cleanup();
    TEST_ASSERT_EQ_INT(plugins_handle_coin(5, "COIN_1"), -1);
    TEST_ASSERT_EQ_INT(plugins_handle_keypad('1'), -1);
    TEST_ASSERT_EQ_INT(plugins_handle_hook(0, 1), -1);
    TEST_ASSERT_EQ_INT(plugins_handle_call_state(0), -1);
    TEST_ASSERT_EQ_INT(plugins_handle_card("0"), -1);
    plugins_tick(); /* must not crash */
}

static void test_plugins_dispatch_missing_handler(void) {
    /* A plugin that registers NULL for a handler returns -1 for that event
     * rather than dereferencing a NULL function pointer. */
    daemon_state_data_t ds;
    daemon_state_init(&ds);
    daemon_state = &ds;
    client = millennium_client_create();

    plugins_init();

    TEST_ASSERT_EQ_INT(plugins_register("No Handlers", "All NULL",
        NULL, NULL, NULL, NULL, NULL, NULL, NULL), 0);
    TEST_ASSERT_EQ_INT(plugins_activate("No Handlers"), 0);

    TEST_ASSERT_EQ_INT(plugins_handle_coin(25, "COIN_8"), -1);
    TEST_ASSERT_EQ_INT(plugins_handle_keypad('7'), -1);
    TEST_ASSERT_EQ_INT(plugins_handle_card("0"), -1);
    plugins_tick(); /* NULL tick handler must not crash */

    millennium_client_destroy(client);
    client = NULL;
    daemon_state = NULL;
    plugins_cleanup();
}

/* ── Plugin registry / dynamic enumeration ─────────────────────────── */

/* Pure comparison exported by plugins/number_guess.c */
int number_guess_compare(int secret, int guess);

/* Pure helpers exported by plugins/time_operator.c */
int operator_keypad_digit(char c);
int operator_shift_down(int digit, int key);

static void test_plugins_builtins_registered(void) {
    char buf[2048];
    daemon_state_data_t ds;
    daemon_state_init(&ds);
    daemon_state = &ds;
    client = millennium_client_create();

    plugins_init();

    /* Eight built-ins ship with the platform. */
    TEST_ASSERT_EQ_INT(plugins_get_count(), 8);

    TEST_ASSERT_EQ_INT(plugins_list(buf, sizeof(buf)), 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "Classic Phone"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Fortune Teller"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Jukebox"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Number Guess"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Simon"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Dial-A-Joke"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Trivia"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "The Operator"));

    millennium_client_destroy(client);
    client = NULL;
    daemon_state = NULL;
    plugins_cleanup();
}

static void test_plugins_get_info(void) {
    const char *name = NULL;
    const char *desc = NULL;
    int active = -1;
    daemon_state_data_t ds;
    daemon_state_init(&ds);
    daemon_state = &ds;
    client = millennium_client_create();

    plugins_init(); /* default-activates Classic Phone (index 0) */

    TEST_ASSERT_EQ_INT(plugins_get_info(0, &name, &desc, &active), 0);
    TEST_ASSERT_EQ_STR(name, "Classic Phone");
    TEST_ASSERT_NOT_NULL((void *)desc);
    TEST_ASSERT_EQ_INT(active, 1);

    /* Out-of-range index is rejected. */
    TEST_ASSERT_EQ_INT(plugins_get_info(999, &name, &desc, &active), -1);
    TEST_ASSERT_EQ_INT(plugins_get_info(-1, NULL, NULL, NULL), -1);

    millennium_client_destroy(client);
    client = NULL;
    daemon_state = NULL;
    plugins_cleanup();
}

static void test_plugins_to_json(void) {
    char buf[2048];
    daemon_state_data_t ds;
    daemon_state_init(&ds);
    daemon_state = &ds;
    client = millennium_client_create();

    plugins_init();
    plugins_activate("Simon");

    TEST_ASSERT(plugins_to_json(buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"plugins\":["));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"name\":\"Number Guess\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"name\":\"Simon\""));
    /* The active plugin is reported and flagged. */
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"active_plugin\":\"Simon\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"active\":true"));

    millennium_client_destroy(client);
    client = NULL;
    daemon_state = NULL;
    plugins_cleanup();
}

static void test_plugins_to_json_escapes(void) {
    char buf[2048];
    daemon_state_data_t ds;
    daemon_state_init(&ds);
    daemon_state = &ds;
    client = millennium_client_create();

    plugins_init();
    /* Names are stored by pointer; a static literal with a quote is safe. */
    TEST_ASSERT_EQ_INT(plugins_register("Quote\"Plugin", "desc\\with\\slash",
        NULL, NULL, NULL, NULL, NULL, NULL, NULL), 0);

    TEST_ASSERT(plugins_to_json(buf, sizeof(buf)) > 0);
    /* The embedded quote and backslash must be escaped, not raw. */
    TEST_ASSERT_NOT_NULL(strstr(buf, "Quote\\\"Plugin"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "desc\\\\with\\\\slash"));

    millennium_client_destroy(client);
    client = NULL;
    daemon_state = NULL;
    plugins_cleanup();
}

static void test_number_guess_compare(void) {
    TEST_ASSERT(number_guess_compare(50, 25) < 0);  /* guess too low */
    TEST_ASSERT(number_guess_compare(50, 75) > 0);  /* guess too high */
    TEST_ASSERT_EQ_INT(number_guess_compare(50, 50), 0); /* exact */
    TEST_ASSERT(number_guess_compare(1, 99) > 0);
    TEST_ASSERT(number_guess_compare(99, 1) < 0);
}

/* ── The Operator (time-travel plugin) pure logic ───────────────────── */

static void test_operator_keypad_digit(void) {
    /* Classic phone lettering: 2=ABC .. 9=WXYZ; non-letters -> -1. */
    TEST_ASSERT_EQ_INT(operator_keypad_digit('A'), 2);
    TEST_ASSERT_EQ_INT(operator_keypad_digit('Z'), 9);
    TEST_ASSERT_EQ_INT(operator_keypad_digit('s'), 7);   /* lowercase too */
    TEST_ASSERT_EQ_INT(operator_keypad_digit('5'), -1);  /* not a letter  */
    TEST_ASSERT_EQ_INT(operator_keypad_digit(' '), -1);
    /* Puzzle 1 self-check: HUG spells 4-8-4. */
    TEST_ASSERT_EQ_INT(operator_keypad_digit('H'), 4);
    TEST_ASSERT_EQ_INT(operator_keypad_digit('U'), 8);
    TEST_ASSERT_EQ_INT(operator_keypad_digit('G'), 4);
}

static void test_operator_shift_down(void) {
    /* Puzzle 3 cipher: subtract the key, wrapping mod 10. */
    TEST_ASSERT_EQ_INT(operator_shift_down(7, 2), 5);
    TEST_ASSERT_EQ_INT(operator_shift_down(0, 2), 8);   /* wraps past zero */
    TEST_ASSERT_EQ_INT(operator_shift_down(1, 2), 9);
    TEST_ASSERT_EQ_INT(operator_shift_down(9, 0), 9);
}

/* ── WAV clip parser ────────────────────────────────────────────────── */

static void wav_put_u16(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
}
static void wav_put_u32(unsigned char *p, unsigned long v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

/* Build a canonical 16-bit PCM WAV (silence) into buf; returns total length. */
static size_t make_wav(unsigned char *buf, int channels, int rate,
                       int frames) {
    int data_bytes = frames * channels * 2;
    memcpy(buf + 0, "RIFF", 4);
    wav_put_u32(buf + 4, (unsigned long)(36 + data_bytes));
    memcpy(buf + 8, "WAVE", 4);
    memcpy(buf + 12, "fmt ", 4);
    wav_put_u32(buf + 16, 16);
    wav_put_u16(buf + 20, 1);                       /* PCM */
    wav_put_u16(buf + 22, (unsigned)channels);
    wav_put_u32(buf + 24, (unsigned long)rate);
    wav_put_u32(buf + 28, (unsigned long)(rate * channels * 2));
    wav_put_u16(buf + 32, (unsigned)(channels * 2));
    wav_put_u16(buf + 34, 16);                      /* bits per sample */
    memcpy(buf + 36, "data", 4);
    wav_put_u32(buf + 40, (unsigned long)data_bytes);
    memset(buf + 44, 0, (size_t)data_bytes);
    return (size_t)(44 + data_bytes);
}

static void test_wav_parse_valid(void) {
    unsigned char buf[256];
    wav_info_t info;
    size_t len = make_wav(buf, 1, 8000, 10);

    TEST_ASSERT_EQ_INT(wav_parse(buf, len, &info), 0);
    TEST_ASSERT_EQ_INT(info.format, 1);
    TEST_ASSERT_EQ_INT(info.channels, 1);
    TEST_ASSERT_EQ_INT(info.sample_rate, 8000);
    TEST_ASSERT_EQ_INT(info.bits_per_sample, 16);
    TEST_ASSERT_EQ_INT((int)info.data_offset, 44);
    TEST_ASSERT_EQ_INT((int)info.data_len, 20);  /* 10 mono frames * 2 bytes */
}

static void test_wav_parse_rejects_bad(void) {
    unsigned char buf[256];
    wav_info_t info;
    size_t len = make_wav(buf, 1, 8000, 4);

    TEST_ASSERT_EQ_INT(wav_parse((const unsigned char *)"nope", 4, &info), -1);
    TEST_ASSERT_EQ_INT(wav_parse(buf, 8, &info), -1);    /* truncated header */
    TEST_ASSERT_EQ_INT(wav_parse(NULL, 100, &info), -1);
    TEST_ASSERT_EQ_INT(wav_parse(buf, len, NULL), -1);

    buf[9] = 'X';                                        /* corrupt WAVE tag */
    TEST_ASSERT_EQ_INT(wav_parse(buf, len, &info), -1);
}

static void test_wav_parse_skips_unknown_chunk(void) {
    /* A LIST chunk (odd length -> a pad byte) before data must be skipped and
     * the data range still located correctly. */
    unsigned char buf[256];
    wav_info_t info;
    size_t n;

    memcpy(buf + 0, "RIFF", 4);
    memcpy(buf + 8, "WAVE", 4);
    memcpy(buf + 12, "fmt ", 4);
    wav_put_u32(buf + 16, 16);
    wav_put_u16(buf + 20, 1);
    wav_put_u16(buf + 22, 1);
    wav_put_u32(buf + 24, 8000);
    wav_put_u32(buf + 28, 16000);
    wav_put_u16(buf + 32, 2);
    wav_put_u16(buf + 34, 16);
    memcpy(buf + 36, "LIST", 4);
    wav_put_u32(buf + 40, 3);
    buf[44] = 'a'; buf[45] = 'b'; buf[46] = 'c'; buf[47] = 0;  /* + pad byte */
    memcpy(buf + 48, "data", 4);
    wav_put_u32(buf + 52, 4);
    buf[56] = 1; buf[57] = 2; buf[58] = 3; buf[59] = 4;
    n = 60;
    wav_put_u32(buf + 4, (unsigned long)(n - 8));

    TEST_ASSERT_EQ_INT(wav_parse(buf, n, &info), 0);
    TEST_ASSERT_EQ_INT((int)info.data_offset, 56);
    TEST_ASSERT_EQ_INT((int)info.data_len, 4);
}

/* ── Display line budget guardrail ──────────────────────────────────── */

/* Each content-heavy built-in exposes its static display strings (mirroring
 * the number_guess_compare export). The VFD scrolls long lines, but the
 * pipeline silently truncates anything >= DISPLAY_MAX_TEXT_LEN, so every
 * authored line must round-trip through the display without loss. */
int dial_a_joke_display_strings(const char **out, int max);
int trivia_display_strings(const char **out, int max);
int jukebox_display_strings(const char **out, int max);
int fortune_teller_display_strings(const char **out, int max);
int time_operator_display_strings(const char **out, int max);

static void check_display_strings(const char *plugin,
                                  int (*collect)(const char **, int)) {
    const char *strings[128];
    char back[DISPLAY_MAX_TEXT_LEN];
    int count, i;

    count = collect(strings, 128);
    TEST_ASSERT(count > 0);
    TEST_ASSERT(count <= 128);          /* buffer holds the whole table */

    for (i = 0; i < count; i++) {
        /* Push the real string through the real display pipeline and read it
         * back: a line over the budget comes back truncated, which fails. */
        display_manager_set_text(strings[i], NULL);
        display_manager_get_text(back, sizeof(back), NULL, 0);
        if (strcmp(back, strings[i]) != 0) {
            fprintf(stderr, "  %s display line truncated: \"%s\" -> \"%s\"\n",
                    plugin, strings[i], back);
        }
        TEST_ASSERT_EQ_STR(back, strings[i]);
    }
}

static void test_plugin_display_lines_fit(void) {
    client = millennium_client_create();
    display_manager_init(client);

    check_display_strings("Dial-A-Joke", dial_a_joke_display_strings);
    check_display_strings("Trivia", trivia_display_strings);
    check_display_strings("Jukebox", jukebox_display_strings);
    check_display_strings("Fortune Teller", fortune_teller_display_strings);
    check_display_strings("The Operator", time_operator_display_strings);

    millennium_client_destroy(client);
    client = NULL;
}

/* ── Plugin SDK ─────────────────────────────────────────────────────── */

static void test_sdk_rand_bounds(void) {
    int i;
    TEST_ASSERT_EQ_INT(sdk_rand_below(0), 0);   /* defensive */
    TEST_ASSERT_EQ_INT(sdk_rand_below(-5), 0);
    TEST_ASSERT_EQ_INT(sdk_rand_below(1), 0);
    TEST_ASSERT_EQ_INT(sdk_rand_range(5, 5), 5);
    for (i = 0; i < 1000; i++) {
        int b = sdk_rand_below(10);
        int r = sdk_rand_range(3, 7);
        int s = sdk_rand_range(7, 3); /* swapped args still valid */
        TEST_ASSERT(b >= 0 && b < 10);
        TEST_ASSERT(r >= 3 && r <= 7);
        TEST_ASSERT(s >= 3 && s <= 7);
    }
}

static void test_sdk_rand_choice(void) {
    static const char *const opts[] = {"a", "b", "c"};
    int i;
    TEST_ASSERT_NULL((void *)sdk_rand_choice(NULL, 3));
    TEST_ASSERT_NULL((void *)sdk_rand_choice(opts, 0));
    for (i = 0; i < 100; i++) {
        const char *c = sdk_rand_choice(opts, 3);
        TEST_ASSERT(c == opts[0] || c == opts[1] || c == opts[2]);
    }
}

static void test_sdk_balance(void) {
    daemon_state_data_t ds;
    daemon_state_init(&ds);
    daemon_state = &ds;

    TEST_ASSERT_EQ_INT(sdk_balance(), 0);
    sdk_add_balance(25);
    TEST_ASSERT_EQ_INT(sdk_balance(), 25);
    sdk_spend_balance(10);
    TEST_ASSERT_EQ_INT(sdk_balance(), 15);
    sdk_spend_balance(1000);                 /* over-spend clamps at 0 */
    TEST_ASSERT_EQ_INT(sdk_balance(), 0);
    sdk_add_balance(50);
    sdk_clear_balance();
    TEST_ASSERT_EQ_INT(sdk_balance(), 0);

    daemon_state = NULL;
    TEST_ASSERT_EQ_INT(sdk_balance(), 0);    /* NULL-safe */
}

static void test_sdk_state(void) {
    daemon_state_data_t ds;
    daemon_state_init(&ds);
    daemon_state = &ds;

    ds.current_state = DAEMON_STATE_IDLE_DOWN;
    TEST_ASSERT_EQ_INT((int)sdk_state(), (int)DAEMON_STATE_IDLE_DOWN);
    TEST_ASSERT_EQ_INT(sdk_receiver_is_up(), 0);

    ds.current_state = DAEMON_STATE_IDLE_UP;
    TEST_ASSERT_EQ_INT(sdk_receiver_is_up(), 1);

    ds.current_state = DAEMON_STATE_CALL_ACTIVE;
    TEST_ASSERT_EQ_INT(sdk_receiver_is_up(), 1);

    daemon_state_add_key(&ds, '4');
    daemon_state_add_key(&ds, '2');
    TEST_ASSERT_EQ_STR(sdk_keypad(), "42");

    daemon_state = NULL;
    TEST_ASSERT_EQ_INT((int)sdk_state(), (int)DAEMON_STATE_INVALID); /* NULL-safe */
    TEST_ASSERT_EQ_STR(sdk_keypad(), "");
}

/* ── Clock seam tests ──────────────────────────────────────────── */

static time_t g_fake_clock = 0;
static time_t fake_clock_source(void) { return g_fake_clock; }

static void test_clock_default_is_real_time(void) {
    /* With no source installed, mclock_now() tracks the real clock. */
    mclock_set_source(NULL);
    TEST_ASSERT(mclock_now() > 0);
    TEST_ASSERT(mclock_now() >= time(NULL) - 2);
}

static void test_clock_source_override(void) {
    g_fake_clock = 1000;
    mclock_set_source(fake_clock_source);
    TEST_ASSERT_EQ_INT((int)mclock_now(), 1000);
    TEST_ASSERT_EQ_INT((int)sdk_now(), 1000); /* SDK reads through the seam */

    g_fake_clock = 1042;                       /* advancing is instant */
    TEST_ASSERT_EQ_INT((int)sdk_now(), 1042);
    TEST_ASSERT_EQ_INT(sdk_elapsed(1000), 42); /* whole seconds since */
    TEST_ASSERT_EQ_INT(sdk_elapsed(2000), 0);  /* future clamps to 0 */

    mclock_set_source(NULL);                    /* restore for later tests */
}

/* ── Emergency number tests ────────────────────────────────────── */

static void test_free_number_911(void) {
    config_data_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    config_set_default_values(&cfg);
    TEST_ASSERT_EQ_INT(1, config_is_free_number(&cfg, "911"));
}

static void test_free_number_311(void) {
    config_data_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    config_set_default_values(&cfg);
    TEST_ASSERT_EQ_INT(1, config_is_free_number(&cfg, "311"));
}

static void test_free_number_0(void) {
    config_data_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    config_set_default_values(&cfg);
    TEST_ASSERT_EQ_INT(1, config_is_free_number(&cfg, "0"));
}

static void test_not_free_number(void) {
    config_data_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    config_set_default_values(&cfg);
    TEST_ASSERT_EQ_INT(0, config_is_free_number(&cfg, "5551234567"));
}

static void test_free_number_custom(void) {
    config_data_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    config_set_default_values(&cfg);
    config_set_value(&cfg, "call.free_numbers", "211,411");
    TEST_ASSERT_EQ_INT(1, config_is_free_number(&cfg, "211"));
    TEST_ASSERT_EQ_INT(1, config_is_free_number(&cfg, "411"));
    TEST_ASSERT_EQ_INT(0, config_is_free_number(&cfg, "911"));
}

/* ── Updater tests ─────────────────────────────────────────────── */

static void test_version_compare_equal(void) {
    TEST_ASSERT_EQ_INT(0, updater_compare_versions("1.0.0", "1.0.0"));
}

static void test_version_compare_major(void) {
    TEST_ASSERT(updater_compare_versions("2.0.0", "1.0.0") > 0);
    TEST_ASSERT(updater_compare_versions("1.0.0", "2.0.0") < 0);
}

static void test_version_compare_minor(void) {
    TEST_ASSERT(updater_compare_versions("1.2.0", "1.1.0") > 0);
    TEST_ASSERT(updater_compare_versions("1.1.0", "1.2.0") < 0);
}

static void test_version_compare_patch(void) {
    TEST_ASSERT(updater_compare_versions("1.0.2", "1.0.1") > 0);
    TEST_ASSERT(updater_compare_versions("1.0.1", "1.0.2") < 0);
}

static void test_version_compare_with_v_prefix(void) {
    TEST_ASSERT_EQ_INT(0, updater_compare_versions("v1.0.0", "1.0.0"));
    TEST_ASSERT(updater_compare_versions("v2.0.0", "v1.0.0") > 0);
}

static void test_version_no_update_initially(void) {
    char latest[64];
    TEST_ASSERT_EQ_INT(0, updater_is_update_available());
    TEST_ASSERT_EQ_INT(0, updater_get_latest_version(latest, sizeof(latest)));
    TEST_ASSERT_EQ_INT(0, (int)strlen(latest));   /* cleared, not left stale */
}

static void test_updater_apply_null_dir(void) {
    char st[256];
    TEST_ASSERT_EQ_INT(-1, updater_apply(NULL));
    updater_get_apply_status(st, sizeof(st));
    TEST_ASSERT(strstr(st, "no source") != NULL);
}

static void test_updater_apply_bad_dir(void) {
    char st[256];
    TEST_ASSERT_EQ_INT(-1, updater_apply("/nonexistent/path/to/repo"));
    updater_get_apply_status(st, sizeof(st));
    TEST_ASSERT(strstr(st, "git pull failed") != NULL);
}

/* ── Card config tests ─────────────────────────────────────────── */

static void test_card_enabled_default(void) {
    config_data_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    config_set_default_values(&cfg);
    TEST_ASSERT_EQ_INT(1, config_get_card_enabled(&cfg));
}

static void test_card_free_card_match(void) {
    config_data_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    config_set_default_values(&cfg);
    config_set_value(&cfg, "card.free_cards", "1234567890123456,9999888877776666");
    TEST_ASSERT_EQ_INT(1, config_is_free_card(&cfg, "1234567890123456"));
    TEST_ASSERT_EQ_INT(1, config_is_free_card(&cfg, "9999888877776666"));
}

static void test_card_free_card_no_match(void) {
    config_data_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    config_set_default_values(&cfg);
    config_set_value(&cfg, "card.free_cards", "1234567890123456");
    TEST_ASSERT_EQ_INT(0, config_is_free_card(&cfg, "0000000000000000"));
}

static void test_card_admin_card_match(void) {
    config_data_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    config_set_default_values(&cfg);
    config_set_value(&cfg, "card.admin_cards", "ADMIN12345678901");
    TEST_ASSERT_EQ_INT(1, config_is_admin_card(&cfg, "ADMIN12345678901"));
}

static void test_card_empty_list(void) {
    config_data_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    config_set_default_values(&cfg);
    TEST_ASSERT_EQ_INT(0, config_is_free_card(&cfg, "1234567890123456"));
    TEST_ASSERT_EQ_INT(0, config_is_admin_card(&cfg, "1234567890123456"));
}

/* ── Display manager ────────────────────────────────────────────── */

/* Control bytes in plugin-supplied text would otherwise reach the VFD as
 * commands or stray line breaks. They must be replaced with spaces while
 * ordinary printable text passes through unchanged. */
static void test_display_sanitizes_control_chars(void) {
    char line1[64];
    char line2[64];

    display_manager_init(NULL);

    /* Embedded newline, tab, carriage return, escape, and DEL all become
     * spaces; the surrounding printable characters are preserved. */
    display_manager_set_text("AB\nCD\tEF", "G\rH\x1bI\x7fJ");
    display_manager_get_text(line1, sizeof(line1), line2, sizeof(line2));
    TEST_ASSERT_EQ_STR(line1, "AB CD EF");
    TEST_ASSERT_EQ_STR(line2, "G H I J");
}

static void test_display_preserves_printable(void) {
    char line1[64];
    char line2[64];

    display_manager_init(NULL);

    /* Plain text (including spaces and punctuation) is untouched. */
    display_manager_set_text("Hello, World!", "Coins: $0.50");
    display_manager_get_text(line1, sizeof(line1), line2, sizeof(line2));
    TEST_ASSERT_EQ_STR(line1, "Hello, World!");
    TEST_ASSERT_EQ_STR(line2, "Coins: $0.50");
}

static void test_display_high_bit_bytes_untouched(void) {
    char line1[64];
    char line2[64];

    display_manager_init(NULL);

    /* Bytes >= 0x80 may map to the VFD's extended glyph set, so they are left
     * alone — only the C0 control range and DEL are scrubbed. */
    display_manager_set_text("X\xa9Y", NULL);
    display_manager_get_text(line1, sizeof(line1), line2, sizeof(line2));
    TEST_ASSERT_EQ_STR(line1, "X\xa9Y");
    TEST_ASSERT_EQ_STR(line2, "");
}

/* ── Logger (async file writer, issue #123) ─────────────────────── */

/* File logging is drained to disk by a background writer thread so a slow disk
 * can't block a producer. Verify the contract callers depend on: after
 * logger_flush(), every enqueued line is actually on disk (nothing lost in the
 * queue), and logger_shutdown() stops the writer cleanly. */
static void test_logger_async_file_write(void) {
    const char *path = "/tmp/millennium_logger_async_test.log";
    char buf[16384];
    FILE *f;
    size_t got;
    int i;

    remove(path);
    logger_set_log_to_console(0);          /* keep the 200 lines off the console */
    logger_set_level(LOG_LEVEL_VERBOSE);   /* let INFO lines pass the level gate */
    logger_set_log_file(path);
    logger_set_log_to_file(1);

    for (i = 0; i < 200; i++) {
        logger_infof_with_category("AsyncTest", "line %d", i);
    }

    logger_flush();   /* block until the writer has drained the queue to disk */

    f = fopen(path, "r");
    TEST_ASSERT_NOT_NULL(f);
    got = fread(buf, 1, sizeof(buf) - 1, f);
    buf[got] = '\0';
    fclose(f);

    /* First and last lines must both be present once flush() returns. */
    TEST_ASSERT_NOT_NULL(strstr(buf, "[AsyncTest] line 0"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "[AsyncTest] line 199"));

    logger_shutdown();                 /* stop the writer, close the file */
    logger_set_log_to_file(0);
    logger_set_log_to_console(1);      /* restore defaults for later suites */
    logger_set_level(LOG_LEVEL_ERROR);
    remove(path);
}

/* The queue-health snapshot (issue #123 follow-up) feeds the metrics endpoint.
 * Verify the contract the daemon relies on: capacity is reported, the writer is
 * marked running while file logging is on, the queue drains to depth 0 after a
 * flush, every enqueued line shows up in the high-water mark, and a normal run
 * well under capacity drops nothing. */
static void test_logger_queue_stats(void) {
    const char *path = "/tmp/millennium_logger_stats_test.log";
    logger_queue_stats_t st;
    int i;

    remove(path);
    logger_set_log_to_console(0);
    logger_set_level(LOG_LEVEL_VERBOSE);
    logger_set_log_file(path);
    logger_set_log_to_file(1);

    /* Capacity is reported and the writer runs while file logging is enabled. */
    logger_get_queue_stats(&st);
    TEST_ASSERT_EQ_INT((int)st.capacity, 1024);
    TEST_ASSERT_EQ_INT(st.started, 1);

    for (i = 0; i < 50; i++) {
        logger_infof_with_category("StatsTest", "line %d", i);
    }
    logger_flush();   /* block until the writer has drained the queue */

    logger_get_queue_stats(&st);
    TEST_ASSERT_EQ_INT((int)st.depth, 0);          /* drained after flush */
    TEST_ASSERT_EQ_INT((int)st.dropped_total, 0);  /* 50 lines << 1024 cap */
    TEST_ASSERT((int)st.high_water >= 1);          /* lines did pass through */

    logger_shutdown();
    logger_set_log_to_file(0);
    logger_set_log_to_console(1);
    logger_set_level(LOG_LEVEL_ERROR);
    remove(path);
}

/* ── Metrics export ─────────────────────────────────────────────── */

static void test_metrics_export_prometheus_basic(void) {
    char *out;
    TEST_ASSERT_EQ_INT(metrics_init(), 0);
    metrics_increment_counter("calls_total", 3);
    metrics_set_gauge("balance_cents", 50.0);
    metrics_observe_histogram("call_duration", 12.5);
    out = metrics_export_prometheus();
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT(strstr(out, "calls_total 3") != NULL);
    TEST_ASSERT(strstr(out, "call_duration_count 1") != NULL);
    free(out);
    metrics_cleanup();
}

static void test_metrics_export_json_basic(void) {
    char *out;
    TEST_ASSERT_EQ_INT(metrics_init(), 0);
    metrics_increment_counter("coins", 7);
    out = metrics_export_json();
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT(out[0] == '{');
    TEST_ASSERT(strstr(out, "\"coins\": 7") != NULL);
    free(out);
    metrics_cleanup();
}

/* Regression for the export buffer-overflow fix (buf_appendf). The JSON
 * exporter sized its buffer from an *average* budget (100 bytes per counter),
 * so a registry of many long-named counters whose lines each exceed that
 * budget overran the total allocation: `pos` ran past `len`, `len - pos`
 * underflowed the size_t, and the next snprintf wrote off the end of the heap
 * block. A registry full of 160-char names blows past the estimate decisively.
 * Under ASan this aborts on the old code; the fixed exporters grow on demand,
 * so it passes and the output stays well-formed and NUL-terminated. */
static void test_metrics_export_no_overflow(void) {
    char name[200];
    char prefix[200];
    char *p, *j;
    int i;

    TEST_ASSERT_EQ_INT(metrics_init(), 0);

    /* A 160-char name template; each emitted line is far larger than the old
     * 100-bytes-per-counter JSON budget. */
    memset(prefix, 'a', 160);
    prefix[160] = '\0';

    for (i = 0; i < 200; i++) {
        snprintf(name, sizeof(name), "%s%d", prefix, i);
        metrics_increment_counter(name, (uint64_t)i);
    }
    for (i = 0; i < 50; i++) {
        snprintf(name, sizeof(name), "%shist%d", prefix, i);
        metrics_observe_histogram(name, (double)i);
    }

    p = metrics_export_prometheus();
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT(p[strlen(p)] == '\0');
    free(p);

    j = metrics_export_json();
    TEST_ASSERT_NOT_NULL(j);
    TEST_ASSERT(j[0] == '{');
    TEST_ASSERT(j[strlen(j)] == '\0');
    free(j);

    metrics_cleanup();
}

/* ── Call-duration metric tests ────────────────────────────────── */

/* call_metrics times a connected call between started()/ended() using the
 * clock seam and records the elapsed seconds into the call_duration_seconds
 * histogram. Drive it with the fake clock so "elapsed time" is exact and
 * instant, then verify the histogram count/sum and the order-tolerance and
 * backwards-clock guards. */
static void test_call_duration_histogram(void) {
    metrics_histogram_stats_t stats;

    TEST_ASSERT_EQ_INT(metrics_init(), 0);
    mclock_set_source(fake_clock_source);
    call_metrics_reset();

    /* A 42-second call. */
    g_fake_clock = 5000;
    call_metrics_started();
    g_fake_clock = 5042;
    call_metrics_ended();

    TEST_ASSERT_EQ_INT(metrics_get_histogram_stats("call_duration_seconds", &stats), 0);
    TEST_ASSERT_EQ_INT((int)stats.count, 1);
    TEST_ASSERT(stats.sum == 42.0);
    TEST_ASSERT(stats.max == 42.0);

    /* A second, 60-second call accumulates into the same histogram. */
    g_fake_clock = 5042;
    call_metrics_started();
    g_fake_clock = 5102;
    call_metrics_ended();

    TEST_ASSERT_EQ_INT(metrics_get_histogram_stats("call_duration_seconds", &stats), 0);
    TEST_ASSERT_EQ_INT((int)stats.count, 2);
    TEST_ASSERT(stats.sum == 102.0);
    TEST_ASSERT(stats.max == 60.0);

    /* ended() without a matching started() records nothing. */
    call_metrics_ended();
    TEST_ASSERT_EQ_INT(metrics_get_histogram_stats("call_duration_seconds", &stats), 0);
    TEST_ASSERT_EQ_INT((int)stats.count, 2);

    /* reset() discards an in-progress call so it is never recorded. */
    g_fake_clock = 6000;
    call_metrics_started();
    call_metrics_reset();
    g_fake_clock = 6100;
    call_metrics_ended();
    TEST_ASSERT_EQ_INT(metrics_get_histogram_stats("call_duration_seconds", &stats), 0);
    TEST_ASSERT_EQ_INT((int)stats.count, 2);

    /* A clock that steps backwards clamps the duration to 0, never negative. */
    g_fake_clock = 7000;
    call_metrics_started();
    g_fake_clock = 6950;
    call_metrics_ended();
    TEST_ASSERT_EQ_INT(metrics_get_histogram_stats("call_duration_seconds", &stats), 0);
    TEST_ASSERT_EQ_INT((int)stats.count, 3);
    TEST_ASSERT(stats.sum == 102.0);   /* the clamped 0 added nothing */

    mclock_set_source(NULL);
    metrics_cleanup();
}

/* call_metrics_ringing_started()/_answered() time the pre-connect phase of an
 * incoming call into call_ring_seconds. call_metrics_incoming_ended() resolves a
 * CALL_INCOMING phase that ended before connecting: with a ring in progress it
 * counts a missed inbound call (calls_missed), and with no ring it counts a
 * failed outbound dial (calls_failed) — the two are mutually exclusive, so the
 * web-initiated outbound start_call path (CALL_INCOMING without a ring) is never
 * miscounted as a miss. */
static void test_call_ring_and_failure_metrics(void) {
    metrics_histogram_stats_t stats;

    TEST_ASSERT_EQ_INT(metrics_init(), 0);
    mclock_set_source(fake_clock_source);
    call_metrics_reset();

    /* An incoming call that rang 8 seconds and was then answered: the ring time
     * is recorded, but no miss is counted. */
    g_fake_clock = 5000;
    call_metrics_ringing_started();
    g_fake_clock = 5008;
    call_metrics_ringing_answered();

    TEST_ASSERT_EQ_INT(metrics_get_histogram_stats("call_ring_seconds", &stats), 0);
    TEST_ASSERT_EQ_INT((int)stats.count, 1);
    TEST_ASSERT(stats.sum == 8.0);
    TEST_ASSERT(stats.max == 8.0);
    TEST_ASSERT_EQ_INT((int)metrics_get_counter("calls_missed"), 0);
    TEST_ASSERT_EQ_INT((int)metrics_get_counter("calls_failed"), 0);

    /* An incoming call that rang 30 seconds and was abandoned: ring time is
     * recorded into the same histogram, and the miss is counted. */
    g_fake_clock = 6000;
    call_metrics_ringing_started();
    g_fake_clock = 6030;
    call_metrics_incoming_ended();

    TEST_ASSERT_EQ_INT(metrics_get_histogram_stats("call_ring_seconds", &stats), 0);
    TEST_ASSERT_EQ_INT((int)stats.count, 2);
    TEST_ASSERT(stats.sum == 38.0);
    TEST_ASSERT(stats.max == 30.0);
    TEST_ASSERT_EQ_INT((int)metrics_get_counter("calls_missed"), 1);
    TEST_ASSERT_EQ_INT((int)metrics_get_counter("calls_failed"), 0);

    /* incoming_ended() with no ring in progress is the outbound start_call path:
     * the dial never connected, so it counts a failure and not a miss, and
     * records nothing into the ring histogram. */
    call_metrics_incoming_ended();
    TEST_ASSERT_EQ_INT(metrics_get_histogram_stats("call_ring_seconds", &stats), 0);
    TEST_ASSERT_EQ_INT((int)stats.count, 2);
    TEST_ASSERT_EQ_INT((int)metrics_get_counter("calls_missed"), 1);
    TEST_ASSERT_EQ_INT((int)metrics_get_counter("calls_failed"), 1);

    /* reset() discards an in-progress ring so it is never recorded; the ended
     * phase then looks like an outbound dial and is counted as a failure. */
    g_fake_clock = 7000;
    call_metrics_ringing_started();
    call_metrics_reset();
    g_fake_clock = 7100;
    call_metrics_incoming_ended();
    TEST_ASSERT_EQ_INT(metrics_get_histogram_stats("call_ring_seconds", &stats), 0);
    TEST_ASSERT_EQ_INT((int)stats.count, 2);
    TEST_ASSERT_EQ_INT((int)metrics_get_counter("calls_missed"), 1);
    TEST_ASSERT_EQ_INT((int)metrics_get_counter("calls_failed"), 2);

    /* A clock that steps backwards clamps the ring duration to 0, never
     * negative, and still counts the miss. */
    g_fake_clock = 8000;
    call_metrics_ringing_started();
    g_fake_clock = 7950;
    call_metrics_incoming_ended();
    TEST_ASSERT_EQ_INT(metrics_get_histogram_stats("call_ring_seconds", &stats), 0);
    TEST_ASSERT_EQ_INT((int)stats.count, 3);
    TEST_ASSERT(stats.sum == 38.0);   /* the clamped 0 added nothing */
    TEST_ASSERT_EQ_INT((int)metrics_get_counter("calls_missed"), 2);
    TEST_ASSERT_EQ_INT((int)metrics_get_counter("calls_failed"), 2);

    mclock_set_source(NULL);
    metrics_cleanup();
}

/* ── State persistence (load validation) ────────────────────────── */

/* Write contents to a temp file so the loader has something to parse. */
static void sp_write_file(const char *path, const char *contents) {
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(contents, f);
        fclose(f);
    }
}

static void test_state_load_round_trip(void) {
    const char *path = "/tmp/millennium_state_good.test";
    persisted_state_t saved;
    persisted_state_t loaded;

    saved.inserted_cents = 75;
    saved.last_state = (int)DAEMON_STATE_IDLE_UP;
    strcpy(saved.active_plugin, "Classic Phone");

    TEST_ASSERT_EQ_INT(state_persistence_save(&saved, path), 0);
    TEST_ASSERT_EQ_INT(state_persistence_load(&loaded, path), 0);
    TEST_ASSERT_EQ_INT(loaded.inserted_cents, 75);
    TEST_ASSERT_EQ_INT(loaded.last_state, (int)DAEMON_STATE_IDLE_UP);
    TEST_ASSERT_EQ_STR(loaded.active_plugin, "Classic Phone");
    remove(path);
}

/* #228: the save path now derives the parent directory to fsync it. That
 * derivation has three cases -- a normal path, a bare filename with no
 * directory part, and a file directly in "/" -- and getting it wrong would
 * break saving entirely. Exercise the two we can write to. */
static void test_state_save_relative_path(void) {
    const char *path = "millennium_state_rel.test";   /* no '/' at all */
    persisted_state_t saved;
    persisted_state_t loaded;

    saved.inserted_cents = 25;
    saved.last_state = (int)DAEMON_STATE_IDLE_DOWN;
    strcpy(saved.active_plugin, "Number Guess");

    TEST_ASSERT_EQ_INT(state_persistence_save(&saved, path), 0);
    TEST_ASSERT_EQ_INT(state_persistence_load(&loaded, path), 0);
    TEST_ASSERT_EQ_INT(loaded.inserted_cents, 25);
    TEST_ASSERT_EQ_STR(loaded.active_plugin, "Number Guess");
    remove(path);
}

static void test_state_save_nested_path(void) {
    const char *path = "/tmp/millennium_state_nested.test";
    persisted_state_t saved;
    persisted_state_t loaded;

    saved.inserted_cents = 50;
    saved.last_state = (int)DAEMON_STATE_IDLE_UP;
    strcpy(saved.active_plugin, "");

    TEST_ASSERT_EQ_INT(state_persistence_save(&saved, path), 0);
    TEST_ASSERT_EQ_INT(state_persistence_load(&loaded, path), 0);
    TEST_ASSERT_EQ_INT(loaded.inserted_cents, 50);
    TEST_ASSERT_EQ_STR(loaded.active_plugin, "");
    remove(path);
}

static void test_state_load_absent_file_is_silent(void) {
    const char *path = "/tmp/millennium_state_absent.test";
    persisted_state_t ps;
    char err[128];

    remove(path);
    /* Missing file fails the load but is NOT flagged as corruption. */
    TEST_ASSERT_EQ_INT(state_persistence_load_ex(&ps, path, err, sizeof(err)), -1);
    TEST_ASSERT_EQ_STR(err, "");
    TEST_ASSERT_EQ_INT(ps.inserted_cents, 0);
}

static void test_state_load_rejects_negative_cents(void) {
    const char *path = "/tmp/millennium_state_neg.test";
    persisted_state_t ps;
    char err[128];

    sp_write_file(path, "inserted_cents=-50\nactive_plugin=Classic Phone\n");
    TEST_ASSERT_EQ_INT(state_persistence_load_ex(&ps, path, err, sizeof(err)), -1);
    TEST_ASSERT(err[0] != '\0');                 /* corruption is reported */
    TEST_ASSERT_EQ_INT(ps.inserted_cents, 0);    /* and the state is zeroed */
    TEST_ASSERT_EQ_STR(ps.active_plugin, "");
    remove(path);
}

static void test_state_load_rejects_huge_cents(void) {
    const char *path = "/tmp/millennium_state_huge.test";
    persisted_state_t ps;
    char err[128];

    sp_write_file(path, "inserted_cents=999999999\n");
    TEST_ASSERT_EQ_INT(state_persistence_load_ex(&ps, path, err, sizeof(err)), -1);
    TEST_ASSERT(err[0] != '\0');
    TEST_ASSERT_EQ_INT(ps.inserted_cents, 0);
    remove(path);
}

static void test_state_load_rejects_nonnumeric_cents(void) {
    const char *path = "/tmp/millennium_state_junk.test";
    persisted_state_t ps;
    char err[128];

    /* atoi() would silently turn this into 0; strict parsing must reject it. */
    sp_write_file(path, "inserted_cents=12abc\n");
    TEST_ASSERT_EQ_INT(state_persistence_load_ex(&ps, path, err, sizeof(err)), -1);
    TEST_ASSERT(err[0] != '\0');
    remove(path);
}

static void test_state_load_accepts_boundary_cents(void) {
    const char *path = "/tmp/millennium_state_max.test";
    persisted_state_t ps;
    char buf[64];

    snprintf(buf, sizeof(buf), "inserted_cents=%d\n", STATE_MAX_INSERTED_CENTS);
    sp_write_file(path, buf);
    TEST_ASSERT_EQ_INT(state_persistence_load(&ps, path), 0);
    TEST_ASSERT_EQ_INT(ps.inserted_cents, STATE_MAX_INSERTED_CENTS);
    remove(path);
}

static void test_state_load_rejects_bad_last_state(void) {
    const char *path = "/tmp/millennium_state_badstate.test";
    persisted_state_t ps;
    char err[128];

    sp_write_file(path, "last_state=99\n");
    TEST_ASSERT_EQ_INT(state_persistence_load_ex(&ps, path, err, sizeof(err)), -1);
    TEST_ASSERT(err[0] != '\0');
    remove(path);
}

static void test_state_load_rejects_control_chars_in_plugin(void) {
    const char *path = "/tmp/millennium_state_ctrl.test";
    persisted_state_t ps;
    char err[128];

    /* A bell/control byte in the name signals a torn write or tampering. */
    sp_write_file(path, "active_plugin=Class\x07ic\n");
    TEST_ASSERT_EQ_INT(state_persistence_load_ex(&ps, path, err, sizeof(err)), -1);
    TEST_ASSERT(err[0] != '\0');
    remove(path);
}

static void test_state_load_ignores_unknown_keys(void) {
    const char *path = "/tmp/millennium_state_unknown.test";
    persisted_state_t ps;

    /* Forward compatibility: an unrecognized key must not fail the load. */
    sp_write_file(path, "inserted_cents=25\nfuture_field=whatever\n");
    TEST_ASSERT_EQ_INT(state_persistence_load(&ps, path), 0);
    TEST_ASSERT_EQ_INT(ps.inserted_cents, 25);
    remove(path);
}

static void test_state_load_null_safety(void) {
    persisted_state_t ps;
    /* NULL arguments must fail cleanly rather than crash. */
    TEST_ASSERT_EQ_INT(state_persistence_load(NULL, "/tmp/x"), -1);
    TEST_ASSERT_EQ_INT(state_persistence_load(&ps, NULL), -1);
}

/* ── Connection queue (web server worker pool, #125) ────────────── */

static void test_conn_queue_fifo_order(void) {
    struct conn_queue q;
    TEST_ASSERT_EQ_INT(conn_queue_init(&q, 4), 0);
    TEST_ASSERT_EQ_INT(conn_queue_count(&q), 0);

    TEST_ASSERT_EQ_INT(conn_queue_try_push(&q, 10), 0);
    TEST_ASSERT_EQ_INT(conn_queue_try_push(&q, 20), 0);
    TEST_ASSERT_EQ_INT(conn_queue_try_push(&q, 30), 0);
    TEST_ASSERT_EQ_INT(conn_queue_count(&q), 3);

    /* Pops come back in the order they went in. */
    TEST_ASSERT_EQ_INT(conn_queue_pop(&q), 10);
    TEST_ASSERT_EQ_INT(conn_queue_pop(&q), 20);
    TEST_ASSERT_EQ_INT(conn_queue_pop(&q), 30);
    TEST_ASSERT_EQ_INT(conn_queue_count(&q), 0);

    conn_queue_destroy(&q);
}

static void test_conn_queue_full_rejects(void) {
    struct conn_queue q;
    TEST_ASSERT_EQ_INT(conn_queue_init(&q, 2), 0);

    TEST_ASSERT_EQ_INT(conn_queue_try_push(&q, 1), 0);
    TEST_ASSERT_EQ_INT(conn_queue_try_push(&q, 2), 0);
    /* Third push is back-pressure: queue is full. */
    TEST_ASSERT_EQ_INT(conn_queue_try_push(&q, 3), -1);
    TEST_ASSERT_EQ_INT(conn_queue_count(&q), 2);

    conn_queue_destroy(&q);
}

static void test_conn_queue_wraps_around(void) {
    struct conn_queue q;
    int i;
    TEST_ASSERT_EQ_INT(conn_queue_init(&q, 3), 0);

    /* Fill, drain partway, refill — exercises the ring buffer wrap. */
    for (i = 0; i < 3; i++) TEST_ASSERT_EQ_INT(conn_queue_try_push(&q, i), 0);
    TEST_ASSERT_EQ_INT(conn_queue_pop(&q), 0);
    TEST_ASSERT_EQ_INT(conn_queue_pop(&q), 1);
    TEST_ASSERT_EQ_INT(conn_queue_try_push(&q, 3), 0);  /* wraps to head slot 0 */
    TEST_ASSERT_EQ_INT(conn_queue_try_push(&q, 4), 0);
    TEST_ASSERT_EQ_INT(conn_queue_pop(&q), 2);
    TEST_ASSERT_EQ_INT(conn_queue_pop(&q), 3);
    TEST_ASSERT_EQ_INT(conn_queue_pop(&q), 4);

    conn_queue_destroy(&q);
}

static void test_conn_queue_close_drains_then_signals(void) {
    struct conn_queue q;
    TEST_ASSERT_EQ_INT(conn_queue_init(&q, 4), 0);

    TEST_ASSERT_EQ_INT(conn_queue_try_push(&q, 7), 0);
    conn_queue_close(&q);

    /* Push after close is rejected. */
    TEST_ASSERT_EQ_INT(conn_queue_try_push(&q, 8), -1);
    /* Already-queued items still drain... */
    TEST_ASSERT_EQ_INT(conn_queue_pop(&q), 7);
    /* ...then pop reports closed-and-empty without blocking. */
    TEST_ASSERT_EQ_INT(conn_queue_pop(&q), -1);

    conn_queue_destroy(&q);
}

static void test_conn_queue_null_safety(void) {
    TEST_ASSERT_EQ_INT(conn_queue_init(NULL, 4), -1);
    TEST_ASSERT_EQ_INT(conn_queue_init((struct conn_queue*)0, 0), -1);
    TEST_ASSERT_EQ_INT(conn_queue_try_push(NULL, 1), -1);
    TEST_ASSERT_EQ_INT(conn_queue_pop(NULL), -1);
    TEST_ASSERT_EQ_INT(conn_queue_count(NULL), 0);
    conn_queue_close(NULL);    /* must not crash */
    conn_queue_destroy(NULL);  /* must not crash */
}

/* A blocked pop must wake and return the fd once a producer pushes. Run the
 * producer on a second thread so the main thread genuinely blocks in pop. */
struct cq_producer_arg { struct conn_queue* q; int fd; };
static void* cq_producer(void* arg) {
    struct cq_producer_arg* a = (struct cq_producer_arg*)arg;
    conn_queue_try_push(a->q, a->fd);
    return NULL;
}
static void test_conn_queue_blocking_pop_wakes(void) {
    struct conn_queue q;
    struct cq_producer_arg arg;
    pthread_t producer;
    TEST_ASSERT_EQ_INT(conn_queue_init(&q, 4), 0);

    arg.q = &q;
    arg.fd = 42;
    TEST_ASSERT_EQ_INT(pthread_create(&producer, NULL, cq_producer, &arg), 0);
    /* Blocks until the producer pushes, then returns its fd. */
    TEST_ASSERT_EQ_INT(conn_queue_pop(&q), 42);
    pthread_join(producer, NULL);

    conn_queue_destroy(&q);
}

/* The health snapshot (#125 follow-up) feeds the worker-pool metrics the daemon
 * publishes. Verify the contract: capacity is reported, depth tracks the live
 * count, high_water records the deepest the queue ever got (not the current
 * depth), pushed_total counts only accepted fds, and rejected_total counts only
 * connections shed when the queue was full. */
static void test_conn_queue_stats(void) {
    struct conn_queue q;
    struct conn_queue_stats st;

    TEST_ASSERT_EQ_INT(conn_queue_init(&q, 2), 0);

    conn_queue_get_stats(&q, &st);
    TEST_ASSERT_EQ_INT(st.capacity, 2);
    TEST_ASSERT_EQ_INT(st.depth, 0);
    TEST_ASSERT_EQ_INT((int)st.high_water, 0);
    TEST_ASSERT_EQ_INT((int)st.pushed_total, 0);
    TEST_ASSERT_EQ_INT((int)st.rejected_total, 0);

    /* Fill to capacity, then overflow twice — both overflows are shed. */
    TEST_ASSERT_EQ_INT(conn_queue_try_push(&q, 1), 0);
    TEST_ASSERT_EQ_INT(conn_queue_try_push(&q, 2), 0);
    TEST_ASSERT_EQ_INT(conn_queue_try_push(&q, 3), -1);
    TEST_ASSERT_EQ_INT(conn_queue_try_push(&q, 4), -1);

    conn_queue_get_stats(&q, &st);
    TEST_ASSERT_EQ_INT(st.depth, 2);
    TEST_ASSERT_EQ_INT((int)st.high_water, 2);
    TEST_ASSERT_EQ_INT((int)st.pushed_total, 2);
    TEST_ASSERT_EQ_INT((int)st.rejected_total, 2);

    /* Draining leaves high_water and the lifetime totals intact. */
    TEST_ASSERT_EQ_INT(conn_queue_pop(&q), 1);
    TEST_ASSERT_EQ_INT(conn_queue_pop(&q), 2);
    conn_queue_get_stats(&q, &st);
    TEST_ASSERT_EQ_INT(st.depth, 0);
    TEST_ASSERT_EQ_INT((int)st.high_water, 2);
    TEST_ASSERT_EQ_INT((int)st.pushed_total, 2);
    TEST_ASSERT_EQ_INT((int)st.rejected_total, 2);

    /* Closing the queue is shutdown, not load shedding: try_push fails but the
     * rejection counter must not move. */
    conn_queue_close(&q);
    TEST_ASSERT_EQ_INT(conn_queue_try_push(&q, 5), -1);
    conn_queue_get_stats(&q, &st);
    TEST_ASSERT_EQ_INT((int)st.rejected_total, 2);

    conn_queue_destroy(&q);
}

/* The snapshot must be NULL-safe both ways: a NULL output is ignored, and a
 * NULL queue zeroes the output (the daemon polls it when the web server is
 * disabled). */
static void test_conn_queue_stats_null_safety(void) {
    struct conn_queue_stats st;

    conn_queue_get_stats(NULL, NULL);   /* must not crash */

    st.capacity = 7;
    st.depth = 7;
    st.high_water = 7;
    st.pushed_total = 7;
    st.rejected_total = 7;
    conn_queue_get_stats(NULL, &st);
    TEST_ASSERT_EQ_INT(st.capacity, 0);
    TEST_ASSERT_EQ_INT(st.depth, 0);
    TEST_ASSERT_EQ_INT((int)st.high_water, 0);
    TEST_ASSERT_EQ_INT((int)st.pushed_total, 0);
    TEST_ASSERT_EQ_INT((int)st.rejected_total, 0);
}

/* ── CLI argument parsing ───────────────────────────────────────── */

static void test_cli_no_args_runs(void) {
    cli_options_t opts;
    char* argv[] = { "millennium-daemon" };
    cli_parse_args(1, argv, &opts);
    TEST_ASSERT_EQ_INT(opts.mode, CLI_MODE_RUN);
    TEST_ASSERT_NULL((void*)opts.config_file);
}

static void test_cli_config_long(void) {
    cli_options_t opts;
    char* argv[] = { "millennium-daemon", "--config", "/etc/millennium/daemon.conf" };
    cli_parse_args(3, argv, &opts);
    TEST_ASSERT_EQ_INT(opts.mode, CLI_MODE_RUN);
    TEST_ASSERT_EQ_STR(opts.config_file, "/etc/millennium/daemon.conf");
}

static void test_cli_config_short(void) {
    cli_options_t opts;
    char* argv[] = { "millennium-daemon", "-c", "/tmp/x.conf" };
    cli_parse_args(3, argv, &opts);
    TEST_ASSERT_EQ_INT(opts.mode, CLI_MODE_RUN);
    TEST_ASSERT_EQ_STR(opts.config_file, "/tmp/x.conf");
}

static void test_cli_config_equals(void) {
    cli_options_t opts;
    char* argv[] = { "millennium-daemon", "--config=/tmp/y.conf" };
    cli_parse_args(2, argv, &opts);
    TEST_ASSERT_EQ_INT(opts.mode, CLI_MODE_RUN);
    TEST_ASSERT_EQ_STR(opts.config_file, "/tmp/y.conf");
}

static void test_cli_config_missing_value(void) {
    cli_options_t opts;
    char* argv[] = { "millennium-daemon", "--config" };
    cli_parse_args(2, argv, &opts);
    TEST_ASSERT_EQ_INT(opts.mode, CLI_MODE_ERROR);
    TEST_ASSERT(opts.error[0] != '\0');
}

static void test_cli_config_empty_equals(void) {
    cli_options_t opts;
    char* argv[] = { "millennium-daemon", "--config=" };
    cli_parse_args(2, argv, &opts);
    TEST_ASSERT_EQ_INT(opts.mode, CLI_MODE_ERROR);
}

static void test_cli_help(void) {
    cli_options_t opts;
    char* argv[] = { "millennium-daemon", "--help" };
    cli_parse_args(2, argv, &opts);
    TEST_ASSERT_EQ_INT(opts.mode, CLI_MODE_HELP);
}

static void test_cli_version(void) {
    cli_options_t opts;
    char* argv[] = { "millennium-daemon", "-v" };
    cli_parse_args(2, argv, &opts);
    TEST_ASSERT_EQ_INT(opts.mode, CLI_MODE_VERSION);
}

static void test_cli_unknown_arg(void) {
    cli_options_t opts;
    char* argv[] = { "millennium-daemon", "--bogus" };
    cli_parse_args(2, argv, &opts);
    TEST_ASSERT_EQ_INT(opts.mode, CLI_MODE_ERROR);
    TEST_ASSERT(opts.error[0] != '\0');
}

/* --config no longer has to be the first argument (the old parser only looked
   at argv[1]); a flag before it must still be honored. */
static void test_cli_config_not_first(void) {
    cli_options_t opts;
    char* argv[] = { "millennium-daemon", "-c", "/a.conf", "--bogus" };
    cli_parse_args(4, argv, &opts);
    TEST_ASSERT_EQ_INT(opts.mode, CLI_MODE_ERROR);  /* the trailing bad flag wins */
}

/* --version/--help short-circuit even when other (valid) args follow. */
static void test_cli_version_short_circuits(void) {
    cli_options_t opts;
    char* argv[] = { "millennium-daemon", "--version", "--config", "/x.conf" };
    cli_parse_args(4, argv, &opts);
    TEST_ASSERT_EQ_INT(opts.mode, CLI_MODE_VERSION);
}

/* Health checks return a status and may write a human-readable diagnostic.
 * /api/health surfaces that message verbatim, so the monitor must record what
 * the check wrote — and synthesize a status-derived default when a check writes
 * nothing — instead of the old hard-coded "Check completed successfully" that
 * was reported even for a CRITICAL check. */
static health_status_t ut_health_with_message(char *message, size_t message_len) {
    snprintf(message, message_len, "serial link down: no data for 45s");
    return HEALTH_STATUS_CRITICAL;
}

static health_status_t ut_health_silent_warning(char *message, size_t message_len) {
    (void)message;       /* deliberately writes no message */
    (void)message_len;
    return HEALTH_STATUS_WARNING;
}

static void test_health_check_records_message(void) {
    health_check_t check;

    health_monitor_register_check("ut_msg", ut_health_with_message, 30);
    health_monitor_run_all_checks();
    TEST_ASSERT(health_monitor_get_check("ut_msg", &check));
    TEST_ASSERT_EQ_INT((int)check.last_status, (int)HEALTH_STATUS_CRITICAL);
    TEST_ASSERT(strcmp(check.last_message, "serial link down: no data for 45s") == 0);
    health_monitor_unregister_check("ut_msg");
}

static void test_health_check_default_message(void) {
    health_check_t check;

    health_monitor_register_check("ut_silent", ut_health_silent_warning, 30);
    health_monitor_run_all_checks();
    TEST_ASSERT(health_monitor_get_check("ut_silent", &check));
    TEST_ASSERT_EQ_INT((int)check.last_status, (int)HEALTH_STATUS_WARNING);
    /* No hard-coded success string; the default reflects the real status. */
    TEST_ASSERT(strstr(check.last_message, "Check completed successfully") == NULL);
    TEST_ASSERT(strstr(check.last_message, "WARNING") != NULL);
    health_monitor_unregister_check("ut_silent");
}

/* The /api/health handler maps the overall health onto an HTTP status code via
 * health_monitor_status_is_serving() so probes can react to the code alone.
 * HEALTHY and WARNING serve (200); CRITICAL and UNKNOWN do not (503), with
 * UNKNOWN failing safe before any check has run. */
static void test_health_status_is_serving(void) {
    TEST_ASSERT_EQ_INT(health_monitor_status_is_serving(HEALTH_STATUS_HEALTHY), 1);
    TEST_ASSERT_EQ_INT(health_monitor_status_is_serving(HEALTH_STATUS_WARNING), 1);
    TEST_ASSERT_EQ_INT(health_monitor_status_is_serving(HEALTH_STATUS_CRITICAL), 0);
    TEST_ASSERT_EQ_INT(health_monitor_status_is_serving(HEALTH_STATUS_UNKNOWN), 0);
}

/* A check-backed status reporter the test can steer, so we can drive the
 * monitor's overall status through every level. */
static health_status_t g_test_check_status = HEALTH_STATUS_HEALTHY;
static health_status_t test_steerable_check(char *message, size_t message_len) {
    (void)message; (void)message_len;
    return g_test_check_status;
}

/* With no checks registered the overall status is UNKNOWN and therefore not
 * serving; once a check runs, the overall status follows it and the serving
 * verdict tracks accordingly. This is the exact path /api/health takes. */
static void test_health_overall_reflects_checks(void) {
    health_check_t check;

    /* Fresh monitor: nothing registered yet -> UNKNOWN -> not serving. */
    TEST_ASSERT_EQ_INT(health_monitor_get_overall_status(), HEALTH_STATUS_UNKNOWN);
    TEST_ASSERT_EQ_INT(health_monitor_status_is_serving(
        health_monitor_get_overall_status()), 0);

    health_monitor_register_check("steerable", test_steerable_check, 30);

    g_test_check_status = HEALTH_STATUS_HEALTHY;
    health_monitor_run_check("steerable");
    TEST_ASSERT_EQ_INT(health_monitor_get_overall_status(), HEALTH_STATUS_HEALTHY);
    TEST_ASSERT_EQ_INT(health_monitor_status_is_serving(
        health_monitor_get_overall_status()), 1);

    g_test_check_status = HEALTH_STATUS_CRITICAL;
    health_monitor_run_check("steerable");
    TEST_ASSERT_EQ_INT(health_monitor_get_overall_status(), HEALTH_STATUS_CRITICAL);
    TEST_ASSERT_EQ_INT(health_monitor_status_is_serving(
        health_monitor_get_overall_status()), 0);

    /* The stored check carries the latest status for the JSON body too. */
    TEST_ASSERT_EQ_INT(health_monitor_get_check("steerable", &check), 1);
    TEST_ASSERT_EQ_INT(check.last_status, HEALTH_STATUS_CRITICAL);

    health_monitor_unregister_check("steerable");
}

/* ── Health monitor → metrics ───────────────────────────────────── */

static health_status_t ut_health_ok(char *message, size_t message_len) {
    (void)message; (void)message_len;
    return HEALTH_STATUS_HEALTHY;
}
static health_status_t ut_health_critical(char *message, size_t message_len) {
    (void)message; (void)message_len;
    return HEALTH_STATUS_CRITICAL;
}

/* The background health checks (serial link, SIP registration) are surfaced as
 * gauges so subsystem failures are alertable via /metrics. Verify the contract
 * the daemon's metrics tick relies on: each check publishes its last status,
 * the overall gauge is the worst of them, and the cumulative tallies advance. */
static void test_health_metrics_published(void) {
    metrics_init();

    health_monitor_register_check("ut_serial", ut_health_ok, 30);
    health_monitor_register_check("ut_sip", ut_health_critical, 60);
    health_monitor_run_all_checks();

    health_monitor_publish_metrics();

    /* Per-check status gauges mirror each check's last result. */
    TEST_ASSERT_EQ_INT((int)metrics_get_gauge("health_check_ut_serial_status"),
                       (int)HEALTH_STATUS_HEALTHY);
    TEST_ASSERT_EQ_INT((int)metrics_get_gauge("health_check_ut_sip_status"),
                       (int)HEALTH_STATUS_CRITICAL);

    /* Overall rollup is the worst status across all checks. */
    TEST_ASSERT_EQ_INT((int)metrics_get_gauge("health_overall_status"),
                       (int)HEALTH_STATUS_CRITICAL);

    /* Cumulative tallies: at least the two we ran, one of them failed. */
    TEST_ASSERT((int)metrics_get_gauge("health_checks_total") >= 2);
    TEST_ASSERT((int)metrics_get_gauge("health_checks_failed") >= 1);

    health_monitor_unregister_check("ut_serial");
    health_monitor_unregister_check("ut_sip");
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
    TEST_SUITE_RUN(test_config_validate_ex_reports_reason);
    TEST_SUITE_RUN(test_config_validate_ex_null_buffer);
    TEST_SUITE_RUN(test_config_validate_ranges);
    TEST_SUITE_RUN(test_config_validate_ports);
    TEST_SUITE_RUN(test_config_validate_port_conflict);
    TEST_SUITE_RUN(test_config_validate_enums);
    TEST_SUITE_RUN(test_config_trim);
    TEST_SUITE_RUN(test_config_null_safety);
    TEST_SUITE_RUN(test_config_overwrite_value);
    TEST_SUITE_RUN(test_config_load_from_file);

    TEST_SUITE_BEGIN("Display Manager");
    TEST_SUITE_RUN(test_display_short_line_static);
    TEST_SUITE_RUN(test_display_scroll_holds_at_start);
    TEST_SUITE_RUN(test_display_scroll_rearms_hold_on_wrap);

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
    TEST_SUITE_RUN(test_plugins_activation_metrics);
    TEST_SUITE_RUN(test_plugins_activate_nonexistent);
    TEST_SUITE_RUN(test_plugins_register_custom);
    TEST_SUITE_RUN(test_plugins_list);
    TEST_SUITE_RUN(test_plugins_duplicate_register);
    TEST_SUITE_RUN(test_plugins_dispatch_to_active);
    TEST_SUITE_RUN(test_plugins_dispatch_no_active);
    TEST_SUITE_RUN(test_plugins_dispatch_missing_handler);
    TEST_SUITE_RUN(test_plugins_builtins_registered);
    TEST_SUITE_RUN(test_plugins_get_info);
    TEST_SUITE_RUN(test_plugins_to_json);
    TEST_SUITE_RUN(test_plugins_to_json_escapes);
    TEST_SUITE_RUN(test_number_guess_compare);
    TEST_SUITE_RUN(test_operator_keypad_digit);
    TEST_SUITE_RUN(test_operator_shift_down);
    TEST_SUITE_RUN(test_wav_parse_valid);
    TEST_SUITE_RUN(test_wav_parse_rejects_bad);
    TEST_SUITE_RUN(test_wav_parse_skips_unknown_chunk);
    TEST_SUITE_RUN(test_plugin_display_lines_fit);

    TEST_SUITE_BEGIN("Plugin SDK");
    TEST_SUITE_RUN(test_sdk_rand_bounds);
    TEST_SUITE_RUN(test_sdk_rand_choice);
    TEST_SUITE_RUN(test_sdk_balance);
    TEST_SUITE_RUN(test_sdk_state);

    TEST_SUITE_BEGIN("Clock Seam");
    TEST_SUITE_RUN(test_clock_default_is_real_time);
    TEST_SUITE_RUN(test_clock_source_override);

    TEST_SUITE_BEGIN("Emergency Numbers");
    TEST_SUITE_RUN(test_free_number_911);
    TEST_SUITE_RUN(test_free_number_311);
    TEST_SUITE_RUN(test_free_number_0);
    TEST_SUITE_RUN(test_not_free_number);
    TEST_SUITE_RUN(test_free_number_custom);

    TEST_SUITE_BEGIN("Card Config");
    TEST_SUITE_RUN(test_card_enabled_default);
    TEST_SUITE_RUN(test_card_free_card_match);
    TEST_SUITE_RUN(test_card_free_card_no_match);
    TEST_SUITE_RUN(test_card_admin_card_match);
    TEST_SUITE_RUN(test_card_empty_list);

    TEST_SUITE_BEGIN("Updater");
    TEST_SUITE_RUN(test_version_compare_equal);
    TEST_SUITE_RUN(test_version_compare_major);
    TEST_SUITE_RUN(test_version_compare_minor);
    TEST_SUITE_RUN(test_version_compare_patch);
    TEST_SUITE_RUN(test_version_compare_with_v_prefix);
    TEST_SUITE_RUN(test_version_no_update_initially);
    TEST_SUITE_RUN(test_updater_apply_null_dir);
    TEST_SUITE_RUN(test_updater_apply_bad_dir);

    TEST_SUITE_BEGIN("Display Manager");
    TEST_SUITE_RUN(test_display_sanitizes_control_chars);
    TEST_SUITE_RUN(test_display_preserves_printable);
    TEST_SUITE_RUN(test_display_high_bit_bytes_untouched);

    TEST_SUITE_BEGIN("Logger");
    TEST_SUITE_RUN(test_logger_async_file_write);
    TEST_SUITE_RUN(test_logger_queue_stats);

    TEST_SUITE_BEGIN("Metrics Export");
    TEST_SUITE_RUN(test_metrics_export_prometheus_basic);
    TEST_SUITE_RUN(test_metrics_export_json_basic);
    TEST_SUITE_RUN(test_metrics_export_no_overflow);

    TEST_SUITE_BEGIN("Call Metrics");
    TEST_SUITE_RUN(test_call_duration_histogram);
    TEST_SUITE_RUN(test_call_ring_and_failure_metrics);

    TEST_SUITE_BEGIN("State Persistence");
    TEST_SUITE_RUN(test_state_load_round_trip);
    TEST_SUITE_RUN(test_state_save_relative_path);
    TEST_SUITE_RUN(test_state_save_nested_path);
    TEST_SUITE_RUN(test_state_load_absent_file_is_silent);
    TEST_SUITE_RUN(test_state_load_rejects_negative_cents);
    TEST_SUITE_RUN(test_state_load_rejects_huge_cents);
    TEST_SUITE_RUN(test_state_load_rejects_nonnumeric_cents);
    TEST_SUITE_RUN(test_state_load_accepts_boundary_cents);
    TEST_SUITE_RUN(test_state_load_rejects_bad_last_state);
    TEST_SUITE_RUN(test_state_load_rejects_control_chars_in_plugin);
    TEST_SUITE_RUN(test_state_load_ignores_unknown_keys);
    TEST_SUITE_RUN(test_state_load_null_safety);

    TEST_SUITE_BEGIN("Connection Queue");
    TEST_SUITE_RUN(test_conn_queue_fifo_order);
    TEST_SUITE_RUN(test_conn_queue_full_rejects);
    TEST_SUITE_RUN(test_conn_queue_wraps_around);
    TEST_SUITE_RUN(test_conn_queue_close_drains_then_signals);
    TEST_SUITE_RUN(test_conn_queue_null_safety);
    TEST_SUITE_RUN(test_conn_queue_blocking_pop_wakes);
    TEST_SUITE_RUN(test_conn_queue_stats);
    TEST_SUITE_RUN(test_conn_queue_stats_null_safety);

    TEST_SUITE_BEGIN("CLI");
    TEST_SUITE_RUN(test_cli_no_args_runs);
    TEST_SUITE_RUN(test_cli_config_long);
    TEST_SUITE_RUN(test_cli_config_short);
    TEST_SUITE_RUN(test_cli_config_equals);
    TEST_SUITE_RUN(test_cli_config_missing_value);
    TEST_SUITE_RUN(test_cli_config_empty_equals);
    TEST_SUITE_RUN(test_cli_help);
    TEST_SUITE_RUN(test_cli_version);
    TEST_SUITE_RUN(test_cli_unknown_arg);
    TEST_SUITE_RUN(test_cli_config_not_first);
    TEST_SUITE_RUN(test_cli_version_short_circuits);

    TEST_SUITE_BEGIN("Health Monitor");
    TEST_SUITE_RUN(test_health_check_records_message);
    TEST_SUITE_RUN(test_health_check_default_message);
    TEST_SUITE_RUN(test_health_status_is_serving);
    TEST_SUITE_RUN(test_health_overall_reflects_checks);

    TEST_SUITE_BEGIN("Health Metrics");
    TEST_SUITE_RUN(test_health_metrics_published);

    TEST_REPORT();
}
