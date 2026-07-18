#ifndef UPDATER_H
#define UPDATER_H

#include <stddef.h>   /* size_t */

/*
 * OTA update checker.
 * Compares the running version against the latest GitHub release tag.
 */

/* Semantic version comparison.
 * Returns <0 if a < b, 0 if equal, >0 if a > b.
 * Expects "X.Y.Z" format (leading 'v' is stripped). */
int updater_compare_versions(const char *a, const char *b);

/*
 * Check GitHub for the latest release tag (blocking; blocks up to 10s).
 * Prefer updater_check_async() for HTTP handlers.
 */
int updater_check(void);

/* #119: Non-blocking. Starts background check if idle; returns immediately. */
void updater_check_async(void);

/* Returns 1 if a check is in progress. */
int updater_is_checking(void);

/* Returns the cached latest version string, or NULL if never checked. */
/* Copy the last known release tag into `out` (always NUL-terminated).
 * Returns 1 if a version is known, 0 otherwise (out becomes ""). Copying under
 * the lock is the point: returning the internal pointer let callers read the
 * buffer while the check thread was writing it (#227). */
int updater_get_latest_version(char *out, size_t out_size);

/* Returns 1 if the latest version is newer than the running version. */
int updater_is_update_available(void);

/*
 * Apply an update: git pull, rebuild, then restart the daemon via
 * systemd.  Blocks for minutes. Prefer updater_apply_async for HTTP handlers.
 *
 * Returns  0 on success (successful restart kills the process).
 * Returns -1 if git pull or build fails.
 */
int updater_apply(const char *source_dir);

/* #118: Non-blocking. Starts apply in background; returns immediately. */
int updater_apply_async(const char *source_dir);

/* Returns 1 if an apply is in progress. */
int updater_is_applying(void);

/* Get a human-readable status message from the last updater_apply call. */
/* Copy the current apply status into `out` (always NUL-terminated). Copying
 * under the lock is the point -- see updater_get_latest_version (#227). */
void updater_get_apply_status(char *out, size_t out_size);

#endif /* UPDATER_H */
