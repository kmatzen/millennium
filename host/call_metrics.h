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
 * call_metrics_ringing_started() stamps the ring; _answered()/_missed() observe
 * the ring-to-resolution seconds into the `call_ring_seconds` histogram, and
 * _missed() additionally bumps the `calls_missed` counter. Together they expose
 * how long the phone rings and how often a ring goes unanswered — neither of
 * which the existing calls_incoming/calls_answered counters can reveal.
 *
 * Timestamps are read through mclock_now() (clock_source.h), so the scenario
 * simulator's advanceable clock measures duration with no real waiting and the
 * same code path runs on the Pi, in the simulator, and in unit tests.
 *
 * Every pair is order-tolerant: an end/resolution without a matching start is a
 * no-op, and a second start simply restamps. Handlers can therefore call them
 * defensively without tracking prior state. Crucially, because the ring timer is
 * only armed by call_metrics_ringing_started() on a genuine inbound ring, the
 * web-initiated outbound "start_call" path (which reuses the CALL_INCOMING state
 * but never starts a ring) is never miscounted as a missed call.
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

/* Mark a ringing call as missed (it rang but ended before being answered);
 * records the ring duration into call_ring_seconds and increments the
 * calls_missed counter. No-op if no ring was in progress. */
void call_metrics_ringing_missed(void);

/* Discard any in-progress call/ring timing without recording it (test seam). */
void call_metrics_reset(void);

#endif /* CALL_METRICS_H */
