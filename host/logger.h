#ifndef LOGGER_H
#define LOGGER_H

#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

/* C89 compatible logging levels */
typedef enum {
    LOG_LEVEL_VERBOSE = 0,
    LOG_LEVEL_DEBUG = 1,
    LOG_LEVEL_INFO = 2,
    LOG_LEVEL_WARN = 3,
    LOG_LEVEL_ERROR = 4
} log_level_t;

/* C89 compatible logger structure */
typedef struct {
    log_level_t current_level;
    char log_file[256];
    int log_to_console;
    int log_to_file;
    FILE* file_stream;
    
    /* Log rotation */
    long max_file_size;     /* Max bytes before rotation (0 = disabled) */
    int max_rotated_files;  /* Number of rotated files to keep */
    long current_file_size; /* Approximate bytes written to current file */

    /* In-memory log storage (512-char entries; truncation may cut UTF-8 mid-char (#108)) */
    char memory_logs[1000][512];
    int memory_logs_count;
    int memory_logs_start;  /* For circular buffer behavior */
} logger_data_t;

/* Global logger instance */
extern logger_data_t* g_logger;

/* Function declarations */
logger_data_t* logger_get_instance(void);
void logger_set_level(log_level_t level);
void logger_set_log_file(const char* filename);
void logger_set_log_to_console(int enable);
void logger_set_log_to_file(int enable);
void logger_set_rotation(long max_file_size, int max_rotated_files);

/* Logging methods */
void logger_log(log_level_t level, const char* message);
void logger_log_with_category(log_level_t level, const char* category, const char* message);

/* Printf-style logging methods */
void logger_logf(log_level_t level, const char* format, ...);
void logger_logf_with_category(log_level_t level, const char* category, const char* format, ...);

/* Convenience methods */
void logger_verbose(const char* message);
void logger_debug(const char* message);
void logger_info(const char* message);
void logger_warn(const char* message);
void logger_error(const char* message);

void logger_verbose_with_category(const char* category, const char* message);
void logger_debug_with_category(const char* category, const char* message);
void logger_info_with_category(const char* category, const char* message);
void logger_warn_with_category(const char* category, const char* message);
void logger_error_with_category(const char* category, const char* message);

/* Printf-style convenience methods */
void logger_verbosef(const char* format, ...);
void logger_debugf(const char* format, ...);
void logger_infof(const char* format, ...);
void logger_warnf(const char* format, ...);
void logger_errorf(const char* format, ...);

void logger_verbosef_with_category(const char* category, const char* format, ...);
void logger_debugf_with_category(const char* category, const char* format, ...);
void logger_infof_with_category(const char* category, const char* format, ...);
void logger_warnf_with_category(const char* category, const char* format, ...);
void logger_errorf_with_category(const char* category, const char* format, ...);

/* Utility methods */
log_level_t logger_parse_level(const char* level_str);
const char* logger_level_to_string(log_level_t level);

/* In-memory log storage */
int logger_get_recent_logs(char logs[][512], int max_entries);

/* Internal functions */
void logger_write_log(log_level_t level, const char* category, const char* message);
void logger_format_timestamp(char* buffer, size_t buffer_size);
const char* logger_format_level(log_level_t level);
void logger_add_to_memory(const char* formatted_message);

/* C89 compatible macros for structured logging */
#define LOG_VERBOSE(...) logger_verbosef(__VA_ARGS__)
#define LOG_DEBUG(...) logger_debugf(__VA_ARGS__)
#define LOG_INFO(...) logger_infof(__VA_ARGS__)
#define LOG_WARN(...) logger_warnf(__VA_ARGS__)
#define LOG_ERROR(...) logger_errorf(__VA_ARGS__)

#define LOG_VERBOSE_CAT(cat, ...) logger_verbosef_with_category(cat, __VA_ARGS__)
#define LOG_DEBUG_CAT(cat, ...) logger_debugf_with_category(cat, __VA_ARGS__)
#define LOG_INFO_CAT(cat, ...) logger_infof_with_category(cat, __VA_ARGS__)
#define LOG_WARN_CAT(cat, ...) logger_warnf_with_category(cat, __VA_ARGS__)
#define LOG_ERROR_CAT(cat, ...) logger_errorf_with_category(cat, __VA_ARGS__)

#endif /* LOGGER_H */
