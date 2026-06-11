#define _POSIX_C_SOURCE 200112L
#include "state_persistence.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

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

    return 0;
}

/*
 * Parse a whole token as a base-10 int. Returns 1 only if the entire (trimmed)
 * token is a valid integer with no trailing garbage; 0 otherwise. This is
 * stricter than atoi(), which silently yields 0 for non-numeric input.
 */
static int parse_strict_int(const char *s, int *out) {
    char *end;
    long val;

    if (!s || *s == '\0') return 0;
    errno = 0;
    val = strtol(s, &end, 10);
    if (end == s) return 0;          /* no digits consumed */
    while (*end == ' ' || *end == '\t') end++;
    if (*end != '\0') return 0;      /* trailing non-numeric garbage */
    if (errno == ERANGE || val < INT_MIN || val > INT_MAX) return 0;
    *out = (int)val;
    return 1;
}

int state_persistence_load(persisted_state_t *state, const char *filepath) {
    FILE *f;
    char line[256];
    int have_cents = 0, have_state = 0;

    if (!state || !filepath) return -1;

    memset(state, 0, sizeof(persisted_state_t));

    f = fopen(filepath, "r");
    if (!f) {
        /* Missing file is the normal first-boot case; not an error worth logging. */
        return -1;
    }

    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        char *val, *nl;
        if (!eq) continue;

        *eq = '\0';
        val = eq + 1;

        /* Strip the trailing newline (and a stray CR from CRLF files). */
        nl = val + strlen(val);
        while (nl > val && (nl[-1] == '\n' || nl[-1] == '\r')) *--nl = '\0';

        if (strcmp(line, "inserted_cents") == 0) {
            int v;
            if (!parse_strict_int(val, &v) || v < 0 || v > STATE_MAX_INSERTED_CENTS) {
                logger_warn_with_category("Persistence",
                    "Corrupt state file: invalid inserted_cents; ignoring");
                goto corrupt;
            }
            state->inserted_cents = v;
            have_cents = 1;
        } else if (strcmp(line, "last_state") == 0) {
            int v;
            if (!parse_strict_int(val, &v) ||
                v < (int)DAEMON_STATE_INVALID || v > (int)DAEMON_STATE_CALL_ACTIVE) {
                logger_warn_with_category("Persistence",
                    "Corrupt state file: invalid last_state; ignoring");
                goto corrupt;
            }
            state->last_state = v;
            have_state = 1;
        } else if (strcmp(line, "active_plugin") == 0) {
            strncpy(state->active_plugin, val, sizeof(state->active_plugin) - 1);
            state->active_plugin[sizeof(state->active_plugin) - 1] = '\0';
        }
    }

    fclose(f);

    /* The writer always emits both numeric fields; a file missing either one is
     * truncated/corrupt, so reject it rather than restore a half-state. */
    if (!have_cents || !have_state) {
        logger_warn_with_category("Persistence",
            "Corrupt state file: missing required fields; ignoring");
        memset(state, 0, sizeof(persisted_state_t));
        return -1;
    }

    return 0;

corrupt:
    fclose(f);
    memset(state, 0, sizeof(persisted_state_t));
    return -1;
}
