#ifndef VERSION_H
#define VERSION_H

/*
 * Build version info. GIT_HASH and BUILD_TIME are injected by the
 * Makefile via -D flags. Provide sensible defaults for standalone
 * compilation or the simulator.
 */

#ifndef GIT_HASH
#define GIT_HASH "unknown"
#endif

#ifndef BUILD_TIME
#define BUILD_TIME "unknown"
#endif

#define VERSION_MAJOR 0
#define VERSION_MINOR 2
#define VERSION_PATCH 0
#define VERSION_STRING "0.2.0"

const char *version_get_string(void);
const char *version_get_git_hash(void);
const char *version_get_build_time(void);

#endif /* VERSION_H */
