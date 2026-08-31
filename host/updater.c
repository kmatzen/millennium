#define _POSIX_C_SOURCE 200112L
#include "updater.h"
#include "version.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <unistd.h>

static char latest_version[64] = {0};
static int latest_sequence_available = 0;
static int  check_state = 0;  /* 0=idle, 1=checking, 2=checked */
static pthread_mutex_t check_mutex = PTHREAD_MUTEX_INITIALIZER;

static int production_ota_available(void) {
#ifdef MILLENNIUM_UNIT_TEST
    /* Unit tests must be hermetic and, critically, must never start the
     * installed updater when they happen to run on a production phone. */
    return 0;
#else
    return access("/usr/local/libexec/millennium-ota", X_OK) == 0 &&
           access("/etc/millennium/update-signing-key.pem", R_OK) == 0;
#endif
}

static int read_production_status(void) {
    FILE *fp;
    char buf[1024];
    char *version;
    char *end;
    size_t len;

    fp = popen("/usr/local/libexec/millennium-ota status 2>/dev/null", "r");
    if (!fp) return -1;
    len = fread(buf, 1, sizeof(buf) - 1, fp);
    pclose(fp);
    if (len == 0) return -1;
    buf[len] = '\0';
    version = strstr(buf, "\"version\":");
    if (!version) return -1;
    version = strchr(version + 10, '"');
    if (!version) return -1;
    version++;
    end = strchr(version, '"');
    if (!end || (size_t)(end - version) >= sizeof(latest_version)) return -1;
    pthread_mutex_lock(&check_mutex);
    memcpy(latest_version, version, (size_t)(end - version));
    latest_version[end - version] = '\0';
    latest_sequence_available = strstr(buf, "\"available\": true") != NULL ||
                                strstr(buf, "\"available\":true") != NULL;
    pthread_mutex_unlock(&check_mutex);
    return 0;
}

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
    if (!production_ota_available()) {
        logger_error_with_category("Updater",
            "Signed OTA worker or trust key is unavailable; refusing insecure fallback");
        return -1;
    }
    if (system("sudo -n /bin/systemctl start millennium-update-check.service") != 0) {
        logger_warn_with_category("Updater", "Signed OTA check service failed");
        return -1;
    }
    return read_production_status();
}

static void *check_thread_func(void *arg) {
    int rc;
    (void)arg;
    rc = do_check();
    pthread_mutex_lock(&check_mutex);
    check_state = 2;  /* checked */
    if (rc != 0) {
        latest_version[0] = '\0';
        latest_sequence_available = 0;
    }
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
    if (rc != 0) {
        latest_version[0] = '\0';
        latest_sequence_available = 0;
    }
    pthread_mutex_unlock(&check_mutex);
    return rc;
}

int updater_get_latest_version(char *out, size_t out_size) {
    int known = 0;
    if (!out || out_size == 0) return 0;
    out[0] = '\0';
    pthread_mutex_lock(&check_mutex);
    if (check_state == 2 && latest_version[0]) {
        size_t n = strlen(latest_version);
        if (n > out_size - 1) n = out_size - 1;
        memcpy(out, latest_version, n);
        out[n] = '\0';
        known = 1;
    }
    pthread_mutex_unlock(&check_mutex);
    return known;
}

int updater_is_update_available(void) {
    int available;
    if (production_ota_available()) {
        pthread_mutex_lock(&check_mutex);
        available = latest_sequence_available;
        pthread_mutex_unlock(&check_mutex);
        return available;
    }
    return 0;
}

static char apply_status[256] = "No update attempted";
static int apply_state = 0;  /* 0=idle, 1=applying */
static char apply_source_dir[512] = {0};
static pthread_mutex_t apply_mutex = PTHREAD_MUTEX_INITIALIZER;
static int (*restart_guard)(void) = NULL;

void updater_set_restart_guard(int (*guard)(void)) {
    pthread_mutex_lock(&apply_mutex);
    restart_guard = guard;
    pthread_mutex_unlock(&apply_mutex);
}

void updater_get_apply_status(char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    pthread_mutex_lock(&apply_mutex);
    /* Copy while holding the lock. Returning apply_status itself only protected
     * the pointer read, not the buffer -- the caller then read it unlocked while
     * updater_apply was snprintf-ing into it (#227).
     *
     * memcpy with an explicit clamp rather than strncpy: when gcc inlines this
     * into a caller whose buffer is the same size as the source, strncpy trips
     * -Wstringop-truncation, and the build is -Werror. */
    {
        size_t n = strlen(apply_status);
        if (n > out_size - 1) n = out_size - 1;
        memcpy(out, apply_status, n);
        out[n] = '\0';
    }
    pthread_mutex_unlock(&apply_mutex);
}

/* #118: Run apply in background; returns immediately. */
static void *apply_thread_func(void *arg) {
    (void)arg;
    updater_apply(apply_source_dir);
    /* Legacy success restarts and kills this process. The production worker is
     * a separate systemd unit and returns after dispatch, so reaching here is
     * also normal and must make a later dashboard request possible. */
    pthread_mutex_lock(&apply_mutex);
    apply_state = 0;
    pthread_mutex_unlock(&apply_mutex);
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

int updater_apply(const char *source_dir) {
    if (!source_dir || !*source_dir) {
        pthread_mutex_lock(&apply_mutex);
        snprintf(apply_status, sizeof(apply_status), "Error: no source directory specified");
        pthread_mutex_unlock(&apply_mutex);
        return -1;
    }

    if (!production_ota_available()) {
        pthread_mutex_lock(&apply_mutex);
        snprintf(apply_status, sizeof(apply_status),
                 "Error: signed OTA worker or trust key unavailable");
        pthread_mutex_unlock(&apply_mutex);
        return -1;
    }
    pthread_mutex_lock(&apply_mutex);
    snprintf(apply_status, sizeof(apply_status), "Starting signed OTA worker...");
    pthread_mutex_unlock(&apply_mutex);
    if (system("sudo -n /bin/systemctl start --no-block millennium-update-apply.service") != 0) {
        pthread_mutex_lock(&apply_mutex);
        snprintf(apply_status, sizeof(apply_status), "Error: could not start signed OTA worker");
        pthread_mutex_unlock(&apply_mutex);
        return -1;
    }
    pthread_mutex_lock(&apply_mutex);
    snprintf(apply_status, sizeof(apply_status),
             "Signed OTA accepted; installation continues in systemd");
    pthread_mutex_unlock(&apply_mutex);
    return 0;
}
