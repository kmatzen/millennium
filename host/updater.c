#define _POSIX_C_SOURCE 200112L
#include "updater.h"
#include "version.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char latest_version[64] = {0};
static int  checked = 0;

static int parse_version(const char *s, int *major, int *minor, int *patch) {
    if (!s) return -1;
    if (*s == 'v' || *s == 'V') s++;
    if (sscanf(s, "%d.%d.%d", major, minor, patch) != 3) return -1;
    return 0;
}

int updater_compare_versions(const char *a, const char *b) {
    int a_maj = 0, a_min = 0, a_pat = 0;
    int b_maj = 0, b_min = 0, b_pat = 0;

    if (parse_version(a, &a_maj, &a_min, &a_pat) != 0) return -1;
    if (parse_version(b, &b_maj, &b_min, &b_pat) != 0) return 1;

    if (a_maj != b_maj) return a_maj - b_maj;
    if (a_min != b_min) return a_min - b_min;
    return a_pat - b_pat;
}

int updater_check(void) {
    FILE *fp;
    char buf[4096];
    const char *tag;
    size_t len;

    /*
     * Query GitHub Releases API.  curl is available on Raspberry Pi OS
     * by default; the -s flag suppresses progress, -m limits timeout.
     */
    fp = popen("curl -s -m 10 "
               "https://api.github.com/repos/kmatzen/millennium/releases/latest"
               " 2>/dev/null", "r");
    if (!fp) {
        logger_warn_with_category("Updater", "Failed to run curl");
        return -1;
    }

    len = fread(buf, 1, sizeof(buf) - 1, fp);
    pclose(fp);
    if (len == 0) {
        logger_warn_with_category("Updater", "Empty response from GitHub");
        return -1;
    }
    buf[len] = '\0';

    /*
     * Minimal JSON extraction: find "tag_name" : "vX.Y.Z"
     * Full JSON parsing is overkill for a single field.
     */
    tag = strstr(buf, "\"tag_name\"");
    if (!tag) {
        logger_debug_with_category("Updater", "No tag_name in GitHub response (may have no releases)");
        return -1;
    }

    /* Advance past the key and colon to the value */
    tag = strchr(tag + 10, '"');
    if (!tag) return -1;
    tag++; /* skip opening quote */

    {
        const char *end = strchr(tag, '"');
        if (!end || (size_t)(end - tag) >= sizeof(latest_version)) return -1;
        memcpy(latest_version, tag, (size_t)(end - tag));
        latest_version[end - tag] = '\0';
    }

    checked = 1;
    {
        char log_msg[128];
        snprintf(log_msg, sizeof(log_msg),
                 "Latest release: %s (running: %s)", latest_version, version_get_string());
        logger_info_with_category("Updater", log_msg);
    }

    return 0;
}

const char *updater_get_latest_version(void) {
    return checked ? latest_version : NULL;
}

int updater_is_update_available(void) {
    if (!checked) return 0;
    return updater_compare_versions(latest_version, version_get_string()) > 0;
}

static char apply_status[256] = "No update attempted";

const char *updater_get_apply_status(void) {
    return apply_status;
}

static int run_command(const char *cmd) {
    int rc;
    char log_msg[512];
    snprintf(log_msg, sizeof(log_msg), "Running: %s", cmd);
    logger_info_with_category("Updater", log_msg);
    rc = system(cmd);
    if (rc != 0) {
        snprintf(log_msg, sizeof(log_msg), "Command failed (exit %d): %s", rc, cmd);
        logger_warn_with_category("Updater", log_msg);
    }
    return rc;
}

int updater_apply(const char *source_dir) {
    char cmd[512];

    if (!source_dir || !*source_dir) {
        snprintf(apply_status, sizeof(apply_status), "Error: no source directory specified");
        return -1;
    }

    snprintf(apply_status, sizeof(apply_status), "Pulling latest code...");
    snprintf(cmd, sizeof(cmd), "cd '%s' && git pull --ff-only origin main 2>&1", source_dir);
    if (run_command(cmd) != 0) {
        snprintf(apply_status, sizeof(apply_status), "Error: git pull failed");
        return -1;
    }

    snprintf(apply_status, sizeof(apply_status), "Building...");
    snprintf(cmd, sizeof(cmd), "cd '%s/host' && make clean && make daemon 2>&1", source_dir);
    if (run_command(cmd) != 0) {
        snprintf(apply_status, sizeof(apply_status), "Error: build failed");
        return -1;
    }

    snprintf(apply_status, sizeof(apply_status), "Installing and restarting...");
    snprintf(cmd, sizeof(cmd), "cd '%s/host' && sudo make install 2>&1", source_dir);
    if (run_command(cmd) != 0) {
        snprintf(apply_status, sizeof(apply_status), "Error: install failed");
        return -1;
    }

    snprintf(apply_status, sizeof(apply_status), "Restarting daemon...");
    logger_info_with_category("Updater", "Restarting daemon via systemd");
    run_command("sudo systemctl restart daemon.service");

    snprintf(apply_status, sizeof(apply_status), "Update applied successfully");
    return 0;
}
