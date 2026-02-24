#define _POSIX_C_SOURCE 200112L
#include "state_persistence.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int state_persistence_save(const persisted_state_t *state, const char *filepath, int use_fsync) {
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
    if (use_fsync) {
        fd = fileno(f);
        if (fd >= 0) fsync(fd);
    }
    fclose(f);

    if (rename(tmp_path, filepath) != 0) {
        logger_warn_with_category("Persistence", "Failed to rename state file");
        remove(tmp_path);
        return -1;
    }

    return 0;
}

int state_persistence_load(persisted_state_t *state, const char *filepath) {
    FILE *f;
    char line[256];

    if (!state || !filepath) return -1;

    memset(state, 0, sizeof(persisted_state_t));

    f = fopen(filepath, "r");
    if (!f) {
        return -1;
    }

    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        char *newline;
        if (!eq) continue;

        *eq = '\0';
        eq++;

        newline = strchr(eq, '\n');
        if (newline) *newline = '\0';

        if (strcmp(line, "inserted_cents") == 0) {
            state->inserted_cents = atoi(eq);
        } else if (strcmp(line, "last_state") == 0) {
            state->last_state = atoi(eq);
        } else if (strcmp(line, "active_plugin") == 0) {
            strncpy(state->active_plugin, eq, sizeof(state->active_plugin) - 1);
            state->active_plugin[sizeof(state->active_plugin) - 1] = '\0';
        }
    }

    fclose(f);
    return 0;
}
