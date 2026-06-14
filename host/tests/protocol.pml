/*
 * SPIN model of the Pi <-> Arduino (Beta) display-command wire protocol.
 *
 * Mirrors the framing in Arduino/sketches/display/display.ino loop():
 *   STX (0x02), one length byte, bounds-check (num_bytes > sizeof(buf) -> bail),
 *   then num_bytes data bytes copied into buf[BUFSZ].
 * waitForSerial()'s 2-second timeout (which makes the receiver bail and resync
 * instead of blocking) is modeled as a nondeterministic bail. The Pi opens the
 * serial fd O_NONBLOCK, so it can't block forever either.
 *
 * BUFSZ and the length range are scaled down; the bounds-check comparison
 * (strict '>' against the buffer size, allowing len == BUFSZ) is modeled exactly
 * as in the firmware, so the off-by-one is what gets verified — the constant's
 * magnitude doesn't change that logic.
 *
 * Properties checked (make model-check):
 *   - overrun-freedom : assert(i < BUFSZ) on every buffer write (safety)
 *   - deadlock-freedom: no invalid end states (SPIN default safety check)
 *   - full-consumption: ltl progress { []<> at_idle } -- the receiver always
 *     returns to idle, i.e. it can never get permanently stuck mid-frame
 *     (any desync/partial/garbage frame self-resyncs).
 */

#define BUFSZ 4
#define STX   2
#define DAT   1     /* any non-STX data/noise byte */

chan link = [2] of { byte };   /* Pi -> Arduino serial bytes */

bool at_idle = true;
byte widx;                     /* last buffer write index (for inspection) */

active proctype Arduino() {
    byte data, num_bytes, i;
idle:
    at_idle = true;
    if
    :: link ? data ->
        at_idle = false;
        if
        :: data == STX ->
            if
            :: link ? num_bytes ->                 /* got the length byte */
                if
                :: num_bytes > BUFSZ -> goto idle  /* bounds check: reject + bail */
                :: else ->
                    i = 0;
                    do
                    :: i < num_bytes ->
                        if
                        :: link ? _ ->
                            assert(i < BUFSZ);      /* OVERRUN-FREEDOM */
                            widx = i;
                            i++
                        :: skip -> goto idle        /* waitForSerial timeout -> resync */
                        fi
                    :: else -> break
                    od;
                    goto idle                       /* frame delivered */
                fi
            :: skip -> goto idle                    /* timeout waiting for length */
            fi
        :: else -> goto idle                        /* other command handled */
        fi
    :: skip -> goto idle                            /* nothing on the line yet */
    fi;
    goto idle
}

active proctype Pi() {
    byte n, k;
    do
    :: /* a possibly-malformed display frame: STX, an arbitrary length, then
        * 0..n data bytes (under-sending models an aborted/desynced frame). */
        link ! STX;
        if
        :: n = 0
        :: n = 1
        :: n = 2
        :: n = 3
        :: n = 4
        :: n = 5    /* > BUFSZ: must be rejected by the bounds check */
        :: n = 6
        fi;
        link ! n;
        k = 0;
        do
        :: k < n -> link ! DAT; k++
        :: k < n -> break          /* abort mid-frame */
        :: else  -> break
        od
    :: link ! DAT                  /* stray noise byte (no STX) */
    od
}

ltl progress { [] <> at_idle }
