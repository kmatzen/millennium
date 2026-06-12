#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../plugins.h"
#include "../plugin_sdk.h"
#include "../config.h"

/*
 * Trivia — a free True/False quiz, a natural fit for the two-line VFD.
 *
 * Lift the receiver and press a key to start. Each round asks TRIVIA_ROUND
 * statements: the claim shows on the top line (scrolling if long) and
 * "1=True 2=False" on the bottom. Press 1 or 2; you get instant feedback and,
 * after a beat, the next question. At the end it shows your score.
 *
 * Config (optional):
 *   trivia.start   force the first question index (default -1 = random).
 *                  Questions then run sequentially, so tests are deterministic.
 */

#define TRIVIA_CAT "Trivia"
#define TRIVIA_ROUND 3      /* questions per game */
#define TQ_IDLE     0
#define TQ_ASK      1
#define TQ_FEEDBACK 2
#define TQ_DONE     3

typedef struct {
    const char *claim;
    int answer; /* 1 = true, 0 = false */
} trivia_q_t;

static const trivia_q_t questions[] = {
    {"A group of crows is called a murder", 1},
    {"Goldfish only have a 3-second memory", 0},
    {"Honey never spoils if sealed", 1},
    {"The Great Wall is visible from space unaided", 0},
    {"Octopuses have three hearts", 1},
    {"Lightning never strikes twice in one spot", 0},
    {"A bolt of lightning is hotter than the sun", 1},
    {"Humans only use 10 percent of their brains", 0}
};

#define NUM_Q ((int)(sizeof(questions) / sizeof(questions[0])))

typedef struct {
    int phase;
    int idx;        /* current question index into questions[] */
    int asked;      /* questions asked so far this game */
    int score;
    time_t next_at;
} trivia_t;

static trivia_t tq;

static void tq_show_idle(void) {
    if (!sdk_receiver_is_up()) {
        sdk_display("Trivia Quiz", "Lift to play");
    } else {
        sdk_display("Trivia Quiz", "Press key: start");
    }
}

static void tq_ask(void) {
    sdk_display(questions[tq.idx].claim, "1=True 2=False");
    tq.phase = TQ_ASK;
}

static void tq_start(void) {
    int start = config_get_int(config_get_instance(), "trivia.start", -1);
    if (start < 0 || start >= NUM_Q) start = sdk_rand_below(NUM_Q);
    tq.idx = start;
    tq.asked = 0;
    tq.score = 0;
    sdk_log(TRIVIA_CAT, "Quiz started");
    tq_ask();
}

static void tq_finish(void) {
    char l2[21];
    snprintf(l2, sizeof(l2), "Score: %d/%d", tq.score, TRIVIA_ROUND);
    sdk_display("Quiz complete!", l2);
    sdk_logf(TRIVIA_CAT, "Quiz complete, score %d/%d", tq.score, TRIVIA_ROUND);
    tq.phase = TQ_DONE;
    tq.next_at = sdk_now() + 3;
}

static void tq_answer(int said_true) {
    int correct = (said_true == questions[tq.idx].answer);
    if (correct) {
        tq.score++;
        sdk_coin_chime();
        sdk_display("Correct!", "Well done");
    } else {
        sdk_display("Wrong!", questions[tq.idx].answer ? "It was True"
                                                       : "It was False");
    }
    tq.asked++;
    tq.idx = (tq.idx + 1) % NUM_Q;
    tq.phase = TQ_FEEDBACK;
    tq.next_at = sdk_now() + 2;
}

/* ── Plugin callbacks ────────────────────────────────────────────────── */

static int tq_handle_keypad(char key) {
    if (tq.phase == TQ_IDLE) {
        tq_start();
    } else if (tq.phase == TQ_ASK) {
        if (key == '1') { sdk_beep(key); tq_answer(1); }
        else if (key == '2') { sdk_beep(key); tq_answer(0); }
    }
    return 0;
}

static int tq_handle_hook(int hook_up, int hook_down) {
    (void)hook_up; (void)hook_down;
    tq.phase = TQ_IDLE;
    tq_show_idle();
    return 0;
}

static void tq_handle_activation(void) {
    tq.phase = TQ_IDLE;
    tq_show_idle();
}

static void tq_handle_tick(void) {
    if (tq.phase == TQ_FEEDBACK && sdk_now() >= tq.next_at) {
        if (tq.asked >= TRIVIA_ROUND) {
            tq_finish();
        } else {
            tq_ask();
        }
    } else if (tq.phase == TQ_DONE && sdk_now() >= tq.next_at) {
        tq.phase = TQ_IDLE;
        tq_show_idle();
    }
}

/* Test/introspection hook (see test_plugin_display_lines_fit): expose every
 * static claim so the unit-test guardrail can verify none exceeds the display
 * line budget. Returns the total count; fills up to `max` into out[]. */
int trivia_display_strings(const char **out, int max) {
    int n = 0, i;
    for (i = 0; i < NUM_Q; i++) {
        if (out && n < max) out[n] = questions[i].claim;
        n++;
    }
    return n;
}

void register_trivia_plugin(void) {
    memset(&tq, 0, sizeof(tq));
    tq.phase = TQ_IDLE;

    plugins_register("Trivia",
                     "Free True/False quiz - press 1 or 2 to answer",
                     NULL,
                     tq_handle_keypad,
                     tq_handle_hook,
                     NULL,
                     NULL,
                     tq_handle_activation,
                     tq_handle_tick);
}
