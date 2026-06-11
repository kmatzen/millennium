#define _POSIX_C_SOURCE 200112L
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>
#include <pthread.h>

/* Global logger instance */
logger_data_t* g_logger = NULL;

/* logger_mutex guards the in-memory ring buffer + console output (fast, never
 * blocks on disk). logger_file_mutex guards the actual file stream and the
 * rotation bookkeeping, and is only ever held by the async writer thread (and
 * by the set_* config calls at startup) — so disk latency never blocks a
 * producer. See the async writer block below. */
static pthread_mutex_t logger_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t logger_file_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Forward declarations for the async writer machinery (defined below). */
static void logger_check_rotation(logger_data_t* logger);
static void logger_emit_to_file(const char* line);
static void logger_writer_start(void);
static void logger_enqueue(const char* line);

/* ── Asynchronous file writer (issue #123) ───────────────────────────────
 * Previously logger_write_log held logger_mutex across fprintf() + fflush(),
 * so a slow log target (NFS, full disk, busy spindle) stalled EVERY thread
 * that logged: main loop, web server, health monitor, PJSIP callbacks. One
 * slow write cascaded into event-processing delay and serial backlog.
 *
 * Now a producer only formats its line, appends it to the in-memory ring +
 * console under logger_mutex, then drops the line into this bounded queue and
 * returns. A dedicated writer thread drains the queue to disk under
 * logger_file_mutex. Disk latency is absorbed by the queue, never by a
 * producer. If the writer can't keep up the queue fills, the newest lines are
 * dropped and counted, and the writer emits a single notice line — memory is
 * bounded and the system never stalls. */
#define LOG_QUEUE_CAP 1024
#define LOG_LINE_MAX  512

static struct {
    char lines[LOG_QUEUE_CAP][LOG_LINE_MAX];
    int head;                  /* index of the next line to write */
    int count;                 /* lines queued but not yet on disk */
    unsigned long dropped;     /* lines discarded on overflow since last notice */
    unsigned long long dropped_total; /* cumulative drops since start (for metrics) */
    unsigned long high_water;  /* max depth observed since start (for metrics) */
    int started;               /* writer thread is running */
    int shutting_down;         /* drain-and-exit requested */
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;  /* a line was queued, or shutdown requested */
    pthread_cond_t drained;    /* queue emptied (count reached 0) */
} log_queue = {
    {{0}}, 0, 0, 0, 0, 0, 0, 0, 0,
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER
};

/* Write one already-formatted line to the log file. Holds logger_file_mutex
 * (so it may block on disk I/O) but NOT logger_mutex — producers run free. */
static void logger_emit_to_file(const char* line) {
    logger_data_t* logger = g_logger;
    int written;

    if (logger == NULL || line == NULL) {
        return;
    }

    pthread_mutex_lock(&logger_file_mutex);
    if (logger->log_to_file && logger->file_stream != NULL) {
        written = fprintf(logger->file_stream, "%s\n", line);
        fflush(logger->file_stream);
        if (written > 0) {
            logger->current_file_size += written;
        }
        logger_check_rotation(logger);
    }
    pthread_mutex_unlock(&logger_file_mutex);
}

/* The writer thread: pop a line, write it to disk, repeat. The line is not
 * committed (head/count advanced) until AFTER the write completes, so
 * logger_flush() only observes count==0 once everything is actually on disk. */
static void* logger_writer_main(void* arg) {
    char line[LOG_LINE_MAX];
    unsigned long dropped;

    (void)arg;

    for (;;) {
        pthread_mutex_lock(&log_queue.lock);
        while (log_queue.count == 0 && !log_queue.shutting_down) {
            pthread_cond_broadcast(&log_queue.drained);
            pthread_cond_wait(&log_queue.not_empty, &log_queue.lock);
        }
        if (log_queue.count == 0 && log_queue.shutting_down) {
            pthread_cond_broadcast(&log_queue.drained);
            pthread_mutex_unlock(&log_queue.lock);
            break;
        }
        /* Peek the head; commit it only after the write succeeds. */
        memcpy(line, log_queue.lines[log_queue.head], LOG_LINE_MAX);
        dropped = log_queue.dropped;
        log_queue.dropped = 0;
        pthread_mutex_unlock(&log_queue.lock);

        if (dropped > 0) {
            char notice[LOG_LINE_MAX];
            snprintf(notice, sizeof(notice),
                "[logger] dropped %lu log line(s): writer fell behind", dropped);
            logger_emit_to_file(notice);
        }
        logger_emit_to_file(line);

        pthread_mutex_lock(&log_queue.lock);
        log_queue.head = (log_queue.head + 1) % LOG_QUEUE_CAP;
        log_queue.count--;
        if (log_queue.count == 0) {
            pthread_cond_broadcast(&log_queue.drained);
        }
        pthread_mutex_unlock(&log_queue.lock);
    }

    return NULL;
}

/* Lazily create the writer thread the first time file logging is enabled.
 * Called from logger_set_log_to_file() while it holds logger_file_mutex. */
static void logger_writer_start(void) {
    pthread_mutex_lock(&log_queue.lock);
    if (log_queue.started) {
        pthread_mutex_unlock(&log_queue.lock);
        return;
    }
    /* Reset the queue so a restart after logger_shutdown() is clean. */
    log_queue.shutting_down = 0;
    log_queue.head = 0;
    log_queue.count = 0;
    log_queue.dropped = 0;
    if (pthread_create(&log_queue.thread, NULL, logger_writer_main, NULL) == 0) {
        log_queue.started = 1;
    }
    pthread_mutex_unlock(&log_queue.lock);
}

/* Append a formatted line to the queue and wake the writer. Never blocks on
 * disk; drops (and counts) the line if the queue is full. */
static void logger_enqueue(const char* line) {
    int tail;
    size_t n;

    pthread_mutex_lock(&log_queue.lock);
    if (!log_queue.started || log_queue.shutting_down) {
        pthread_mutex_unlock(&log_queue.lock);
        return;
    }
    if (log_queue.count >= LOG_QUEUE_CAP) {
        log_queue.dropped++;           /* bounded memory: drop the newest line */
        log_queue.dropped_total++;     /* lifetime total for metrics/alerting */
        pthread_mutex_unlock(&log_queue.lock);
        return;
    }
    tail = (log_queue.head + log_queue.count) % LOG_QUEUE_CAP;
    n = strlen(line);
    if (n >= LOG_LINE_MAX) {
        n = LOG_LINE_MAX - 1;
    }
    memcpy(log_queue.lines[tail], line, n);
    log_queue.lines[tail][n] = '\0';
    log_queue.count++;
    if ((unsigned long)log_queue.count > log_queue.high_water) {
        log_queue.high_water = (unsigned long)log_queue.count;
    }
    pthread_cond_signal(&log_queue.not_empty);
    pthread_mutex_unlock(&log_queue.lock);
}

/* Block until every queued line has been written to disk. Used at shutdown
 * checkpoints and by the unit test. No-op if the writer isn't running. */
void logger_flush(void) {
    pthread_mutex_lock(&log_queue.lock);
    while (log_queue.started && log_queue.count > 0) {
        pthread_cond_wait(&log_queue.drained, &log_queue.lock);
    }
    pthread_mutex_unlock(&log_queue.lock);
}

/* Snapshot the async writer queue's health under its own lock. Cheap and
 * non-blocking (never touches the file mutex), so the metrics refresh can poll
 * it every tick. depth/started reflect the current instant; high_water and
 * dropped_total are lifetime totals so a metrics counter can rate() over them. */
void logger_get_queue_stats(logger_queue_stats_t* out) {
    if (out == NULL) {
        return;
    }
    pthread_mutex_lock(&log_queue.lock);
    out->started = log_queue.started;
    out->depth = (unsigned long)log_queue.count;
    out->high_water = log_queue.high_water;
    out->capacity = LOG_QUEUE_CAP;
    out->dropped_total = log_queue.dropped_total;
    pthread_mutex_unlock(&log_queue.lock);
}

/* Drain the queue, stop the writer thread, and close the log file. Idempotent.
 * Call once during daemon shutdown after the final log line is emitted. */
void logger_shutdown(void) {
    int started;

    pthread_mutex_lock(&log_queue.lock);
    started = log_queue.started;
    log_queue.shutting_down = 1;
    pthread_cond_broadcast(&log_queue.not_empty);
    pthread_mutex_unlock(&log_queue.lock);

    if (started) {
        pthread_join(log_queue.thread, NULL);
        pthread_mutex_lock(&log_queue.lock);
        log_queue.started = 0;
        pthread_mutex_unlock(&log_queue.lock);
    }

    pthread_mutex_lock(&logger_file_mutex);
    if (g_logger != NULL && g_logger->file_stream != NULL) {
        fclose(g_logger->file_stream);
        g_logger->file_stream = NULL;
    }
    pthread_mutex_unlock(&logger_file_mutex);
}


logger_data_t* logger_get_instance(void) {
    if (g_logger == NULL) {
        g_logger = (logger_data_t*)malloc(sizeof(logger_data_t));
        if (g_logger != NULL) {
            g_logger->current_level = LOG_LEVEL_INFO;
            g_logger->log_file[0] = '\0';
            g_logger->log_to_console = 1;
            g_logger->log_to_file = 0;
            g_logger->file_stream = NULL;
            g_logger->max_file_size = 0;
            g_logger->max_rotated_files = 0;
            g_logger->current_file_size = 0;
            g_logger->memory_logs_count = 0;
            g_logger->memory_logs_start = 0;
        }
    }
    return g_logger;
}

void logger_set_level(log_level_t level) {
    logger_data_t* logger = logger_get_instance();
    if (logger != NULL) {
        logger->current_level = level;
    }
}

void logger_set_log_file(const char* filename) {
    logger_data_t* logger = logger_get_instance();
    if (logger == NULL || filename == NULL) {
        return;
    }
    
    pthread_mutex_lock(&logger_file_mutex);
    strncpy(logger->log_file, filename, sizeof(logger->log_file) - 1);
    logger->log_file[sizeof(logger->log_file) - 1] = '\0';

    if (logger->log_to_file) {
        if (logger->file_stream != NULL) {
            fclose(logger->file_stream);
        }
        logger->file_stream = fopen(filename, "a");
        if (logger->file_stream == NULL) {
            logger->log_to_file = 0;
            logger->current_file_size = 0;
        } else {
            fseek(logger->file_stream, 0, SEEK_END);
            logger->current_file_size = ftell(logger->file_stream);
        }
    }
    pthread_mutex_unlock(&logger_file_mutex);
}

void logger_set_log_to_console(int enable) {
    logger_data_t* logger = logger_get_instance();
    if (logger != NULL) {
        logger->log_to_console = enable;
    }
}

void logger_set_log_to_file(int enable) {
    logger_data_t* logger = logger_get_instance();
    if (logger == NULL) {
        return;
    }
    
    pthread_mutex_lock(&logger_file_mutex);
    logger->log_to_file = enable;
    if (enable && strlen(logger->log_file) > 0) {
        if (logger->file_stream != NULL) {
            fclose(logger->file_stream);
        }
        logger->file_stream = fopen(logger->log_file, "a");
        if (logger->file_stream == NULL) {
            logger->log_to_file = 0;
            logger->current_file_size = 0;
        } else {
            fseek(logger->file_stream, 0, SEEK_END);
            logger->current_file_size = ftell(logger->file_stream);
            logger_writer_start();   /* spin up the async writer thread */
        }
    } else {
        if (logger->file_stream != NULL) {
            fclose(logger->file_stream);
            logger->file_stream = NULL;
        }
    }
    pthread_mutex_unlock(&logger_file_mutex);
}

void logger_set_rotation(long max_file_size, int max_rotated_files) {
    logger_data_t* logger = logger_get_instance();
    if (logger == NULL) {
        return;
    }
    pthread_mutex_lock(&logger_file_mutex);
    logger->max_file_size = max_file_size;
    logger->max_rotated_files = (max_rotated_files > 0) ? max_rotated_files : 0;
    pthread_mutex_unlock(&logger_file_mutex);
}

static void logger_rotate_files(logger_data_t* logger) {
    char src_path[512];
    char dst_path[512];
    int i;

    if (logger->file_stream != NULL) {
        fclose(logger->file_stream);
        logger->file_stream = NULL;
    }

    /* Delete the oldest rotated file if it exists */
    snprintf(dst_path, sizeof(dst_path), "%s.%d", logger->log_file, logger->max_rotated_files);
    remove(dst_path);

    /* Shift rotated files: .N-1 -> .N, .N-2 -> .N-1, ... .1 -> .2 */
    for (i = logger->max_rotated_files - 1; i >= 1; i--) {
        snprintf(src_path, sizeof(src_path), "%s.%d", logger->log_file, i);
        snprintf(dst_path, sizeof(dst_path), "%s.%d", logger->log_file, i + 1);
        rename(src_path, dst_path);
    }

    /* Rotate current log file to .1 */
    snprintf(dst_path, sizeof(dst_path), "%s.1", logger->log_file);
    rename(logger->log_file, dst_path);

    /* Reopen a fresh log file */
    logger->file_stream = fopen(logger->log_file, "a");
    logger->current_file_size = 0;
    if (logger->file_stream == NULL) {
        logger->log_to_file = 0;
    }
}

static void logger_check_rotation(logger_data_t* logger) {
    if (logger->max_file_size <= 0 || logger->max_rotated_files <= 0) {
        return;
    }
    if (logger->current_file_size >= logger->max_file_size) {
        logger_rotate_files(logger);
    }
}

void logger_log(log_level_t level, const char* message) {
    logger_log_with_category(level, "", message);
}

void logger_log_with_category(log_level_t level, const char* category, const char* message) {
    logger_data_t* logger = logger_get_instance();
    if (logger == NULL || message == NULL) {
        return;
    }
    
    if (level >= logger->current_level) {
        logger_write_log(level, category, message);
    }
}

log_level_t logger_parse_level(const char* level_str) {
    char upper[32];
    int i;
    
    if (level_str == NULL) {
        return LOG_LEVEL_INFO;
    }
    
    /* Convert to uppercase */
    for (i = 0; level_str[i] != '\0' && i < (int)(sizeof(upper) - 1); i++) {
        upper[i] = toupper(level_str[i]);
    }
    upper[i] = '\0';
    
    if (strcmp(upper, "VERBOSE") == 0) return LOG_LEVEL_VERBOSE;
    if (strcmp(upper, "DEBUG") == 0) return LOG_LEVEL_DEBUG;
    if (strcmp(upper, "INFO") == 0) return LOG_LEVEL_INFO;
    if (strcmp(upper, "WARN") == 0) return LOG_LEVEL_WARN;
    if (strcmp(upper, "ERROR") == 0) return LOG_LEVEL_ERROR;
    
    return LOG_LEVEL_INFO; /* Default level */
}

const char* logger_level_to_string(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_VERBOSE: return "VERBOSE";
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO: return "INFO";
        case LOG_LEVEL_WARN: return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

void logger_write_log(log_level_t level, const char* category, const char* message) {
    logger_data_t* logger = logger_get_instance();
    char timestamp[64];  /* Increased buffer size for timestamp */
    char formatted_message[512];
    const char* level_str;
    int to_file;

    if (logger == NULL || message == NULL) {
        return;
    }

    pthread_mutex_lock(&logger_mutex);

    logger_format_timestamp(timestamp, sizeof(timestamp));
    level_str = logger_format_level(level);

    /* Format the log message */
    if (category != NULL && strlen(category) > 0) {
        snprintf(formatted_message, sizeof(formatted_message), "[%s] [%s] [%s] %s", timestamp, level_str, category, message);
    } else {
        snprintf(formatted_message, sizeof(formatted_message), "[%s] [%s] %s", timestamp, level_str, message);
    }

    /* Store in memory */
    logger_add_to_memory(formatted_message);

    /* Output to console */
    if (logger->log_to_console) {
        if (level >= LOG_LEVEL_WARN) {
            fprintf(stderr, "%s\n", formatted_message);
        } else {
            printf("%s\n", formatted_message);
        }
    }

    to_file = logger->log_to_file;

    pthread_mutex_unlock(&logger_mutex);

    /* File output is handed off to the async writer thread (issue #123) so a
     * slow disk never blocks this producer. The writer drains the queue under
     * logger_file_mutex and performs the actual fprintf/fflush/rotation. */
    if (to_file) {
        logger_enqueue(formatted_message);
    }
}

void logger_format_timestamp(char* buffer, size_t buffer_size) {
    time_t now;
    struct tm* tm_info;
    long milliseconds;
    
    if (buffer == NULL || buffer_size < 32) {
        return;
    }
    
    time(&now);
    tm_info = localtime(&now);
    
    /* Get milliseconds (approximate) */
    milliseconds = now % 1000;
    
    /* Format timestamp: YYYY-MM-DD HH:MM:SS.mmm */
    snprintf(buffer, 64, "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
            tm_info->tm_year + 1900,
            tm_info->tm_mon + 1,
            tm_info->tm_mday,
            tm_info->tm_hour,
            tm_info->tm_min,
            tm_info->tm_sec,
            milliseconds);
}

const char* logger_format_level(log_level_t level) {
    return logger_level_to_string(level);
}

void logger_add_to_memory(const char* formatted_message) {
    logger_data_t* logger = logger_get_instance();
    int index;
    size_t mlen, mcap;

    if (logger == NULL || formatted_message == NULL) {
        return;
    }

    /* Use circular buffer */
    index = (logger->memory_logs_start + logger->memory_logs_count) % 1000;

    /* Copy with intentional truncation, always null-terminated. (memcpy rather
     * than strncpy so neither gcc -Wstringop-truncation nor clang complains.) */
    mcap = sizeof(logger->memory_logs[index]) - 1;
    mlen = strlen(formatted_message);
    if (mlen > mcap) mlen = mcap;
    memcpy(logger->memory_logs[index], formatted_message, mlen);
    logger->memory_logs[index][mlen] = '\0';

    if (logger->memory_logs_count < 1000) {
        logger->memory_logs_count++;
    } else {
        /* Buffer is full, move start pointer */
        logger->memory_logs_start = (logger->memory_logs_start + 1) % 1000;
    }
}

int logger_get_recent_logs(char logs[][512], int max_entries) {
    logger_data_t* logger = logger_get_instance();
    int i, j, count;

    if (logger == NULL || logs == NULL || max_entries <= 0) {
        return 0;
    }

    pthread_mutex_lock(&logger_mutex);

    count = (max_entries < logger->memory_logs_count) ? max_entries : logger->memory_logs_count;

    for (i = 0, j = logger->memory_logs_start; i < count; i++) {
        strncpy(logs[i], logger->memory_logs[j], sizeof(logs[i]) - 1);
        logs[i][sizeof(logs[i]) - 1] = '\0';
        j = (j + 1) % 1000;
    }

    pthread_mutex_unlock(&logger_mutex);

    return count;
}

/* Parse the severity out of a stored line: "[timestamp] [LEVEL] ..." */
static log_level_t logger_parse_line_level(const char* line) {
    const char* p;
    if (line == NULL) return LOG_LEVEL_INFO;
    p = strchr(line, ']');            /* end of [timestamp] */
    if (p == NULL) return LOG_LEVEL_INFO;
    p = strchr(p + 1, '[');           /* start of [LEVEL]   */
    if (p == NULL) return LOG_LEVEL_INFO;
    p++;
    if (strncmp(p, "VERBOSE", 7) == 0) return LOG_LEVEL_VERBOSE;
    if (strncmp(p, "DEBUG", 5) == 0)   return LOG_LEVEL_DEBUG;
    if (strncmp(p, "INFO", 4) == 0)    return LOG_LEVEL_INFO;
    if (strncmp(p, "WARN", 4) == 0)    return LOG_LEVEL_WARN;
    if (strncmp(p, "ERROR", 5) == 0)   return LOG_LEVEL_ERROR;
    return LOG_LEVEL_INFO;
}

int logger_get_recent_logs_min_level(char logs[][512], int max_entries, log_level_t min_level) {
    logger_data_t* logger = logger_get_instance();
    int n, k, idx, collected, a, b;
    char tmp[512];

    if (logger == NULL || logs == NULL || max_entries <= 0) {
        return 0;
    }

    pthread_mutex_lock(&logger_mutex);

    n = logger->memory_logs_count;
    collected = 0;
    /* Walk newest -> oldest across the whole ring, keep up to max_entries
     * matching the minimum level. */
    for (k = 1; k <= n && collected < max_entries; k++) {
        idx = (logger->memory_logs_start + n - k) % 1000;
        if (logger_parse_line_level(logger->memory_logs[idx]) >= min_level) {
            strncpy(logs[collected], logger->memory_logs[idx], 511);
            logs[collected][511] = '\0';
            collected++;
        }
    }
    /* Collected newest-first; reverse to oldest-first to match the other API. */
    for (a = 0, b = collected - 1; a < b; a++, b--) {
        memcpy(tmp, logs[a], 512);
        memcpy(logs[a], logs[b], 512);
        memcpy(logs[b], tmp, 512);
    }

    pthread_mutex_unlock(&logger_mutex);

    return collected;
}

/* Convenience methods */
void logger_verbose(const char* message) {
    logger_log(LOG_LEVEL_VERBOSE, message);
}

void logger_debug(const char* message) {
    logger_log(LOG_LEVEL_DEBUG, message);
}

void logger_info(const char* message) {
    logger_log(LOG_LEVEL_INFO, message);
}

void logger_warn(const char* message) {
    logger_log(LOG_LEVEL_WARN, message);
}

void logger_error(const char* message) {
    logger_log(LOG_LEVEL_ERROR, message);
}

void logger_verbose_with_category(const char* category, const char* message) {
    logger_log_with_category(LOG_LEVEL_VERBOSE, category, message);
}

void logger_debug_with_category(const char* category, const char* message) {
    logger_log_with_category(LOG_LEVEL_DEBUG, category, message);
}

void logger_info_with_category(const char* category, const char* message) {
    logger_log_with_category(LOG_LEVEL_INFO, category, message);
}

void logger_warn_with_category(const char* category, const char* message) {
    logger_log_with_category(LOG_LEVEL_WARN, category, message);
}

void logger_error_with_category(const char* category, const char* message) {
    logger_log_with_category(LOG_LEVEL_ERROR, category, message);
}

/* C89 compatible printf-style logging - single implementation */
static void logger_vlogf(log_level_t level, const char* category, const char* format, va_list args) {
    char buffer[512];
    
    if (format == NULL) {
        return;
    }
    
    vsnprintf(buffer, sizeof(buffer), format, args);
    
    if (category && strlen(category) > 0) {
        logger_log_with_category(level, category, buffer);
    } else {
        logger_log(level, buffer);
    }
}

/* Printf-style logging methods */
void logger_logf(log_level_t level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    logger_vlogf(level, NULL, format, args);
    va_end(args);
}

void logger_logf_with_category(log_level_t level, const char* category, const char* format, ...) {
    va_list args;
    va_start(args, format);
    logger_vlogf(level, category, format, args);
    va_end(args);
}

/* Convenience methods using the simplified implementation */
void logger_verbosef(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logger_vlogf(LOG_LEVEL_VERBOSE, NULL, format, args);
    va_end(args);
}

void logger_debugf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logger_vlogf(LOG_LEVEL_DEBUG, NULL, format, args);
    va_end(args);
}

void logger_infof(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logger_vlogf(LOG_LEVEL_INFO, NULL, format, args);
    va_end(args);
}

void logger_warnf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logger_vlogf(LOG_LEVEL_WARN, NULL, format, args);
    va_end(args);
}

void logger_errorf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logger_vlogf(LOG_LEVEL_ERROR, NULL, format, args);
    va_end(args);
}

void logger_verbosef_with_category(const char* category, const char* format, ...) {
    va_list args;
    va_start(args, format);
    logger_vlogf(LOG_LEVEL_VERBOSE, category, format, args);
    va_end(args);
}

void logger_debugf_with_category(const char* category, const char* format, ...) {
    va_list args;
    va_start(args, format);
    logger_vlogf(LOG_LEVEL_DEBUG, category, format, args);
    va_end(args);
}

void logger_infof_with_category(const char* category, const char* format, ...) {
    va_list args;
    va_start(args, format);
    logger_vlogf(LOG_LEVEL_INFO, category, format, args);
    va_end(args);
}

void logger_warnf_with_category(const char* category, const char* format, ...) {
    va_list args;
    va_start(args, format);
    logger_vlogf(LOG_LEVEL_WARN, category, format, args);
    va_end(args);
}

void logger_errorf_with_category(const char* category, const char* format, ...) {
    va_list args;
    va_start(args, format);
    logger_vlogf(LOG_LEVEL_ERROR, category, format, args);
    va_end(args);
}
