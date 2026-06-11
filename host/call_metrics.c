#include "call_metrics.h"
#include "clock_source.h"
#include "metrics.h"

/* Name of the histogram populated by call_metrics_ended(). */
#define CALL_DURATION_HISTOGRAM "call_duration_seconds"

/* In-progress call timing. Accessed only from the daemon's main event-loop
 * thread (and the single-threaded simulator / unit tests), each under the same
 * serialization as the surrounding call/hook handlers, so no extra lock here. */
static time_t g_call_start;
static int    g_call_in_progress;

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

void call_metrics_reset(void) {
    g_call_in_progress = 0;
    g_call_start = 0;
}
