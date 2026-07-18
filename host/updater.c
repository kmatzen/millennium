#define _POSIX_C_SOURCE 200112L
#include "updater.h"
#include "version.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>

static char latest_version[64] = {0};
static int  check_state = 0;  /* 0=idle, 1=checking, 2=checked */
static pthread_mutex_t check_mutex = PTHREAD_MUTEX_INITIALIZER;

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

/* Blocking implementation (runs in background thread) */
static int do_check(void) {
    FILE *fp;
    char buf[4096];
    const char *tag;
    size_t len;

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

    tag = strstr(buf, "\"tag_name\"");
    if (!tag) {
        logger_debug_with_category("Updater", "No tag_name in GitHub response (may have no releases)");
        return -1;
    }

    tag = strchr(tag + 10, '"');
    if (!tag) return -1;
    tag++;

    {
        const char *end = strchr(tag, '"');
        if (!end || (size_t)(end - tag) >= sizeof(latest_version)) return -1;
        /* Under the lock: readers take check_mutex, so the write must too (#227). */
        pthread_mutex_lock(&check_mutex);
        memcpy(latest_version, tag, (size_t)(end - tag));
        latest_version[end - tag] = '\0';
        pthread_mutex_unlock(&check_mutex);
    }

    {
        char log_msg[128];
        snprintf(log_msg, sizeof(log_msg),
                 "Latest release: %s (running: %s)", latest_version, version_get_string());
        logger_info_with_category("Updater", log_msg);
    }

    return 0;
}

static void *check_thread_func(void *arg) {
    int rc;
    (void)arg;
    rc = do_check();
    pthread_mutex_lock(&check_mutex);
    check_state = 2;  /* checked */
    if (rc != 0) latest_version[0] = '\0';
    pthread_mutex_unlock(&check_mutex);
    return NULL;
}

/* #119: Non-blocking. Starts background check if idle; returns immediately. */
void updater_check_async(void) {
    pthread_t th;
    pthread_mutex_lock(&check_mutex);
    /* Start a check unless one is already running. The guard used to be
     * `check_state == 0`, but nothing ever set check_state back to 0, so the
     * state machine ratcheted 0 -> 1 -> 2 and this endpoint went inert for the
     * life of the process. That was worst when the FIRST check failed:
     * check_thread_func clears latest_version, so the phone reported "no update
     * known" until the daemon was restarted. Found by tests/Updater.tla
     * (CheckNotStuck); see docs/OTA_UPDATE.md.
     *
     * check_state stays 2 after a completed check so updater_get_latest_version
     * keeps reporting the last known version while a re-check runs. */
    if (check_state != 1) {
        check_state = 1;
        pthread_mutex_unlock(&check_mutex);
        if (pthread_create(&th, NULL, check_thread_func, NULL) == 0) {
            pthread_detach(th);
        } else {
            pthread_mutex_lock(&check_mutex);
            check_state = 0;
            pthread_mutex_unlock(&check_mutex);
        }
    } else {
        pthread_mutex_unlock(&check_mutex);
    }
}

/* Returns 1 if a check is in progress (curl running in background). */
int updater_is_checking(void) {
    int s;
    pthread_mutex_lock(&check_mutex);
    s = (check_state == 1);
    pthread_mutex_unlock(&check_mutex);
    return s;
}

/* Legacy blocking API; prefer updater_check_async for HTTP handlers. */
int updater_check(void) {
    int rc = do_check();
    pthread_mutex_lock(&check_mutex);
    check_state = 2;
    if (rc != 0) latest_version[0] = '\0';
    pthread_mutex_unlock(&check_mutex);
    return rc;
}

int updater_get_latest_version(char *out, size_t out_size) {
    int known = 0;
    if (!out || out_size == 0) return 0;
    out[0] = '\0';
    pthread_mutex_lock(&check_mutex);
    if (check_state == 2 && latest_version[0]) {
        strncpy(out, latest_version, out_size - 1);
        out[out_size - 1] = '\0';
        known = 1;
    }
    pthread_mutex_unlock(&check_mutex);
    return known;
}

int updater_is_update_available(void) {
    char lv[64];
    /* Compare against a copy, not the shared buffer: holding the pointer past
     * the unlock had the same race as the getters (#227). */
    if (!updater_get_latest_version(lv, sizeof(lv))) return 0;
    return updater_compare_versions(lv, version_get_string()) > 0;
}

static char apply_status[256] = "No update attempted";
static int apply_state = 0;  /* 0=idle, 1=applying */
static char apply_source_dir[512] = {0};
static pthread_mutex_t apply_mutex = PTHREAD_MUTEX_INITIALIZER;

void updater_get_apply_status(char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    pthread_mutex_lock(&apply_mutex);
    /* Copy while holding the lock. Returning apply_status itself only protected
     * the pointer read, not the buffer -- the caller then read it unlocked while
     * updater_apply was snprintf-ing into it (#227). */
    strncpy(out, apply_status, out_size - 1);
    out[out_size - 1] = '\0';
    pthread_mutex_unlock(&apply_mutex);
}

/* #118: Run apply in background; returns immediately. */
static void *apply_thread_func(void *arg) {
    int rc;
    (void)arg;
    rc = updater_apply(apply_source_dir);
    if (rc != 0) {
        pthread_mutex_lock(&apply_mutex);
        apply_state = 0;
        pthread_mutex_unlock(&apply_mutex);
    }
    /* On success, systemctl restart kills us before we get here */
    return NULL;
}

/* #118: Non-blocking. Starts apply in background; returns immediately. */
int updater_apply_async(const char *source_dir) {
    pthread_t th;
    if (!source_dir || !*source_dir) {
        pthread_mutex_lock(&apply_mutex);
        snprintf(apply_status, sizeof(apply_status), "Error: no source directory specified");
        pthread_mutex_unlock(&apply_mutex);
        return -1;
    }
    pthread_mutex_lock(&apply_mutex);
    if (apply_state == 1) {
        pthread_mutex_unlock(&apply_mutex);
        return 0;  /* Already applying */
    }
    strncpy(apply_source_dir, source_dir, sizeof(apply_source_dir) - 1);
    apply_source_dir[sizeof(apply_source_dir) - 1] = '\0';
    apply_state = 1;
    snprintf(apply_status, sizeof(apply_status), "Applying update in background...");
    pthread_mutex_unlock(&apply_mutex);
    if (pthread_create(&th, NULL, apply_thread_func, NULL) == 0) {
        pthread_detach(th);
        return 0;
    }
    pthread_mutex_lock(&apply_mutex);
    apply_state = 0;
    snprintf(apply_status, sizeof(apply_status), "Error: failed to start apply thread");
    pthread_mutex_unlock(&apply_mutex);
    return -1;
}

int updater_is_applying(void) {
    int s;
    pthread_mutex_lock(&apply_mutex);
    s = (apply_state == 1);
    pthread_mutex_unlock(&apply_mutex);
    return s;
}

static int run_command(const char *cmd) {
    int rc;
    logger_infof_with_category("Updater", "Running: %s", cmd);
    rc = system(cmd);
    if (rc != 0) {
        logger_warnf_with_category("Updater", "Command failed (exit %d): %s", rc, cmd);
    }
    return rc;
}

int updater_apply(const char *source_dir) {
    char cmd[512];

    if (!source_dir || !*source_dir) {
        pthread_mutex_lock(&apply_mutex);
        snprintf(apply_status, sizeof(apply_status), "Error: no source directory specified");
        pthread_mutex_unlock(&apply_mutex);
        return -1;
    }

    pthread_mutex_lock(&apply_mutex);
    snprintf(apply_status, sizeof(apply_status), "Pulling latest code...");
    pthread_mutex_unlock(&apply_mutex);
    snprintf(cmd, sizeof(cmd), "cd '%s' && git pull --ff-only origin main 2>&1", source_dir);
    if (run_command(cmd) != 0) {
        pthread_mutex_lock(&apply_mutex);
        snprintf(apply_status, sizeof(apply_status), "Error: git pull failed");
        pthread_mutex_unlock(&apply_mutex);
        return -1;
    }

    pthread_mutex_lock(&apply_mutex);
    snprintf(apply_status, sizeof(apply_status), "Building...");
    pthread_mutex_unlock(&apply_mutex);
    snprintf(cmd, sizeof(cmd), "cd '%s/host' && make clean && make daemon 2>&1", source_dir);
    if (run_command(cmd) != 0) {
        pthread_mutex_lock(&apply_mutex);
        snprintf(apply_status, sizeof(apply_status), "Error: build failed");
        pthread_mutex_unlock(&apply_mutex);
        return -1;
    }

    pthread_mutex_lock(&apply_mutex);
    snprintf(apply_status, sizeof(apply_status), "Installing and restarting...");
    pthread_mutex_unlock(&apply_mutex);
    snprintf(cmd, sizeof(cmd), "cd '%s/host' && sudo make install 2>&1", source_dir);
    if (run_command(cmd) != 0) {
        pthread_mutex_lock(&apply_mutex);
        snprintf(apply_status, sizeof(apply_status), "Error: install failed");
        pthread_mutex_unlock(&apply_mutex);
        return -1;
    }

    pthread_mutex_lock(&apply_mutex);
    snprintf(apply_status, sizeof(apply_status), "Restarting daemon...");
    pthread_mutex_unlock(&apply_mutex);
    logger_info_with_category("Updater", "Restarting daemon via systemd");
    /* Check this like every other step. On success systemd kills us before the
     * next line runs, so reaching it at all means the restart did not take --
     * reporting "applied successfully" there left the dashboard claiming the
     * new build was live while the old binary kept running. Found by
     * tests/Updater.tla (StatusHonest); see docs/OTA_UPDATE.md. */
    if (run_command("sudo systemctl restart daemon.service") != 0) {
        pthread_mutex_lock(&apply_mutex);
        snprintf(apply_status, sizeof(apply_status),
                 "Error: installed, but daemon restart failed");
        pthread_mutex_unlock(&apply_mutex);
        return -1;
    }

    pthread_mutex_lock(&apply_mutex);
    snprintf(apply_status, sizeof(apply_status), "Update applied successfully");
    pthread_mutex_unlock(&apply_mutex);
    return 0;
}
