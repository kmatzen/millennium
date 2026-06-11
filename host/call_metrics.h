#ifndef CALL_METRICS_H
#define CALL_METRICS_H

/*
 * Call-duration instrumentation.
 *
 * A call is "connected" from the moment audio is established (the phone enters
 * CALL_ACTIVE) until either party hangs up. call_metrics_started() stamps the
 * connect time; call_metrics_ended() observes the elapsed wall-clock seconds
 * into the `call_duration_seconds` histogram, exposing count/sum/min/max/mean/
 * percentiles to Prometheus. For a payphone that bills by the call, the spread
 * of call lengths is the headline usage signal the coin counters can't show.
 *
 * Timestamps are read through mclock_now() (clock_source.h), so the scenario
 * simulator's advanceable clock measures duration with no real waiting and the
 * same code path runs on the Pi, in the simulator, and in unit tests.
 *
 * The pair is order-tolerant: an end without a start (e.g. hanging up while a
 * call was still only ringing) is a no-op, and a second start simply restamps.
 * Handlers can therefore call them defensively without tracking prior state.
 */

/* Mark the start of a connected call (entry into CALL_ACTIVE). */
void call_metrics_started(void);

/* Mark the end of a connected call; records its duration if one was started. */
void call_metrics_ended(void);

/* Discard any in-progress call timing without recording it (test seam). */
void call_metrics_reset(void);

#endif /* CALL_METRICS_H */
