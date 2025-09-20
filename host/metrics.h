#ifndef METRICS_H
#define METRICS_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

/* Forward declarations */
typedef struct metrics_counter metrics_counter_t;
typedef struct metrics_gauge metrics_gauge_t;
typedef struct metrics_histogram metrics_histogram_t;
typedef struct metrics_histogram_stats metrics_histogram_stats_t;
typedef struct metrics metrics_t;

/* Counter structure */
struct metrics_counter {
    uint64_t value;
    time_t last_reset;
};

/* Gauge structure */
struct metrics_gauge {
    double value;
    time_t last_update;
};

/* Histogram statistics structure */
struct metrics_histogram_stats {
    uint64_t count;
    double sum;
    double min;
    double max;
    double mean;
    double median;
    double p95;
    double p99;
};

/* Histogram structure */
struct metrics_histogram {
    double *values;
    size_t values_size;
    size_t values_capacity;
    uint64_t count;
    double sum;
    double min_value;
    double max_value;
};

/* Main metrics structure */
struct metrics {
    metrics_counter_t *counters;
    char **counter_names;
    size_t counter_count;
    size_t counter_capacity;
    
    metrics_gauge_t *gauges;
    char **gauge_names;
    size_t gauge_count;
    size_t gauge_capacity;
    
    metrics_histogram_t *histograms;
    char **histogram_names;
    size_t histogram_count;
    size_t histogram_capacity;
    
    time_t start_time;
};

/* Global metrics instance */
extern metrics_t *g_metrics;

/* Initialization and cleanup */
int metrics_init(void);
void metrics_cleanup(void);
metrics_t *metrics_get_instance(void);

/* Counter operations */
int metrics_increment_counter(const char *name, uint64_t amount);
int metrics_reset_counter(const char *name);
uint64_t metrics_get_counter(const char *name);

/* Gauge operations */
int metrics_set_gauge(const char *name, double value);
int metrics_increment_gauge(const char *name, double amount);
int metrics_decrement_gauge(const char *name, double amount);
double metrics_get_gauge(const char *name);

/* Histogram operations */
int metrics_observe_histogram(const char *name, double value);
int metrics_get_histogram_stats(const char *name, metrics_histogram_stats_t *stats);

/* Utility methods */
int metrics_reset_all(void);

/* Export methods */
char *metrics_export_prometheus(void);
char *metrics_export_json(void);

/* Helper functions */
char *metrics_sanitize_name(const char *name);
char *metrics_format_timestamp(void);

/* Memory management helpers */
void metrics_free_string(char *str);
void metrics_free_histogram_stats(metrics_histogram_stats_t *stats);

#endif /* METRICS_H */