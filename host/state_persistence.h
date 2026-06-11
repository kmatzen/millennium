#ifndef STATE_PERSISTENCE_H
#define STATE_PERSISTENCE_H

#include "daemon_state.h"

typedef struct {
    int inserted_cents;
    int last_state;         /* daemon_state_t enum value */
    char active_plugin[64];
} persisted_state_t;

/*
 * Sanity bound on a restored coin balance. A real balance is a few dollars;
 * anything beyond this is corruption (e.g. a torn write or random garbage), so
 * the loader rejects the whole file rather than restoring an absurd balance.
 * 100000 cents = $1000, far above any plausible inserted amount.
 */
#define STATE_MAX_INSERTED_CENTS 100000

/*
 * Save current state to disk. Uses fsync for durability.
 * Returns 0 on success, -1 on failure.
 */
int state_persistence_save(const persisted_state_t *state, const char *filepath);

/*
 * Load persisted state from disk and validate it.
 *
 * A file is accepted only if it carries both numeric fields and every value is
 * in range: inserted_cents in [0, STATE_MAX_INSERTED_CENTS] and last_state a
 * valid daemon_state_t (DAEMON_STATE_INVALID..DAEMON_STATE_CALL_ACTIVE). A
 * missing file, a missing required field, a non-numeric value, or an
 * out-of-range value is treated as "no usable state".
 *
 * Returns 0 on success, -1 if the file doesn't exist or is corrupt. On failure
 * *state is zeroed out, so the caller can boot as if no state were persisted.
 */
int state_persistence_load(persisted_state_t *state, const char *filepath);

#endif /* STATE_PERSISTENCE_H */
