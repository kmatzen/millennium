#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

/* C89 compatible configuration structure */
typedef struct {
    char keys[100][256];    /* Configuration keys */
    char values[100][256];  /* Configuration values */
    int count;              /* Number of key-value pairs */
} config_data_t;

/* Global config instance */
extern config_data_t* g_config;

/* Function declarations */
config_data_t* config_get_instance(void);
int config_load_from_file(config_data_t* config, const char* config_path);
int config_load_from_environment(config_data_t* config);
int config_validate(const config_data_t* config);

/* Configuration getters */
const char* config_get_string(const config_data_t* config, const char* key, const char* default_value);
int config_get_int(const config_data_t* config, const char* key, int default_value);
int config_get_bool(const config_data_t* config, const char* key, int default_value);

/* Hardware Configuration */
const char* config_get_display_device(const config_data_t* config);
int config_get_baud_rate(const config_data_t* config);

/* Call Configuration */
int config_get_call_cost_cents(const config_data_t* config);
int config_get_call_timeout_seconds(const config_data_t* config);

/* Logging Configuration */
const char* config_get_log_level(const config_data_t* config);
const char* config_get_log_file(const config_data_t* config);
int config_get_log_to_file(const config_data_t* config);
int config_get_log_max_size_bytes(const config_data_t* config);
int config_get_log_max_files(const config_data_t* config);

/* System Configuration */
int config_get_update_interval_ms(const config_data_t* config);
int config_get_max_retries(const config_data_t* config);

/* Metrics Server Configuration */
int config_get_metrics_server_enabled(const config_data_t* config);
int config_get_metrics_server_port(const config_data_t* config);
int config_get_metrics_server_disable_during_audio(const config_data_t* config);

/* Web Server Configuration */
int config_get_web_server_enabled(const config_data_t* config);
int config_get_web_server_port(const config_data_t* config);

/* Internal functions */
void config_set_default_values(config_data_t* config);
char* config_trim(const char* str, char* result, size_t result_size);
int config_set_value(config_data_t* config, const char* key, const char* value);

#endif /* CONFIG_H */
