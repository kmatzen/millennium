#define _POSIX_C_SOURCE 200112L
#include "metrics.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <pthread.h>

/* Global metrics instance */
metrics_t *g_metrics = NULL;

static pthread_mutex_t metrics_mutex = PTHREAD_MUTEX_INITIALIZER;

/* C89-compatible strdup implementation */
static char *my_strdup(const char *s) {
    size_t len;
    char *copy;
    
    if (!s) return NULL;
    
    len = strlen(s) + 1;
    copy = malloc(len);
    if (copy) {
        memcpy(copy, s, len);
    }
    return copy;
}

/* Helper function to find counter index by name */
static int find_counter_index(const char *name) {
    int i;
    for (i = 0; i < (int)g_metrics->counter_count; i++) {
        if (strcmp(g_metrics->counter_names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

/* Helper function to find gauge index by name */
static int find_gauge_index(const char *name) {
    int i;
    for (i = 0; i < (int)g_metrics->gauge_count; i++) {
        if (strcmp(g_metrics->gauge_names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

/* Helper function to find histogram index by name */
static int find_histogram_index(const char *name) {
    int i;
    for (i = 0; i < (int)g_metrics->histogram_count; i++) {
        if (strcmp(g_metrics->histogram_names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

/* Helper function to add a counter */
static int add_counter(const char *name) {
    if (g_metrics->counter_count >= g_metrics->counter_capacity) {
        size_t new_capacity = g_metrics->counter_capacity * 2;
        if (new_capacity == 0) new_capacity = 16;
        
        metrics_counter_t *new_counters = realloc(g_metrics->counters, 
            new_capacity * sizeof(metrics_counter_t));
        if (!new_counters) return -1;
        
        char **new_names = realloc(g_metrics->counter_names, 
            new_capacity * sizeof(char*));
        if (!new_names) {
            free(new_counters);
            return -1;
        }
        
        g_metrics->counters = new_counters;
        g_metrics->counter_names = new_names;
        g_metrics->counter_capacity = new_capacity;
    }
    
    int index = (int)g_metrics->counter_count;
    g_metrics->counter_names[index] = my_strdup(name);
    if (!g_metrics->counter_names[index]) return -1;
    
    g_metrics->counters[index].value = 0;
    g_metrics->counters[index].last_reset = time(NULL);
    
    g_metrics->counter_count++;
    return index;
}

/* Helper function to add a gauge */
static int add_gauge(const char *name) {
    if (g_metrics->gauge_count >= g_metrics->gauge_capacity) {
        size_t new_capacity = g_metrics->gauge_capacity * 2;
        if (new_capacity == 0) new_capacity = 16;
        
        metrics_gauge_t *new_gauges = realloc(g_metrics->gauges, 
            new_capacity * sizeof(metrics_gauge_t));
        if (!new_gauges) return -1;
        
        char **new_names = realloc(g_metrics->gauge_names, 
            new_capacity * sizeof(char*));
        if (!new_names) {
            free(new_gauges);
            return -1;
        }
        
        g_metrics->gauges = new_gauges;
        g_metrics->gauge_names = new_names;
        g_metrics->gauge_capacity = new_capacity;
    }
    
    int index = (int)g_metrics->gauge_count;
    g_metrics->gauge_names[index] = my_strdup(name);
    if (!g_metrics->gauge_names[index]) return -1;
    
    g_metrics->gauges[index].value = 0.0;
    g_metrics->gauges[index].last_update = time(NULL);
    
    g_metrics->gauge_count++;
    return index;
}

/* Helper function to add a histogram */
static int add_histogram(const char *name) {
    if (g_metrics->histogram_count >= g_metrics->histogram_capacity) {
        size_t new_capacity = g_metrics->histogram_capacity * 2;
        if (new_capacity == 0) new_capacity = 16;
        
        metrics_histogram_t *new_histograms = realloc(g_metrics->histograms, 
            new_capacity * sizeof(metrics_histogram_t));
        if (!new_histograms) return -1;
        
        char **new_names = realloc(g_metrics->histogram_names, 
            new_capacity * sizeof(char*));
        if (!new_names) {
            free(new_histograms);
            return -1;
        }
        
        g_metrics->histograms = new_histograms;
        g_metrics->histogram_names = new_names;
        g_metrics->histogram_capacity = new_capacity;
    }
    
    int index = (int)g_metrics->histogram_count;
    g_metrics->histogram_names[index] = my_strdup(name);
    if (!g_metrics->histogram_names[index]) return -1;
    
    g_metrics->histograms[index].values = NULL;
    g_metrics->histograms[index].values_size = 0;
    g_metrics->histograms[index].values_capacity = 0;
    g_metrics->histograms[index].count = 0;
    g_metrics->histograms[index].sum = 0.0;
    g_metrics->histograms[index].min_value = DBL_MAX;
    g_metrics->histograms[index].max_value = -DBL_MAX;
    
    g_metrics->histogram_count++;
    return index;
}

/* Simple comparison function for qsort */
static int compare_doubles(const void *a, const void *b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

int metrics_init(void) {
    if (g_metrics) return 0; /* Already initialized */
    
    g_metrics = malloc(sizeof(metrics_t));
    if (!g_metrics) return -1;
    
    memset(g_metrics, 0, sizeof(metrics_t));
    g_metrics->start_time = time(NULL);
    
    return 0;
}

void metrics_cleanup(void) {
    int i;
    
    if (!g_metrics) return;
    
    /* Cleanup counters */
    for (i = 0; i < (int)g_metrics->counter_count; i++) {
        free(g_metrics->counter_names[i]);
    }
    free(g_metrics->counters);
    free(g_metrics->counter_names);
    
    /* Cleanup gauges */
    for (i = 0; i < (int)g_metrics->gauge_count; i++) {
        free(g_metrics->gauge_names[i]);
    }
    free(g_metrics->gauges);
    free(g_metrics->gauge_names);
    
    /* Cleanup histograms */
    for (i = 0; i < (int)g_metrics->histogram_count; i++) {
        free(g_metrics->histograms[i].values);
        free(g_metrics->histogram_names[i]);
    }
    free(g_metrics->histograms);
    free(g_metrics->histogram_names);
    
    free(g_metrics);
    g_metrics = NULL;
}

metrics_t *metrics_get_instance(void) {
    return g_metrics;
}

int metrics_increment_counter(const char *name, uint64_t amount) {
    int index;
    
    if (!g_metrics || !name) return -1;
    
    pthread_mutex_lock(&metrics_mutex);
    
    index = find_counter_index(name);
    if (index < 0) {
        index = add_counter(name);
        if (index < 0) {
            pthread_mutex_unlock(&metrics_mutex);
            return -1;
        }
    }
    
    g_metrics->counters[index].value += amount;
    pthread_mutex_unlock(&metrics_mutex);
    return 0;
}

int metrics_reset_counter(const char *name) {
    int index;
    
    if (!g_metrics || !name) return -1;
    
    pthread_mutex_lock(&metrics_mutex);
    
    index = find_counter_index(name);
    if (index >= 0) {
        g_metrics->counters[index].value = 0;
        g_metrics->counters[index].last_reset = time(NULL);
    }
    
    pthread_mutex_unlock(&metrics_mutex);
    return 0;
}

uint64_t metrics_get_counter(const char *name) {
    int index;
    uint64_t value;
    
    if (!g_metrics || !name) return 0;
    
    pthread_mutex_lock(&metrics_mutex);
    
    index = find_counter_index(name);
    if (index >= 0) {
        value = g_metrics->counters[index].value;
        pthread_mutex_unlock(&metrics_mutex);
        return value;
    }
    
    pthread_mutex_unlock(&metrics_mutex);
    return 0;
}

int metrics_set_gauge(const char *name, double value) {
    int index;
    
    if (!g_metrics || !name) return -1;
    
    pthread_mutex_lock(&metrics_mutex);
    
    index = find_gauge_index(name);
    if (index < 0) {
        index = add_gauge(name);
        if (index < 0) {
            pthread_mutex_unlock(&metrics_mutex);
            return -1;
        }
    }
    
    g_metrics->gauges[index].value = value;
    g_metrics->gauges[index].last_update = time(NULL);
    
    pthread_mutex_unlock(&metrics_mutex);
    return 0;
}

int metrics_increment_gauge(const char *name, double amount) {
    int index;
    
    if (!g_metrics || !name) return -1;
    
    pthread_mutex_lock(&metrics_mutex);
    
    index = find_gauge_index(name);
    if (index < 0) {
        index = add_gauge(name);
        if (index < 0) {
            pthread_mutex_unlock(&metrics_mutex);
            return -1;
        }
    }
    
    g_metrics->gauges[index].value += amount;
    g_metrics->gauges[index].last_update = time(NULL);
    
    pthread_mutex_unlock(&metrics_mutex);
    return 0;
}

int metrics_decrement_gauge(const char *name, double amount) {
    int index;
    
    if (!g_metrics || !name) return -1;
    
    pthread_mutex_lock(&metrics_mutex);
    
    index = find_gauge_index(name);
    if (index < 0) {
        index = add_gauge(name);
        if (index < 0) {
            pthread_mutex_unlock(&metrics_mutex);
            return -1;
        }
    }
    
    g_metrics->gauges[index].value -= amount;
    g_metrics->gauges[index].last_update = time(NULL);
    
    pthread_mutex_unlock(&metrics_mutex);
    return 0;
}

double metrics_get_gauge(const char *name) {
    int index;
    double value;
    
    if (!g_metrics || !name) return 0.0;
    
    pthread_mutex_lock(&metrics_mutex);
    
    index = find_gauge_index(name);
    if (index >= 0) {
        value = g_metrics->gauges[index].value;
        pthread_mutex_unlock(&metrics_mutex);
        return value;
    }
    
    pthread_mutex_unlock(&metrics_mutex);
    return 0.0;
}

int metrics_observe_histogram(const char *name, double value) {
    int index;
    metrics_histogram_t *hist;
    
    if (!g_metrics || !name) return -1;
    
    pthread_mutex_lock(&metrics_mutex);
    
    index = find_histogram_index(name);
    if (index < 0) {
        index = add_histogram(name);
        if (index < 0) {
            pthread_mutex_unlock(&metrics_mutex);
            return -1;
        }
    }
    hist = &g_metrics->histograms[index];
    
    /* Add value to the values array */
    if (hist->values_size >= hist->values_capacity) {
        size_t new_capacity = hist->values_capacity * 2;
        if (new_capacity == 0) new_capacity = 16;
        
        double *new_values = realloc(hist->values, new_capacity * sizeof(double));
        if (!new_values) {
            pthread_mutex_unlock(&metrics_mutex);
            return -1;
        }
        
        hist->values = new_values;
        hist->values_capacity = new_capacity;
    }
    
    hist->values[hist->values_size++] = value;
    
    /* Keep only last 1000 values to prevent memory growth */
    if (hist->values_size > 1000) {
        memmove(hist->values, hist->values + 1, (hist->values_size - 1) * sizeof(double));
        hist->values_size--;
    }
    
    /* Update statistics */
    hist->count++;
    hist->sum += value;
    if (value < hist->min_value) hist->min_value = value;
    if (value > hist->max_value) hist->max_value = value;
    
    pthread_mutex_unlock(&metrics_mutex);
    return 0;
}

static int metrics_get_histogram_stats_unlocked(const char *name, metrics_histogram_stats_t *stats) {
    int index;
    metrics_histogram_t *hist;
    double *values_copy = NULL;
    
    if (!g_metrics || !name || !stats) return -1;
    
    index = find_histogram_index(name);
    if (index < 0) return -1;
    hist = &g_metrics->histograms[index];
    
    /* Copy current statistics */
    stats->count = hist->count;
    stats->sum = hist->sum;
    stats->min = hist->min_value;
    stats->max = hist->max_value;
    
    if (stats->count > 0) {
        stats->mean = stats->sum / stats->count;
    } else {
        stats->mean = 0.0;
    }
    
    /* Copy values for percentile calculations */
    if (hist->values_size > 0) {
        values_copy = malloc(hist->values_size * sizeof(double));
        if (values_copy) {
            memcpy(values_copy, hist->values, hist->values_size * sizeof(double));
            qsort(values_copy, hist->values_size, sizeof(double), compare_doubles);
            
            if (hist->values_size % 2 == 0) {
                stats->median = (values_copy[hist->values_size/2 - 1] + 
                               values_copy[hist->values_size/2]) / 2.0;
            } else {
                stats->median = values_copy[hist->values_size/2];
            }
            
            stats->p95 = values_copy[(size_t)(hist->values_size * 0.95)];
            stats->p99 = values_copy[(size_t)(hist->values_size * 0.99)];
        }
    }
    
    if (!values_copy) {
        stats->median = stats->p95 = stats->p99 = 0.0;
    } else {
        free(values_copy);
    }
    
    return 0;
}

int metrics_get_histogram_stats(const char *name, metrics_histogram_stats_t *stats) {
    int result;
    
    if (!g_metrics || !name || !stats) return -1;
    
    pthread_mutex_lock(&metrics_mutex);
    result = metrics_get_histogram_stats_unlocked(name, stats);
    pthread_mutex_unlock(&metrics_mutex);
    return result;
}

int metrics_reset_all(void) {
    int i;
    
    if (!g_metrics) return -1;
    
    pthread_mutex_lock(&metrics_mutex);
    
    /* Reset counters */
    for (i = 0; i < (int)g_metrics->counter_count; i++) {
        g_metrics->counters[i].value = 0;
        g_metrics->counters[i].last_reset = time(NULL);
    }
    
    /* Reset gauges */
    for (i = 0; i < (int)g_metrics->gauge_count; i++) {
        g_metrics->gauges[i].value = 0.0;
        g_metrics->gauges[i].last_update = time(NULL);
    }
    
    /* Reset histograms */
    for (i = 0; i < (int)g_metrics->histogram_count; i++) {
        free(g_metrics->histograms[i].values);
        g_metrics->histograms[i].values = NULL;
        g_metrics->histograms[i].values_size = 0;
        g_metrics->histograms[i].values_capacity = 0;
        g_metrics->histograms[i].count = 0;
        g_metrics->histograms[i].sum = 0.0;
        g_metrics->histograms[i].min_value = DBL_MAX;
        g_metrics->histograms[i].max_value = -DBL_MAX;
    }
    
    pthread_mutex_unlock(&metrics_mutex);
    return 0;
}

char *metrics_export_prometheus(void) {
    char *result = NULL;
    int i;
    size_t len = 0;
    size_t pos = 0;
    
    if (!g_metrics) return NULL;
    
    pthread_mutex_lock(&metrics_mutex);
    
    /* Calculate approximate size needed */
    len = 1024; /* Base size */
    len += g_metrics->counter_count * 100;
    len += g_metrics->gauge_count * 100;
    len += g_metrics->histogram_count * 500;
    
    result = malloc(len);
    if (!result) {
        pthread_mutex_unlock(&metrics_mutex);
        return NULL;
    }
    
    /* Add timestamp */
    pos += snprintf(result + pos, len - pos,
        "# HELP millennium_metrics_start_time Start time of the metrics collection\n");
    pos += snprintf(result + pos, len - pos,
        "# TYPE millennium_metrics_start_time counter\n");
    pos += snprintf(result + pos, len - pos,
        "millennium_metrics_start_time %ld\n\n", (long)g_metrics->start_time);
    
    /* Export counters */
    for (i = 0; i < (int)g_metrics->counter_count; i++) {
        char *sanitized_name;
        
        sanitized_name = metrics_sanitize_name(g_metrics->counter_names[i]);
        if (sanitized_name) {
            pos += snprintf(result + pos, len - pos,
                "# HELP %s Counter metric\n", sanitized_name);
            pos += snprintf(result + pos, len - pos,
                "# TYPE %s counter\n", sanitized_name);
            pos += snprintf(result + pos, len - pos,
                "%s %llu\n", sanitized_name, (unsigned long long)g_metrics->counters[i].value);
            free(sanitized_name);
        }
    }
    
    if (g_metrics->counter_count > 0) {
        pos += snprintf(result + pos, len - pos, "\n");
    }
    
    /* Export gauges */
    for (i = 0; i < (int)g_metrics->gauge_count; i++) {
        char *sanitized_name;
        
        sanitized_name = metrics_sanitize_name(g_metrics->gauge_names[i]);
        if (sanitized_name) {
            pos += snprintf(result + pos, len - pos,
                "# HELP %s Gauge metric\n", sanitized_name);
            pos += snprintf(result + pos, len - pos,
                "# TYPE %s gauge\n", sanitized_name);
            pos += snprintf(result + pos, len - pos,
                "%s %.2f\n", sanitized_name, g_metrics->gauges[i].value);
            free(sanitized_name);
        }
    }
    
    if (g_metrics->gauge_count > 0) {
        pos += snprintf(result + pos, len - pos, "\n");
    }
    
    /* Export histograms */
    for (i = 0; i < (int)g_metrics->histogram_count; i++) {
        metrics_histogram_stats_t stats;
        char *sanitized_name;
        
        if (metrics_get_histogram_stats_unlocked(g_metrics->histogram_names[i], &stats) == 0) {
            sanitized_name = metrics_sanitize_name(g_metrics->histogram_names[i]);
            if (sanitized_name) {
                pos += snprintf(result + pos, len - pos,
                    "# HELP %s_count Histogram count\n", sanitized_name);
                pos += snprintf(result + pos, len - pos,
                    "# TYPE %s_count counter\n", sanitized_name);
                pos += snprintf(result + pos, len - pos,
                    "%s_count %llu\n", sanitized_name, (unsigned long long)stats.count);
                
                pos += snprintf(result + pos, len - pos,
                    "# HELP %s_sum Histogram sum\n", sanitized_name);
                pos += snprintf(result + pos, len - pos,
                    "# TYPE %s_sum counter\n", sanitized_name);
                pos += snprintf(result + pos, len - pos,
                    "%s_sum %.2f\n", sanitized_name, stats.sum);
                
                pos += snprintf(result + pos, len - pos,
                    "# HELP %s_min Histogram minimum\n", sanitized_name);
                pos += snprintf(result + pos, len - pos,
                    "# TYPE %s_min gauge\n", sanitized_name);
                pos += snprintf(result + pos, len - pos,
                    "%s_min %.2f\n", sanitized_name, stats.min);
                
                pos += snprintf(result + pos, len - pos,
                    "# HELP %s_max Histogram maximum\n", sanitized_name);
                pos += snprintf(result + pos, len - pos,
                    "# TYPE %s_max gauge\n", sanitized_name);
                pos += snprintf(result + pos, len - pos,
                    "%s_max %.2f\n", sanitized_name, stats.max);
                
                pos += snprintf(result + pos, len - pos,
                    "# HELP %s_mean Histogram mean\n", sanitized_name);
                pos += snprintf(result + pos, len - pos,
                    "# TYPE %s_mean gauge\n", sanitized_name);
                pos += snprintf(result + pos, len - pos,
                    "%s_mean %.2f\n", sanitized_name, stats.mean);
                
                pos += snprintf(result + pos, len - pos,
                    "# HELP %s_median Histogram median\n", sanitized_name);
                pos += snprintf(result + pos, len - pos,
                    "# TYPE %s_median gauge\n", sanitized_name);
                pos += snprintf(result + pos, len - pos,
                    "%s_median %.2f\n", sanitized_name, stats.median);
                
                pos += snprintf(result + pos, len - pos,
                    "# HELP %s_p95 Histogram 95th percentile\n", sanitized_name);
                pos += snprintf(result + pos, len - pos,
                    "# TYPE %s_p95 gauge\n", sanitized_name);
                pos += snprintf(result + pos, len - pos,
                    "%s_p95 %.2f\n", sanitized_name, stats.p95);
                
                pos += snprintf(result + pos, len - pos,
                    "# HELP %s_p99 Histogram 99th percentile\n", sanitized_name);
                pos += snprintf(result + pos, len - pos,
                    "# TYPE %s_p99 gauge\n", sanitized_name);
                pos += snprintf(result + pos, len - pos,
                    "%s_p99 %.2f\n\n", sanitized_name, stats.p99);
                
                free(sanitized_name);
            }
        }
    }
    
    pthread_mutex_unlock(&metrics_mutex);
    return result;
}

char *metrics_export_json(void) {
    char *result = NULL;
    char *timestamp;
    int i;
    size_t len = 0;
    size_t pos = 0;
    
    if (!g_metrics) return NULL;
    
    pthread_mutex_lock(&metrics_mutex);
    
    timestamp = metrics_format_timestamp();
    if (!timestamp) {
        pthread_mutex_unlock(&metrics_mutex);
        return NULL;
    }
    
    /* Calculate approximate size needed */
    len = 1024; /* Base size */
    len += g_metrics->counter_count * 100;
    len += g_metrics->gauge_count * 100;
    len += g_metrics->histogram_count * 500;
    len += strlen(timestamp);
    
    result = malloc(len);
    if (!result) {
        free(timestamp);
        pthread_mutex_unlock(&metrics_mutex);
        return NULL;
    }
    
                pos += snprintf(result + pos, len - pos, "{\n");
                pos += snprintf(result + pos, len - pos, "  \"timestamp\": \"%s\",\n", timestamp);
                pos += snprintf(result + pos, len - pos, "  \"counters\": {\n");
    
    /* Export counters */
    for (i = 0; i < (int)g_metrics->counter_count; i++) {
        if (i > 0) pos += snprintf(result + pos, len - pos, ",\n");
                pos += snprintf(result + pos, len - pos, "    \"%s\": %llu",
            g_metrics->counter_names[i], (unsigned long long)g_metrics->counters[i].value);
    }
    
                pos += snprintf(result + pos, len - pos, "\n  },\n");
                pos += snprintf(result + pos, len - pos, "  \"gauges\": {\n");
    
    /* Export gauges */
    for (i = 0; i < (int)g_metrics->gauge_count; i++) {
        if (i > 0) pos += snprintf(result + pos, len - pos, ",\n");
                pos += snprintf(result + pos, len - pos, "    \"%s\": %.2f",
            g_metrics->gauge_names[i], g_metrics->gauges[i].value);
    }
    
                pos += snprintf(result + pos, len - pos, "\n  },\n");
                pos += snprintf(result + pos, len - pos, "  \"histograms\": {\n");
    
    /* Export histograms */
    for (i = 0; i < (int)g_metrics->histogram_count; i++) {
        metrics_histogram_stats_t stats;
        
        if (metrics_get_histogram_stats_unlocked(g_metrics->histogram_names[i], &stats) == 0) {
            if (i > 0) pos += snprintf(result + pos, len - pos, ",\n");
            pos += snprintf(result + pos, len - pos, "    \"%s\": {\n",
                g_metrics->histogram_names[i]);
            pos += snprintf(result + pos, len - pos,
                "      \"count\": %llu,\n", (unsigned long long)stats.count);
            pos += snprintf(result + pos, len - pos,
                "      \"sum\": %.2f,\n", stats.sum);
            pos += snprintf(result + pos, len - pos,
                "      \"min\": %.2f,\n", stats.min);
            pos += snprintf(result + pos, len - pos,
                "      \"max\": %.2f,\n", stats.max);
            pos += snprintf(result + pos, len - pos,
                "      \"mean\": %.2f,\n", stats.mean);
            pos += snprintf(result + pos, len - pos,
                "      \"median\": %.2f,\n", stats.median);
            pos += snprintf(result + pos, len - pos,
                "      \"p95\": %.2f,\n", stats.p95);
            pos += snprintf(result + pos, len - pos,
                "      \"p99\": %.2f\n", stats.p99);
            pos += snprintf(result + pos, len - pos, "    }");
        }
    }
    
                pos += snprintf(result + pos, len - pos, "\n  }\n");
                pos += snprintf(result + pos, len - pos, "}\n");
    
    free(timestamp);
    pthread_mutex_unlock(&metrics_mutex);
    return result;
}

char *metrics_sanitize_name(const char *name) {
    char *result;
    size_t len;
    size_t i, j;
    
    if (!name) return NULL;
    
    len = strlen(name);
    result = malloc(len + 10); /* Extra space for prefix if needed */
    if (!result) return NULL;
    
    j = 0;
    
    /* Ensure it starts with a letter */
    if (len > 0 && !isalpha((unsigned char)name[0])) {
        result[j++] = 'm';
        result[j++] = 'e';
        result[j++] = 't';
        result[j++] = 'r';
        result[j++] = 'i';
        result[j++] = 'c';
        result[j++] = '_';
    }
    
    /* Replace invalid characters with underscores */
    for (i = 0; i < len; i++) {
        if (isalnum((unsigned char)name[i]) || name[i] == '_') {
            result[j++] = name[i];
        } else {
            result[j++] = '_';
        }
    }
    
    result[j] = '\0';
    return result;
}

char *metrics_format_timestamp(void) {
    time_t now;
    struct tm *tm_info;
    char *result;
    
    time(&now);
    tm_info = localtime(&now);
    if (!tm_info) return NULL;
    
    result = malloc(32);
    if (!result) return NULL;
    
    strftime(result, 32, "%Y-%m-%dT%H:%M:%S", tm_info);
    return result;
}

void metrics_free_string(char *str) {
    if (str) free(str);
}

void metrics_free_histogram_stats(metrics_histogram_stats_t *stats) {
    /* No dynamic memory in stats structure */
    (void)stats;
}
