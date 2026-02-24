#ifndef STATE_PERSISTENCE_H
#define STATE_PERSISTENCE_H

#include "daemon_state.h"

typedef struct {
    int inserted_cents;
    int last_state;         /* daemon_state_t enum value */
    char active_plugin[64];
} persisted_state_t;

/*
 * Save current state to disk. When use_fsync is non-zero, calls fsync for
 * durability; set to 0 on slow storage to avoid stalling (#120).
 * Returns 0 on success, -1 on failure.
 */
int state_persistence_save(const persisted_state_t *state, const char *filepath, int use_fsync);

/*
 * Load persisted state from disk.
 * Returns 0 on success, -1 if file doesn't exist or is corrupt.
 * On failure, *state is zeroed out.
 */
int state_persistence_load(persisted_state_t *state, const char *filepath);

#endif /* STATE_PERSISTENCE_H */
