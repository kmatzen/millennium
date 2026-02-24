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
static pthread_mutex_t logger_mutex = PTHREAD_MUTEX_INITIALIZER;


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
        }
    } else {
        if (logger->file_stream != NULL) {
            fclose(logger->file_stream);
            logger->file_stream = NULL;
        }
    }
}

void logger_set_rotation(long max_file_size, int max_rotated_files) {
    logger_data_t* logger = logger_get_instance();
    if (logger == NULL) {
        return;
    }
    logger->max_file_size = max_file_size;
    logger->max_rotated_files = (max_rotated_files > 0) ? max_rotated_files : 0;
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

/* formatted_message is 512 chars; truncation may cut UTF-8 mid-char (#108) */
void logger_write_log(log_level_t level, const char* category, const char* message) {
    logger_data_t* logger = logger_get_instance();
    char timestamp[64];
    char formatted_message[512];
    const char* level_str;
    
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
    
    /* Output to file */
    if (logger->log_to_file && logger->file_stream != NULL) {
        int written = fprintf(logger->file_stream, "%s\n", formatted_message);
        fflush(logger->file_stream);
        if (written > 0) {
            logger->current_file_size += written;
        }
        logger_check_rotation(logger);
    }
    
    pthread_mutex_unlock(&logger_mutex);
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
    
    if (logger == NULL || formatted_message == NULL) {
        return;
    }
    
    /* Use circular buffer */
    index = (logger->memory_logs_start + logger->memory_logs_count) % 1000;
    
    /* Safe copy with guaranteed null termination - truncation is intentional */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
    strncpy(logger->memory_logs[index], formatted_message, sizeof(logger->memory_logs[index]) - 1);
#pragma GCC diagnostic pop
    logger->memory_logs[index][sizeof(logger->memory_logs[index]) - 1] = '\0';
    
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
