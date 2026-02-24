#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700
#include "millennium_sdk.h"
#include "events.h"
#include "baresip_interface.h"
#include "logger.h"
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
static int open_serial_port(struct millennium_client *client, const char *device);

/* SIP registration state: 0=unknown, 1=ok, -1=fail */
static int g_sip_registered = 0;
static char g_sip_last_error[256] = {0};
static pthread_mutex_t g_sip_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Event queue implementation */
static void event_queue_push(struct millennium_client *client, void *event) {
    struct event_queue_node *node = malloc(sizeof(struct event_queue_node));
    if (!node) {
        logger_error_with_category("SDK", "Failed to allocate memory for event queue node");
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

#define MAX_INPUT_BUFFER_SIZE (256 * 1024)  /* 256KB cap to prevent unbounded growth (#127) */

static void string_buffer_ensure_capacity(struct millennium_client *client, size_t needed) {
    if (needed > MAX_INPUT_BUFFER_SIZE) {
        logger_warn_with_category("SDK", "Input buffer would exceed cap, discarding");
        client->input_buffer_size = 0;
        if (client->input_buffer) client->input_buffer[0] = '\0';
        return;
    }
    if (needed >= client->input_buffer_capacity) {
        size_t new_capacity = client->input_buffer_capacity * 2;
        if (new_capacity < needed) {
            new_capacity = needed + 1;
        }
        if (new_capacity > MAX_INPUT_BUFFER_SIZE) {
            new_capacity = MAX_INPUT_BUFFER_SIZE;
        }
        char *new_buffer = realloc(client->input_buffer, new_capacity);
        if (new_buffer) {
            client->input_buffer = new_buffer;
            client->input_buffer_capacity = new_capacity;
        } else {
            logger_error_with_category("SDK", "Failed to reallocate input buffer");
        }
    }
}

static void string_buffer_append(struct millennium_client *client, const char *data, size_t len) {
    size_t needed;
    if (!client || !data || len == 0) return;
    
    needed = client->input_buffer_size + len + 1;
    string_buffer_ensure_capacity(client, needed);
    /* Only append if we have space (realloc may have failed) (#128) */
    if (client->input_buffer && needed <= client->input_buffer_capacity) {
        memcpy(client->input_buffer + client->input_buffer_size, data, len);
        client->input_buffer_size += len;
        client->input_buffer[client->input_buffer_size] = '\0';
    }
}

/* UA event handler */
static void ua_event_handler(enum baresip_ua_event ev, struct bevent *event, void *client) {
    struct call *call;
    struct ua *ua;
    call_state_t state_value;
    call_state_event_t *call_event;
    
    logger_infof_with_category("SDK", "UA event: %s", baresip_uag_event_str(ev));

    /* Track SIP registration state for health check */
    if (ev == BARESIP_UA_EVENT_REGISTER_OK) {
        pthread_mutex_lock(&g_sip_mutex);
        g_sip_registered = 1;
        g_sip_last_error[0] = '\0';
        pthread_mutex_unlock(&g_sip_mutex);
    } else if (ev == BARESIP_UA_EVENT_REGISTER_FAIL) {
        pthread_mutex_lock(&g_sip_mutex);
        g_sip_registered = -1;
        ua = baresip_bevent_get_ua(event);
        if (ua) {
            struct account *acc = baresip_ua_account(ua);
            const char *aor = acc ? baresip_account_aor(acc) : NULL;
            const char *text = baresip_bevent_get_text(event);
            if (text && text[0]) {
                logger_warnf_with_category("SDK", "SIP registration failed%s%s (%.64s)",
                    aor ? ": " : "", aor ? aor : "unknown", text);
                snprintf(g_sip_last_error, sizeof(g_sip_last_error), "%.128s", text);
            } else {
                logger_warnf_with_category("SDK", "SIP registration failed%s%s",
                    aor ? ": " : "", aor ? aor : "");
                snprintf(g_sip_last_error, sizeof(g_sip_last_error), "Registration failed%s",
                    aor ? "" : " (no account)");
            }
        } else {
            g_sip_last_error[0] = '\0';
        }
        pthread_mutex_unlock(&g_sip_mutex);
    }
    
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

    logger_info_with_category("SDK", "--- Audio Sources ---");
    for (le = baresip_ausrcl_head(); le; le = baresip_list_next(le)) {
        ausrc = baresip_list_data(le);
        if (ausrc && baresip_ausrc_name(ausrc)) {
            logger_infof_with_category("SDK", "Source: %s", baresip_ausrc_name(ausrc));
        }
    }

    logger_info_with_category("SDK", "--- Audio Players ---");
    for (le = baresip_auplayl_head(); le; le = baresip_list_next(le)) {
        auplay = baresip_auplay_data(le);
        if (auplay && baresip_auplay_name(auplay)) {
            logger_infof_with_category("SDK", "Player: %s", baresip_auplay_name(auplay));
        }
    }
}

void millennium_sdk_get_sip_status(int *registered, char *last_error, size_t last_error_size) {
    pthread_mutex_lock(&g_sip_mutex);
    if (registered) *registered = g_sip_registered;
    if (last_error && last_error_size > 0) {
        strncpy(last_error, g_sip_last_error, last_error_size - 1);
        last_error[last_error_size - 1] = '\0';
    }
    pthread_mutex_unlock(&g_sip_mutex);
}

/* Open (or reopen) the serial port, configuring termios. Returns 0 on success. */
static int open_serial_port(struct millennium_client *client, const char *device) {
    int flags;
    struct termios options;

    if (client->display_fd != -1) {
        close(client->display_fd);
        client->display_fd = -1;
    }

    client->display_fd = open(device, O_RDWR | O_NOCTTY);
    if (client->display_fd == -1) {
        logger_errorf_with_category("SDK", "Failed to open serial device %s: %s", device, strerror(errno));
        return -1;
    }

    flags = fcntl(client->display_fd, F_GETFL, 0);
    if (flags == -1 || fcntl(client->display_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        logger_errorf_with_category("SDK", "Failed to set non-blocking mode: %s", strerror(errno));
        close(client->display_fd);
        client->display_fd = -1;
        return -1;
    }

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

    clock_gettime(CLOCK_MONOTONIC, &client->last_serial_activity);
    client->serial_healthy = 1;
    client->reconnect_attempts = 0;

    return 0;
}

/* Millennium client implementation */
struct millennium_client *millennium_client_create(void) {
    struct millennium_client *client = malloc(sizeof(struct millennium_client));
    if (!client) {
        logger_error_with_category("SDK", "Failed to allocate memory for MillenniumClient");
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
        logger_error_with_category("SDK", "Failed to allocate input buffer");
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
    
    {
        const char *display_device = "/dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00";
        strncpy(client->serial_device_path, display_device, sizeof(client->serial_device_path) - 1);
        client->serial_device_path[sizeof(client->serial_device_path) - 1] = '\0';

        if (open_serial_port(client, client->serial_device_path) != 0) {
            millennium_client_destroy(client);
            return NULL;
        }
    }

    logger_debug_with_category("SDK", "libre_init");
    int err = baresip_libre_init();
    if (err) {
        logger_error_with_category("SDK", "libre_init failed");
        millennium_client_destroy(client);
        return NULL;
    }

    baresip_re_thread_async_init(ASYNC_WORKERS);

    baresip_log_enable_debug(1);

    int dbg_level = BARESIP_DBG_DEBUG;
    int dbg_flags = BARESIP_DBG_ANSI | BARESIP_DBG_TIME;
    baresip_dbg_init(dbg_level, dbg_flags);

    logger_debug_with_category("SDK", "conf_configure");
    err = baresip_conf_configure();
    if (err) {
        logger_error_with_category("SDK", "conf_configure failed");
        millennium_client_destroy(client);
        return NULL;
    }

    logger_debug_with_category("SDK", "baresip_init_c");
    if (baresip_init_c(baresip_conf_config()) != 0) {
        logger_error_with_category("SDK", "Failed to initialize Baresip.");
        millennium_client_destroy(client);
        return NULL;
    }

    baresip_play_set_path(baresip_player_c(), "/usr/local/share/baresip");

    err = baresip_ua_init("baresip v2.0.0 (x86_64/linux)", 1, 1, 1);
    if (err) {
        logger_error_with_category("SDK", "ua_init failed");
        millennium_client_destroy(client);
        return NULL;
    }

    logger_debug_with_category("SDK", "conf_modules");
    err = baresip_conf_modules();
    if (err) {
        logger_error_with_category("SDK", "conf_modules failed");
        millennium_client_destroy(client);
        return NULL;
    }

    baresip_bevent_register(ua_event_handler, client);

    /* Create thread */
    if (pthread_create((pthread_t *)&client->thread_handle, NULL, baresip_thread_func, NULL) != 0) {
        logger_error_with_category("SDK", "Failed to create baresip thread");
        millennium_client_destroy(client);
        return NULL;
    }

    logger_info_with_category("SDK", "MillenniumClient initialized successfully.");
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
        logger_info_with_category("SDK", "MillenniumClient closed.");
    }
}

void millennium_client_call(struct millennium_client *client, const char *number) {
    char target[64];
    struct ua *ua;
    char *uric;
    int err;
    struct call *call;
    
    snprintf(target, sizeof(target), "+1%s", number);

    logger_debugf_with_category("SDK", "ua: %p", client->ua);
    
    logger_infof_with_category("SDK", "Initiating call to: %s", target);
    
    /* Find the appropriate UA for this request URI */
    ua = baresip_ua_find_requri(target);
    
    if (!ua) {
        logger_errorf_with_category("SDK", "Could not find UA for: %s", target);
        return;
    }
    
    /* Store the UA for later use in answer/hangup */
    client->ua = ua;
    
    /* Complete the URI */
    uric = NULL;
    err = baresip_account_uri_complete_strdup(baresip_ua_account(ua), &uric, target);
    if (err != 0) {
        logger_errorf_with_category("SDK", "Failed to complete URI: %s %d", target, err);
        return;
    }
    
    logger_infof_with_category("SDK", "Using UA: %s", baresip_account_aor(baresip_ua_account(ua)));
    
    logger_infof_with_category("SDK", "Completed URI: %s", uric);
    
    /* Make the call */
    call = NULL;
    err = baresip_ua_connect(ua, &call, NULL, uric, BARESIP_VIDMODE_OFF);
    
    /* Clean up the completed URI */
    baresip_mem_deref(uric);
    
    if (err != 0) {
        logger_errorf_with_category("SDK", "Failed to initiate call to: %s %d", target, err);
    } else {
        logger_infof_with_category("SDK", "Calling: %s", number);
    }
}

void millennium_client_answer_call(struct millennium_client *client) {
    if (!client->ua) {
        logger_error_with_category("SDK", "Cannot answer call: UA is null");
        return;
    }
    
    /* Find the current call for this UA */
    struct call *call = baresip_ua_call((struct ua *)client->ua);
    if (!call) {
        logger_error_with_category("SDK", "Cannot answer call: No active call found");
        return;
    }
    
    baresip_ua_answer((struct ua *)client->ua, call, BARESIP_VIDMODE_OFF);
    logger_info_with_category("SDK", "Call answered.");
}

void millennium_client_hangup(struct millennium_client *client) {
    if (!client->ua) {
        logger_error_with_category("SDK", "Cannot hangup call: UA is null");
        return;
    }
    
    /* Find the current call for this UA */
    struct call *call = baresip_ua_call((struct ua *)client->ua);
    if (!call) {
        logger_warn_with_category("SDK", "Cannot hangup call: No active call found");
        return;
    }
    
    baresip_ua_hangup((struct ua *)client->ua, call, 0, "Call terminated");
    logger_info_with_category("SDK", "Call terminated.");
}

void millennium_client_serial_activity(struct millennium_client *client) {
    if (!client) return;
    clock_gettime(CLOCK_MONOTONIC, &client->last_serial_activity);
    if (!client->serial_healthy) {
        logger_info_with_category("SDK", "Serial link recovered");
        client->serial_healthy = 1;
        client->reconnect_attempts = 0;
    }
}

int millennium_client_serial_is_healthy(struct millennium_client *client) {
    if (!client) return 0;
    return client->serial_healthy;
}

void millennium_client_check_serial(struct millennium_client *client) {
    if (!client || client->display_fd == -1) return;

#if SERIAL_WATCHDOG_ENABLED
    struct timespec now;
    long elapsed;
    int backoff;

    clock_gettime(CLOCK_MONOTONIC, &now);
    elapsed = now.tv_sec - client->last_serial_activity.tv_sec;

    if (client->serial_healthy && elapsed > SERIAL_WATCHDOG_SECONDS) {
        logger_warnf_with_category("SDK",
            "Serial watchdog: no activity for %ld seconds, marking link dead", elapsed);
        client->serial_healthy = 0;
        client->reconnect_attempts = 0;
        client->next_reconnect_time = now;
    }

    if (!client->serial_healthy) {
        if (now.tv_sec < client->next_reconnect_time.tv_sec) return;

        logger_infof_with_category("SDK",
            "Serial reconnect attempt %d", client->reconnect_attempts + 1);

        if (open_serial_port(client, client->serial_device_path) == 0) {
            logger_info_with_category("SDK", "Serial port reopened successfully");

            /* Re-init coin validator (send 'f' command) */
            {
                uint8_t coin_init = 'f';
                millennium_client_write_to_coin_validator(client, coin_init);
            }

            /* Refresh display */
            if (client->display_message) {
                client->display_dirty = 1;
            }
        } else {
            client->reconnect_attempts++;
            backoff = 1 << client->reconnect_attempts;
            if (backoff > SERIAL_MAX_BACKOFF_SECONDS) backoff = SERIAL_MAX_BACKOFF_SECONDS;
            client->next_reconnect_time.tv_sec = now.tv_sec + backoff;
            logger_warnf_with_category("SDK",
                "Serial reconnect failed, next attempt in %d seconds", backoff);
        }
    }
#endif /* SERIAL_WATCHDOG_ENABLED */
}

void millennium_client_update(struct millennium_client *client) {
    char buffer[1024];
    ssize_t bytes_read;

    if (client->display_fd == -1) return;

    /* Read directly from the file descriptor */
    while ((bytes_read = read(client->display_fd, buffer, sizeof(buffer))) > 0) {
        millennium_client_serial_activity(client);
        string_buffer_append(client, buffer, bytes_read);
        millennium_client_process_event_buffer(client);
    }

    if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
        logger_errorf_with_category("SDK", "Error reading from display_fd: %s", strerror(errno));
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
            logger_info_with_category("SDK", "waiting");
        }
    }
}

void millennium_client_process_event_buffer(struct millennium_client *client) {
    while (client->input_buffer_size > 0) {
        logger_debug_with_category("SDK", client->input_buffer);
        
        size_t event_start = 0;
        size_t i;
        for (i = 0; i < client->input_buffer_size; i++) {
            char c = client->input_buffer[i];
            if (c == '@' || c == 'K' || c == 'C' || c == 'V' || c == 'A' || 
                c == 'B' || c == 'D' || c == 'E' || c == 'F' || c == 'H' ||
                c == EVENT_TYPE_HEARTBEAT) {
                event_start = i;
                break;
            }
        }
        
        if (i >= client->input_buffer_size) {
            return; /* No event marker found */
        }

        char event_type = client->input_buffer[event_start];
        char *payload = millennium_client_extract_payload(client, event_type, event_start);
        
        logger_debugf_with_category("SDK", "Event type: %c", event_type);
        
        if (payload) {
            logger_debugf_with_category("SDK", "Payload: %s", payload);
        }

        millennium_client_create_and_queue_event_char(client, event_type, payload);
        
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
        
        /* Free payload after processing to prevent memory leak */
        if (payload) {
            free(payload);
            payload = NULL;
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
    case EVENT_TYPE_HEARTBEAT:
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
    logger_debugf_with_category("SDK", "Creating event of type: %c", event_type);
    
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
    } else if (event_type == EVENT_TYPE_HEARTBEAT) {
        /* Silently consumed; serial_activity was already updated on read */
    } else {
        logger_warnf_with_category("SDK", "Unknown event type: %c", event_type);
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
        logger_error_with_category("SDK", "Failed to duplicate display message");
    }
}

void millennium_client_write_to_display(struct millennium_client *client, const char *message) {
    if (!message) return;
    
    logger_debugf_with_category("SDK", "Writing message to display: %s", message);

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
                logger_errorf_with_category("SDK", "Error writing to display: %s", strerror(errno));
                return;
            }
        }
    }

    logger_debugf_with_category("SDK", "Successfully wrote %lu bytes to display.", (unsigned long)total_bytes_written);
}

void millennium_client_write_to_coin_validator(struct millennium_client *client, uint8_t data) {
    logger_debugf_with_category("SDK", "Writing to coin validator: %d", data);

    /* Step 1: Write the command */
    millennium_client_write_command(client, 0x03, &data, 1);

    logger_debugf_with_category("SDK", "Successfully wrote command to coin validator: %d", data);
}

void *millennium_client_next_event(struct millennium_client *client) {
    if (!event_queue_empty(client)) {
        void *event = event_queue_pop(client);
        char *repr = event_get_repr((event_t *)event);
        logger_debugf_with_category("SDK", "Dequeued event: %s %s", 
                event_get_name((event_t *)event), repr ? repr : "");
        if (repr) free(repr);
        return event;
    }
    return NULL;
}

void millennium_client_set_ua(struct millennium_client *client, void *ua) {
    client->ua = ua;
    logger_debugf_with_category("SDK", "UA set to: %p", client->ua);
}

void millennium_client_write_command(struct millennium_client *client, uint8_t command, const uint8_t *data, size_t data_size) {
    logger_debugf_with_category("SDK", "Writing command to display: %d", command);

    if (!client) return;

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
                logger_errorf_with_category("SDK", "Failed to write command: %d, error: %s", 
                        command, strerror(errno));
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
                    logger_errorf_with_category("SDK", "Error writing data to display, error: %s", strerror(errno));
                    return;
                }
            }
        }

        logger_debugf_with_category("SDK", "Successfully wrote %lu bytes of data to display.", (unsigned long)total_bytes_written);
    }

    /* Any successful write counts as serial activity for the watchdog */
    millennium_client_serial_activity(client);
}
