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

#ifndef VERSION_STRING
#define VERSION_STRING "0.0.0-development"
#endif

const char *version_get_string(void);
const char *version_get_git_hash(void);
const char *version_get_build_time(void);

#endif /* VERSION_H */
