#ifndef STATE_PERSISTENCE_H
#define STATE_PERSISTENCE_H

#include <stddef.h>
#include "daemon_state.h"

typedef struct {
    int inserted_cents;
    int last_state;         /* daemon_state_t enum value */
    char active_plugin[64];
} persisted_state_t;

/*
 * Upper bound on a restorable coin balance. A real payphone session never
 * holds anywhere near $1000; a value above this in the state file indicates
 * corruption (overflow, a hand-edit, or a torn write), not a real balance.
 */
#define STATE_MAX_INSERTED_CENTS 100000

/*
 * Save current state to disk. Uses fsync for durability.
 * Returns 0 on success, -1 on failure.
 */
int state_persistence_save(const persisted_state_t *state, const char *filepath);

/*
 * Load persisted state from disk.
 * Returns 0 on success, -1 if file doesn't exist or is corrupt.
 * On failure, *state is zeroed out.
 */
int state_persistence_load(persisted_state_t *state, const char *filepath);

/*
 * Like state_persistence_load(), but every value read from disk is
 * range-checked before it is trusted: inserted_cents must be a non-negative
 * integer in 0..STATE_MAX_INSERTED_CENTS, last_state must be a valid
 * daemon_state_t enum value, and active_plugin must be a printable,
 * length-bounded name. Any malformed or out-of-range field fails the load
 * (returning -1 with *state zeroed) so a corrupt file can never restore a
 * bogus coin balance or state.
 *
 * On a corrupt file, a human-readable reason is written to err (if non-NULL
 * and err_size > 0). When the file is simply absent, the load fails but err
 * is left empty, so callers can distinguish "no saved state" (normal first
 * boot) from "corrupt saved state" (worth warning about).
 */
int state_persistence_load_ex(persisted_state_t *state, const char *filepath,
                              char *err, size_t err_size);

#endif /* STATE_PERSISTENCE_H */
