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
    
    /* State persistence (used when config file fails to load) */
    {
        const char *state_file = getenv("MILLENNIUM_STATE_FILE");
        if (state_file != NULL) {
            config_set_value(config, "persistence.state_file", state_file);
        }
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

/* Case-insensitive string compare for small config tokens. */
static int config_str_eq_ci(const char* a, const char* b) {
    if (a == NULL || b == NULL) {
        return 0;
    }
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int config_is_valid_log_level(const char* level) {
    return config_str_eq_ci(level, "VERBOSE") ||
           config_str_eq_ci(level, "DEBUG") ||
           config_str_eq_ci(level, "INFO") ||
           config_str_eq_ci(level, "WARN") ||
           config_str_eq_ci(level, "ERROR");
}

/* Record a failure reason into the caller's buffer (if any) and return 0. */
#define CONFIG_FAIL(...)                                  \
    do {                                                  \
        if (err != NULL && err_size > 0) {                \
            snprintf(err, err_size, __VA_ARGS__);         \
        }                                                 \
        return 0;                                         \
    } while (0)

int config_validate_ex(const config_data_t* config, char* err, size_t err_size) {
    int web_enabled, metrics_enabled, web_port, metrics_port;
    const char* transport;

    if (err != NULL && err_size > 0) {
        err[0] = '\0';
    }

    /* Call economics */
    if (config_get_call_cost_cents(config) <= 0) {
        CONFIG_FAIL("call.cost_cents must be > 0 (got %d)",
                    config_get_call_cost_cents(config));
    }
    if (config_get_call_timeout_seconds(config) <= 0) {
        CONFIG_FAIL("call.timeout_seconds must be > 0 (got %d)",
                    config_get_call_timeout_seconds(config));
    }
    if (config_get_idle_timeout_seconds(config) <= 0) {
        CONFIG_FAIL("call.idle_timeout_seconds must be > 0 (got %d)",
                    config_get_idle_timeout_seconds(config));
    }

    /* Hardware */
    if (config_get_baud_rate(config) <= 0) {
        CONFIG_FAIL("hardware.baud_rate must be > 0 (got %d)",
                    config_get_baud_rate(config));
    }

    /* System loop */
    if (config_get_update_interval_ms(config) <= 0) {
        CONFIG_FAIL("system.update_interval_ms must be > 0 (got %d)",
                    config_get_update_interval_ms(config));
    }
    if (config_get_max_retries(config) < 0) {
        CONFIG_FAIL("system.max_retries must be >= 0 (got %d)",
                    config_get_max_retries(config));
    }

    /* Logging */
    if (!config_is_valid_log_level(config_get_log_level(config))) {
        CONFIG_FAIL("logging.level '%s' is not one of "
                    "VERBOSE/DEBUG/INFO/WARN/ERROR",
                    config_get_log_level(config));
    }
    if (config_get_log_max_size_bytes(config) <= 0) {
        CONFIG_FAIL("logging.max_size_bytes must be > 0 (got %d)",
                    config_get_log_max_size_bytes(config));
    }
    if (config_get_log_max_files(config) < 1) {
        CONFIG_FAIL("logging.max_files must be >= 1 (got %d)",
                    config_get_log_max_files(config));
    }

    /* Network ports */
    web_port = config_get_web_server_port(config);
    metrics_port = config_get_metrics_server_port(config);
    if (web_port < 1 || web_port > 65535) {
        CONFIG_FAIL("web_server.port %d out of range 1..65535", web_port);
    }
    if (metrics_port < 1 || metrics_port > 65535) {
        CONFIG_FAIL("metrics_server.port %d out of range 1..65535",
                    metrics_port);
    }
    web_enabled = config_get_web_server_enabled(config);
    metrics_enabled = config_get_metrics_server_enabled(config);
    if (web_enabled && metrics_enabled && web_port == metrics_port) {
        CONFIG_FAIL("web_server.port and metrics_server.port both set to %d "
                    "while both servers are enabled", web_port);
    }

    /* SIP transport (only when explicitly configured) */
    transport = config_get_string(config, "sip.transport", "");
    if (transport[0] != '\0' &&
        !config_str_eq_ci(transport, "udp") &&
        !config_str_eq_ci(transport, "tcp") &&
        !config_str_eq_ci(transport, "tls")) {
        CONFIG_FAIL("sip.transport '%s' is not one of udp/tcp/tls", transport);
    }

    return 1;
}

#undef CONFIG_FAIL

int config_validate(const config_data_t* config) {
    return config_validate_ex(config, NULL, 0);
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

int config_get_idle_timeout_seconds(const config_data_t* config) {
    return config_get_int(config, "call.idle_timeout_seconds", 60);
}

/* Card Configuration */
int config_get_card_enabled(const config_data_t* config) {
    return config_get_bool(config, "card.enabled", 1);
}

const char* config_get_card_free_cards(const config_data_t* config) {
    return config_get_string(config, "card.free_cards", "");
}

const char* config_get_card_admin_cards(const config_data_t* config) {
    return config_get_string(config, "card.admin_cards", "");
}

static int config_card_in_list(const char *list, const char *card_number) {
    const char *p;
    size_t clen;
    if (!card_number || !*card_number || !list) return 0;
    clen = strlen(card_number);
    p = list;
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t entry_len = comma ? (size_t)(comma - p) : strlen(p);
        while (entry_len > 0 && (*p == ' ' || *p == '\t')) { p++; entry_len--; }
        while (entry_len > 0 && (p[entry_len-1] == ' ' || p[entry_len-1] == '\t')) { entry_len--; }
        if (entry_len == clen && strncmp(p, card_number, clen) == 0) return 1;
        if (!comma) break;
        p = comma + 1;
    }
    return 0;
}

int config_is_free_card(const config_data_t* config, const char* card_number) {
    return config_card_in_list(config_get_card_free_cards(config), card_number);
}

int config_is_admin_card(const config_data_t* config, const char* card_number) {
    return config_card_in_list(config_get_card_admin_cards(config), card_number);
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
    /* Privileged port 80; the systemd unit grants CAP_NET_BIND_SERVICE. */
    return config_get_int(config, "web_server.port", 80);
}
