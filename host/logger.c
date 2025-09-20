#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>

/* Global logger instance */
logger_data_t* g_logger = NULL;

/* C89 compatible safe sprintf - ensures null termination */
static void safe_sprintf(char* buffer, size_t buffer_size, const char* format, va_list args) {
    vsprintf(buffer, format, args);
    /* Ensure null termination */
    buffer[buffer_size - 1] = '\0';
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
        }
    } else {
        if (logger->file_stream != NULL) {
            fclose(logger->file_stream);
            logger->file_stream = NULL;
        }
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
    
    if (logger == NULL || message == NULL) {
        return;
    }
    
    logger_format_timestamp(timestamp, sizeof(timestamp));
    level_str = logger_format_level(level);
    
    /* Format the log message */
    if (category != NULL && strlen(category) > 0) {
        sprintf(formatted_message, "[%s] [%s] [%s] %s", timestamp, level_str, category, message);
    } else {
        sprintf(formatted_message, "[%s] [%s] %s", timestamp, level_str, message);
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
        fprintf(logger->file_stream, "%s\n", formatted_message);
        fflush(logger->file_stream);
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
    sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
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
    
    count = (max_entries < logger->memory_logs_count) ? max_entries : logger->memory_logs_count;
    
    for (i = 0, j = logger->memory_logs_start; i < count; i++) {
        strncpy(logs[i], logger->memory_logs[j], sizeof(logs[i]) - 1);
        logs[i][sizeof(logs[i]) - 1] = '\0';
        j = (j + 1) % 1000;
    }
    
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

/* Printf-style logging methods */
void logger_logf(log_level_t level, const char* format, ...) {
    char buffer[512];
    va_list args;
    
    if (format == NULL) {
        return;
    }
    
    va_start(args, format);
    safe_sprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    logger_log(level, buffer);
}

void logger_logf_with_category(log_level_t level, const char* category, const char* format, ...) {
    char buffer[512];
    va_list args;
    
    if (format == NULL) {
        return;
    }
    
    va_start(args, format);
    safe_sprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    logger_log_with_category(level, category, buffer);
}

/* Printf-style convenience methods */
void logger_verbosef(const char* format, ...) {
    char buffer[512];
    va_list args;
    
    if (format == NULL) {
        return;
    }
    
    va_start(args, format);
    safe_sprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    logger_verbose(buffer);
}

void logger_debugf(const char* format, ...) {
    char buffer[512];
    va_list args;
    
    if (format == NULL) {
        return;
    }
    
    va_start(args, format);
    safe_sprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    logger_debug(buffer);
}

void logger_infof(const char* format, ...) {
    char buffer[512];
    va_list args;
    
    if (format == NULL) {
        return;
    }
    
    va_start(args, format);
    safe_sprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    logger_info(buffer);
}

void logger_warnf(const char* format, ...) {
    char buffer[512];
    va_list args;
    
    if (format == NULL) {
        return;
    }
    
    va_start(args, format);
    safe_sprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    logger_warn(buffer);
}

void logger_errorf(const char* format, ...) {
    char buffer[512];
    va_list args;
    
    if (format == NULL) {
        return;
    }
    
    va_start(args, format);
    safe_sprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    logger_error(buffer);
}

void logger_verbosef_with_category(const char* category, const char* format, ...) {
    char buffer[512];
    va_list args;
    
    if (format == NULL) {
        return;
    }
    
    va_start(args, format);
    safe_sprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    logger_verbose_with_category(category, buffer);
}

void logger_debugf_with_category(const char* category, const char* format, ...) {
    char buffer[512];
    va_list args;
    
    if (format == NULL) {
        return;
    }
    
    va_start(args, format);
    safe_sprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    logger_debug_with_category(category, buffer);
}

void logger_infof_with_category(const char* category, const char* format, ...) {
    char buffer[512];
    va_list args;
    
    if (format == NULL) {
        return;
    }
    
    va_start(args, format);
    safe_sprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    logger_info_with_category(category, buffer);
}

void logger_warnf_with_category(const char* category, const char* format, ...) {
    char buffer[512];
    va_list args;
    
    if (format == NULL) {
        return;
    }
    
    va_start(args, format);
    safe_sprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    logger_warn_with_category(category, buffer);
}

void logger_errorf_with_category(const char* category, const char* format, ...) {
    char buffer[512];
    va_list args;
    
    if (format == NULL) {
        return;
    }
    
    va_start(args, format);
    safe_sprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    logger_error_with_category(category, buffer);
}
