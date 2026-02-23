#define _POSIX_C_SOURCE 200112L
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Global config instance */
config_data_t* g_config = NULL;

config_data_t* config_get_instance(void) {
    if (g_config == NULL) {
        g_config = (config_data_t*)malloc(sizeof(config_data_t));
        if (g_config != NULL) {
            g_config->count = 0;
            config_set_default_values(g_config);
        }
    }
    return g_config;
}

int config_load_from_file(config_data_t* config, const char* config_path) {
    FILE* file;
    char line[512];
    char key[256];
    char value[256];
    char trimmed_key[256];
    char trimmed_value[256];
    char* equals_pos;
    char* newline_pos;
    
    if (config == NULL || config_path == NULL) {
        return 0;
    }
    
    file = fopen(config_path, "r");
    if (file == NULL) {
        return 0;
    }
    
    config_set_default_values(config);
    
    while (fgets(line, sizeof(line), file) != NULL) {
        /* Remove newline character */
        newline_pos = strchr(line, '\n');
        if (newline_pos != NULL) {
            *newline_pos = '\0';
        }
        newline_pos = strchr(line, '\r');
        if (newline_pos != NULL) {
            *newline_pos = '\0';
        }
        
        /* Skip empty lines and comments */
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        
        /* Find the equals sign */
        equals_pos = strchr(line, '=');
        if (equals_pos == NULL) {
            continue;
        }
        
        /* Split key and value */
        *equals_pos = '\0';
        strncpy(key, line, sizeof(key) - 1);
        key[sizeof(key) - 1] = '\0';
        strncpy(value, equals_pos + 1, sizeof(value) - 1);
        value[sizeof(value) - 1] = '\0';
        
        /* Trim whitespace */
        config_trim(key, trimmed_key, sizeof(trimmed_key));
        config_trim(value, trimmed_value, sizeof(trimmed_value));
        
        if (strlen(trimmed_key) > 0 && strlen(trimmed_value) > 0) {
            config_set_value(config, trimmed_key, trimmed_value);
        }
    }
    
    fclose(file);
    return 1;
}

int config_load_from_environment(config_data_t* config) {
    const char* log_level;
    const char* log_file;
    
    if (config == NULL) {
        return 0;
    }
    
    config_set_default_values(config);
    
    /* Load logging configuration */
    log_level = getenv("MILLENNIUM_LOG_LEVEL");
    if (log_level != NULL) {
        config_set_value(config, "logging.level", log_level);
    }
    
    log_file = getenv("MILLENNIUM_LOG_FILE");
    if (log_file != NULL) {
        config_set_value(config, "logging.file", log_file);
    }
    
    return 1;
}

const char* config_get_string(const config_data_t* config, const char* key, const char* default_value) {
    int i;
    
    if (config == NULL || key == NULL) {
        return default_value;
    }
    
    for (i = 0; i < config->count; i++) {
        if (strcmp(config->keys[i], key) == 0) {
            return config->values[i];
        }
    }
    
    return default_value;
}

int config_get_int(const config_data_t* config, const char* key, int default_value) {
    const char* value_str;
    int value;
    
    value_str = config_get_string(config, key, NULL);
    if (value_str == NULL) {
        return default_value;
    }
    
    value = atoi(value_str);
    return value;
}

int config_get_bool(const config_data_t* config, const char* key, int default_value) {
    const char* value_str;
    char lower_value[256];
    int i;
    
    value_str = config_get_string(config, key, NULL);
    if (value_str == NULL) {
        return default_value;
    }
    
    /* Convert to lowercase */
    for (i = 0; value_str[i] != '\0' && i < (int)(sizeof(lower_value) - 1); i++) {
        lower_value[i] = tolower(value_str[i]);
    }
    lower_value[i] = '\0';
    
    if (strcmp(lower_value, "true") == 0 || 
        strcmp(lower_value, "1") == 0 || 
        strcmp(lower_value, "yes") == 0) {
        return 1;
    }
    
    return 0;
}

int config_validate(const config_data_t* config) {
    /* Validate numeric values */
    if (config_get_call_cost_cents(config) <= 0) {
        return 0;
    }
    
    if (config_get_update_interval_ms(config) <= 0) {
        return 0;
    }
    
    return 1;
}

void config_set_default_values(config_data_t* config) {
    if (config == NULL) {
        return;
    }
    
    config->count = 0;
    
    config_set_value(config, "hardware.display_device", "/dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00");
    config_set_value(config, "hardware.baud_rate", "9600");
    
    config_set_value(config, "call.cost_cents", "50");
    config_set_value(config, "call.timeout_seconds", "300");
    
    config_set_value(config, "logging.level", "INFO");
    config_set_value(config, "logging.file", "");
    config_set_value(config, "logging.to_file", "false");
    
    config_set_value(config, "system.update_interval_ms", "33");
    config_set_value(config, "system.max_retries", "3");
    
    config_set_value(config, "metrics_server.enabled", "false");
    config_set_value(config, "metrics_server.port", "8080");
}

char* config_trim(const char* str, char* result, size_t result_size) {
    const char* start;
    const char* end;
    size_t len;
    
    if (str == NULL || result == NULL || result_size == 0) {
        if (result != NULL && result_size > 0) {
            result[0] = '\0';
        }
        return result;
    }
    
    /* Find first non-space character */
    start = str;
    while (*start != '\0' && isspace(*start)) {
        start++;
    }
    
    /* Find last non-space character */
    end = str + strlen(str) - 1;
    while (end > start && isspace(*end)) {
        end--;
    }
    
    /* Copy trimmed string */
    len = end - start + 1;
    if (len >= result_size) {
        len = result_size - 1;
    }
    
    strncpy(result, start, len);
    result[len] = '\0';
    
    return result;
}

int config_set_value(config_data_t* config, const char* key, const char* value) {
    int i;
    
    if (config == NULL || key == NULL || value == NULL) {
        return 0;
    }
    
    if (config->count >= 100) {
        return 0; /* Too many entries */
    }
    
    /* Check if key already exists and update it */
    for (i = 0; i < config->count; i++) {
        if (strcmp(config->keys[i], key) == 0) {
            /* Safe copy with guaranteed null termination */
            if (strlen(value) >= sizeof(config->values[i])) {
                /* Truncate if too long */
                strncpy(config->values[i], value, sizeof(config->values[i]) - 1);
                config->values[i][sizeof(config->values[i]) - 1] = '\0';
            } else {
                strncpy(config->values[i], value, sizeof(config->values[i]) - 1);
                config->values[i][sizeof(config->values[i]) - 1] = '\0';
            }
            return 1;
        }
    }
    
    /* Add new key-value pair */
    /* Safe copy with guaranteed null termination */
    if (strlen(key) >= sizeof(config->keys[config->count])) {
        /* Truncate if too long */
        strncpy(config->keys[config->count], key, sizeof(config->keys[config->count]) - 1);
        config->keys[config->count][sizeof(config->keys[config->count]) - 1] = '\0';
    } else {
        strncpy(config->keys[config->count], key, sizeof(config->keys[config->count]) - 1);
        config->keys[config->count][sizeof(config->keys[config->count]) - 1] = '\0';
    }
    
    if (strlen(value) >= sizeof(config->values[config->count])) {
        /* Truncate if too long */
        strncpy(config->values[config->count], value, sizeof(config->values[config->count]) - 1);
        config->values[config->count][sizeof(config->values[config->count]) - 1] = '\0';
    } else {
        strncpy(config->values[config->count], value, sizeof(config->values[config->count]) - 1);
        config->values[config->count][sizeof(config->values[config->count]) - 1] = '\0';
    }
    config->count++;
    
    return 1;
}

/* Hardware Configuration */
const char* config_get_display_device(const config_data_t* config) {
    return config_get_string(config, "hardware.display_device", "/dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00");
}

int config_get_baud_rate(const config_data_t* config) {
    return config_get_int(config, "hardware.baud_rate", 9600);
}

/* Call Configuration */
int config_get_call_cost_cents(const config_data_t* config) {
    return config_get_int(config, "call.cost_cents", 50);
}

int config_get_call_timeout_seconds(const config_data_t* config) {
    return config_get_int(config, "call.timeout_seconds", 300);
}

const char* config_get_free_numbers(const config_data_t* config) {
    return config_get_string(config, "call.free_numbers", "911,311,0");
}

int config_is_free_number(const config_data_t* config, const char* number) {
    const char *list, *p;
    size_t nlen;
    if (!number || !*number) return 0;
    list = config_get_free_numbers(config);
    if (!list) return 0;
    nlen = strlen(number);
    p = list;
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t entry_len = comma ? (size_t)(comma - p) : strlen(p);
        /* Skip leading whitespace */
        while (entry_len > 0 && (*p == ' ' || *p == '\t')) { p++; entry_len--; }
        /* Trim trailing whitespace */
        while (entry_len > 0 && (p[entry_len-1] == ' ' || p[entry_len-1] == '\t')) { entry_len--; }
        if (entry_len == nlen && strncmp(p, number, nlen) == 0) return 1;
        if (!comma) break;
        p = comma + 1;
    }
    return 0;
}

/* Logging Configuration */
const char* config_get_log_level(const config_data_t* config) {
    return config_get_string(config, "logging.level", "INFO");
}

const char* config_get_log_file(const config_data_t* config) {
    return config_get_string(config, "logging.file", "");
}

int config_get_log_to_file(const config_data_t* config) {
    return config_get_bool(config, "logging.to_file", 0);
}

int config_get_log_max_size_bytes(const config_data_t* config) {
    return config_get_int(config, "logging.max_size_bytes", 1048576);
}

int config_get_log_max_files(const config_data_t* config) {
    return config_get_int(config, "logging.max_files", 5);
}

/* State Persistence Configuration */
const char* config_get_state_file(const config_data_t* config) {
    return config_get_string(config, "persistence.state_file", "/var/lib/millennium/state");
}

/* System Configuration */
int config_get_update_interval_ms(const config_data_t* config) {
    return config_get_int(config, "system.update_interval_ms", 33);
}

int config_get_max_retries(const config_data_t* config) {
    return config_get_int(config, "system.max_retries", 3);
}

/* Metrics Server Configuration */
int config_get_metrics_server_enabled(const config_data_t* config) {
    return config_get_bool(config, "metrics_server.enabled", 0);
}

int config_get_metrics_server_port(const config_data_t* config) {
    return config_get_int(config, "metrics_server.port", 8080);
}

int config_get_metrics_server_disable_during_audio(const config_data_t* config) {
    return config_get_bool(config, "metrics_server.disable_during_audio", 1);
}

/* Web Server Configuration */
int config_get_web_server_enabled(const config_data_t* config) {
    return config_get_bool(config, "web_server.enabled", 1);
}

int config_get_web_server_port(const config_data_t* config) {
    return config_get_int(config, "web_server.port", 8081);
}
