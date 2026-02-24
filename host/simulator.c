#define _POSIX_C_SOURCE 200809L
#include "millennium_sdk.h"
#include "events.h"
#include "config.h"
#include "daemon_state.h"
#include "logger.h"
#include "metrics.h"
#include "event_processor.h"
#include "plugins.h"
#include "state_persistence.h"
#include "display_manager.h"
#include "audio_tones.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>

/* ── Simulated time ────────────────────────────────────────────────── */

static time_t sim_clock;
static int    sim_clock_initialized = 0;

#ifdef __linux__
extern time_t __real_time(time_t *tloc);

time_t __wrap_time(time_t *tloc) {
    if (sim_clock_initialized) {
        if (tloc) *tloc = sim_clock;
        return sim_clock;
    }
    return __real_time(tloc);
}
#endif

static void sim_time_init(void) {
    sim_clock = time(NULL);
    sim_clock_initialized = 1;
}

static void sim_time_advance(int seconds) {
    sim_clock += seconds;
}

/* ── Display capture ───────────────────────────────────────────────── */

static char  sim_display_line1[64];
static char  sim_display_line2[64];
static char  sim_display_raw[256];
static int   sim_display_count = 0;

static void sim_parse_display(const char *message) {
    const char *lf;
    size_t len;

    if (!message) return;

    strncpy(sim_display_raw, message, sizeof(sim_display_raw) - 1);
    sim_display_raw[sizeof(sim_display_raw) - 1] = '\0';

    lf = strchr(message, '\n');
    if (lf) {
        len = (size_t)(lf - message);
        if (len >= sizeof(sim_display_line1))
            len = sizeof(sim_display_line1) - 1;
        memcpy(sim_display_line1, message, len);
        sim_display_line1[len] = '\0';

        strncpy(sim_display_line2, lf + 1, sizeof(sim_display_line2) - 1);
        sim_display_line2[sizeof(sim_display_line2) - 1] = '\0';
    } else {
        strncpy(sim_display_line1, message, sizeof(sim_display_line1) - 1);
        sim_display_line1[sizeof(sim_display_line1) - 1] = '\0';
        sim_display_line2[0] = '\0';
    }

    /* Trim trailing spaces */
    len = strlen(sim_display_line1);
    while (len > 0 && sim_display_line1[len - 1] == ' ')
        sim_display_line1[--len] = '\0';
    len = strlen(sim_display_line2);
    while (len > 0 && sim_display_line2[len - 1] == ' ')
        sim_display_line2[--len] = '\0';

    sim_display_count++;
}

/* ── Call simulation state ─────────────────────────────────────────── */

static int sim_call_pending  = 0;
static int sim_call_active   = 0;
static int sim_hangup_called = 0;

/* ── Millennium SDK stubs ──────────────────────────────────────────── */

static millennium_client_t *sim_client = NULL;

/* Event queue — reused from the SDK, simplified */
static void sim_queue_push(millennium_client_t *c, void *ev) {
    struct event_queue_node *node = malloc(sizeof(struct event_queue_node));
    if (!node) return;
    node->event = ev;
    node->next  = NULL;
    if (c->event_queue_tail) {
        c->event_queue_tail->next = node;
    } else {
        c->event_queue_head = node;
    }
    c->event_queue_tail = node;
}

static void *sim_queue_pop(millennium_client_t *c) {
    struct event_queue_node *node;
    void *ev;
    if (!c->event_queue_head) return NULL;
    node = c->event_queue_head;
    ev   = node->event;
    c->event_queue_head = node->next;
    if (!c->event_queue_head) c->event_queue_tail = NULL;
    free(node);
    return ev;
}

millennium_client_t *millennium_client_create(void) {
    millennium_client_t *c = calloc(1, sizeof(millennium_client_t));
    if (!c) return NULL;
    c->display_fd  = -1;
    c->is_open     = 1;
    c->input_buffer = malloc(64);
    if (c->input_buffer) {
        c->input_buffer[0] = '\0';
        c->input_buffer_size = 0;
        c->input_buffer_capacity = 64;
    }
    sim_client = c;
    return c;
}

void millennium_client_destroy(millennium_client_t *c) {
    if (!c) return;
    if (c->input_buffer)    free(c->input_buffer);
    if (c->display_message) free(c->display_message);
    /* drain queue */
    while (c->event_queue_head) {
        void *ev = sim_queue_pop(c);
        if (ev) event_destroy((event_t *)ev);
    }
    free(c);
    if (sim_client == c) sim_client = NULL;
}

void millennium_client_close(millennium_client_t *c) {
    if (c) c->is_open = 0;
}

void millennium_client_set_display(millennium_client_t *c, const char *message) {
    (void)c;
    sim_parse_display(message);
    fprintf(stderr, "[DISPLAY] %s | %s\n", sim_display_line1, sim_display_line2);
}

void millennium_client_write_to_coin_validator(millennium_client_t *c, uint8_t data) {
    (void)c;
    fprintf(stderr, "[COIN_VALIDATOR] cmd=0x%02x ('%c')\n", data, data);
}

void millennium_client_update(millennium_client_t *c) { (void)c; }

void *millennium_client_next_event(millennium_client_t *c) {
    return sim_queue_pop(c);
}

void millennium_client_call(millennium_client_t *c, const char *number) {
    (void)c;
    fprintf(stderr, "[CALL] Dialing %s\n", number ? number : "(null)");
    sim_call_pending = 1;
}

void millennium_client_answer_call(millennium_client_t *c) {
    (void)c;
    fprintf(stderr, "[CALL] Answered\n");
}

void millennium_client_hangup(millennium_client_t *c) {
    (void)c;
    fprintf(stderr, "[CALL] Hangup\n");
    sim_call_active  = 0;
    sim_call_pending = 0;
    sim_hangup_called = 1;
}

int millennium_client_send_dtmf(millennium_client_t *c, char key) {
    (void)c;
    if (sim_call_active) fprintf(stderr, "[DTMF] %c\n", key);
    return sim_call_active ? 0 : -1;
}

void millennium_client_set_ua(millennium_client_t *c, void *ua) {
    (void)c; (void)ua;
}

void millennium_client_write_command(millennium_client_t *c, uint8_t cmd,
                                     const uint8_t *data, size_t sz) {
    (void)c; (void)cmd; (void)data; (void)sz;
}

int millennium_client_serial_is_healthy(millennium_client_t *c) {
    (void)c;
    return 1;
}

void millennium_client_check_serial(millennium_client_t *c) {
    (void)c;
}

void millennium_client_serial_activity(millennium_client_t *c) {
    (void)c;
}

/* Audio tone stubs */
void audio_tones_init(void) {}
void audio_tones_cleanup(void) {}
void audio_tones_play_dial_tone(void) { fprintf(stderr, "[TONE] Dial tone\n"); }
void audio_tones_play_dtmf(char key) { fprintf(stderr, "[TONE] DTMF %c\n", key); }
void audio_tones_play_ringback(void) { fprintf(stderr, "[TONE] Ringback\n"); }
void audio_tones_play_busy_tone(void) { fprintf(stderr, "[TONE] Busy\n"); }
void audio_tones_play_coin_tone(void) { fprintf(stderr, "[TONE] Coin\n"); }
void audio_tones_stop(void) { fprintf(stderr, "[TONE] Stop\n"); }
int audio_tones_is_playing(void) { return 0; }

void millennium_client_process_event_buffer(millennium_client_t *c) { (void)c; }

char *millennium_client_extract_payload(millennium_client_t *c, char t, size_t s) {
    (void)c; (void)t; (void)s;
    return NULL;
}

void millennium_client_create_and_queue_event_char(millennium_client_t *c, char t, const char *p) {
    (void)c; (void)t; (void)p;
}

void millennium_client_create_and_queue_event_ptr(millennium_client_t *c, void *ev) {
    if (c && ev) sim_queue_push(c, ev);
}

void millennium_client_write_to_display(millennium_client_t *c, const char *m) {
    (void)c; (void)m;
}

logger_level_t millennium_logger_parse_level(const char *s) { (void)s; return LOGGER_INFO; }
void millennium_logger_set_level(logger_level_t l) { (void)l; }
void millennium_logger_log(logger_level_t l, const char *m) { (void)l; (void)m; }
logger_level_t millennium_logger_get_current_level(void) { return LOGGER_INFO; }
void list_audio_devices(void) {}

/* ── Plugin stubs for plugins we don't test ────────────────────────── */

void register_jukebox_plugin(void) {
    plugins_register("Jukebox", "Coin-operated music player (stub)",
                     NULL, NULL, NULL, NULL, NULL, NULL, NULL);
}

/* ── Daemon globals (same as daemon.c) ─────────────────────────────── */

daemon_state_data_t *daemon_state  = NULL;
millennium_client_t *client        = NULL;
event_processor_t   *event_processor = NULL;
pthread_mutex_t      daemon_state_mutex = PTHREAD_MUTEX_INITIALIZER;
time_t               daemon_start_time;

/* ── Minimal event handlers (mirror daemon.c essentials) ───────────── */

static void sim_handle_coin(coin_event_t *ev) {
    char *code;
    int val = 0;

    if (!ev || !daemon_state) return;
    code = coin_event_get_coin_code(ev);
    if (!code) return;

    if (strcmp(code, "COIN_6") == 0)      val = 5;
    else if (strcmp(code, "COIN_7") == 0) val = 10;
    else if (strcmp(code, "COIN_8") == 0) val = 25;

    if (val > 0 && daemon_state->current_state == DAEMON_STATE_IDLE_UP) {
        daemon_state->inserted_cents += val;
        daemon_state_update_activity(daemon_state);
        metrics_increment_counter("coins_inserted", 1);
        metrics_increment_counter("coins_value_cents", val);
        plugins_handle_coin(val, code);
    }
    free(code);
}

static void sim_handle_hook(hook_state_change_event_t *ev) {
    int hook_up, hook_down;
    if (!ev || !daemon_state) return;

    hook_up   = (hook_state_change_event_get_direction(ev) == 'U');
    hook_down = (hook_state_change_event_get_direction(ev) == 'D');

    if (hook_up) {
        if (daemon_state->current_state == DAEMON_STATE_CALL_INCOMING) {
            daemon_state->current_state = DAEMON_STATE_CALL_ACTIVE;
        } else if (daemon_state->current_state == DAEMON_STATE_IDLE_DOWN) {
            daemon_state->current_state = DAEMON_STATE_IDLE_UP;
            daemon_state->inserted_cents = 0;
            daemon_state_clear_keypad(daemon_state);
        }
        daemon_state_update_activity(daemon_state);
    } else if (hook_down) {
        daemon_state_clear_keypad(daemon_state);
        daemon_state->inserted_cents = 0;
        daemon_state->current_state = DAEMON_STATE_IDLE_DOWN;
        daemon_state_update_activity(daemon_state);
    }

    plugins_handle_hook(hook_up, hook_down);

    if (hook_down) {
        millennium_client_hangup(client);
    }
}

static void sim_handle_keypad(keypad_event_t *ev) {
    char key;
    int in_call;
    if (!ev || !daemon_state) return;
    key = keypad_event_get_key(ev);
    in_call = (daemon_state->current_state == DAEMON_STATE_CALL_ACTIVE ||
               daemon_state->current_state == DAEMON_STATE_CALL_INCOMING);

    if (isdigit(key) &&
        daemon_state->current_state == DAEMON_STATE_IDLE_UP &&
        daemon_state_get_keypad_length(daemon_state) < 10) {

        daemon_state_add_key(daemon_state, key);
        daemon_state_update_activity(daemon_state);
        plugins_handle_keypad(key);
    } else if (in_call && (isdigit(key) || key == '*' || key == '#')) {
        /* #95: Pass keys to plugin for DTMF during call */
        plugins_handle_keypad(key);
    }
}

static void sim_handle_call_state(call_state_event_t *ev) {
    call_state_t st;
    int phone_down, phone_up;
    if (!ev || !daemon_state) return;
    st = call_state_event_get_state(ev);
    phone_down = (daemon_state->current_state == DAEMON_STATE_IDLE_DOWN);
    phone_up = (daemon_state->current_state == DAEMON_STATE_IDLE_UP);

    if (st == EVENT_CALL_STATE_INCOMING && (phone_down || phone_up)) {
        /* #92: Accept incoming when handset down or up */
        daemon_state->current_state = DAEMON_STATE_CALL_INCOMING;
        daemon_state_update_activity(daemon_state);
    } else if (st == EVENT_CALL_STATE_ACTIVE) {
        daemon_state->current_state = DAEMON_STATE_CALL_ACTIVE;
        daemon_state_update_activity(daemon_state);
    } else if (st == EVENT_CALL_STATE_INVALID) {
        /* #90: Remote hung up */
        if (daemon_state->current_state == DAEMON_STATE_CALL_ACTIVE ||
            daemon_state->current_state == DAEMON_STATE_CALL_INCOMING) {
            daemon_state_clear_keypad(daemon_state);
            daemon_state->inserted_cents = 0;
            daemon_state->current_state = DAEMON_STATE_IDLE_UP;
            daemon_state_update_activity(daemon_state);
            sim_call_active = 0;
        }
    }

    plugins_handle_call_state(st);
}

static void sim_handle_card(card_event_t *ev) {
    if (!ev || !daemon_state) return;
    fprintf(stderr, "[CARD] Swipe: %.16s\n", ev->card_number);
    plugins_handle_card(ev->card_number);
}

/* Process one event through the handlers */
static void sim_process_event(event_t *ev) {
    if (!ev) return;
    switch (ev->type) {
    case EVENT_COIN:
        sim_handle_coin((coin_event_t *)ev);
        break;
    case EVENT_HOOK_STATE_CHANGE:
        sim_handle_hook((hook_state_change_event_t *)ev);
        break;
    case EVENT_KEYPAD:
        sim_handle_keypad((keypad_event_t *)ev);
        break;
    case EVENT_CALL_STATE:
        sim_handle_call_state((call_state_event_t *)ev);
        break;
    case EVENT_CARD:
        sim_handle_card((card_event_t *)ev);
        break;
    default:
        break;
    }
}

/* Drain the event queue (e.g. after a call triggers an auto-event) */
static void sim_drain_events(void) {
    void *ev;
    while ((ev = millennium_client_next_event(client)) != NULL) {
        sim_process_event((event_t *)ev);
        event_destroy((event_t *)ev);
    }
}

/* If a call was placed by the plugin, auto-generate CALL_ACTIVE event */
static void sim_check_pending_call(void) {
    call_state_event_t *ev;
    if (!sim_call_pending) return;
    sim_call_pending = 0;
    sim_call_active  = 1;

    ev = call_state_event_create("CALL_ESTABLISHED", NULL, EVENT_CALL_STATE_ACTIVE);
    if (ev) {
        sim_process_event((event_t *)ev);
        event_destroy((event_t *)ev);
    }
}

/* ── Scenario helpers ──────────────────────────────────────────────── */

static char *trim(char *s) {
    char *end;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

static int display_contains(const char *text) {
    if (strstr(sim_display_line1, text)) return 1;
    if (strstr(sim_display_line2, text)) return 1;
    return 0;
}

/* ── Scenario runner ───────────────────────────────────────────────── */

static int run_scenario(const char *path) {
    FILE *f;
    char line[512];
    int line_num = 0;
    int failures = 0;
    char *cmd;
    char *arg;

    f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "ERROR: cannot open scenario file: %s\n", path);
        return 1;
    }

    while (fgets(line, sizeof(line), f)) {
        line_num++;
        cmd = trim(line);

        /* skip blanks and comments */
        if (cmd[0] == '\0' || cmd[0] == '#') continue;

        fprintf(stderr, "[SCENARIO:%d] %s\n", line_num, cmd);

        /* ── hook_up / hook_down ─────────────────────────────────── */
        if (strcmp(cmd, "hook_up") == 0) {
            hook_state_change_event_t *ev = hook_state_change_event_create('U');
            if (ev) { sim_process_event((event_t *)ev); event_destroy((event_t *)ev); }
            sim_check_pending_call();
            sim_drain_events();
        }
        else if (strcmp(cmd, "hook_down") == 0) {
            hook_state_change_event_t *ev = hook_state_change_event_create('D');
            if (ev) { sim_process_event((event_t *)ev); event_destroy((event_t *)ev); }
            sim_drain_events();
        }

        /* ── coin <5|10|25> ──────────────────────────────────────── */
        else if (strncmp(cmd, "coin ", 5) == 0) {
            int cents = atoi(cmd + 5);
            uint8_t code;
            coin_event_t *ev;
            if      (cents == 5)  code = 0x36;
            else if (cents == 10) code = 0x37;
            else if (cents == 25) code = 0x38;
            else {
                fprintf(stderr, "  ERROR: invalid coin value: %d\n", cents);
                failures++;
                continue;
            }
            ev = coin_event_create(code);
            if (ev) { sim_process_event((event_t *)ev); event_destroy((event_t *)ev); }
            sim_check_pending_call();
            sim_drain_events();
        }

        /* ── key <digit> ─────────────────────────────────────────── */
        else if (strncmp(cmd, "key ", 4) == 0) {
            char key = cmd[4];
            keypad_event_t *ev;
            if (!isdigit(key)) {
                fprintf(stderr, "  ERROR: invalid key: %c\n", key);
                failures++;
                continue;
            }
            ev = keypad_event_create(key);
            if (ev) { sim_process_event((event_t *)ev); event_destroy((event_t *)ev); }
            sim_check_pending_call();
            sim_drain_events();
        }

        /* ── keys <digits...> (shorthand for multiple key presses) ─ */
        else if (strncmp(cmd, "keys ", 5) == 0) {
            const char *p = cmd + 5;
            while (*p) {
                if (isdigit(*p)) {
                    keypad_event_t *ev = keypad_event_create(*p);
                    if (ev) { sim_process_event((event_t *)ev); event_destroy((event_t *)ev); }
                    sim_check_pending_call();
                    sim_drain_events();
                }
                p++;
            }
        }

        /* ── card <number> ──────────────────────────────────────── */
        else if (strncmp(cmd, "card ", 5) == 0) {
            const char *card_num = cmd + 5;
            card_event_t *ev = card_event_create(card_num);
            if (ev) { sim_process_event((event_t *)ev); event_destroy((event_t *)ev); }
            sim_drain_events();
        }

        /* ── call_incoming / call_active ─────────────────────────── */
        else if (strcmp(cmd, "call_incoming") == 0) {
            call_state_event_t *ev = call_state_event_create(
                "CALL_INCOMING", NULL, EVENT_CALL_STATE_INCOMING);
            if (ev) { sim_process_event((event_t *)ev); event_destroy((event_t *)ev); }
            sim_drain_events();
        }
        else if (strcmp(cmd, "call_active") == 0) {
            call_state_event_t *ev = call_state_event_create(
                "CALL_ESTABLISHED", NULL, EVENT_CALL_STATE_ACTIVE);
            if (ev) { sim_process_event((event_t *)ev); event_destroy((event_t *)ev); }
            sim_call_active = 1;
            sim_drain_events();
        }
        else if (strcmp(cmd, "call_ended") == 0) {
            call_state_event_t *ev = call_state_event_create(
                "CALL_CLOSED", NULL, EVENT_CALL_STATE_INVALID);
            if (ev) { sim_process_event((event_t *)ev); event_destroy((event_t *)ev); }
            sim_call_active = 0;
            sim_drain_events();
        }

        /* ── wait <seconds> ──────────────────────────────────────── */
        else if (strncmp(cmd, "wait ", 5) == 0) {
            int secs = atoi(cmd + 5);
            int i;
            fprintf(stderr, "  advancing %d seconds...\n", secs);
            for (i = 0; i < secs; i++) {
#ifdef __linux__
                sim_time_advance(1);
#else
                { struct timespec ts = {1, 0}; nanosleep(&ts, NULL); }
                sim_time_advance(1);
#endif
                plugins_tick();
                display_manager_tick();
                sim_drain_events();
                if (sim_hangup_called) {
                    sim_hangup_called = 0;
                    daemon_state->current_state = DAEMON_STATE_IDLE_DOWN;
                }
            }
        }

        /* ── assert_display <text> ───────────────────────────────── */
        else if (strncmp(cmd, "assert_display ", 15) == 0) {
            arg = trim(cmd + 15);
            if (display_contains(arg)) {
                fprintf(stderr, "  PASS: display contains \"%s\"\n", arg);
            } else {
                fprintf(stderr, "  FAIL: display does NOT contain \"%s\"\n", arg);
                fprintf(stderr, "        line1: \"%s\"\n", sim_display_line1);
                fprintf(stderr, "        line2: \"%s\"\n", sim_display_line2);
                failures++;
            }
        }

        /* ── assert_no_display <text> ────────────────────────────── */
        else if (strncmp(cmd, "assert_no_display ", 18) == 0) {
            arg = trim(cmd + 18);
            if (!display_contains(arg)) {
                fprintf(stderr, "  PASS: display does not contain \"%s\"\n", arg);
            } else {
                fprintf(stderr, "  FAIL: display DOES contain \"%s\"\n", arg);
                fprintf(stderr, "        line1: \"%s\"\n", sim_display_line1);
                fprintf(stderr, "        line2: \"%s\"\n", sim_display_line2);
                failures++;
            }
        }

        /* ── assert_state <state_name> ───────────────────────────── */
        else if (strncmp(cmd, "assert_state ", 13) == 0) {
            const char *actual = daemon_state_to_string(daemon_state->current_state);
            arg = trim(cmd + 13);
            if (strstr(actual, arg)) {
                fprintf(stderr, "  PASS: state is %s\n", actual);
            } else {
                fprintf(stderr, "  FAIL: expected state containing \"%s\", got \"%s\"\n",
                        arg, actual);
                failures++;
            }
        }

        /* ── print (debug) ───────────────────────────────────────── */
        else if (strcmp(cmd, "print") == 0) {
            fprintf(stderr, "  state=%s  coins=%d  display=\"%s\" | \"%s\"\n",
                    daemon_state_to_string(daemon_state->current_state),
                    daemon_state->inserted_cents,
                    sim_display_line1, sim_display_line2);
        }

        /* ── save_state <filepath> ─────────────────────────────── */
        else if (strncmp(cmd, "save_state ", 11) == 0) {
            persisted_state_t ps;
            arg = trim(cmd + 11);
            ps.inserted_cents = daemon_state->inserted_cents;
            ps.last_state = (int)daemon_state->current_state;
            strncpy(ps.active_plugin,
                    plugins_get_active_name() ? plugins_get_active_name() : "",
                    sizeof(ps.active_plugin) - 1);
            ps.active_plugin[sizeof(ps.active_plugin) - 1] = '\0';
            state_persistence_save(&ps, arg);
        }

        /* ── load_state <filepath> ─────────────────────────────── */
        else if (strncmp(cmd, "load_state ", 11) == 0) {
            persisted_state_t ps;
            arg = trim(cmd + 11);
            if (state_persistence_load(&ps, arg) == 0) {
                daemon_state->inserted_cents = ps.inserted_cents;
                daemon_state->current_state = (daemon_state_t)ps.last_state;
                if (strlen(ps.active_plugin) > 0) {
                    plugins_activate(ps.active_plugin);
                }
                plugins_tick();
                sim_drain_events();
                fprintf(stderr, "  loaded: coins=%d plugin=%s last_state=%d\n",
                        ps.inserted_cents, ps.active_plugin, ps.last_state);
            } else {
                fprintf(stderr, "  FAIL: could not load state from %s\n", arg);
                failures++;
            }
        }

        /* ── set_display <line1>|<line2> ─────────────────────────── */
        else if (strncmp(cmd, "set_display ", 12) == 0) {
            char buf[512];
            char *sep;
            strncpy(buf, cmd + 12, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            sep = strchr(buf, '|');
            if (sep) {
                *sep = '\0';
                display_manager_set_text(buf, sep + 1);
            } else {
                display_manager_set_text(buf, "");
            }
        }

        /* ── tick_display [N] ───────────────────────────────────── */
        else if (strncmp(cmd, "tick_display", 12) == 0) {
            int count = 1;
            if (cmd[12] == ' ') count = atoi(cmd + 13);
            if (count < 1) count = 1;
            { int t; for (t = 0; t < count; t++) display_manager_tick(); }
            sim_drain_events();
        }

        /* ── config <key> <value> ──────────────────────────────── */
        else if (strncmp(cmd, "config ", 7) == 0) {
            char cfg_key[256];
            char cfg_val[256];
            const char *p = cmd + 7;
            int ki = 0;
            while (*p && *p != ' ' && ki < 255) { cfg_key[ki++] = *p++; }
            cfg_key[ki] = '\0';
            while (*p == ' ') p++;
            strncpy(cfg_val, p, sizeof(cfg_val) - 1);
            cfg_val[sizeof(cfg_val) - 1] = '\0';
            config_set_value(config_get_instance(), cfg_key, cfg_val);
            fprintf(stderr, "  config: %s = %s\n", cfg_key, cfg_val);
        }

        /* ── unknown ─────────────────────────────────────────────── */
        else {
            fprintf(stderr, "  WARNING: unknown command: %s\n", cmd);
        }
    }

    fclose(f);
    return failures;
}

/* ── main ──────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    config_data_t *config;
    int total_failures = 0;
    int i;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <scenario.test> [scenario2.test ...]\n", argv[0]);
        return 1;
    }

    /* Initialize subsystems */
    config = config_get_instance();
    config_set_default_values(config);
    config_set_value(config, "call.timeout_seconds", "5");
    config_set_value(config, "call.cost_cents", "50");

    logger_set_level(LOG_LEVEL_WARN);

    daemon_start_time = time(NULL);

    daemon_state = calloc(1, sizeof(daemon_state_data_t));
    if (!daemon_state) { fprintf(stderr, "FATAL: alloc\n"); return 1; }
    daemon_state_init(daemon_state);

    if (metrics_init() != 0) { fprintf(stderr, "FATAL: metrics_init\n"); return 1; }

    client = millennium_client_create();
    if (!client) { fprintf(stderr, "FATAL: client\n"); return 1; }

    display_manager_init(client);
    plugins_init();

    sim_time_init();

    /* Run each scenario file */
    for (i = 1; i < argc; i++) {
        int failures;

        fprintf(stderr, "\n══════════════════════════════════════\n");
        fprintf(stderr, "  SCENARIO: %s\n", argv[i]);
        fprintf(stderr, "══════════════════════════════════════\n\n");

        /* Reset state between scenarios */
        daemon_state_init(daemon_state);
        sim_display_line1[0] = '\0';
        sim_display_line2[0] = '\0';
        sim_display_count    = 0;
        sim_call_pending     = 0;
        sim_call_active      = 0;
        sim_hangup_called    = 0;
        sim_time_init();
        plugins_activate("Classic Phone");

        failures = run_scenario(argv[i]);
        if (failures > 0) {
            fprintf(stderr, "\n  RESULT: %d assertion(s) FAILED\n", failures);
        } else {
            fprintf(stderr, "\n  RESULT: ALL PASSED\n");
        }
        total_failures += failures;
    }

    /* Cleanup */
    plugins_cleanup();
    millennium_client_destroy(client);
    metrics_cleanup();
    free(daemon_state);

    fprintf(stderr, "\n");
    if (total_failures > 0) {
        fprintf(stderr, "TOTAL: %d failure(s)\n", total_failures);
        return 1;
    }
    fprintf(stderr, "TOTAL: ALL SCENARIOS PASSED\n");
    return 0;
}
