#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../plugins.h"
#include "../plugin_sdk.h"
#include "../config.h"

/*
 * Number Guess (Hi-Lo) — a coin-operated guessing game.
 *
 * Insert a coin to start; the machine picks a secret number in [1, 99].
 * Type a 1-2 digit guess and press '#' to submit. The display tells you
 * "Higher" or "Lower" until you nail it, then chimes. '*' clears your entry.
 *
 * This plugin is written entirely against plugin_sdk.h — it never touches a
 * global or a subsystem directly — so it doubles as the reference example for
 * authoring new plugins.
 *
 * Config (all optional):
 *   guess.cost_cents   price of one game        (default 25)
 *   guess.secret       force the secret 1..99   (default 0 = random)
 *                      Used by scenario tests for determinism.
 */

#define GUESS_CAT "NumberGuess"
#define GUESS_MIN 1
#define GUESS_MAX 99

#define NG_PLAY_IDLE 0   /* waiting for coins */
#define NG_PLAY_LIVE 1   /* secret chosen, accepting guesses */
#define NG_PLAY_WON  2   /* celebrating; reset after a short delay */

typedef struct {
    int cost_cents;
    int secret;
    int guesses;
    int phase;
    char entry[3];   /* up to 2 digits + NUL */
    int entry_len;
    time_t reset_at; /* when phase==WON, return to idle at this time */
} number_guess_t;

static number_guess_t ng;

static int ng_cost(void) {
    return config_get_int(config_get_instance(), "guess.cost_cents", 25);
}

static int ng_pick_secret(void) {
    int forced = config_get_int(config_get_instance(), "guess.secret", 0);
    if (forced >= GUESS_MIN && forced <= GUESS_MAX) return forced;
    return sdk_rand_range(GUESS_MIN, GUESS_MAX);
}

static void ng_show_prompt(void) {
    if (!sdk_receiver_is_up()) {
        sdk_display("Lift receiver", "to play Hi-Lo");
        return;
    }
    {
        int bal = sdk_balance();
        if (bal >= ng.cost_cents) {
            sdk_display("Insert coin or", "press # to play");
        } else {
            char l1[21];
            snprintf(l1, sizeof(l1), "Insert %dc", ng.cost_cents);
            sdk_display(l1, "to play Hi-Lo");
        }
    }
}

static void ng_show_entry(void) {
    char l2[21];
    snprintf(l2, sizeof(l2), "Guess: %s_", ng.entry);
    sdk_display("Pick 1-99, # ok", l2);
}

static void ng_start(void) {
    ng.secret = ng_pick_secret();
    ng.guesses = 0;
    ng.entry[0] = '\0';
    ng.entry_len = 0;
    ng.phase = NG_PLAY_LIVE;
    sdk_logf(GUESS_CAT, "Game started (secret hidden), cost %dc", ng.cost_cents);
    ng_show_entry();
}

static void ng_reset(void) {
    ng.phase = NG_PLAY_IDLE;
    ng.guesses = 0;
    ng.entry[0] = '\0';
    ng.entry_len = 0;
    ng.cost_cents = ng_cost();
    ng_show_prompt();
}

/* Pure, testable comparison: -1 too low, 0 correct, +1 too high. */
int number_guess_compare(int secret, int guess) {
    if (guess < secret) return -1;
    if (guess > secret) return 1;
    return 0;
}

static void ng_submit(void) {
    int guess, cmp;
    char l2[21];

    if (ng.entry_len == 0) return; /* nothing entered */
    guess = atoi(ng.entry);
    ng.entry[0] = '\0';
    ng.entry_len = 0;
    ng.guesses++;

    cmp = number_guess_compare(ng.secret, guess);
    if (cmp == 0) {
        sdk_coin_chime();
        snprintf(l2, sizeof(l2), "in %d tries!", ng.guesses);
        sdk_display("Correct!", l2);
        sdk_logf(GUESS_CAT, "Solved in %d guesses", ng.guesses);
        ng.phase = NG_PLAY_WON;
        ng.reset_at = time(NULL) + 3;
    } else if (cmp < 0) {
        snprintf(l2, sizeof(l2), "Higher (try %d)", ng.guesses);
        sdk_display("Too low!", l2);
    } else {
        snprintf(l2, sizeof(l2), "Lower (try %d)", ng.guesses);
        sdk_display("Too high!", l2);
    }
}

/* ── Plugin callbacks ────────────────────────────────────────────────── */

static int ng_handle_coin(int coin_value, const char *coin_code) {
    (void)coin_value; (void)coin_code;
    if (ng.phase == NG_PLAY_IDLE && sdk_balance() >= ng.cost_cents) {
        sdk_spend_balance(ng.cost_cents);
        ng_start();
    } else {
        ng_show_prompt();
    }
    return 0;
}

static int ng_handle_keypad(char key) {
    if (ng.phase == NG_PLAY_IDLE) {
        /* Allow '#' to start a game if the player already has credit. */
        if (key == '#' && sdk_balance() >= ng.cost_cents) {
            sdk_spend_balance(ng.cost_cents);
            ng_start();
        }
        return 0;
    }
    if (ng.phase != NG_PLAY_LIVE) return 0; /* ignore during celebration */

    if (key >= '0' && key <= '9') {
        if (ng.entry_len < 2) {
            ng.entry[ng.entry_len++] = key;
            ng.entry[ng.entry_len] = '\0';
            sdk_beep(key);
            ng_show_entry();
        }
    } else if (key == '*') {
        ng.entry[0] = '\0';
        ng.entry_len = 0;
        ng_show_entry();
    } else if (key == '#') {
        ng_submit();
    }
    return 0;
}

static int ng_handle_hook(int hook_up, int hook_down) {
    (void)hook_up; (void)hook_down;
    ng_reset();
    return 0;
}

static void ng_handle_activation(void) {
    ng_reset();
}

static void ng_handle_tick(void) {
    if (ng.phase == NG_PLAY_WON && time(NULL) >= ng.reset_at) {
        ng_reset();
    }
}

void register_number_guess_plugin(void) {
    memset(&ng, 0, sizeof(ng));
    ng.cost_cents = ng_cost();
    ng.phase = NG_PLAY_IDLE;

    plugins_register("Number Guess",
                     "Hi-Lo guessing game - insert a coin, find the number",
                     ng_handle_coin,
                     ng_handle_keypad,
                     ng_handle_hook,
                     NULL,
                     NULL,
                     ng_handle_activation,
                     ng_handle_tick);
}
