#include "version.h"

const char *version_get_string(void) {
    return VERSION_STRING;
}

const char *version_get_git_hash(void) {
    return GIT_HASH;
}

const char *version_get_build_time(void) {
    return BUILD_TIME;
}
