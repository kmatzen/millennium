#ifndef CALL_METRICS_H
#define CALL_METRICS_H

/*
 * Call-duration and ring instrumentation.
 *
 * A call is "connected" from the moment audio is established (the phone enters
 * CALL_ACTIVE) until either party hangs up. call_metrics_started() stamps the
 * connect time; call_metrics_ended() observes the elapsed wall-clock seconds
 * into the `call_duration_seconds` histogram, exposing count/sum/min/max/mean/
 * percentiles to Prometheus. For a payphone that bills by the call, the spread
 * of call lengths is the headline usage signal the coin counters can't show.
 *
 * The ring functions cover the phase *before* a call connects. An incoming call
 * "rings" from the moment it arrives (CALL_INCOMING) until it is either answered
 * (handset lifted / SIP answer) or ends unanswered (caller gives up, timeout).
 * call_metrics_ringing_started() stamps the ring; _answered() and
 * _incoming_ended() observe the ring-to-resolution seconds into the
 * `call_ring_seconds` histogram. Together they expose how long the phone rings
 * and how often a ring goes unanswered — neither of which the existing
 * calls_incoming/calls_answered counters can reveal.
 *
 * call_metrics_incoming_ended() resolves the single daemon transition where a
 * CALL_INCOMING phase ends *without* the call ever becoming active, and counts
 * exactly one of two mutually exclusive outcomes:
 *   - a genuine inbound ring was in progress -> the caller gave up: records the
 *     ring time and bumps the `calls_missed` counter;
 *   - no ring was armed -> the phone was placing an OUTBOUND call that never
 *     connected (busy / no answer / rejected / SIP or network error), so it
 *     bumps the `calls_failed` counter instead. Outbound dial failures were
 *     previously invisible to monitoring; for a payphone that bills by the
 *     call, the connect-failure rate (the gap between calls_initiated and
 *     calls_established) is the headline reliability signal.
 *
 * Timestamps are read through mclock_now() (clock_source.h), so the scenario
 * simulator's advanceable clock measures duration with no real waiting and the
 * same code path runs on the Pi, in the simulator, and in unit tests.
 *
 * The ring start/answer pair is order-tolerant: a resolution without a matching
 * start records nothing into the histogram, and a second start simply restamps.
 * Because the ring timer is only armed by call_metrics_ringing_started() on a
 * genuine inbound ring, the web-initiated outbound "start_call" path (which
 * reuses the CALL_INCOMING state but never starts a ring) is never miscounted as
 * a missed call — call_metrics_incoming_ended() attributes it to calls_failed.
 */

/* Mark the start of a connected call (entry into CALL_ACTIVE). */
void call_metrics_started(void);

/* Mark the end of a connected call; records its duration if one was started. */
void call_metrics_ended(void);

/* Mark the start of an incoming call's ring (entry into CALL_INCOMING from a
 * genuine inbound ring). Pairs with _answered()/_missed() below. */
void call_metrics_ringing_started(void);

/* Mark a ringing call as answered; records the ring-to-answer duration into the
 * call_ring_seconds histogram. No-op if no ring was in progress. */
void call_metrics_ringing_answered(void);

/* Resolve a CALL_INCOMING phase that ended before the call became active.
 * If an inbound ring was in progress, records the ring duration into
 * call_ring_seconds and increments calls_missed (the caller gave up); otherwise
 * the phone was dialing out and the attempt never connected, so it increments
 * calls_failed instead. Exactly one counter moves, so a transition is never
 * double-counted as both a miss and a failure. */
void call_metrics_incoming_ended(void);

/* Discard any in-progress call/ring timing without recording it (test seam). */
void call_metrics_reset(void);

#endif /* CALL_METRICS_H */
