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
