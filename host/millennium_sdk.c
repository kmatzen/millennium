#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700
#include "millennium_sdk.h"
#include "events.h"
#include "baresip_interface.h"
#include <errno.h>
#include <fcntl.h>
/* #include <linux/serial.h> */ /* Linux-specific, not available on macOS */
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* Global logger instance */
static millennium_logger_t g_logger = {LOGGER_INFO};

/* Forward declarations */
static void ua_event_handler(enum baresip_ua_event ev, struct bevent *event, void *client);
static void *baresip_thread_func(void *arg);
static void event_queue_push(struct millennium_client *client, void *event);
static void *event_queue_pop(struct millennium_client *client);
static int event_queue_empty(struct millennium_client *client);
static void event_queue_clear(struct millennium_client *client);
static char *string_duplicate(const char *src);
static void string_buffer_append(struct millennium_client *client, const char *data, size_t len);
static void string_buffer_ensure_capacity(struct millennium_client *client, size_t needed);

/* Logger functions */
logger_level_t millennium_logger_parse_level(const char *level_str) {
    if (strcmp(level_str, "DEBUG") == 0)
        return LOGGER_DEBUG;
    if (strcmp(level_str, "INFO") == 0)
        return LOGGER_INFO;
    if (strcmp(level_str, "WARN") == 0)
        return LOGGER_WARN;
    if (strcmp(level_str, "ERROR") == 0)
        return LOGGER_ERROR;
    return LOGGER_INFO; /* Default level */
}

void millennium_logger_set_level(logger_level_t level) {
    g_logger.current_level = level;
}

logger_level_t millennium_logger_get_current_level(void) {
    return g_logger.current_level;
}

void millennium_logger_log(logger_level_t level, const char *message) {
    if (level >= g_logger.current_level) {
        const char *level_str;
        switch (level) {
        case LOGGER_VERBOSE:
            level_str = "VERBOSE";
            break;
        case LOGGER_DEBUG:
            level_str = "DEBUG";
            break;
        case LOGGER_INFO:
            level_str = "INFO";
            break;
        case LOGGER_WARN:
            level_str = "WARN";
            break;
        case LOGGER_ERROR:
            level_str = "ERROR";
            break;
        default:
            level_str = "UNKNOWN";
            break;
        }
        fprintf(stderr, "%s: %s\n", level_str, message);
    }
}

/* Event queue implementation */
static void event_queue_push(struct millennium_client *client, void *event) {
    struct event_queue_node *node = malloc(sizeof(struct event_queue_node));
    if (!node) {
        millennium_logger_log(LOGGER_ERROR, "Failed to allocate memory for event queue node");
        return;
    }
    
    node->event = event;
    node->next = NULL;
    
    if (client->event_queue_tail) {
        client->event_queue_tail->next = node;
    } else {
        client->event_queue_head = node;
    }
    client->event_queue_tail = node;
}

static void *event_queue_pop(struct millennium_client *client) {
    if (!client->event_queue_head) {
        return NULL;
    }
    
    struct event_queue_node *node = client->event_queue_head;
    void *event = node->event;
    
    client->event_queue_head = node->next;
    if (!client->event_queue_head) {
        client->event_queue_tail = NULL;
    }
    
    free(node);
    return event;
}

static int event_queue_empty(struct millennium_client *client) {
    return client->event_queue_head == NULL;
}

static void event_queue_clear(struct millennium_client *client) {
    while (!event_queue_empty(client)) {
        void *event = event_queue_pop(client);
        if (event) {
            event_destroy((event_t *)event);
        }
    }
}

/* String utilities */
static char *string_duplicate(const char *src) {
    if (!src) return NULL;
    size_t len = strlen(src) + 1;
    char *dst = malloc(len);
    if (dst) {
        strcpy(dst, src);
    }
    return dst;
}

static void string_buffer_ensure_capacity(struct millennium_client *client, size_t needed) {
    if (needed >= client->input_buffer_capacity) {
        size_t new_capacity = client->input_buffer_capacity * 2;
        if (new_capacity < needed) {
            new_capacity = needed + 1;
        }
        
        char *new_buffer = realloc(client->input_buffer, new_capacity);
        if (new_buffer) {
            client->input_buffer = new_buffer;
            client->input_buffer_capacity = new_capacity;
        } else {
            millennium_logger_log(LOGGER_ERROR, "Failed to reallocate input buffer");
        }
    }
}

static void string_buffer_append(struct millennium_client *client, const char *data, size_t len) {
    if (!client || !data || len == 0) return;
    
    string_buffer_ensure_capacity(client, client->input_buffer_size + len + 1);
    if (client->input_buffer) {
        memcpy(client->input_buffer + client->input_buffer_size, data, len);
        client->input_buffer_size += len;
        client->input_buffer[client->input_buffer_size] = '\0';
    }
}

/* UA event handler */
static void ua_event_handler(enum baresip_ua_event ev, struct bevent *event, void *client) {
    char message[256];
    struct call *call;
    struct ua *ua;
    call_state_t state_value;
    call_state_event_t *call_event;
    
    snprintf(message, sizeof(message), "UA event: %s", baresip_uag_event_str(ev));
    millennium_logger_log(LOGGER_INFO, message);
    
    if (client) {
        call = baresip_bevent_get_call(event);
        if (call) {
            /* For incoming calls, we need to set the UA pointer */
            if (ev == BARESIP_UA_EVENT_CALL_INCOMING) {
                ua = baresip_call_get_ua(call);
                if (ua) {
                    millennium_client_set_ua((struct millennium_client *)client, ua);
                }
            }
        }

        if (ev == BARESIP_UA_EVENT_CALL_INCOMING) {
            state_value = EVENT_CALL_STATE_INCOMING;
        } else if (ev == BARESIP_UA_EVENT_CALL_ESTABLISHED) {
            state_value = EVENT_CALL_STATE_ACTIVE;
        } else {
            state_value = EVENT_CALL_STATE_INVALID;
        }

        call_event = call_state_event_create(baresip_uag_event_str(ev), call, state_value);
        if (call_event) {
            millennium_client_create_and_queue_event_ptr((struct millennium_client *)client, (void *)call_event);
        }
    }
}

/* Thread function for baresip */
static void *baresip_thread_func(void *arg) {
    (void)arg; /* Suppress unused parameter warning */
    baresip_re_main(NULL);
    return NULL;
}

/* Audio device listing */
void list_audio_devices(void) {
    struct le *le;
    struct ausrc *ausrc;
    struct auplay *auplay;
    char message[256];

    millennium_logger_log(LOGGER_INFO, "--- Audio Sources ---");
    for (le = baresip_ausrcl_head(); le; le = baresip_list_next(le)) {
        ausrc = baresip_list_data(le);
        if (ausrc && baresip_ausrc_name(ausrc)) {
            snprintf(message, sizeof(message), "Source: %s", baresip_ausrc_name(ausrc));
            millennium_logger_log(LOGGER_INFO, message);
        }
    }

    millennium_logger_log(LOGGER_INFO, "--- Audio Players ---");
    for (le = baresip_auplayl_head(); le; le = baresip_list_next(le)) {
        auplay = baresip_auplay_data(le);
        if (auplay && baresip_auplay_name(auplay)) {
            snprintf(message, sizeof(message), "Player: %s", baresip_auplay_name(auplay));
            millennium_logger_log(LOGGER_INFO, message);
        }
    }
}

/* Millennium client implementation */
struct millennium_client *millennium_client_create(void) {
    struct millennium_client *client = malloc(sizeof(struct millennium_client));
    if (!client) {
        millennium_logger_log(LOGGER_ERROR, "Failed to allocate memory for MillenniumClient");
        return NULL;
    }
    
    /* Initialize all fields */
    memset(client, 0, sizeof(struct millennium_client));
    client->display_fd = -1;
    client->is_open = 0;
    client->input_buffer_capacity = 1024;
    client->input_buffer = malloc(client->input_buffer_capacity);
    if (!client->input_buffer) {
        free(client);
        millennium_logger_log(LOGGER_ERROR, "Failed to allocate input buffer");
        return NULL;
    }
    client->input_buffer[0] = '\0';
    client->input_buffer_size = 0;
    client->event_queue_head = NULL;
    client->event_queue_tail = NULL;
    client->thread_handle = NULL;
    client->display_message = NULL;
    client->display_dirty = 0;
    client->ua = NULL;
    
    /* Set function pointers */
    client->create_and_queue_event_char = millennium_client_create_and_queue_event_char;
    client->create_and_queue_event_ptr = millennium_client_create_and_queue_event_ptr;
    
    /* Get current time */
    clock_gettime(CLOCK_MONOTONIC, &client->last_update_time);
    
    const char *display_device = "/dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00";
    
    client->display_fd = open(display_device, O_RDWR | O_NOCTTY);
    if (client->display_fd == -1) {
        millennium_logger_log(LOGGER_ERROR, "Failed to open display device.");
        millennium_client_destroy(client);
        return NULL;
    }

    /* Set display_fd to non-blocking mode */
    int flags = fcntl(client->display_fd, F_GETFL, 0);
    if (flags == -1) {
        char message[256];
        snprintf(message, sizeof(message), "Failed to get file descriptor flags: %s", strerror(errno));
        millennium_logger_log(LOGGER_ERROR, message);
        millennium_client_destroy(client);
        return NULL;
    }

    if (fcntl(client->display_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        char message[256];
        snprintf(message, sizeof(message), "Failed to set non-blocking mode: %s", strerror(errno));
        millennium_logger_log(LOGGER_ERROR, message);
        millennium_client_destroy(client);
        return NULL;
    }

    struct termios options;
    tcgetattr(client->display_fd, &options);
    cfsetispeed(&options, BAUD_RATE);
    cfsetospeed(&options, BAUD_RATE);
    options.c_cflag |= (CS8 | CLOCAL | CREAD);
    options.c_cflag &= ~(PARENB | CSTOPB);
#ifdef CRTSCTS
    options.c_cflag &= ~CRTSCTS;
#endif
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tcsetattr(client->display_fd, TCSANOW, &options);

    millennium_logger_log(LOGGER_DEBUG, "libre_init");
    int err = baresip_libre_init();
    if (err) {
        millennium_logger_log(LOGGER_ERROR, "libre_init failed");
        millennium_client_destroy(client);
        return NULL;
    }

    baresip_re_thread_async_init(ASYNC_WORKERS);

    baresip_log_enable_debug(1);

    int dbg_level = BARESIP_DBG_DEBUG;
    int dbg_flags = BARESIP_DBG_ANSI | BARESIP_DBG_TIME;
    baresip_dbg_init(dbg_level, dbg_flags);

    millennium_logger_log(LOGGER_DEBUG, "conf_configure");
    err = baresip_conf_configure();
    if (err) {
        millennium_logger_log(LOGGER_ERROR, "conf_configure failed");
        millennium_client_destroy(client);
        return NULL;
    }

    millennium_logger_log(LOGGER_DEBUG, "baresip_init_c");
    if (baresip_init_c(baresip_conf_config()) != 0) {
        millennium_logger_log(LOGGER_ERROR, "Failed to initialize Baresip.");
        millennium_client_destroy(client);
        return NULL;
    }

    baresip_play_set_path(baresip_player_c(), "/usr/local/share/baresip");

    err = baresip_ua_init("baresip v2.0.0 (x86_64/linux)", 1, 1, 1);
    if (err) {
        millennium_logger_log(LOGGER_ERROR, "ua_init failed");
        millennium_client_destroy(client);
        return NULL;
    }

    millennium_logger_log(LOGGER_DEBUG, "conf_modules");
    err = baresip_conf_modules();
    if (err) {
        millennium_logger_log(LOGGER_ERROR, "conf_modules failed");
        millennium_client_destroy(client);
        return NULL;
    }

    baresip_bevent_register(ua_event_handler, client);

    /* Create thread */
    if (pthread_create((pthread_t *)&client->thread_handle, NULL, baresip_thread_func, NULL) != 0) {
        millennium_logger_log(LOGGER_ERROR, "Failed to create baresip thread");
        millennium_client_destroy(client);
        return NULL;
    }

    millennium_logger_log(LOGGER_INFO, "MillenniumClient initialized successfully.");
    client->is_open = 1;
    return client;
}

void millennium_client_destroy(struct millennium_client *client) {
    if (client) {
        millennium_client_close(client);
        
        if (client->input_buffer) {
            free(client->input_buffer);
        }
        if (client->display_message) {
            free(client->display_message);
        }
        
        event_queue_clear(client);
        free(client);
    }
}

void millennium_client_close(struct millennium_client *client) {
    if (client && client->is_open) {
        if (client->display_fd != -1) {
            close(client->display_fd);
            client->display_fd = -1;
        }
        
        baresip_ua_stop_all(1);
        baresip_ua_close();
        baresip_module_app_unload();
        baresip_conf_close();
        baresip_close_c();
        baresip_mod_close();
        baresip_re_thread_async_close();
        baresip_libre_close();
        
        if (client->thread_handle) {
            pthread_t *thread_ptr = (pthread_t *)client->thread_handle;
            pthread_join(*thread_ptr, NULL);
            client->thread_handle = NULL;
        }
        
        client->is_open = 0;
        millennium_logger_log(LOGGER_INFO, "MillenniumClient closed.");
    }
}

void millennium_client_call(struct millennium_client *client, const char *number) {
    char target[64];
    char message[256];
    struct ua *ua;
    char *uric;
    int err;
    struct call *call;
    
    snprintf(target, sizeof(target), "+1%s", number);

    snprintf(message, sizeof(message), "ua: %p", client->ua);
    millennium_logger_log(LOGGER_DEBUG, message);
    
    snprintf(message, sizeof(message), "Initiating call to: %s", target);
    millennium_logger_log(LOGGER_INFO, message);
    
    /* Find the appropriate UA for this request URI */
    ua = baresip_ua_find_requri(target);
    
    if (!ua) {
        snprintf(message, sizeof(message), "Could not find UA for: %s", target);
        millennium_logger_log(LOGGER_ERROR, message);
        return;
    }
    
    /* Store the UA for later use in answer/hangup */
    client->ua = ua;
    
    /* Complete the URI */
    uric = NULL;
    err = baresip_account_uri_complete_strdup(baresip_ua_account(ua), &uric, target);
    if (err != 0) {
        snprintf(message, sizeof(message), "Failed to complete URI: %s %d", target, err);
        millennium_logger_log(LOGGER_ERROR, message);
        return;
    }
    
    snprintf(message, sizeof(message), "Using UA: %s", baresip_account_aor(baresip_ua_account(ua)));
    millennium_logger_log(LOGGER_INFO, message);
    
    snprintf(message, sizeof(message), "Completed URI: %s", uric);
    millennium_logger_log(LOGGER_INFO, message);
    
    /* Make the call */
    call = NULL;
    err = baresip_ua_connect(ua, &call, NULL, uric, BARESIP_VIDMODE_OFF);
    
    /* Clean up the completed URI */
    baresip_mem_deref(uric);
    
    if (err != 0) {
        snprintf(message, sizeof(message), "Failed to initiate call to: %s %d", target, err);
        millennium_logger_log(LOGGER_ERROR, message);
    } else {
        snprintf(message, sizeof(message), "Calling: %s", number);
        millennium_logger_log(LOGGER_INFO, message);
    }
}

void millennium_client_answer_call(struct millennium_client *client) {
    if (!client->ua) {
        millennium_logger_log(LOGGER_ERROR, "Cannot answer call: UA is null");
        return;
    }
    
    /* Find the current call for this UA */
    struct call *call = baresip_ua_call((struct ua *)client->ua);
    if (!call) {
        millennium_logger_log(LOGGER_ERROR, "Cannot answer call: No active call found");
        return;
    }
    
    baresip_ua_answer((struct ua *)client->ua, call, BARESIP_VIDMODE_OFF);
    millennium_logger_log(LOGGER_INFO, "Call answered.");
}

void millennium_client_hangup(struct millennium_client *client) {
    if (!client->ua) {
        millennium_logger_log(LOGGER_ERROR, "Cannot hangup call: UA is null");
        return;
    }
    
    /* Find the current call for this UA */
    struct call *call = baresip_ua_call((struct ua *)client->ua);
    if (!call) {
        millennium_logger_log(LOGGER_WARN, "Cannot hangup call: No active call found");
        return;
    }
    
    baresip_ua_hangup((struct ua *)client->ua, call, 0, "Call terminated");
    millennium_logger_log(LOGGER_INFO, "Call terminated.");
}

void millennium_client_update(struct millennium_client *client) {
    char buffer[1024];
    ssize_t bytes_read;

    /* Read directly from the file descriptor */
    while ((bytes_read = read(client->display_fd, buffer, sizeof(buffer))) > 0) {
        string_buffer_append(client, buffer, bytes_read);
        millennium_client_process_event_buffer(client);
    }

    if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
        /* Log only if the error is not due to no data being available */
        char message[256];
        snprintf(message, sizeof(message), "Error reading from display_fd: %s", strerror(errno));
        millennium_logger_log(LOGGER_ERROR, message);
    }

    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    
    long elapsed_ms = (current_time.tv_sec - client->last_update_time.tv_sec) * 1000 +
                     (current_time.tv_nsec - client->last_update_time.tv_nsec) / 1000000;
    
    if (client->display_dirty && elapsed_ms > 33) {
        millennium_client_write_to_display(client, client->display_message);
        client->last_update_time = current_time;
        client->display_dirty = 0;
    } else {
        if (client->display_dirty) {
            millennium_logger_log(LOGGER_INFO, "waiting");
        }
    }
}

void millennium_client_process_event_buffer(struct millennium_client *client) {
    while (client->input_buffer_size > 0) {
        millennium_logger_log(LOGGER_DEBUG, client->input_buffer);
        
        size_t event_start = 0;
        size_t i;
        for (i = 0; i < client->input_buffer_size; i++) {
            char c = client->input_buffer[i];
            if (c == '@' || c == 'K' || c == 'C' || c == 'V' || c == 'A' || 
                c == 'B' || c == 'D' || c == 'E' || c == 'F' || c == 'H') {
                event_start = i;
                break;
            }
        }
        
        if (i >= client->input_buffer_size) {
            return; /* No event marker found */
        }

        char event_type = client->input_buffer[event_start];
        char *payload = millennium_client_extract_payload(client, event_type, event_start);
        
        char message[256];
        snprintf(message, sizeof(message), "Event type: %c", event_type);
        millennium_logger_log(LOGGER_DEBUG, message);
        
        if (payload) {
            snprintf(message, sizeof(message), "Payload: %s", payload);
            millennium_logger_log(LOGGER_DEBUG, message);
        }

        millennium_client_create_and_queue_event_char(client, event_type, payload);
        
        if (payload) {
            free(payload);
        }
        
        /* Remove processed data from buffer */
        size_t payload_len = payload ? strlen(payload) : 0;
        size_t remove_len = event_start + payload_len + 1;
        if (remove_len < client->input_buffer_size) {
            memmove(client->input_buffer, client->input_buffer + remove_len, 
                   client->input_buffer_size - remove_len);
            client->input_buffer_size -= remove_len;
            client->input_buffer[client->input_buffer_size] = '\0';
        } else {
            client->input_buffer_size = 0;
            client->input_buffer[0] = '\0';
        }
    }
}

char *millennium_client_extract_payload(struct millennium_client *client, char event_type, size_t event_start) {
    size_t payload_length = 0;
    switch (event_type) {
    case EVENT_TYPE_KEYPAD:
    case EVENT_TYPE_HOOK:
    case EVENT_TYPE_COIN:
        payload_length = 1;
        break;
    case EVENT_TYPE_CARD:
        payload_length = 16;
        break;
    case EVENT_TYPE_EEPROM_ERROR:
        payload_length = 3;
        break;
    case EVENT_TYPE_COIN_UPLOAD_START:
    case EVENT_TYPE_COIN_UPLOAD_END:
    case EVENT_TYPE_COIN_VALIDATION_START:
    case EVENT_TYPE_COIN_VALIDATION_END:
        payload_length = 0;
        break;
    default:
        return NULL;
    }

    if (event_start + payload_length < client->input_buffer_size) {
        char *payload = malloc(payload_length + 1);
        if (payload) {
            memcpy(payload, client->input_buffer + event_start + 1, payload_length);
            payload[payload_length] = '\0';
            return payload;
        }
    }
    return NULL;
}

void millennium_client_create_and_queue_event_ptr(struct millennium_client *client, void *event) {
    if (event) {
        event_queue_push(client, event);
    }
}

void millennium_client_create_and_queue_event_char(struct millennium_client *client, char event_type, const char *payload) {
    char message[256];
    snprintf(message, sizeof(message), "Creating event of type: %c", event_type);
    millennium_logger_log(LOGGER_DEBUG, message);
    
    if (event_type == EVENT_TYPE_KEYPAD && payload && strlen(payload) > 0) {
        keypad_event_t *event = keypad_event_create(payload[0]);
        if (event) event_queue_push(client, (void *)event);
    } else if (event_type == EVENT_TYPE_CARD && payload) {
        card_event_t *event = card_event_create(payload);
        if (event) event_queue_push(client, (void *)event);
    } else if (event_type == EVENT_TYPE_COIN && payload && strlen(payload) > 0) {
        coin_event_t *event = coin_event_create((uint8_t)payload[0]);
        if (event) event_queue_push(client, (void *)event);
    } else if (event_type == EVENT_TYPE_HOOK && payload && strlen(payload) > 0) {
        hook_state_change_event_t *event = hook_state_change_event_create(payload[0]);
        if (event) event_queue_push(client, (void *)event);
    } else if (event_type == EVENT_TYPE_COIN_UPLOAD_START) {
        coin_eeprom_upload_start_t *event = coin_eeprom_upload_start_create();
        if (event) event_queue_push(client, (void *)event);
    } else if (event_type == EVENT_TYPE_COIN_UPLOAD_END) {
        coin_eeprom_upload_end_t *event = coin_eeprom_upload_end_create();
        if (event) event_queue_push(client, (void *)event);
    } else if (event_type == EVENT_TYPE_COIN_VALIDATION_START) {
        coin_eeprom_validation_start_t *event = coin_eeprom_validation_start_create();
        if (event) event_queue_push(client, (void *)event);
    } else if (event_type == EVENT_TYPE_COIN_VALIDATION_END) {
        coin_eeprom_validation_end_t *event = coin_eeprom_validation_end_create();
        if (event) event_queue_push(client, (void *)event);
    } else if (event_type == EVENT_TYPE_EEPROM_ERROR && payload && strlen(payload) >= 3) {
        uint8_t addr = (uint8_t)payload[0];
        uint8_t expected = (uint8_t)payload[1];
        uint8_t actual = (uint8_t)payload[2];
        coin_eeprom_validation_error_t *event = coin_eeprom_validation_error_create(addr, expected, actual);
        if (event) event_queue_push(client, (void *)event);
    } else {
        snprintf(message, sizeof(message), "Unknown event type: %c", event_type);
        millennium_logger_log(LOGGER_WARN, message);
    }
}

void millennium_client_set_display(struct millennium_client *client, const char *message) {
    if (!message) return;
    
    if (client->display_message && strcmp(message, client->display_message) == 0) {
        return;
    }
    
    client->display_dirty = 1;
    if (client->display_message) {
        free(client->display_message);
        client->display_message = NULL;
    }
    client->display_message = string_duplicate(message);
    if (!client->display_message) {
        millennium_logger_log(LOGGER_ERROR, "Failed to duplicate display message");
    }
}

void millennium_client_write_to_display(struct millennium_client *client, const char *message) {
    if (!message) return;
    
    char log_message[256];
    snprintf(log_message, sizeof(log_message), "Writing message to display: %s", message);
    millennium_logger_log(LOGGER_DEBUG, log_message);

    /* Step 1: Write the command */
    uint8_t cmd_data = (uint8_t)strlen(message);
    millennium_client_write_command(client, 0x02, &cmd_data, 1);

    /* Step 2: Write the message in a loop to ensure all bytes are written */
    size_t total_bytes_written = 0;
    size_t message_length = strlen(message);

    while (total_bytes_written < message_length) {
        ssize_t bytes_written = write(client->display_fd, message + total_bytes_written,
                                      message_length - total_bytes_written);

        if (bytes_written > 0) {
            total_bytes_written += bytes_written;
        } else if (bytes_written == -1) {
            if (errno == EINTR) {
                /* Interrupted by a signal, retry */
                continue;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Non-blocking mode: handle or wait before retrying */
                struct timespec sleep_time = {0, 200000}; /* 200 microseconds */
                nanosleep(&sleep_time, NULL);
                continue;
            } else {
                /* Log other errors and exit */
                snprintf(log_message, sizeof(log_message), "Error writing to display: %s", strerror(errno));
                millennium_logger_log(LOGGER_ERROR, log_message);
                return;
            }
        }
    }

    snprintf(log_message, sizeof(log_message), "Successfully wrote %lu bytes to display.", (unsigned long)total_bytes_written);
    millennium_logger_log(LOGGER_DEBUG, log_message);
}

void millennium_client_write_to_coin_validator(struct millennium_client *client, uint8_t data) {
    char message[256];
    snprintf(message, sizeof(message), "Writing to coin validator: %d", data);
    millennium_logger_log(LOGGER_DEBUG, message);

    /* Step 1: Write the command */
    millennium_client_write_command(client, 0x03, &data, 1);

    snprintf(message, sizeof(message), "Successfully wrote command to coin validator: %d", data);
    millennium_logger_log(LOGGER_DEBUG, message);
}

void *millennium_client_next_event(struct millennium_client *client) {
    if (!event_queue_empty(client)) {
        void *event = event_queue_pop(client);
        char *repr = event_get_repr((event_t *)event);
        char message[512];
        snprintf(message, sizeof(message), "Dequeued event: %s %s", 
                event_get_name((event_t *)event), repr ? repr : "");
        millennium_logger_log(LOGGER_DEBUG, message);
        if (repr) free(repr);
        return event;
    }
    return NULL;
}

void millennium_client_set_ua(struct millennium_client *client, void *ua) {
    client->ua = ua;
    char message[256];
    snprintf(message, sizeof(message), "UA set to: %p", client->ua);
    millennium_logger_log(LOGGER_DEBUG, message);
}

void millennium_client_write_command(struct millennium_client *client, uint8_t command, const uint8_t *data, size_t data_size) {
    char message[256];
    snprintf(message, sizeof(message), "Writing command to display: %d", command);
    millennium_logger_log(LOGGER_DEBUG, message);

    /* Step 1: Write the command */
    while (1) {
        ssize_t bytes_written = write(client->display_fd, &command, 1);
        if (bytes_written == 1) {
            break;
        } else if (bytes_written == -1) {
            if (errno == EINTR) {
                continue;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct timespec sleep_time = {0, 200000}; /* 200 microseconds */
                nanosleep(&sleep_time, NULL);
                continue;
            } else {
                snprintf(message, sizeof(message), "Failed to write command: %d, error: %s", 
                        command, strerror(errno));
                millennium_logger_log(LOGGER_ERROR, message);
                return;
            }
        }
    }

    /* Step 2: Write the data if it exists */
    if (data && data_size > 0) {
        size_t total_bytes_written = 0;

        while (total_bytes_written < data_size) {
            ssize_t result = write(client->display_fd, data + total_bytes_written,
                                 data_size - total_bytes_written);

            if (result > 0) {
                total_bytes_written += result;
            } else if (result == -1) {
                if (errno == EINTR) {
                    /* Interrupted by a signal, retry */
                    continue;
                } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    /* Non-blocking mode: handle or log and retry */
                    struct timespec sleep_time = {0, 200000}; /* 200 microseconds */
                    nanosleep(&sleep_time, NULL);
                    continue;
                } else {
                    snprintf(message, sizeof(message), "Error writing data to display, error: %s", strerror(errno));
                    millennium_logger_log(LOGGER_ERROR, message);
                    return;
                }
            }
        }

        snprintf(message, sizeof(message), "Successfully wrote %lu bytes of data to display.", (unsigned long)total_bytes_written);
        millennium_logger_log(LOGGER_DEBUG, message);
    }
}
