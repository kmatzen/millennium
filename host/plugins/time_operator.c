#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../plugins.h"
#include "../plugin_sdk.h"

/*
 * The Operator — an interactive time-travel narrative.
 *
 * The conceit: at the stroke of the year 2000 the Millennium's clock failed to
 * roll over and the phone came unstuck in time. Lifting the handset no longer
 * reaches a person; it reaches the Operator, a switchboard voice stranded at
 * that frozen minute. Dial a 4-digit YEAR, pay a fare in coins that scales with
 * how far you travel, optionally swipe a magstripe card as a temporal pass to
 * unlock sealed eras, and listen to each era play out on the VFD. Meddling in
 * one era and then opening another jams the line with a paradox you must repair.
 *
 * The whole experience runs locally with the tone palette and the two-line
 * display — no real SIP call is placed, so the daemon stays in IDLE_UP
 * throughout and scenario tests assert on the displayed text. All narrative
 * content is original.
 *
 * Scope: this is the Phase-1 MVP. The SDK exposes no audio-file playback and no
 * plugin-state persistence hook (only coin balance and the active plugin name
 * survive a restart), so era ambience is tones-only and every narrative
 * consequence — visited eras, the temporal pass, an armed paradox — lives in
 * memory for the current power-on session and resets when the plugin is
 * (re)activated.
 */

#define TS_CAT "Operator"

#define FARE_BASE_CENTS       25
#define FARE_PER_DECADE_CENTS 25
#define FARE_MAX_CENTS        500
#define YEAR_MIN              1900
#define YEAR_MAX              2100
#define FUTURE_SEALED_YEAR    2050
#define FRAME_SECONDS         2
#define DRIFT_SECONDS         30
#define DROP_SECONDS          3
#define CONNECT_FRAMES        2

/* Internal plugin states (distinct from daemon_state_t). TS_DORMANT must be 0
 * so a memset-zeroed session struct starts dormant. */
enum {
    TS_DORMANT = 0,
    TS_OPERATOR,
    TS_AWAIT_FARE,
    TS_CONNECTING,
    TS_IN_ERA,
    TS_DROP,
    TS_PARADOX
};

typedef struct { const char *l1; const char *l2; } frame_t;

typedef struct {
    int            lo, hi;       /* inclusive year bracket */
    const char    *label;        /* shown on line 1 throughout the era */
    const frame_t *frames;
    int            frame_count;
    const char    *observe;      /* '1' branch flavor */
    const char    *interfere;    /* '2' branch flavor (arms a paradox) */
} era_t;

static const frame_t era0_frames[] = {
    {"THE WIRES", "operator plugs in"},
    {"THE WIRES", "1=listen 2=meddle"}
};
static const frame_t era1_frames[] = {
    {"GOLDEN AIR", "radio crackles on"},
    {"GOLDEN AIR", "1=listen 2=meddle"}
};
static const frame_t era2_frames[] = {
    {"SPACE LINE", "a countdown ticks"},
    {"SPACE LINE", "1=listen 2=meddle"}
};
static const frame_t era3_frames[] = {
    {"LAST ANALOG", "modems screech"},
    {"LAST ANALOG", "1=listen 2=meddle"}
};
static const frame_t era4_frames[] = {
    {"FROZEN MINUTE", "11:59 forever"},
    {"FROZEN MINUTE", "#=back home"}
};
static const frame_t era5_frames[] = {
    {"STATIC YEARS", "dead air, faint"},
    {"STATIC YEARS", "1=listen 2=meddle"}
};
static const frame_t era6_frames[] = {
    {"SEALED FUTURE", "the pass glows"},
    {"SEALED FUTURE", "1=listen 2=meddle"}
};

/* Brackets tile 1900..2100 with two gaps handled specially before lookup:
 * 1999 is the signature dead-end (never reaches midnight) and 2000 is "home".
 * The 2050+ era is gated behind a temporal pass. */
static const era_t eras[] = {
    {1900, 1929, "THE WIRES",     era0_frames, 2, "a faint voice: hi",  "you change a word"},
    {1930, 1959, "GOLDEN AIR",    era1_frames, 2, "swing fills the line","you mute the band"},
    {1960, 1989, "SPACE LINE",    era2_frames, 2, "...three two one...", "you halt the count"},
    {1990, 1998, "LAST ANALOG",   era3_frames, 2, "the young web sings", "you cut the modem"},
    {2000, 2000, "FROZEN MINUTE", era4_frames, 2, "home. stay a while",  "the clock twitches"},
    {2001, 2049, "STATIC YEARS",  era5_frames, 2, "...seven... nine...", "you read a number"},
    {2050, 2100, "SEALED FUTURE", era6_frames, 2, "it knows your name",  "you ask it to stop"}
};

#define NUM_ERAS ((int)(sizeof(eras) / sizeof(eras[0])))

typedef struct {
    int         state;
    char        year_buf[5];     /* up to 4 digits + NUL */
    int         year_len;
    int         current_year;    /* year of the active connection */
    int         pending_year;    /* year awaiting fare */
    int         fare_cents;      /* fare for pending_year */
    int         dest;            /* era index of current connection, -1 = drop */
    int         era;             /* era index while TS_IN_ERA */
    int         frame_index;
    time_t      frame_at;        /* paces frame scrolling / connecting */
    time_t      last_input_at;   /* drives temporal-drift timeout in an era */
    int         pass_active;     /* a temporal pass was swiped this session */
    int         armed;           /* a paradox is armed (line will jam) */
    int         source_year;     /* year where the paradox was armed */
    int         visited_mask;    /* eras visited this session */
    const char *banner;          /* operator line-1 banner */
} time_state_t;

static time_state_t ts;

/* ── Pure helpers (no hardware; unit-tested directly) ────────────────── */

/* Parse a buffer of exactly 4 digits into a year, or -1 otherwise. */
int parse_year(const char *buf) {
    int i, val = 0;
    if (!buf || strlen(buf) != 4) return -1;
    for (i = 0; i < 4; i++) {
        if (buf[i] < '0' || buf[i] > '9') return -1;
        val = val * 10 + (buf[i] - '0');
    }
    return val;
}

/* Fare in cents for a target year; the frozen minute (2000) is free. */
int fare_for_year(int year) {
    int decades, fare;
    if (year == 2000) return 0;
    decades = abs(year - 2000) / 10;
    fare = FARE_BASE_CENTS + FARE_PER_DECADE_CENTS * decades;
    if (fare > FARE_MAX_CENTS) fare = FARE_MAX_CENTS;
    return fare;
}

/* Era index whose bracket contains year, or -1 (e.g. the 1999 gap). */
int era_index_for_year(int year) {
    int i;
    for (i = 0; i < NUM_ERAS; i++) {
        if (year >= eras[i].lo && year <= eras[i].hi) return i;
    }
    return -1;
}

/* ── Rendering ───────────────────────────────────────────────────────── */

static void ts_render_dormant(void) {
    sdk_display("THE OPERATOR", "Lift to dial");
}

static void ts_render_operator(void) {
    char l2[21];
    if (ts.year_len == 0) {
        sdk_display(ts.banner ? ts.banner : "TIME OPERATOR",
                    "DIAL A YEAR  #=BACK");
    } else {
        snprintf(l2, sizeof(l2), "YEAR: %s_", ts.year_buf);
        sdk_display("TIME OPERATOR", l2);
    }
}

static void ts_render_need(void) {
    char l1[21], l2[21];
    snprintf(l1, sizeof(l1), "NEED $%d.%02d", ts.fare_cents / 100,
             ts.fare_cents % 100);
    snprintf(l2, sizeof(l2), "HAVE $%d.%02d", sdk_balance() / 100,
             sdk_balance() % 100);
    sdk_display(l1, l2);
}

static void ts_render_connecting(void) {
    char l2[21];
    snprintf(l2, sizeof(l2), "YEAR %d", ts.current_year);
    sdk_display(ts.frame_index == 0 ? "CONNECTING..." : "ringing...", l2);
}

static void ts_render_era(void) {
    const era_t *e = &eras[ts.era];
    int i = ts.frame_index;
    if (i >= e->frame_count) i = e->frame_count - 1;
    sdk_display(e->frames[i].l1, e->frames[i].l2);
}

/* ── State transitions (each stops any continuous tone first) ────────── */

static void ts_enter_operator(const char *banner) {
    sdk_stop_audio();
    ts.state = TS_OPERATOR;
    ts.year_buf[0] = '\0';
    ts.year_len = 0;
    ts.banner = banner ? banner : "TIME OPERATOR";
    ts_render_operator();
}

static void ts_enter_era(int idx) {
    sdk_stop_audio();
    ts.state = TS_IN_ERA;
    ts.era = idx;
    ts.frame_index = 0;
    ts.frame_at = sdk_now();
    ts.last_input_at = sdk_now();
    ts.visited_mask |= (1 << idx);
    sdk_logf(TS_CAT, "Arrived in era %d (%s)", idx, eras[idx].label);
    ts_render_era();
}

static void ts_enter_drop(void) {
    sdk_stop_audio();
    sdk_busy_tone();
    ts.state = TS_DROP;
    ts.frame_at = sdk_now();
    sdk_display("ALMOST 2000...", "NEVER MIDNIGHT");
}

static void ts_enter_paradox(void) {
    sdk_stop_audio();
    sdk_busy_tone();
    ts.state = TS_PARADOX;
    sdk_logf(TS_CAT, "Paradox jammed the line (source %d)", ts.source_year);
    sdk_display("LINE FAULT 19##", "#=OPERATOR");
}

static void ts_begin_connection(int year) {
    ts.current_year = year;
    ts.dest = (year == 1999) ? -1 : era_index_for_year(year);
    sdk_stop_audio();
    sdk_ringback();
    ts.state = TS_CONNECTING;
    ts.frame_index = 0;
    ts.frame_at = sdk_now();
    ts_render_connecting();
}

/* Attempt to route to a year already known to be in range and unsealed. */
static void ts_try_connect(int year) {
    int fare = fare_for_year(year);
    int target;

    if (sdk_balance() < fare) {
        ts.pending_year = year;
        ts.fare_cents = fare;
        ts.state = TS_AWAIT_FARE;
        ts_render_need();
        return;
    }

    target = (year == 1999) ? -1 : era_index_for_year(year);
    if (ts.armed && target >= 0 &&
        target != era_index_for_year(ts.source_year)) {
        /* Opening a second era while one is meddled-with jams the line. */
        sdk_spend_balance(fare);
        ts_enter_paradox();
        return;
    }

    sdk_spend_balance(fare);
    ts_begin_connection(year);
}

static void ts_eval_year(void) {
    int year = parse_year(ts.year_buf);
    if (year < YEAR_MIN || year > YEAR_MAX) {
        ts_enter_operator("NO SUCH YEAR");
        return;
    }
    if (year >= FUTURE_SEALED_YEAR && !ts.pass_active) {
        ts_enter_operator("FUTURE SEALED");
        return;
    }
    ts_try_connect(year);
}

/* ── Plugin callbacks ────────────────────────────────────────────────── */

static int ts_handle_coin(int coin_value, const char *coin_code) {
    char l2[21];
    (void)coin_value;
    (void)coin_code;
    sdk_coin_chime();
    if (ts.state == TS_AWAIT_FARE) {
        if (sdk_balance() >= ts.fare_cents) {
            ts_try_connect(ts.pending_year);
        } else {
            ts_render_need();
        }
    } else if (ts.state == TS_OPERATOR) {
        snprintf(l2, sizeof(l2), "CREDIT $%d.%02d", sdk_balance() / 100,
                 sdk_balance() % 100);
        sdk_display("TIME OPERATOR", l2);
    }
    return 0;
}

static int ts_handle_keypad(char key) {
    const era_t *e;

    switch (ts.state) {
    case TS_OPERATOR:
        if (key >= '0' && key <= '9') {
            if (ts.year_len < 4) {
                ts.year_buf[ts.year_len++] = key;
                ts.year_buf[ts.year_len] = '\0';
                sdk_beep(key);
                ts_render_operator();
                if (ts.year_len == 4) ts_eval_year();
            }
        } else if (key == '*') {
            if (ts.year_len > 0) ts.year_buf[--ts.year_len] = '\0';
            ts.banner = "TIME OPERATOR";
            ts_render_operator();
        } else if (key == '#') {
            ts.year_buf[0] = '\0';
            ts.year_len = 0;
            ts.banner = "TIME OPERATOR";
            ts_render_operator();
        }
        break;

    case TS_AWAIT_FARE:
        if (key == '*' || key == '#') {
            ts_enter_operator(NULL);
        } else {
            sdk_beep(key);
        }
        break;

    case TS_IN_ERA:
        e = &eras[ts.era];
        ts.last_input_at = sdk_now();
        if (key == '#') {
            ts_enter_operator(NULL);
        } else if (key == '1') {
            if (ts.armed && ts.current_year == ts.source_year) {
                ts.armed = 0;
                sdk_logf(TS_CAT, "Paradox mended at %d", ts.current_year);
                sdk_display("TIMELINE", "MENDED");
            } else {
                sdk_display(e->label, e->observe);
            }
        } else if (key == '2') {
            ts.armed = 1;
            ts.source_year = ts.current_year;
            sdk_display("TIMELINE BENT", e->interfere);
        } else {
            sdk_beep(key);
        }
        break;

    case TS_PARADOX:
        if (key == '#') {
            ts_enter_operator(NULL);
        } else {
            sdk_beep(key);
        }
        break;

    default:
        break;
    }
    return 0;
}

static int ts_handle_hook(int hook_up, int hook_down) {
    if (hook_up) {
        ts_enter_operator(NULL);
    } else if (hook_down) {
        sdk_stop_audio();
        ts.state = TS_DORMANT;
        ts.year_buf[0] = '\0';
        ts.year_len = 0;
        ts_render_dormant();
    }
    return 0;
}

static int ts_handle_card(const char *card_number) {
    (void)card_number;
    ts.pass_active = 1;
    sdk_log(TS_CAT, "Temporal pass accepted");
    sdk_display("TEMPORAL PASS", "PASS ACCEPTED");
    return 0;
}

static void ts_handle_activation(void) {
    memset(&ts, 0, sizeof(ts));
    if (sdk_receiver_is_up()) {
        ts_enter_operator(NULL);
    } else {
        ts.state = TS_DORMANT;
        ts_render_dormant();
    }
}

static void ts_handle_tick(void) {
    const era_t *e;

    switch (ts.state) {
    case TS_CONNECTING:
        if (sdk_elapsed(ts.frame_at) >= FRAME_SECONDS) {
            ts.frame_index++;
            ts.frame_at = sdk_now();
            if (ts.frame_index < CONNECT_FRAMES) {
                ts_render_connecting();
            } else if (ts.dest < 0) {
                ts_enter_drop();
            } else {
                ts_enter_era(ts.dest);
            }
        }
        break;

    case TS_IN_ERA:
        if (sdk_elapsed(ts.last_input_at) >= DRIFT_SECONDS) {
            ts_enter_operator("TEMPORAL DRIFT");
            break;
        }
        e = &eras[ts.era];
        if (ts.frame_index < e->frame_count - 1 &&
            sdk_elapsed(ts.frame_at) >= FRAME_SECONDS) {
            ts.frame_index++;
            ts.frame_at = sdk_now();
            ts_render_era();
        }
        break;

    case TS_DROP:
        if (sdk_elapsed(ts.frame_at) >= DROP_SECONDS) {
            ts_enter_operator("LINE DROPPED");
        }
        break;

    default:
        break;
    }
}

/* Test/introspection hook (see test_plugin_display_lines_fit): expose every
 * authored display string so the guardrail can confirm none exceeds the line
 * budget. Returns the total count; fills up to `max` into out[]. */
int time_operator_display_strings(const char **out, int max) {
    static const char *const ui[] = {
        "THE OPERATOR", "Lift to dial", "TIME OPERATOR", "DIAL A YEAR  #=BACK",
        "CONNECTING...", "ringing...", "ALMOST 2000...", "NEVER MIDNIGHT",
        "LINE FAULT 19##", "#=OPERATOR", "TEMPORAL PASS", "PASS ACCEPTED",
        "TIMELINE", "MENDED", "TIMELINE BENT", "NO SUCH YEAR", "FUTURE SEALED",
        "TEMPORAL DRIFT", "LINE DROPPED"
    };
    int n = 0, i, f;
    for (i = 0; i < (int)(sizeof(ui) / sizeof(ui[0])); i++) {
        if (out && n < max) out[n] = ui[i];
        n++;
    }
    for (i = 0; i < NUM_ERAS; i++) {
        if (out && n < max) out[n] = eras[i].label;       n++;
        if (out && n < max) out[n] = eras[i].observe;     n++;
        if (out && n < max) out[n] = eras[i].interfere;   n++;
        for (f = 0; f < eras[i].frame_count; f++) {
            if (out && n < max) out[n] = eras[i].frames[f].l1; n++;
            if (out && n < max) out[n] = eras[i].frames[f].l2; n++;
        }
    }
    return n;
}

void register_time_operator_plugin(void) {
    memset(&ts, 0, sizeof(ts));
    ts.state = TS_DORMANT;

    plugins_register("The Operator",
                     "Dial a year - a time-travel story line",
                     ts_handle_coin,
                     ts_handle_keypad,
                     ts_handle_hook,
                     NULL,
                     ts_handle_card,
                     ts_handle_activation,
                     ts_handle_tick);
}
