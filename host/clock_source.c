#include "clock_source.h"

/* NULL in production: mclock_now() falls through to the real time(NULL). The
 * simulator installs a source returning its advanceable clock. */
static time_t (*g_clock_source)(void) = 0;

time_t mclock_now(void) {
    return g_clock_source ? g_clock_source() : time(NULL);
}

void mclock_set_source(time_t (*source)(void)) {
    g_clock_source = source;
}
