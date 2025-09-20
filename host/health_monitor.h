#ifndef HEALTH_MONITOR_H
#define HEALTH_MONITOR_H

#include <time.h>
#include <stddef.h>

/* C89 compatible health status enum */
typedef enum {
    HEALTH_STATUS_HEALTHY = 0,
    HEALTH_STATUS_WARNING = 1,
    HEALTH_STATUS_CRITICAL = 2,
    HEALTH_STATUS_UNKNOWN = 3
} health_status_t;

/* Function pointer type for health check functions */
typedef health_status_t (*health_check_func_t)(void);

/* C89 compatible health check structure */
typedef struct {
    char name[64];  /* Fixed size string for C89 */
    health_check_func_t check_function;
    time_t interval_seconds;  /* Interval in seconds */
    time_t last_check_time;
    health_status_t last_status;
    char last_message[256];  /* Fixed size message buffer */
} health_check_t;

/* C89 compatible statistics structure */
typedef struct {
    time_t start_time;
    unsigned long total_checks;
    unsigned long failed_checks;
    unsigned long warning_checks;
} health_statistics_t;

/* C89 compatible health monitor structure */
typedef struct {
    health_check_t checks[32];  /* Fixed size array for C89 */
    int checks_count;
    int monitoring_active;
    int should_stop;
    health_statistics_t statistics;
} health_monitor_t;

/* Global instance access */
health_monitor_t* health_monitor_get_instance(void);

/* Health check management */
int health_monitor_register_check(const char* name, 
                                 health_check_func_t check_function,
                                 time_t interval_seconds);
void health_monitor_unregister_check(const char* name);

/* Status reporting */
health_status_t health_monitor_get_overall_status(void);
int health_monitor_get_all_checks(health_check_t* checks_out, int max_checks);
int health_monitor_get_check(const char* name, health_check_t* check_out);

/* Manual check execution */
void health_monitor_run_check(const char* name);
void health_monitor_run_all_checks(void);

/* Monitoring control */
void health_monitor_start_monitoring(void);
void health_monitor_stop_monitoring(void);
int health_monitor_is_monitoring(void);

/* Statistics */
int health_monitor_get_statistics(health_statistics_t* stats_out);

/* Utility methods */
const char* health_monitor_status_to_string(health_status_t status);
health_status_t health_monitor_string_to_status(const char* status_str);

/* Predefined system health checks */
health_status_t system_health_check_serial_connection(void);
health_status_t system_health_check_sip_connection(void);
health_status_t system_health_check_memory_usage(void);
health_status_t system_health_check_disk_space(void);
health_status_t system_health_check_system_load(void);

#endif /* HEALTH_MONITOR_H */