#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include "plugin_sdk.h"
#include "plugins.h"
#include "display_manager.h"
#include "audio_tones.h"
#include "millennium_sdk.h"
#include "clock_source.h"
#include "logger.h"
#include "config.h"
#include "metrics.h"
#include "call_metrics.h"

/* Globals owned by the daemon / simulator / test harness. */
extern daemon_state_data_t *daemon_state;
extern millennium_client_t *client;

/* ── Time ────────────────────────────────────────────────────────────── */

time_t sdk_now(void) { return mclock_now(); }

int sdk_elapsed(time_t past) {
    time_t now = mclock_now();
    return now > past ? (int)(now - past) : 0;
}

/* ── Display ─────────────────────────────────────────────────────────── */

void sdk_display(const char *line1, const char *line2) {
    display_manager_set_text(line1, line2);
}

void sdk_display_line(const char *line1) {
    display_manager_set_text(line1, NULL);
}

void sdk_displayf(const char *fmt, ...) {
    char line1[DISPLAY_MAX_TEXT_LEN];   /* match the display line budget so a
                                         * long formatted line scrolls rather
                                         * than being truncated here first */
    va_list ap;
    if (!fmt) { display_manager_set_text(NULL, NULL); return; }
    va_start(ap, fmt);
    vsnprintf(line1, sizeof(line1), fmt, ap);
    va_end(ap);
    display_manager_set_text(line1, NULL);
}

/* ── Audio feedback ──────────────────────────────────────────────────── */

void sdk_beep(char key) { audio_tones_play_dtmf(key); }
void sdk_coin_chime(void) { audio_tones_play_coin_tone(); }
void sdk_dial_tone(void) { audio_tones_play_dial_tone(); }
void sdk_ringback(void) { audio_tones_play_ringback(); }
void sdk_busy_tone(void) { audio_tones_play_busy_tone(); }
void sdk_stop_audio(void) { audio_tones_stop(); }
int sdk_audio_is_playing(void) { return audio_tones_is_playing(); }

void sdk_play_clip(const char *name) {
    char path[512];
    const char *dir;
    size_t i;

    if (!name || !name[0]) return;
    /* Whitelist the name so it can't escape the clip directory. */
    for (i = 0; name[i] != '\0'; i++) {
        char ch = name[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) {
            return;
        }
    }

    dir = config_get_string(config_get_instance(), "audio.clip_dir",
                            "/usr/local/share/millennium/audio");
    snprintf(path, sizeof(path), "%s/%s.wav", dir, name);
    audio_tones_play_clip(path);
}

/* ── Calls ───────────────────────────────────────────────────────────── */

void sdk_call(const char *number) {
    if (client && number) millennium_client_call(client, number);
}
void sdk_answer(void) {
    if (client) millennium_client_answer_call(client);
}
void sdk_hangup(void) {
    if (client) millennium_client_hangup(client);
}
void sdk_send_dtmf(char key) {
    if (client) millennium_client_send_dtmf(client, key);
}

/* ── Phone state ─────────────────────────────────────────────────────── */

daemon_state_t sdk_state(void) {
    return daemon_state ? daemon_state->current_state : DAEMON_STATE_INVALID;
}

int sdk_receiver_is_up(void) {
    daemon_state_t s = sdk_state();
    return (s != DAEMON_STATE_IDLE_DOWN && s != DAEMON_STATE_INVALID) ? 1 : 0;
}

const char *sdk_keypad(void) {
    return daemon_state ? daemon_state->keypad_buffer : "";
}

void sdk_clear_keypad(void) {
    if (daemon_state) daemon_state_clear_keypad(daemon_state);
}

/* ── Coin balance ────────────────────────────────────────────────────── */

int sdk_balance(void) {
    return daemon_state ? daemon_state->inserted_cents : 0;
}

void sdk_add_balance(int cents) {
    if (cents != 0) plugins_adjust_inserted_cents(cents);
}

void sdk_spend_balance(int cents) {
    if (cents > 0) plugins_adjust_inserted_cents(-cents);
}

void sdk_clear_balance(void) {
    if (daemon_state && daemon_state->inserted_cents != 0) {
        plugins_adjust_inserted_cents(-daemon_state->inserted_cents);
    }
}

/* ── Session teardown (see plugin_sdk.h) ─────────────────────────────── */

void sdk_release_session(void) {
    daemon_state_t s = sdk_state();
    int in_call = (s == DAEMON_STATE_CALL_ACTIVE || s == DAEMON_STATE_CALL_INCOMING);

    sdk_stop_audio();

    if (in_call) {
        sdk_hangup();
        /* Resolve the call phase so the duration/miss metrics stay honest --
         * mirrors what handle_call_state_event does on EVENT_CALL_STATE_INVALID. */
        if (s == DAEMON_STATE_CALL_ACTIVE) {
            call_metrics_ended();
        } else {
            call_metrics_incoming_ended();
        }
        metrics_increment_counter("plugin_switch_calls_released", 1);
        logger_warn_with_category("Plugins",
                "Plugin switched during a call; releasing it");
    }

    sdk_clear_keypad();

    if (daemon_state && in_call) {
        /* Land in the idle state matching the physical handset, exactly as the
         * call-ended path does (see docs/EVENT_ORDERING.md). */
        daemon_state->current_state = daemon_state->handset_up
                ? DAEMON_STATE_IDLE_UP : DAEMON_STATE_IDLE_DOWN;
        daemon_state_update_activity(daemon_state);
    }
}

/* ── Logging ─────────────────────────────────────────────────────────── */

void sdk_log(const char *category, const char *msg) {
    logger_info_with_category(category ? category : "Plugin",
                              msg ? msg : "");
}

void sdk_logf(const char *category, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    if (!fmt) return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    logger_info_with_category(category ? category : "Plugin", buf);
}

/* ── Randomness ──────────────────────────────────────────────────────── */

static int sdk_rand_seeded = 0;

static void sdk_rand_ensure_seed(void) {
    if (!sdk_rand_seeded) {
        srand((unsigned int)time(NULL) ^ (unsigned int)(size_t)&sdk_rand_seeded);
        sdk_rand_seeded = 1;
    }
}

int sdk_rand_below(int n) {
    if (n <= 0) return 0;
    sdk_rand_ensure_seed();
    return rand() % n;
}

int sdk_rand_range(int lo, int hi) {
    if (hi < lo) { int t = lo; lo = hi; hi = t; }
    return lo + sdk_rand_below(hi - lo + 1);
}

const char *sdk_rand_choice(const char *const *choices, int n) {
    if (!choices || n <= 0) return NULL;
    return choices[sdk_rand_below(n)];
}
