#include "call_metrics.h"
#include "clock_source.h"
#include "metrics.h"

/* Name of the histogram populated by call_metrics_ended(). */
#define CALL_DURATION_HISTOGRAM "call_duration_seconds"

/* Histogram and counter populated by the ring functions. */
#define CALL_RING_HISTOGRAM "call_ring_seconds"
#define CALLS_MISSED_COUNTER "calls_missed"

/* In-progress call/ring timing. Accessed only from the daemon's main event-loop
 * thread (and the single-threaded simulator / unit tests), each under the same
 * serialization as the surrounding call/hook handlers, so no extra lock here. */
static time_t g_call_start;
static int    g_call_in_progress;
static time_t g_ring_start;
static int    g_ring_in_progress;

void call_metrics_started(void) {
    g_call_start = mclock_now();
    g_call_in_progress = 1;
}

void call_metrics_ended(void) {
    time_t now;
    double elapsed;

    if (!g_call_in_progress) {
        return;
    }

    now = mclock_now();
    elapsed = (double)(now - g_call_start);
    /* Guard against a clock that moved backwards (NTP step, manual set). */
    if (elapsed < 0.0) {
        elapsed = 0.0;
    }

    metrics_observe_histogram(CALL_DURATION_HISTOGRAM, elapsed);

    g_call_in_progress = 0;
    g_call_start = 0;
}

void call_metrics_ringing_started(void) {
    g_ring_start = mclock_now();
    g_ring_in_progress = 1;
}

/* Observe the elapsed ring time into the call_ring_seconds histogram and clear
 * the ring timer. Returns 1 if a ring was actually in progress (and therefore
 * recorded), 0 otherwise, so callers can attribute the outcome accordingly. */
static int call_metrics_ringing_observe(void) {
    time_t now;
    double elapsed;

    if (!g_ring_in_progress) {
        return 0;
    }

    now = mclock_now();
    elapsed = (double)(now - g_ring_start);
    /* Guard against a clock that moved backwards (NTP step, manual set). */
    if (elapsed < 0.0) {
        elapsed = 0.0;
    }

    metrics_observe_histogram(CALL_RING_HISTOGRAM, elapsed);

    g_ring_in_progress = 0;
    g_ring_start = 0;
    return 1;
}

void call_metrics_ringing_answered(void) {
    (void)call_metrics_ringing_observe();
}

void call_metrics_ringing_missed(void) {
    if (call_metrics_ringing_observe()) {
        metrics_increment_counter(CALLS_MISSED_COUNTER, 1);
    }
}

void call_metrics_reset(void) {
    g_call_in_progress = 0;
    g_call_start = 0;
    g_ring_in_progress = 0;
    g_ring_start = 0;
}
