#ifndef PLUGIN_SDK_H
#define PLUGIN_SDK_H

/*
 * Plugin SDK — the friendly facade for writing Millennium plugins.
 *
 * A plugin is just a set of callbacks (see plugins.h: handle_coin,
 * handle_keypad, handle_hook, handle_call_state, handle_card,
 * handle_activation, handle_tick) registered with plugins_register().
 * Inside those callbacks you drive the phone hardware. Historically that
 * meant reaching into a tangle of globals (`client`, `daemon_state`),
 * display_manager_*, audio_tones_*, logger_*, and so on.
 *
 * This SDK wraps all of that behind one small, stable, documented surface so
 * experimenters can focus on behaviour, not plumbing. Everything here is a
 * thin forwarder to an existing subsystem, is NULL-safe, and is a harmless
 * no-op when the underlying hardware is unavailable (e.g. audio on macOS,
 * or before the daemon is fully wired up). That means the same plugin code
 * runs unchanged on the Pi, in the scenario simulator, and in unit tests.
 *
 * A minimal plugin:
 *
 *   #include "../plugin_sdk.h"
 *
 *   static int on_key(char key) {
 *       sdk_beep(key);                 // DTMF feedback
 *       sdk_display("You pressed", &key); // (use a real string in practice)
 *       return 0;
 *   }
 *   static void on_activate(void) { sdk_display("Hello!", "Press any key"); }
 *
 *   void register_hello_plugin(void) {
 *       plugins_register("Hello", "Minimal demo",
 *           NULL, on_key, NULL, NULL, NULL, on_activate, NULL);
 *   }
 */

#include <time.h>
#include "daemon_state.h"

/* ── Time ────────────────────────────────────────────────────────────────
 * Always read the clock through these instead of time(NULL). They honor the
 * scenario simulator's advanceable clock, so a plugin's timing logic (e.g.
 * "reveal the answer 2 seconds later") behaves identically on the Pi and in
 * the instant, no-sleep scenario tests. */

/* Current time in seconds, like time(NULL) but simulator-aware. */
time_t sdk_now(void);

/* Whole seconds elapsed since `past` (clamped to >= 0). Equivalent to
 * sdk_now() - past, the idiom plugins use to age timestamps. */
int sdk_elapsed(time_t past);

/* ── Display ─────────────────────────────────────────────────────────────
 * The VFD is two lines of 20 chars. Lines longer than 20 chars auto-scroll.
 * Pass NULL for a line to clear it. */

/* Set both display lines. */
void sdk_display(const char *line1, const char *line2);

/* Set the first line, clear the second. */
void sdk_display_line(const char *line1);

/* printf-style convenience that formats into the two lines.
 * sdk_displayf("Score: %d", n) sets line1; line2 is cleared. */
void sdk_displayf(const char *fmt, ...);

/* ── Audio feedback ──────────────────────────────────────────────────────
 * Short tones auto-stop; continuous tones play until sdk_stop_audio().
 * All are no-ops if the audio device is unavailable. */

void sdk_beep(char key);          /* DTMF tone for a keypad key (~150ms) */
void sdk_coin_chime(void);        /* coin-accepted chime (~200ms) */
void sdk_dial_tone(void);         /* continuous dial tone */
void sdk_ringback(void);          /* continuous ringback cadence */
void sdk_busy_tone(void);         /* continuous busy cadence */
void sdk_stop_audio(void);        /* stop any continuous tone */
int  sdk_audio_is_playing(void);  /* 1 if a tone is currently sounding */

/* Play a recorded clip by logical name (letters/digits/_/- only, no path or
 * extension). Resolves to <audio.clip_dir>/<name>.wav (config key
 * "audio.clip_dir", default /usr/local/share/millennium/audio). Clips should be
 * 16-bit PCM WAV, ideally 8 kHz mono to match the narrowband audio hardware.
 * Interrupts any sound currently playing; a missing/unsupported file is a
 * harmless no-op that leaves the current sound untouched, so it can be layered
 * after a fallback tone. See host/AUDIO_CLIPS.md. */
void sdk_play_clip(const char *name);

/* ── Calls (VoIP) ────────────────────────────────────────────────────────
 * Place/answer/end calls and send in-call DTMF. No-ops if SIP isn't ready. */

void sdk_call(const char *number);
void sdk_answer(void);
void sdk_hangup(void);
void sdk_send_dtmf(char key);

/* ── Phone state ─────────────────────────────────────────────────────────*/

/* Current state-machine state (INVALID/IDLE_DOWN/IDLE_UP/CALL_INCOMING/
 * CALL_ACTIVE). Returns DAEMON_STATE_INVALID if state is unavailable. */
daemon_state_t sdk_state(void);

/* 1 when the handset is lifted (any state other than IDLE_DOWN/INVALID). */
int sdk_receiver_is_up(void);

/* The digits the user has typed (daemon-level buffer). Never NULL. */
const char *sdk_keypad(void);

/* Clear the daemon's shared dial buffer. Apps that keep their own per-field
 * entry call this so /api/state's keypad buffer doesn't accumulate every key. */
void sdk_clear_keypad(void);

/* ── Coin balance ────────────────────────────────────────────────────────
 * The daemon tracks a shared inserted-cents balance. These helpers read and
 * adjust it while keeping the daemon's view in sync (so the dashboard, coin
 * return, and persistence stay correct). A plugin may instead keep its own
 * private counter — both styles work; pick one and be consistent. */

int  sdk_balance(void);            /* current inserted cents (>= 0) */
void sdk_add_balance(int cents);   /* credit (e.g. on a coin event) */
void sdk_spend_balance(int cents); /* debit (e.g. when charging for a play) */
void sdk_clear_balance(void);      /* zero the balance (e.g. coin return) */

/* ── Logging ─────────────────────────────────────────────────────────────
 * `category` is a short tag shown in logs (use your plugin name). */

void sdk_log(const char *category, const char *msg);
void sdk_logf(const char *category, const char *fmt, ...);

/* ── Randomness ──────────────────────────────────────────────────────────
 * Seeded once automatically on first use. */

int sdk_rand_below(int n);            /* uniform-ish in [0, n); 0 if n<=0 */
int sdk_rand_range(int lo, int hi);   /* inclusive [lo, hi] */

/* Pick a random element from an array of strings (NULL if n<=0). */
const char *sdk_rand_choice(const char *const *choices, int n);

/* ── Session teardown (daemon-internal) ──────────────────────────────────
 * Release anything the phone is holding on behalf of the ACTIVE plugin: hang
 * up a call in progress, stop any continuous tone, clear the keypad, and drop
 * back to the idle state matching the physical handset.
 *
 * Called by plugins_activate when switching between two different plugins
 * (#223). The plugin API has no handle_deactivation hook, so an outgoing
 * plugin gets no chance to clean up after itself -- switching mid-call used to
 * leave the SIP call up, the audio running, and the daemon in CALL_ACTIVE,
 * while the outgoing plugin forgot it ever had a call. Rather than obliging
 * every plugin author to implement teardown correctly, the daemon releases the
 * resources it owns.
 *
 * The coin balance is deliberately NOT cleared: an operator switching plugins
 * should not swallow the customer's money, and the incoming plugin reads the
 * balance from daemon_state on activation.
 *
 * Not part of the plugin-facing API -- plugins should not call this. */
void sdk_release_session(void);

#endif /* PLUGIN_SDK_H */
