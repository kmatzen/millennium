#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../plugins.h"
#include "../plugin_sdk.h"
#include "../config.h"

/*
 * Dial-a-Joke — the classic payphone novelty line, free to play.
 *
 * Lift the receiver and press any key for a joke: the setup shows first, then
 * after a short beat the punchline lands. Press a key for the next one. Long
 * lines auto-scroll on the VFD. A nice showcase of timed (tick-driven) reveals
 * and the display.
 *
 * Config (optional):
 *   joke.index   force a starting joke index (default -1 = random).
 *                Used by scenario tests for determinism.
 */

#define JOKE_CAT "DialAJoke"

#define DJ_IDLE    0  /* waiting for a key */
#define DJ_SETUP   1  /* showing setup, punchline pending */

typedef struct {
    const char *setup;
    const char *punch;
} joke_t;

static const joke_t jokes[] = {
    {"Why did the phone", "wear glasses? It lost its contacts!"},
    {"I'd tell a UDP joke", "but you might not get it."},
    {"What do you call a", "fake noodle? An impasta!"},
    {"Why don't payphones", "ever get lonely? They take all calls!"},
    {"I used to be a banker", "but I lost interest."},
    {"What's a phone's", "favorite snack? Microchips!"},
    {"Why was the cell phone", "wearing a sweater? It was a little chilly!"},
    {"I'm reading a book on", "anti-gravity. It's impossible to put down!"},
    {"Why is 6 afraid of 7?", "Because 7 ate 9!"}
};

#define NUM_JOKES ((int)(sizeof(jokes) / sizeof(jokes[0])))

typedef struct {
    int phase;
    int current;
    time_t reveal_at;
} dial_a_joke_t;

static dial_a_joke_t dj;

static void dj_show_idle(void) {
    if (!sdk_receiver_is_up()) {
        sdk_display("Lift receiver", "for a joke!");
    } else {
        sdk_display("Dial-A-Joke", "Press any key");
    }
}

static void dj_pick(void) {
    int forced = config_get_int(config_get_instance(), "joke.index", -1);
    if (forced >= 0 && forced < NUM_JOKES) {
        dj.current = forced;
    } else {
        dj.current = sdk_rand_below(NUM_JOKES);
    }
}

static void dj_tell(void) {
    dj_pick();
    sdk_display(jokes[dj.current].setup, "...");
    sdk_logf(JOKE_CAT, "Telling joke %d", dj.current);
    dj.phase = DJ_SETUP;
    dj.reveal_at = sdk_now() + 2;
}

/* ── Plugin callbacks ────────────────────────────────────────────────── */

static int dj_handle_keypad(char key) {
    (void)key;
    /* Any key tells the next joke (when idle or after a punchline). */
    if (dj.phase == DJ_IDLE) {
        dj_tell();
    }
    return 0;
}

static int dj_handle_hook(int hook_up, int hook_down) {
    (void)hook_up; (void)hook_down;
    dj.phase = DJ_IDLE;
    dj_show_idle();
    return 0;
}

static void dj_handle_activation(void) {
    dj.phase = DJ_IDLE;
    dj_show_idle();
}

static void dj_handle_tick(void) {
    if (dj.phase == DJ_SETUP && sdk_now() >= dj.reveal_at) {
        sdk_display(jokes[dj.current].punch, "Press key: more");
        dj.phase = DJ_IDLE;
    }
}

/* Test/introspection hook (see test_plugin_display_lines_fit): expose every
 * static display string so the unit-test guardrail can verify none exceeds the
 * display line budget. Returns the total count; fills up to `max` into out[]. */
int dial_a_joke_display_strings(const char **out, int max) {
    int n = 0, i;
    for (i = 0; i < NUM_JOKES; i++) {
        if (out && n < max) out[n] = jokes[i].setup;
        n++;
        if (out && n < max) out[n] = jokes[i].punch;
        n++;
    }
    return n;
}

void register_dial_a_joke_plugin(void) {
    memset(&dj, 0, sizeof(dj));
    dj.phase = DJ_IDLE;

    plugins_register("Dial-A-Joke",
                     "Free joke line - press a key, hear a groaner",
                     NULL,
                     dj_handle_keypad,
                     dj_handle_hook,
                     NULL,
                     NULL,
                     dj_handle_activation,
                     dj_handle_tick);
}
