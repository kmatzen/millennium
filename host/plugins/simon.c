#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../plugins.h"
#include "../plugin_sdk.h"
#include "../config.h"

/*
 * Simon — a free-to-play memory game.
 *
 * The phone plays back a growing sequence of keys (1-4) as tones; repeat it
 * on the keypad. Each round adds one key. Miss one and it shows your score.
 * No coins required — a deliberate contrast with the coin-operated plugins,
 * proving the platform handles both.
 *
 * Config (optional):
 *   simon.seq   force the sequence, e.g. "1342" (default = random).
 *               Used by scenario tests for determinism.
 */

#define SIMON_CAT "Simon"
#define SIMON_MAX 16        /* longest sequence we track */
#define SIMON_NOTE_SECS 1   /* playback pace (time() has 1s resolution) */

#define S_IDLE  0   /* waiting for a key to start */
#define S_SHOW  1   /* playing back the sequence */
#define S_INPUT 2   /* player repeating the sequence */
#define S_OVER  3   /* game over, brief pause then back to idle */

typedef struct {
    int phase;
    char seq[SIMON_MAX + 1];
    int seq_len;
    int show_idx;       /* next note to reveal during S_SHOW */
    int input_idx;      /* next note expected during S_INPUT */
    int round;          /* completed rounds = score */
    time_t next_at;     /* time of next timed transition */
} simon_t;

static simon_t sm;

static const char *simon_forced(void) {
    return config_get_string(config_get_instance(), "simon.seq", "");
}

/* Choose the note at position idx: from the forced sequence if available,
 * otherwise random in '1'..'4'. */
static char simon_note_for(int idx) {
    const char *forced = simon_forced();
    if (forced && (int)strlen(forced) > idx) return forced[idx];
    return (char)('1' + sdk_rand_below(4));
}

static void simon_show_idle(void) {
    sdk_display("Simon", "Press 1-4 to play");
}

static void simon_begin(void) {
    sm.seq_len = 1;
    sm.seq[0] = simon_note_for(0);
    sm.seq[1] = '\0';
    sm.round = 0;
    sm.show_idx = 0;
    sm.phase = S_SHOW;
    sm.next_at = time(NULL); /* reveal first note on the next tick */
    sdk_log(SIMON_CAT, "Game started");
}

static void simon_next_round(void) {
    if (sm.seq_len < SIMON_MAX) {
        sm.seq[sm.seq_len] = simon_note_for(sm.seq_len);
        sm.seq_len++;
        sm.seq[sm.seq_len] = '\0';
    }
    sm.show_idx = 0;
    sm.phase = S_SHOW;
    sm.next_at = time(NULL);
}

static void simon_enter_input(void) {
    sm.input_idx = 0;
    sm.phase = S_INPUT;
    sdk_display("Your turn", "Repeat the tune");
}

static void simon_game_over(void) {
    char l2[21];
    snprintf(l2, sizeof(l2), "Score: %d", sm.round);
    sdk_display("Game Over", l2);
    sdk_logf(SIMON_CAT, "Game over, score %d", sm.round);
    sm.phase = S_OVER;
    sm.next_at = time(NULL) + 3;
}

/* ── Plugin callbacks ────────────────────────────────────────────────── */

static int simon_handle_keypad(char key) {
    if (sm.phase == S_IDLE) {
        if (key >= '1' && key <= '4') simon_begin();
        return 0;
    }
    if (sm.phase != S_INPUT) return 0; /* ignore input while watching/over */

    if (key < '1' || key > '4') return 0; /* only 1-4 are notes */

    if (key == sm.seq[sm.input_idx]) {
        sdk_beep(key);
        sm.input_idx++;
        if (sm.input_idx >= sm.seq_len) {
            sm.round++;
            sdk_coin_chime();
            simon_next_round();
        }
    } else {
        simon_game_over();
    }
    return 0;
}

static int simon_handle_hook(int hook_up, int hook_down) {
    (void)hook_up; (void)hook_down;
    sm.phase = S_IDLE;
    simon_show_idle();
    return 0;
}

static void simon_handle_activation(void) {
    sm.phase = S_IDLE;
    simon_show_idle();
}

static void simon_handle_tick(void) {
    time_t now = time(NULL);

    if (sm.phase == S_SHOW) {
        if (now < sm.next_at) return;
        if (sm.show_idx < sm.seq_len) {
            char note = sm.seq[sm.show_idx];
            char l2[21];
            snprintf(l2, sizeof(l2), "Note: %c", note);
            sdk_display("Watch...", l2);
            sdk_beep(note);
            sm.show_idx++;
            sm.next_at = now + SIMON_NOTE_SECS;
        } else {
            simon_enter_input();
        }
    } else if (sm.phase == S_OVER) {
        if (now >= sm.next_at) {
            sm.phase = S_IDLE;
            simon_show_idle();
        }
    }
}

void register_simon_plugin(void) {
    memset(&sm, 0, sizeof(sm));
    sm.phase = S_IDLE;

    plugins_register("Simon",
                     "Free memory game - repeat the growing tune (keys 1-4)",
                     NULL,
                     simon_handle_keypad,
                     simon_handle_hook,
                     NULL,
                     NULL,
                     simon_handle_activation,
                     simon_handle_tick);
}
