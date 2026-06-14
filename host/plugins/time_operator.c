#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../plugins.h"
#include "../plugin_sdk.h"

/*
 * The Operator — "The Last Call", an interactive time-travel story.
 *
 * Premise: at 11:59 PM on New Year's Eve 1999 a woman lifted a payphone to call
 * her estranged sister Ruth and make peace, and the clock froze mid-ring. The
 * Operator has held that unfinished call ever since. The caller helps her finish
 * it: recover the three pieces of Ruth's number — each overheard at a moment the
 * woman almost dialed it across her life — then dial the whole number. The call
 * connects, the clock turns over to 2000, and the Operator is freed.
 *
 * Design: see host/OPERATOR_STORY.md for the full script and decision tree.
 *
 * Legibility: the Operator's voice states the goal and the next step; the VFD is
 * a permanent HUD (current hint + PIECES n/3). Coins never gate winning — at the
 * hub a coin buys a sharper hint, in an era it buys more "line time" before
 * temporal drift pulls you back. "Listen, don't speak": in a key era 1=LISTEN
 * yields the piece, 2=SPEAK tangles the line (recover by pressing 1). Sessions
 * reset on hang-up after a short grace, so each caller gets the whole arc but a
 * fumbled hang-up doesn't wipe progress. All consequence is in-memory for the
 * session (the SDK has no persistence hook). No real SIP call is placed.
 *
 * Audio clips (operator/era/win lines) are optional no-ops if absent; their text
 * and a regen script live in host/audio/. All narrative content is original.
 */

#define TS_CAT "Operator"

#define YEAR_MIN           1900
#define YEAR_MAX           2100
#define FUTURE_SEALED_YEAR 2050
#define TRAVEL_SECS        2
#define REVEAL_SECS        3
#define FLAVOR_SECS        3
#define DRIFT_SECS         30
#define EXTEND_SECS        30   /* a coin buys this much extra line time */
#define GRACE_SECS         30   /* re-lift within this window resumes a session */
#define FINAL_CONNECT_SECS 3
#define FINAL_CLOCK_SECS   4

/* Ruth's number, in three positional pieces A-B-C (tunable). */
#define PIECE_A "36"
#define PIECE_B "41"
#define PIECE_C "55"
#define FINAL_NUMBER PIECE_A PIECE_B PIECE_C   /* "364155" */

/* Year roles (which moment in the caller's life a year holds). */
enum {
    ROLE_KEY_A = 0,   /* 1950s — two girls, a kitchen radio          */
    ROLE_KEY_B,       /* 1970s-80s — the night she lost her nerve    */
    ROLE_KEY_C,       /* 1995-99 — the sealed final year (needs pass)*/
    ROLE_EARLY,       /* before she was born                         */
    ROLE_HOME,        /* 2000 — the frozen minute                    */
    ROLE_FUTURE,      /* sealed future                               */
    ROLE_OTHER,       /* a year she never held the number            */
    ROLE_BAD          /* out of range                                */
};

/* Plugin states (distinct from daemon_state_t). TS_DORMANT must be 0 so a
 * memset-zeroed session starts dormant. */
enum {
    TS_DORMANT = 0,
    TS_HUB,        /* with the Operator; dialing a year             */
    TS_TRAVEL,     /* connecting beat (ringback)                    */
    TS_FLAVOR,     /* a non-key era; brief scene then back          */
    TS_KEY,        /* a key era; LISTEN/SPEAK (maybe locked/tangled)*/
    TS_REVEAL,     /* a piece was just found                        */
    TS_READY,      /* all 3 pieces; dialing the final number        */
    TS_FINAL,      /* the call connects; scripted beats             */
    TS_WIN         /* time resumes; she is free                     */
};

typedef struct {
    int    state;
    char   buf[8];        /* year (4) or final number (6) entry      */
    int    len;
    int    pieces;        /* bitmask: bit0=A bit1=B bit2=C           */
    int    pass;          /* a temporal pass (card) was swiped       */
    int    won;           /* completed; next lift starts fresh       */
    int    travel_year;
    int    cur_key;       /* key index while TS_KEY (0/1/2), else -1 */
    int    locked;        /* current key era sealed, awaiting access */
    int    tangled;       /* current key era tangled (spoke)         */
    int    final_step;
    time_t timer_at;      /* transient-state timer                   */
    time_t last_input_at; /* drift timer base in TS_KEY              */
    time_t hangup_at;     /* for the grace-period resume             */
} op_state_t;

static op_state_t op;

/* ── Pure helpers (no hardware; unit-tested) ─────────────────────────── */

int parse_year(const char *buf) {
    int i, val = 0;
    if (!buf || strlen(buf) != 4) return -1;
    for (i = 0; i < 4; i++) {
        if (buf[i] < '0' || buf[i] > '9') return -1;
        val = val * 10 + (buf[i] - '0');
    }
    return val;
}

static int year_role(int year) {
    if (year < YEAR_MIN || year > YEAR_MAX) return ROLE_BAD;
    if (year >= 1950 && year <= 1959) return ROLE_KEY_A;
    if (year >= 1970 && year <= 1989) return ROLE_KEY_B;
    if (year >= 1995 && year <= 1999) return ROLE_KEY_C;
    if (year == 2000) return ROLE_HOME;
    if (year >= FUTURE_SEALED_YEAR) return ROLE_FUTURE;
    if (year < 1950) return ROLE_EARLY;
    return ROLE_OTHER;
}

/* Stable string tag for the same classification, for unit tests. */
const char *operator_year_role(int year) {
    switch (year_role(year)) {
        case ROLE_KEY_A: return "A";
        case ROLE_KEY_B: return "B";
        case ROLE_KEY_C: return "C";
        case ROLE_EARLY: return "early";
        case ROLE_HOME:  return "home";
        case ROLE_FUTURE:return "future";
        case ROLE_OTHER: return "other";
        default:         return "bad";
    }
}

/* Lowest piece index (0..2) not yet found, or 3 when all three are held. */
int operator_target_piece(int pieces) {
    if (!(pieces & 1)) return 0;
    if (!(pieces & 2)) return 1;
    if (!(pieces & 4)) return 2;
    return 3;
}

static int op_have(int idx) { return (op.pieces & (1 << idx)) != 0; }
static int op_count(void) {
    return ((op.pieces & 1) ? 1 : 0) + ((op.pieces & 2) ? 1 : 0) +
           ((op.pieces & 4) ? 1 : 0);
}

/* ── Rendering (every VFD line <= 20 chars: no scroll, stable to assert) ─ */

static void op_render_dormant(void) {
    sdk_display("THE OPERATOR", "Lift: the last call");
}

static void op_render_hub(void) {
    char l2[24];
    int t = operator_target_piece(op.pieces);
    const char *l1 = (t == 0) ? "FIND THE 1950s" :
                     (t == 1) ? "FIND THE 1970s" : "FIND 1998 SEALED";
    if (op.len > 0) {
        char l2b[24];
        snprintf(l2b, sizeof(l2b), "YEAR: %s_", op.buf);
        sdk_display("DIAL A YEAR", l2b);
        return;
    }
    snprintf(l2, sizeof(l2), "DIAL A YEAR  %d/3", op_count());
    sdk_display(l1, l2);
}

static void op_render_ready(void) {
    char l2[24];
    if (op.len > 0) {
        snprintf(l2, sizeof(l2), "%s_", op.buf);
        sdk_display("DIALING...", l2);
        return;
    }
    sdk_display("DIAL HER NUMBER", PIECE_A " " PIECE_B " " PIECE_C);
}

/* ── State transitions (each stops any continuous tone first) ────────── */

static const char *op_next_clip(void) {
    switch (operator_target_piece(op.pieces)) {
        case 0:  return "op_next_a";
        case 1:  return "op_next_b";
        case 2:  return "op_next_c";
        default: return "op_ready";
    }
}

static void op_enter_ready(const char *clip) {
    sdk_stop_audio();
    op.state = TS_READY;
    op.buf[0] = '\0';
    op.len = 0;
    op_render_ready();
    sdk_play_clip(clip ? clip : "op_ready");
}

/* Enter the hub. clip==NULL plays the hint for the current target. */
static void op_enter_hub(const char *clip) {
    if (operator_target_piece(op.pieces) == 3) {
        op_enter_ready(clip);
        return;
    }
    sdk_stop_audio();
    op.state = TS_HUB;
    op.buf[0] = '\0';
    op.len = 0;
    op_render_hub();
    sdk_play_clip(clip ? clip : op_next_clip());
}

static void op_enter_dormant(void) {
    sdk_stop_audio();
    op.state = TS_DORMANT;
    op.hangup_at = sdk_now();
    op.buf[0] = '\0';
    op.len = 0;
    op_render_dormant();
}

static void op_reset_session(void) {
    op.pieces = 0;
    op.pass = 0;
    op.won = 0;
    op.tangled = 0;
    op.locked = 0;
    op.cur_key = -1;
    op.buf[0] = '\0';
    op.len = 0;
}

static void op_enter_travel(int year) {
    char l2[24];
    sdk_stop_audio();
    sdk_ringback();
    op.state = TS_TRAVEL;
    op.travel_year = year;
    op.timer_at = sdk_now();
    snprintf(l2, sizeof(l2), "YEAR %d", year);
    sdk_display("CONNECTING...", l2);
}

static void op_enter_flavor(const char *clip, const char *label) {
    sdk_stop_audio();
    op.state = TS_FLAVOR;
    op.timer_at = sdk_now();
    sdk_display(label, "BACK TO OPERATOR");
    sdk_play_clip(clip);
}

static void op_render_key(void) {
    const char *l1 = (op.cur_key == 0) ? "1955 TWO SISTERS" :
                     (op.cur_key == 1) ? "1978 SHE PAUSES" : "1998 A CARD";
    if (op.locked) {
        sdk_display("1998: SEALED", "SWIPE PASS / COIN");
    } else if (op.tangled) {
        sdk_display("LINE TANGLED", "press 1=LISTEN");
    } else {
        sdk_display(l1, "1=LISTEN 2=SPEAK");
    }
}

static void op_enter_key(int idx, int locked) {
    sdk_stop_audio();
    op.state = TS_KEY;
    op.cur_key = idx;
    op.locked = locked;
    op.tangled = 0;
    op.last_input_at = sdk_now();
    op_render_key();
    if (locked) {
        sdk_play_clip("era3_sealed");
    } else {
        sdk_play_clip(idx == 0 ? "era1_arrive" :
                      idx == 1 ? "era2_arrive" : "era3_arrive");
    }
}

static void op_enter_reveal(void) {
    char l1[24], l2[24];
    const char *piece = (op.cur_key == 0) ? PIECE_A :
                        (op.cur_key == 1) ? PIECE_B : PIECE_C;
    char letter = (char)('A' + op.cur_key);
    op.pieces |= (1 << op.cur_key);
    op.state = TS_REVEAL;
    op.timer_at = sdk_now();
    snprintf(l1, sizeof(l1), "PIECE %c: %s", letter, piece);
    snprintf(l2, sizeof(l2), "PIECES %d/3", op_count());
    sdk_display(l1, l2);
    sdk_logf(TS_CAT, "Found piece %c (%s); %d/3", letter, piece, op_count());
    sdk_play_clip(op.cur_key == 0 ? "era1_listen" :
                  op.cur_key == 1 ? "era2_listen" : "era3_listen");
}

static void op_enter_win(void) {
    sdk_stop_audio();
    op.state = TS_WIN;
    op.won = 1;
    sdk_display("12:00  2000", "SHE IS FREE");
    sdk_log(TS_CAT, "The last call connected; the Operator is free");
    sdk_play_clip("win_free");
}

static void op_enter_final(void) {
    sdk_stop_audio();
    op.state = TS_FINAL;
    op.final_step = 0;
    op.timer_at = sdk_now();
    sdk_display("...RINGING...", "the last call");
    sdk_play_clip("final_connect");
}

/* Resolve a completed TRAVEL beat to the right era. */
static void op_resolve_travel(void) {
    int role = year_role(op.travel_year);
    int idx = (role == ROLE_KEY_A) ? 0 : (role == ROLE_KEY_B) ? 1 : 2;

    switch (role) {
    case ROLE_KEY_A:
    case ROLE_KEY_B:
    case ROLE_KEY_C:
        if (op_have(idx)) {
            op_enter_flavor("flv_heard", "ALREADY HEARD");
        } else if (role == ROLE_KEY_C && !op.pass) {
            op_enter_key(idx, 1);   /* sealed */
        } else {
            op_enter_key(idx, 0);
        }
        break;
    case ROLE_EARLY:  op_enter_flavor("flv_early",  "TOO EARLY");      break;
    case ROLE_HOME:   op_enter_flavor("flv_2000",   "FROZEN MINUTE");  break;
    case ROLE_FUTURE: op_enter_flavor("flv_future", "FUTURE SEALED");  break;
    default:          op_enter_flavor("flv_other",  "NOTHING HERE");   break;
    }
}

/* ── Year / number entry ─────────────────────────────────────────────── */

static void op_eval_year(void) {
    int year = parse_year(op.buf);
    if (year_role(year) == ROLE_BAD) {
        op.buf[0] = '\0';
        op.len = 0;
        sdk_display("NO SUCH YEAR", "try 1900-2100");
        return;
    }
    op_enter_travel(year);
}

static void op_eval_number(void) {
    if (strcmp(op.buf, FINAL_NUMBER) == 0) {
        op_enter_final();
    } else {
        op.buf[0] = '\0';
        op.len = 0;
        sdk_display("WRONG NUMBER", PIECE_A " " PIECE_B " " PIECE_C);
    }
}

/* ── Plugin callbacks ────────────────────────────────────────────────── */

static int op_handle_coin(int coin_value, const char *coin_code) {
    (void)coin_value;
    (void)coin_code;
    sdk_coin_chime();
    if (op.state == TS_HUB) {
        int t = operator_target_piece(op.pieces);
        const char *l1 = (t == 0) ? "DIAL 1955" :
                         (t == 1) ? "DIAL 1978" : "DIAL 1998";
        char l2[24];
        snprintf(l2, sizeof(l2), "DIAL A YEAR  %d/3", op_count());
        sdk_display(l1, l2);
        sdk_play_clip(t == 0 ? "op_hint_a" : t == 1 ? "op_hint_b" : "op_hint_c");
    } else if (op.state == TS_KEY) {
        if (op.locked) {
            op_enter_key(op.cur_key, 0);   /* feed the coinbox to force it open */
        } else {
            op.last_input_at = sdk_now();   /* a coin buys more line time */
            sdk_play_clip("coin_hold");
        }
    } else if (op.state == TS_READY) {
        op_render_ready();
    }
    return 0;
}

static int op_handle_keypad(char key) {
    switch (op.state) {
    case TS_HUB:
        if (key >= '0' && key <= '9') {
            if (op.len < 4) {
                op.buf[op.len++] = key;
                op.buf[op.len] = '\0';
                sdk_beep(key);
                op_render_hub();
                if (op.len == 4) op_eval_year();
            }
        } else if (key == '*') {
            if (op.len > 0) op.buf[--op.len] = '\0';
            op_render_hub();
        } else if (key == '#') {
            op.buf[0] = '\0'; op.len = 0;
            op_render_hub();
            sdk_play_clip(op_next_clip());
        }
        break;

    case TS_READY:
        if (key >= '0' && key <= '9') {
            if (op.len < 6) {
                op.buf[op.len++] = key;
                op.buf[op.len] = '\0';
                sdk_beep(key);
                op_render_ready();
                if (op.len == 6) op_eval_number();
            }
        } else if (key == '*') {
            if (op.len > 0) op.buf[--op.len] = '\0';
            op_render_ready();
        } else if (key == '#') {
            op.buf[0] = '\0'; op.len = 0;
            op_render_ready();
        }
        break;

    case TS_KEY:
        op.last_input_at = sdk_now();
        if (op.locked) {
            if (key == '#') op_enter_hub(NULL);
            else sdk_beep(key);
        } else if (key == '1') {
            op_enter_reveal();
        } else if (key == '2') {
            op.tangled = 1;
            op_render_key();
            sdk_play_clip("px_tangle");
        } else if (key == '#') {
            op_enter_hub(NULL);
        } else {
            sdk_beep(key);
        }
        break;

    case TS_FLAVOR:
        if (key == '#') op_enter_hub(NULL);
        break;

    case TS_REVEAL:
        if (key == '#') op_enter_hub(NULL);
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
            op_enter_hub("op_intro");
        } else {
            op_enter_hub("op_resume");   /* keeps pieces/pass within grace */
        }
    } else if (hook_down) {
        op_enter_dormant();
    }
    return 0;
}

static int op_handle_card(const char *card_number) {
    (void)card_number;
    op.pass = 1;
    if (op.state == TS_KEY && op.locked) {
        op_enter_key(op.cur_key, 0);     /* unlock and arrive */
    } else {
        sdk_display("TEMPORAL PASS", "PASS ACCEPTED");
        sdk_log(TS_CAT, "Temporal pass accepted");
    }
    return 0;
}

static void op_handle_activation(void) {
    memset(&op, 0, sizeof(op));
    op.cur_key = -1;
    if (sdk_receiver_is_up()) {
        op_enter_hub("op_intro");
    } else {
        op.state = TS_DORMANT;
        op_render_dormant();
    }
}

static void op_handle_tick(void) {
    switch (op.state) {
    case TS_TRAVEL:
        if (sdk_elapsed(op.timer_at) >= TRAVEL_SECS) op_resolve_travel();
        break;
    case TS_FLAVOR:
        if (sdk_elapsed(op.timer_at) >= FLAVOR_SECS) op_enter_hub(NULL);
        break;
    case TS_REVEAL:
        if (sdk_elapsed(op.timer_at) >= REVEAL_SECS) op_enter_hub(NULL);
        break;
    case TS_KEY:
        if (sdk_elapsed(op.last_input_at) >= DRIFT_SECS) {
            op_enter_hub("drift_back");
        }
        break;
    case TS_FINAL:
        if (op.final_step == 0 && sdk_elapsed(op.timer_at) >= FINAL_CONNECT_SECS) {
            op.final_step = 1;
            op.timer_at = sdk_now();
            sdk_display("11:59 ... 12:00", "the clock turns");
            sdk_play_clip("final_clock");
        } else if (op.final_step == 1 &&
                   sdk_elapsed(op.timer_at) >= FINAL_CLOCK_SECS) {
            op_enter_win();
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
        "THE OPERATOR", "Lift: the last call", "FIND THE 1950s",
        "FIND THE 1970s", "FIND 1998 SEALED", "DIAL A YEAR", "DIAL HER NUMBER",
        PIECE_A " " PIECE_B " " PIECE_C, "DIALING...", "CONNECTING...",
        "BACK TO OPERATOR", "TOO EARLY", "FROZEN MINUTE", "FUTURE SEALED",
        "NOTHING HERE", "ALREADY HEARD", "1955 TWO SISTERS", "1978 SHE PAUSES",
        "1998 A CARD", "1998: SEALED", "SWIPE PASS / COIN", "1=LISTEN 2=SPEAK",
        "LINE TANGLED", "press 1=LISTEN", "DIAL 1955", "DIAL 1978", "DIAL 1998",
        "TEMPORAL PASS", "PASS ACCEPTED", "NO SUCH YEAR", "try 1900-2100",
        "WRONG NUMBER", "...RINGING...", "the last call", "11:59 ... 12:00",
        "the clock turns", "12:00  2000", "SHE IS FREE"
    };
    int n = 0, i;
    for (i = 0; i < (int)(sizeof(ui) / sizeof(ui[0])); i++) {
        n = op_collect(out, max, n, ui[i]);
    }
    return n;
}

void register_time_operator_plugin(void) {
    memset(&op, 0, sizeof(op));
    op.state = TS_DORMANT;
    op.cur_key = -1;

    plugins_register("The Operator",
                     "The Last Call - help finish a call frozen in time",
                     op_handle_coin,
                     op_handle_keypad,
                     op_handle_hook,
                     NULL,
                     op_handle_card,
                     op_handle_activation,
                     op_handle_tick);
}
