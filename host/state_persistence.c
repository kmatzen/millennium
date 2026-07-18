#define _POSIX_C_SOURCE 200112L
#include "state_persistence.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

int state_persistence_save(const persisted_state_t *state, const char *filepath) {
    FILE *f;
    char tmp_path[512];
    int fd;

    if (!state || !filepath) return -1;

    /* Write to a temp file then rename for atomicity */
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", filepath);

    f = fopen(tmp_path, "w");
    if (!f) {
        logger_warn_with_category("Persistence", "Failed to open state file for writing");
        return -1;
    }

    fprintf(f, "inserted_cents=%d\n", state->inserted_cents);
    fprintf(f, "last_state=%d\n", state->last_state);
    fprintf(f, "active_plugin=%s\n", state->active_plugin);

    fflush(f);
    fd = fileno(f);
    if (fd >= 0) {
        fsync(fd);
    }
    fclose(f);

    if (rename(tmp_path, filepath) != 0) {
        logger_warn_with_category("Persistence", "Failed to rename state file");
        remove(tmp_path);
        return -1;
    }

    /* Sync the parent directory so the rename itself is durable (#228).
     * fsync on the file only makes its *contents* durable; the directory entry
     * created by rename() is not, so a power cut here could roll the balance
     * back to the previous file or leave the .tmp behind. The phone runs on
     * mains with no UPS, so this is a routine event, not a corner case.
     *
     * Best-effort: a failure to sync the directory does not invalidate the
     * write that already succeeded, so it is logged rather than returned. */
    {
        char dir_path[512];
        char *slash;
        int dfd;

        strncpy(dir_path, filepath, sizeof(dir_path) - 1);
        dir_path[sizeof(dir_path) - 1] = '\0';
        slash = strrchr(dir_path, '/');
        if (slash == dir_path) {
            dir_path[1] = '\0';          /* file sits in "/" */
        } else if (slash) {
            *slash = '\0';
        } else {
            strcpy(dir_path, ".");       /* relative path, no directory part */
        }

        dfd = open(dir_path, O_RDONLY);
        if (dfd >= 0) {
            if (fsync(dfd) != 0) {
                logger_warn_with_category("Persistence",
                        "Failed to fsync state directory; save may not survive power loss");
            }
            close(dfd);
        } else {
            logger_warn_with_category("Persistence",
                    "Failed to open state directory for fsync");
        }
    }

    return 0;
}

/* Record a corruption reason into the caller's buffer (if any). */
#define STATE_FAIL(...)                                   \
    do {                                                  \
        if (err != NULL && err_size > 0) {                \
            snprintf(err, err_size, __VA_ARGS__);         \
        }                                                 \
    } while (0)

/*
 * Parse a base-10 integer that occupies the entire (whitespace-trimmed)
 * string. Returns 0 and stores the value in *out on success; -1 for any
 * malformed input: empty, non-numeric, trailing junk, or out of long range.
 * Unlike atoi(), garbage does not silently become 0.
 */
static int parse_strict_long(const char *s, long *out) {
    char *end;
    long v;

    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    if (*s == '\0') return -1;

    errno = 0;
    v = strtol(s, &end, 10);
    if (end == s) return -1;          /* no digits consumed */
    if (errno == ERANGE) return -1;   /* overflowed long */

    while (*end == ' ' || *end == '\t' || *end == '\r') end++;
    if (*end != '\0') return -1;      /* trailing junk after the number */

    *out = v;
    return 0;
}

/*
 * A persisted plugin name is trusted only if it fits the field and contains
 * nothing but printable characters. The empty string is valid and means "no
 * active plugin". This rejects control bytes and truncated/oversized names
 * that a torn write or hand-edit could leave behind.
 */
static int plugin_name_is_valid(const char *s) {
    size_t i;
    size_t len = strlen(s);

    if (len >= 64) return 0;          /* would not fit persisted_state_t.active_plugin */
    for (i = 0; i < len; i++) {
        if (!isprint((unsigned char)s[i])) return 0;
    }
    return 1;
}

int state_persistence_load_ex(persisted_state_t *state, const char *filepath,
                              char *err, size_t err_size) {
    FILE *f;
    char line[256];

    if (err != NULL && err_size > 0) err[0] = '\0';

    if (!state || !filepath) return -1;

    memset(state, 0, sizeof(persisted_state_t));

    f = fopen(filepath, "r");
    if (!f) {
        /* Absent file is normal first-boot, not corruption: leave err empty. */
        return -1;
    }

    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        char *key;
        char *val;
        char *newline;
        if (!eq) continue;

        *eq = '\0';
        key = line;
        val = eq + 1;

        newline = strchr(val, '\n');
        if (newline) *newline = '\0';

        if (strcmp(key, "inserted_cents") == 0) {
            long v;
            if (parse_strict_long(val, &v) != 0 ||
                v < 0 || v > STATE_MAX_INSERTED_CENTS) {
                STATE_FAIL("inserted_cents out of range 0..%d (got '%s')",
                           STATE_MAX_INSERTED_CENTS, val);
                fclose(f);
                memset(state, 0, sizeof(persisted_state_t));
                return -1;
            }
            state->inserted_cents = (int)v;
        } else if (strcmp(key, "last_state") == 0) {
            long v;
            if (parse_strict_long(val, &v) != 0 ||
                v < (long)DAEMON_STATE_INVALID ||
                v > (long)DAEMON_STATE_CALL_ACTIVE) {
                STATE_FAIL("last_state out of range %d..%d (got '%s')",
                           (int)DAEMON_STATE_INVALID,
                           (int)DAEMON_STATE_CALL_ACTIVE, val);
                fclose(f);
                memset(state, 0, sizeof(persisted_state_t));
                return -1;
            }
            state->last_state = (int)v;
        } else if (strcmp(key, "active_plugin") == 0) {
            if (!plugin_name_is_valid(val)) {
                STATE_FAIL("active_plugin is not a valid name");
                fclose(f);
                memset(state, 0, sizeof(persisted_state_t));
                return -1;
            }
            strncpy(state->active_plugin, val, sizeof(state->active_plugin) - 1);
            state->active_plugin[sizeof(state->active_plugin) - 1] = '\0';
        }
        /* Unknown keys are ignored for forward compatibility. */
    }

    fclose(f);
    return 0;
}

int state_persistence_load(persisted_state_t *state, const char *filepath) {
    return state_persistence_load_ex(state, filepath, NULL, 0);
}
