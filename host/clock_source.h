#ifndef CLOCK_SOURCE_H
#define CLOCK_SOURCE_H

#include <time.h>

/*
 * Clock seam — the single function all daemon/plugin timing reads through.
 *
 * The daemon's timing logic (idle timeouts, "reveal the answer in 2s", coin
 * return windows, …) used to call time(NULL) directly. That made simulated
 * time depend on a Linux-only -Wl,--wrap=time linker trick: on the Mac (the
 * primary dev box) the wrap doesn't apply, so the scenario simulator's `wait`
 * had to fall back to a real nanosleep — making `make test` actually sleep.
 *
 * Routing every timing read through mclock_now() removes that platform split.
 * In production no source is installed, so mclock_now() == time(NULL). The
 * simulator installs a source that returns its advanceable clock, so `wait`
 * advances time instantly on every platform.
 */

/* Current wall-clock time in seconds, honoring an installed source (see
 * mclock_set_source). With no source installed this is exactly time(NULL). */
time_t mclock_now(void);

/* Install (or, with NULL, clear) the clock source. The simulator points this
 * at its advanceable clock; the live daemon leaves it NULL. */
void mclock_set_source(time_t (*source)(void));

#endif /* CLOCK_SOURCE_H */
