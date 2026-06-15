#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../plugins.h"
#include "../plugin_sdk.h"

/*
 * The Operator — "The Last Call", a puzzle game.
 *
 * Premise: a call has rung on this line since 11:59 PM, New Year's Eve 1999 — a
 * woman calling her estranged sister Ruth to make peace, frozen mid-ring. The
 * Operator can still connect it, but Ruth's seven-digit number is lost,
 * "scattered in the things she told me while she waited". The caller works the
 * number out by solving three puzzles, dials it, and time moves again.
 *
 * Design: see host/OPERATOR_STORY.md. The puzzles are meant to be HARD; the
 * story the Operator narrates IS the puzzle input. There is no guess-and-check
 * and nothing is gated on the coin validator or a magstripe card — only the
 * keypad (digits + * hint + # repeat), the display, and the earpiece. A three
 * step hint ladder (press *) keeps a persistent player from getting stuck.
 *
 *   Puzzle 1  keypad letters : a word scratched by the phone -> spell it (HUG=484)
 *   Puzzle 2  logic          : her age in 1999 from narrated facts (=51)
 *   Puzzle 3  cipher         : "7 0" shifted down by 2 (the two sisters) (=58)
 *   -> dial 4844858 -> the call connects, the clock turns to 2000.
 *
 * Audio clips are optional no-ops if absent; their text + a regen script live in
 * host/audio/. All narrative content is original.
 */

#define TS_CAT "Operator"

#define SOLVED_SECS        3   /* "GOT IT" confirm beat before the next puzzle  */
#define FINAL_CONNECT_SECS 8   /* "...RINGING..."; final_connect runs ~7.4 s    */
#define FINAL_CLOCK_SECS   6   /* "11:59 ... 12:00"; final_clock runs ~5.4 s    */
#define WIN_HOLD_SECS      8   /* hold the win (win_free ~6.8 s) before the coda */
#define ENTRY_TIMEOUT_SECS 8   /* clear a half-typed answer after this idle      */
#define GRACE_SECS         30  /* re-lift within this window resumes progress     */

/* The three puzzle answers; concatenated, they are Ruth's number. */
#define ANS1 "484"   /* puzzle 1: HUG on the keypad (4-8-4)        */
#define ANS2 "51"    /* puzzle 2: her age on the frozen night       */
#define ANS3 "58"    /* puzzle 3: 7,0 shifted down by the two of us */
#define FINAL_NUMBER ANS1 ANS2 ANS3   /* "4844858" */
#define FINAL_LEN 7

/* Plugin states (distinct from daemon_state_t). TS_DORMANT must be 0 so a
 * memset-zeroed session starts dormant. */
enum {
    TS_DORMANT = 0,
    TS_PUZZLE,     /* solving puzzle op.pz (0..2)              */
    TS_SOLVED,     /* brief confirm beat after a correct answer */
    TS_DIAL,       /* all three solved; dial the whole number   */
    TS_FINAL,      /* the call connects; scripted beats         */
    TS_WIN         /* time resumes; she is free                 */
};

typedef struct {
    const char *answer;     /* expected digit string                       */
    const char *clue1;      /* VFD line 1 (terse; the audio carries it all)*/
    const char *clue2;      /* VFD line 2 (terse prompt)                   */
    const char *clip;       /* the spoken riddle (replayed on #)           */
    const char *hint[3][2]; /* three escalating hints, each l1 / l2        */
} puzzle_t;

/* Every VFD string here is <= 20 chars (no scroll); the guardrail test checks. */
static const puzzle_t PUZ[3] = {
    { ANS1, "WORD BY THE PHONE", "SPELL IT  *=HINT", "puz1",
      { { "LOOK AT THE KEYS",  "letters on dial" },
        { "3 LETTERS",         "not 'sorry' but..." },
        { "H  U  G",           "= 4 8 4" } } },
    { ANS2, "HER AGE IN 1999",  "ENTER 2  *=HINT", "puz2",
      { { "FIND HER BIRTH YR",  "ignore the rest" },
        { "RUTH 1940",          "sister +11 = 1951" },
        { "1999 - 1951",        "= 51" } } },
    { ANS3, "CROSSED: 7 0",     "ENTER 2  *=HINT", "puz3",
      { { "THE KEY IS 2",       "'the two of us'" },
        { "SUBTRACT 2 EACH",    "wrap past zero" },
        { "7->5   0->8",        "= 58" } } }
};

typedef struct {
    int    state;
    int    pz;                 /* current puzzle index 0..2          */
    char   buf[8];             /* answer entry for the current puzzle */
    int    len;
    int    hintlvl;            /* 0..3 hint level for current puzzle  */
    char   assembled[FINAL_LEN + 1]; /* number built up as puzzles solve */
    char   dialed[FINAL_LEN + 1];    /* entry during the final dial   */
    int    dlen;
    int    won;                /* completed; next lift starts fresh   */
    int    step;              /* final-sequence sub-step             */
    time_t timer_at;           /* transient-state / entry-idle timer  */
    time_t last_input_at;
    time_t hangup_at;          /* for the grace-period resume         */
} op_state_t;

static op_state_t op;

/* ── Pure helpers (no hardware; unit-tested) ─────────────────────────── */

/* The phone keypad digit for a letter A-Z (the classic 2=ABC..9=WXYZ map),
 * or -1 for a non-letter. */
int operator_keypad_digit(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    switch (c) {
        case 'A': case 'B': case 'C':           return 2;
        case 'D': case 'E': case 'F':           return 3;
        case 'G': case 'H': case 'I':           return 4;
        case 'J': case 'K': case 'L':           return 5;
        case 'M': case 'N': case 'O':           return 6;
        case 'P': case 'Q': case 'R': case 'S': return 7;
        case 'T': case 'U': case 'V':           return 8;
        case 'W': case 'X': case 'Y': case 'Z': return 9;
        default:                                return -1;
    }
}

/* Subtract key from a shifted digit, wrapping mod 10 (the puzzle-3 cipher). */
int operator_shift_down(int digit, int key) {
    int r = (digit - key) % 10;
    if (r < 0) r += 10;
    return r;
}

/* ── Rendering ───────────────────────────────────────────────────────── */

static void op_render_dormant(void) {
    sdk_display("THE OPERATOR", "Lift: the last call");
}

static void op_render_puzzle(void) {
    const puzzle_t *p = &PUZ[op.pz];
    char l2[24];
    if (op.len > 0) {                       /* mid-entry: show what's typed */
        snprintf(l2, sizeof(l2), "> %s_", op.buf);
        sdk_display(p->clue1, l2);
    } else if (op.hintlvl > 0) {            /* a hint was requested         */
        sdk_display(p->hint[op.hintlvl - 1][0], p->hint[op.hintlvl - 1][1]);
    } else {                                /* the clue prompt              */
        sdk_display(p->clue1, p->clue2);
    }
}

static void op_render_dial(void) {
    char l2[24];
    if (op.dlen > 0) {
        snprintf(l2, sizeof(l2), "> %s_", op.dialed);
    } else {
        /* show the assembled number grouped: 484 51 58 */
        snprintf(l2, sizeof(l2), "%.3s %.2s %.2s",
                 op.assembled, op.assembled + 3, op.assembled + 5);
    }
    sdk_display("DIAL HER NUMBER", l2);
}

/* ── State transitions ───────────────────────────────────────────────── */

static void op_enter_puzzle(int i, const char *clip) {
    sdk_stop_audio();
    op.state = TS_PUZZLE;
    op.pz = i;
    op.buf[0] = '\0';
    op.len = 0;
    op.hintlvl = 0;
    op.last_input_at = sdk_now();
    op_render_puzzle();
    sdk_play_clip(clip ? clip : PUZ[i].clip);
}

static void op_enter_dial(const char *clip) {
    sdk_stop_audio();
    op.state = TS_DIAL;
    op.dialed[0] = '\0';
    op.dlen = 0;
    op.last_input_at = sdk_now();
    op_render_dial();
    sdk_play_clip(clip ? clip : "op_ready");
}

static void op_enter_solved(void) {
    char l2[24];
    sdk_stop_audio();
    op.state = TS_SOLVED;
    op.timer_at = sdk_now();
    snprintf(l2, sizeof(l2), "HER #: %s", op.assembled);
    sdk_display("GOT IT", l2);
    sdk_logf(TS_CAT, "Puzzle %d solved; number so far %s", op.pz + 1, op.assembled);
    sdk_play_clip("solved");
}

static void op_enter_final(void) {
    sdk_stop_audio();
    op.state = TS_FINAL;
    op.step = 0;
    op.timer_at = sdk_now();
    sdk_display("...RINGING...", "the last call");
    sdk_play_clip("final_connect");
}

static void op_enter_win(void) {
    sdk_stop_audio();
    op.state = TS_WIN;
    op.won = 1;
    op.step = 0;
    op.timer_at = sdk_now();
    sdk_display("12:00  2000", "SHE IS FREE");
    sdk_log(TS_CAT, "The last call connected; the Operator is free");
    sdk_play_clip("win_free");
}

static void op_enter_dormant(void) {
    sdk_stop_audio();
    op.state = TS_DORMANT;
    op.hangup_at = sdk_now();
    op_render_dormant();
}

static void op_reset_session(void) {
    op.pz = 0;
    op.buf[0] = '\0';
    op.len = 0;
    op.hintlvl = 0;
    op.assembled[0] = '\0';
    op.dialed[0] = '\0';
    op.dlen = 0;
    op.won = 0;
    op.step = 0;
}

/* ── Answer checking ─────────────────────────────────────────────────── */

static void op_check_answer(void) {
    if (strcmp(op.buf, PUZ[op.pz].answer) == 0) {
        /* lock this chunk onto the number being assembled */
        size_t a = strlen(op.assembled);
        snprintf(op.assembled + a, sizeof(op.assembled) - a, "%s", PUZ[op.pz].answer);
        op_enter_solved();
    } else {
        op.buf[0] = '\0';
        op.len = 0;
        sdk_display("NOT IT", "* hint  # clue");
        sdk_play_clip("wrong");
    }
}

static void op_check_number(void) {
    if (strcmp(op.dialed, FINAL_NUMBER) == 0) {
        op_enter_final();
    } else {
        op.dialed[0] = '\0';
        op.dlen = 0;
        sdk_display("WRONG NUMBER", "TRY AGAIN");
    }
}

/* ── Plugin callbacks ────────────────────────────────────────────────── */

static int op_handle_keypad(char key) {
    /* This app keeps its own per-field entry; keep the daemon's shared dial
     * buffer clean so /api/state isn't a pile of every digit pressed. */
    sdk_clear_keypad();

    switch (op.state) {
    case TS_PUZZLE:
        op.last_input_at = sdk_now();
        if (key == '*') {
            if (op.hintlvl < 3) op.hintlvl++;
            op.buf[0] = '\0';
            op.len = 0;
            op_render_puzzle();
        } else if (key == '#') {
            op.buf[0] = '\0';
            op.len = 0;
            op_render_puzzle();
            sdk_play_clip(PUZ[op.pz].clip);
        } else if (key >= '0' && key <= '9') {
            int need = (int)strlen(PUZ[op.pz].answer);
            if (op.len < need) {
                op.buf[op.len++] = key;
                op.buf[op.len] = '\0';
                sdk_beep(key);
                op_render_puzzle();
                if (op.len == need) op_check_answer();
            }
        }
        break;

    case TS_DIAL:
        op.last_input_at = sdk_now();
        if (key == '#') {
            op.dialed[0] = '\0';
            op.dlen = 0;
            op_render_dial();
        } else if (key >= '0' && key <= '9') {
            if (op.dlen < FINAL_LEN) {
                op.dialed[op.dlen++] = key;
                op.dialed[op.dlen] = '\0';
                sdk_beep(key);
                op_render_dial();
                if (op.dlen == FINAL_LEN) op_check_number();
            }
        }
        break;

    default:
        break;
    }
    return 0;
}

static int op_handle_hook(int hook_up, int hook_down) {
    if (hook_up) {
        if (op.won || sdk_elapsed(op.hangup_at) > GRACE_SECS) {
            op_reset_session();
            op_enter_puzzle(0, "op_intro");      /* fresh: mission + riddle 1 */
        } else if (op.state == TS_DIAL || op.pz >= 3) {
            op_enter_dial("op_ready");           /* resume at the final dial  */
        } else {
            op_enter_puzzle(op.pz, NULL);        /* resume the current puzzle */
        }
    } else if (hook_down) {
        op_enter_dormant();
    }
    return 0;
}

static void op_handle_activation(void) {
    memset(&op, 0, sizeof(op));
    if (sdk_receiver_is_up()) {
        op_enter_puzzle(0, "op_intro");
    } else {
        op.state = TS_DORMANT;
        op_render_dormant();
    }
}

static void op_handle_tick(void) {
    switch (op.state) {
    case TS_PUZZLE:
        /* A half-typed answer left idle clears itself. */
        if (op.len > 0 && sdk_elapsed(op.last_input_at) >= ENTRY_TIMEOUT_SECS) {
            op.buf[0] = '\0';
            op.len = 0;
            op_render_puzzle();
        }
        break;
    case TS_DIAL:
        if (op.dlen > 0 && sdk_elapsed(op.last_input_at) >= ENTRY_TIMEOUT_SECS) {
            op.dialed[0] = '\0';
            op.dlen = 0;
            op_render_dial();
        }
        break;
    case TS_SOLVED:
        if (sdk_elapsed(op.timer_at) >= SOLVED_SECS) {
            if (op.pz < 2) op_enter_puzzle(op.pz + 1, NULL);
            else { op.pz = 3; op_enter_dial(NULL); }
        }
        break;
    case TS_FINAL:
        if (op.step == 0 && sdk_elapsed(op.timer_at) >= FINAL_CONNECT_SECS) {
            op.step = 1;
            op.timer_at = sdk_now();
            sdk_display("11:59 ... 12:00", "the clock turns");
            sdk_play_clip("final_clock");
        } else if (op.step == 1 && sdk_elapsed(op.timer_at) >= FINAL_CLOCK_SECS) {
            op_enter_win();
        }
        break;
    case TS_WIN:
        if (op.step == 0 && sdk_elapsed(op.timer_at) >= WIN_HOLD_SECS) {
            op.step = 1;
            sdk_display("THE LINE IS QUIET", "hang up: she's home");
        }
        break;
    default:
        break;
    }
}

/* Test/introspection hook: expose every authored display string so the
 * test_plugin_display_lines_fit guardrail can confirm none exceeds the budget. */
static int op_collect(const char **out, int max, int n, const char *s) {
    if (out && n < max) out[n] = s;
    return n + 1;
}

int time_operator_display_strings(const char **out, int max) {
    static const char *const ui[] = {
        "THE OPERATOR", "Lift: the last call",
        "GOT IT", "HER #: 4845158", "NOT IT", "* hint  # clue",
        "DIAL HER NUMBER", "484 51 58", "> 4845158_", "WRONG NUMBER", "TRY AGAIN",
        "...RINGING...", "the last call", "11:59 ... 12:00", "the clock turns",
        "12:00  2000", "SHE IS FREE", "THE LINE IS QUIET", "hang up: she's home"
    };
    int n = 0, i, h;
    for (i = 0; i < (int)(sizeof(ui) / sizeof(ui[0])); i++) {
        n = op_collect(out, max, n, ui[i]);
    }
    for (i = 0; i < 3; i++) {
        n = op_collect(out, max, n, PUZ[i].clue1);
        n = op_collect(out, max, n, PUZ[i].clue2);
        for (h = 0; h < 3; h++) {
            n = op_collect(out, max, n, PUZ[i].hint[h][0]);
            n = op_collect(out, max, n, PUZ[i].hint[h][1]);
        }
    }
    return n;
}

void register_time_operator_plugin(void) {
    memset(&op, 0, sizeof(op));
    op.state = TS_DORMANT;

    plugins_register("The Operator",
                     "The Last Call - work out her number and finish the call",
                     NULL,                 /* coins do not gate anything here   */
                     op_handle_keypad,
                     op_handle_hook,
                     NULL,                 /* no real SIP call                  */
                     NULL,                 /* no card needed                    */
                     op_handle_activation,
                     op_handle_tick);
}
