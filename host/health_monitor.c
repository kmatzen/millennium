#include "health_monitor.h"
#include "logger.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <pthread.h>

/* Global instance */
static health_monitor_t g_health_monitor;
static int g_initialized = 0;
static pthread_mutex_t g_monitor_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_monitoring_thread;

/* Forward declarations */
static void* monitoring_loop(void* arg);
static void update_statistics(health_status_t status);
static int find_check_index(const char* name);
static health_status_t execute_check(health_check_t* check);

health_monitor_t* health_monitor_get_instance(void) {
    if (!g_initialized) {
        /* Initialize the global instance */
        memset(&g_health_monitor, 0, sizeof(health_monitor_t));
        g_health_monitor.checks_count = 0;
        g_health_monitor.monitoring_active = 0;
        g_health_monitor.should_stop = 0;
        g_health_monitor.statistics.start_time = time(NULL);
        g_initialized = 1;
    }
    return &g_health_monitor;
}

int health_monitor_register_check(const char* name, 
                                 health_check_func_t check_function,
                                 time_t interval_seconds) {
    health_monitor_t* monitor = health_monitor_get_instance();
    int existing_index;
    int index;

    if (!name || !check_function) {
        return 0; /* Invalid parameters */
    }

    pthread_mutex_lock(&g_monitor_mutex);

    /* Check if already exists */
    existing_index = find_check_index(name);
    if (existing_index >= 0) {
        pthread_mutex_unlock(&g_monitor_mutex);
        return 0; /* Already exists */
    }
    
    /* Check if we have space */
    if (monitor->checks_count >= 32) {
        pthread_mutex_unlock(&g_monitor_mutex);
        return 0; /* No space */
    }
    
    /* Add new check */
    index = monitor->checks_count;
    strncpy(monitor->checks[index].name, name, sizeof(monitor->checks[index].name) - 1);
    monitor->checks[index].name[sizeof(monitor->checks[index].name) - 1] = '\0';
    monitor->checks[index].check_function = check_function;
    monitor->checks[index].interval_seconds = interval_seconds;
    monitor->checks[index].last_check_time = time(NULL);
    monitor->checks[index].last_status = HEALTH_STATUS_UNKNOWN;
    strcpy(monitor->checks[index].last_message, "Not yet checked");
    
    monitor->checks_count++;
    
    pthread_mutex_unlock(&g_monitor_mutex);
    
    logger_info_with_category("HealthMonitor", "Registered health check");
    return 1; /* Success */
}

void health_monitor_unregister_check(const char* name) {
    health_monitor_t* monitor = health_monitor_get_instance();
    int index;

    if (!name) {
        return;
    }

    pthread_mutex_lock(&g_monitor_mutex);

    index = find_check_index(name);
    if (index >= 0) {
        /* Shift remaining checks down */
        int i;
        for (i = index; i < monitor->checks_count - 1; i++) {
            monitor->checks[i] = monitor->checks[i + 1];
        }
        monitor->checks_count--;
    }
    
    pthread_mutex_unlock(&g_monitor_mutex);
    
    if (index >= 0) {
        logger_info_with_category("HealthMonitor", "Unregistered health check");
    }
}

health_status_t health_monitor_get_overall_status(void) {
    health_monitor_t* monitor = health_monitor_get_instance();
    health_status_t overall_status = HEALTH_STATUS_HEALTHY;
    int i;
    
    pthread_mutex_lock(&g_monitor_mutex);
    
    if (monitor->checks_count == 0) {
        overall_status = HEALTH_STATUS_UNKNOWN;
    } else {
        for (i = 0; i < monitor->checks_count; i++) {
            if (monitor->checks[i].last_status > overall_status) {
                overall_status = monitor->checks[i].last_status;
            }
        }
    }
    
    pthread_mutex_unlock(&g_monitor_mutex);
    
    return overall_status;
}

int health_monitor_get_all_checks(health_check_t* checks_out, int max_checks) {
    health_monitor_t* monitor = health_monitor_get_instance();
    int count = 0;
    int i;
    
    if (!checks_out || max_checks <= 0) {
        return 0;
    }
    
    pthread_mutex_lock(&g_monitor_mutex);
    
    count = (monitor->checks_count < max_checks) ? monitor->checks_count : max_checks;
    for (i = 0; i < count; i++) {
        checks_out[i] = monitor->checks[i];
    }
    
    pthread_mutex_unlock(&g_monitor_mutex);
    
    return count;
}

int health_monitor_get_check(const char* name, health_check_t* check_out) {
    health_monitor_t* monitor = health_monitor_get_instance();
    int index;
    
    if (!name || !check_out) {
        return 0;
    }
    
    pthread_mutex_lock(&g_monitor_mutex);
    
    index = find_check_index(name);
    if (index >= 0) {
        *check_out = monitor->checks[index];
        pthread_mutex_unlock(&g_monitor_mutex);
        return 1;
    }
    
    pthread_mutex_unlock(&g_monitor_mutex);
    
    /* Return empty check */
    strncpy(check_out->name, name, sizeof(check_out->name) - 1);
    check_out->name[sizeof(check_out->name) - 1] = '\0';
    check_out->check_function = NULL;
    check_out->interval_seconds = 0;
    check_out->last_check_time = 0;
    check_out->last_status = HEALTH_STATUS_UNKNOWN;
    strcpy(check_out->last_message, "Check not found");
    
    return 0;
}

void health_monitor_run_check(const char* name) {
    health_monitor_t* monitor = health_monitor_get_instance();
    int index;
    health_status_t status;

    if (!name) {
        return;
    }

    pthread_mutex_lock(&g_monitor_mutex);

    index = find_check_index(name);
    if (index < 0) {
        pthread_mutex_unlock(&g_monitor_mutex);
        logger_warn_with_category("HealthMonitor", "Health check not found");
        return;
    }

    /* Run the check */
    status = execute_check(&monitor->checks[index]);

    pthread_mutex_unlock(&g_monitor_mutex);
    
    if (status != HEALTH_STATUS_HEALTHY) {
        logger_warn_with_category("HealthMonitor", "Health check returned non-healthy status");
    }
}

void health_monitor_run_all_checks(void) {
    health_monitor_t* monitor = health_monitor_get_instance();
    int i;

    pthread_mutex_lock(&g_monitor_mutex);

    for (i = 0; i < monitor->checks_count; i++) {
        execute_check(&monitor->checks[i]);
    }

    pthread_mutex_unlock(&g_monitor_mutex);
}

void health_monitor_start_monitoring(void) {
    health_monitor_t* monitor = health_monitor_get_instance();
    
    pthread_mutex_lock(&g_monitor_mutex);
    
    if (monitor->monitoring_active) {
        pthread_mutex_unlock(&g_monitor_mutex);
        return;
    }
    
    monitor->monitoring_active = 1;
    monitor->should_stop = 0;
    monitor->statistics.start_time = time(NULL);
    
    if (pthread_create(&g_monitoring_thread, NULL, monitoring_loop, NULL) != 0) {
        monitor->monitoring_active = 0;
        pthread_mutex_unlock(&g_monitor_mutex);
        return;
    }
    
    pthread_mutex_unlock(&g_monitor_mutex);
    
    logger_info_with_category("HealthMonitor", "Health monitoring started");
}

void health_monitor_stop_monitoring(void) {
    health_monitor_t* monitor = health_monitor_get_instance();
    
    pthread_mutex_lock(&g_monitor_mutex);
    monitor->should_stop = 1;
    monitor->monitoring_active = 0;
    pthread_mutex_unlock(&g_monitor_mutex);
    
    /* Wait for thread to finish */
    pthread_join(g_monitoring_thread, NULL);
    
    logger_info_with_category("HealthMonitor", "Health monitoring stopped");
}

int health_monitor_is_monitoring(void) {
    health_monitor_t* monitor = health_monitor_get_instance();
    int result;
    
    pthread_mutex_lock(&g_monitor_mutex);
    result = monitor->monitoring_active;
    pthread_mutex_unlock(&g_monitor_mutex);
    
    return result;
}

int health_monitor_get_statistics(health_statistics_t* stats_out) {
    health_monitor_t* monitor = health_monitor_get_instance();
    
    if (!stats_out) {
        return 0;
    }
    
    pthread_mutex_lock(&g_monitor_mutex);
    *stats_out = monitor->statistics;
    pthread_mutex_unlock(&g_monitor_mutex);
    
    return 1;
}

const char* health_monitor_status_to_string(health_status_t status) {
    switch (status) {
        case HEALTH_STATUS_HEALTHY: return "HEALTHY";
        case HEALTH_STATUS_WARNING: return "WARNING";
        case HEALTH_STATUS_CRITICAL: return "CRITICAL";
        case HEALTH_STATUS_UNKNOWN: return "UNKNOWN";
        default: return "UNKNOWN";
    }
}

int health_monitor_status_is_serving(health_status_t status) {
    switch (status) {
        case HEALTH_STATUS_HEALTHY:
        case HEALTH_STATUS_WARNING:
            return 1;
        case HEALTH_STATUS_CRITICAL:
        case HEALTH_STATUS_UNKNOWN:
        default:
            return 0;
    }
}

health_status_t health_monitor_string_to_status(const char* status_str) {
    if (!status_str) {
        return HEALTH_STATUS_UNKNOWN;
    }
    
    /* Simple string comparison (case sensitive for C89 compatibility) */
    if (strcmp(status_str, "HEALTHY") == 0) return HEALTH_STATUS_HEALTHY;
    if (strcmp(status_str, "WARNING") == 0) return HEALTH_STATUS_WARNING;
    if (strcmp(status_str, "CRITICAL") == 0) return HEALTH_STATUS_CRITICAL;
    if (strcmp(status_str, "UNKNOWN") == 0) return HEALTH_STATUS_UNKNOWN;
    
    return HEALTH_STATUS_UNKNOWN;
}

/* Static helper functions */
static void* monitoring_loop(void* arg) {
    health_monitor_t* monitor = health_monitor_get_instance();
    time_t now;
    int i;
    
    (void)arg; /* Suppress unused parameter warning */
    
    while (1) {
        pthread_mutex_lock(&g_monitor_mutex);
        
        if (monitor->should_stop) {
            pthread_mutex_unlock(&g_monitor_mutex);
            break;
        }
        
        now = time(NULL);
        
        for (i = 0; i < monitor->checks_count; i++) {
            if (now - monitor->checks[i].last_check_time >= monitor->checks[i].interval_seconds) {
                execute_check(&monitor->checks[i]);
            }
        }
        
        pthread_mutex_unlock(&g_monitor_mutex);
        
        sleep(1); /* Sleep for 1 second */
    }
    
    return NULL;
}

/* Run one check, recording status, timestamp, and message. The check may fill
 * `message` with a diagnostic; if it leaves the buffer empty we synthesize a
 * status-derived default so the /api/health "message" field always reflects the
 * actual finding (it was previously hard-coded to "Check completed successfully"
 * even when a check returned CRITICAL). Caller must hold g_monitor_mutex. */
static health_status_t execute_check(health_check_t* check) {
    char message[256];
    health_status_t status;

    message[0] = '\0';
    status = check->check_function(message, sizeof(message));

    check->last_status = status;
    check->last_check_time = time(NULL);
    if (message[0] != '\0') {
        strncpy(check->last_message, message, sizeof(check->last_message) - 1);
        check->last_message[sizeof(check->last_message) - 1] = '\0';
    } else {
        snprintf(check->last_message, sizeof(check->last_message),
                 "Status: %s", health_monitor_status_to_string(status));
    }

    update_statistics(status);
    return status;
}

static void update_statistics(health_status_t status) {
    health_monitor_t* monitor = health_monitor_get_instance();
    
    monitor->statistics.total_checks++;
    
    if (status == HEALTH_STATUS_CRITICAL) {
        monitor->statistics.failed_checks++;
    } else if (status == HEALTH_STATUS_WARNING) {
        monitor->statistics.warning_checks++;
    }
}

static int find_check_index(const char* name) {
    health_monitor_t* monitor = health_monitor_get_instance();
    int i;
    
    for (i = 0; i < monitor->checks_count; i++) {
        if (strcmp(monitor->checks[i].name, name) == 0) {
            return i;
        }
    }
    
    return -1;
}

/* System health check implementations.
 *
 * These are generic, registerable checks kept alongside the monitor. The daemon
 * registers its own hardware-aware serial/SIP checks (see daemon.c); the two
 * here are simple stand-ins for builds without that wiring. Each writes a short
 * diagnostic into `message` for /api/health. */
health_status_t system_health_check_serial_connection(char *message, size_t message_len) {
    snprintf(message, message_len, "Serial connection assumed up (generic check)");
    return HEALTH_STATUS_HEALTHY;
}

health_status_t system_health_check_sip_connection(char *message, size_t message_len) {
    snprintf(message, message_len, "SIP connection assumed up (generic check)");
    return HEALTH_STATUS_HEALTHY;
}

health_status_t system_health_check_memory_usage(char *message, size_t message_len) {
    FILE* status_file;
    char line[256];
    char key[32];
    char value[32];
    char unit[32];
    long memory_kb;
    long memory_mb;

    status_file = fopen("/proc/self/status", "r");
    if (!status_file) {
        snprintf(message, message_len, "Could not read /proc/self/status");
        return HEALTH_STATUS_UNKNOWN;
    }

    while (fgets(line, sizeof(line), status_file)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            if (sscanf(line, "%31s %31s %31s", key, value, unit) == 3) {
                memory_kb = atol(value);
                memory_mb = memory_kb / 1024;

                fclose(status_file);

                if (memory_mb > 1000) { /* More than 1GB */
                    snprintf(message, message_len,
                             "Resident memory %ld MB exceeds 1000 MB", memory_mb);
                    return HEALTH_STATUS_CRITICAL;
                } else if (memory_mb > 500) { /* More than 500MB */
                    snprintf(message, message_len,
                             "Resident memory %ld MB exceeds 500 MB", memory_mb);
                    return HEALTH_STATUS_WARNING;
                }

                snprintf(message, message_len, "Resident memory %ld MB", memory_mb);
                return HEALTH_STATUS_HEALTHY;
            }
        }
    }

    fclose(status_file);
    snprintf(message, message_len, "VmRSS not found in /proc/self/status");
    return HEALTH_STATUS_UNKNOWN;
}

health_status_t system_health_check_disk_space(char *message, size_t message_len) {
    struct statvfs stat;
    unsigned long free_space;
    unsigned long total_space;
    double free_percentage;

    if (statvfs("/", &stat) != 0) {
        snprintf(message, message_len, "statvfs(\"/\") failed");
        return HEALTH_STATUS_UNKNOWN;
    }

    free_space = stat.f_bavail * stat.f_frsize;
    total_space = stat.f_blocks * stat.f_frsize;

    free_percentage = (double)free_space / total_space * 100.0;

    if (free_percentage < 5.0) {
        snprintf(message, message_len,
                 "Root filesystem %.1f%% free (below 5%%)", free_percentage);
        return HEALTH_STATUS_CRITICAL;
    } else if (free_percentage < 10.0) {
        snprintf(message, message_len,
                 "Root filesystem %.1f%% free (below 10%%)", free_percentage);
        return HEALTH_STATUS_WARNING;
    }

    snprintf(message, message_len, "Root filesystem %.1f%% free", free_percentage);
    return HEALTH_STATUS_HEALTHY;
}

health_status_t system_health_check_system_load(char *message, size_t message_len) {
    FILE* loadavg_file;
    char line[256];
    double load1, load5, load15;
    FILE* cpuinfo;
    char cpu_line[256];
    int cpu_count = 0;
    double load_percentage;

    loadavg_file = fopen("/proc/loadavg", "r");
    if (!loadavg_file) {
        snprintf(message, message_len, "Could not read /proc/loadavg");
        return HEALTH_STATUS_UNKNOWN;
    }

    if (fgets(line, sizeof(line), loadavg_file)) {
        if (sscanf(line, "%lf %lf %lf", &load1, &load5, &load15) == 3) {
            /* Get number of CPU cores */
            cpuinfo = fopen("/proc/cpuinfo", "r");
            if (cpuinfo) {
                while (fgets(cpu_line, sizeof(cpu_line), cpuinfo)) {
                    if (strncmp(cpu_line, "processor", 9) == 0) {
                        cpu_count++;
                    }
                }
                fclose(cpuinfo);
            }

            if (cpu_count == 0) {
                cpu_count = 1; /* Fallback */
            }

            load_percentage = (load1 / cpu_count) * 100.0;

            fclose(loadavg_file);

            if (load_percentage > 90.0) {
                snprintf(message, message_len,
                         "Load %.2f = %.0f%% of %d core(s)",
                         load1, load_percentage, cpu_count);
                return HEALTH_STATUS_CRITICAL;
            } else if (load_percentage > 70.0) {
                snprintf(message, message_len,
                         "Load %.2f = %.0f%% of %d core(s)",
                         load1, load_percentage, cpu_count);
                return HEALTH_STATUS_WARNING;
            }

            snprintf(message, message_len,
                     "Load %.2f = %.0f%% of %d core(s)",
                     load1, load_percentage, cpu_count);
            return HEALTH_STATUS_HEALTHY;
        }
    }

    fclose(loadavg_file);
    snprintf(message, message_len, "Could not parse /proc/loadavg");
    return HEALTH_STATUS_UNKNOWN;
}
