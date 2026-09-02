#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700
#include "millennium_sdk.h"
#include "events.h"
#include "pjsip_interface.h"
#include "config.h"
#include "logger.h"
#include "metrics.h"
#include "coin_gate.h"
#include "serial_recovery.h"
#include "serial_settings.h"
#include "mcu_protocol.h"
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
static void sip_event_cb(enum pjsip_iface_event ev, const char *text, void *client);
static void event_queue_push(struct millennium_client *client, void *event);
static void *event_queue_pop(struct millennium_client *client);
static int event_queue_empty(struct millennium_client *client);
static void event_queue_clear(struct millennium_client *client);
static char *string_duplicate(const char *src);
static void string_buffer_append(struct millennium_client *client, const char *data, size_t len);
static void string_buffer_ensure_capacity(struct millennium_client *client, size_t needed);
static int open_serial_port(struct millennium_client *client, const char *device);
static int send_mcu_hello(struct millennium_client *client);
static speed_t serial_speed_from_baud(int baud_rate);
static int serial_write_all(struct millennium_client *client, const uint8_t *data,
                            size_t length);
static void process_mcu_frame(struct millennium_client *client, const mcu_frame_t *frame);

/* SIP registration state: 0=unknown, 1=ok, -1=fail */
static int g_sip_registered = 0;
static char g_sip_last_error[256] = {0};
static pthread_mutex_t g_sip_mutex = PTHREAD_MUTEX_INITIALIZER;

/* The event queue is produced by both the main loop (serial events) and PJSUA
 * worker threads (SIP call-state events, via the SIP callback) and consumed by
 * the main loop, so its linked-list/malloc operations must be serialized or the
 * heap corrupts. This guards push/pop/empty; clear() runs only at shutdown and
 * reuses those locked primitives, so it stays lock-free itself. */
static pthread_mutex_t g_queue_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Event queue implementation */
static void event_queue_push(struct millennium_client *client, void *event) {
    struct event_queue_node *node = malloc(sizeof(struct event_queue_node));
    if (!node) {
        logger_error_with_category("SDK", "Failed to allocate memory for event queue node");
        return;
    }

    node->event = event;
    node->next = NULL;

    pthread_mutex_lock(&g_queue_mutex);
    if (client->event_queue_tail) {
        client->event_queue_tail->next = node;
    } else {
        client->event_queue_head = node;
    }
    client->event_queue_tail = node;
    pthread_mutex_unlock(&g_queue_mutex);
}

static void *event_queue_pop(struct millennium_client *client) {
    struct event_queue_node *node;
    void *event;

    pthread_mutex_lock(&g_queue_mutex);
    if (!client->event_queue_head) {
        pthread_mutex_unlock(&g_queue_mutex);
        return NULL;
    }

    node = client->event_queue_head;
    event = node->event;

    client->event_queue_head = node->next;
    if (!client->event_queue_head) {
        client->event_queue_tail = NULL;
    }
    pthread_mutex_unlock(&g_queue_mutex);

    free(node);
    return event;
}

static int event_queue_empty(struct millennium_client *client) {
    int empty;
    pthread_mutex_lock(&g_queue_mutex);
    empty = (client->event_queue_head == NULL);
    pthread_mutex_unlock(&g_queue_mutex);
    return empty;
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
    size_t len;
    char *dst;
    if (!src) return NULL;
    len = strlen(src) + 1;
    dst = malloc(len);
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
        char *new_buffer;
        if (new_capacity < needed) {
            new_capacity = needed + 1;
        }
        if (new_capacity > MAX_INPUT_BUFFER_SIZE) {
            new_capacity = MAX_INPUT_BUFFER_SIZE;
        }
        new_buffer = realloc(client->input_buffer, new_capacity);
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

/* SIP event callback (invoked from a PJSUA worker thread). Mirrors the old
 * baresip ua_event_handler: it tracks registration state for the health check
 * and queues call-state events for the daemon's event loop. */
static void sip_event_cb(enum pjsip_iface_event ev, const char *text, void *client) {
    call_state_t state_value;
    call_state_event_t *call_event;
    const char *label;

    switch (ev) {
    case PJSIP_IFACE_REG_OK:
        pthread_mutex_lock(&g_sip_mutex);
        g_sip_registered = 1;
        g_sip_last_error[0] = '\0';
        pthread_mutex_unlock(&g_sip_mutex);
        return;
    case PJSIP_IFACE_REG_FAIL:
        pthread_mutex_lock(&g_sip_mutex);
        g_sip_registered = -1;
        if (text && text[0])
            snprintf(g_sip_last_error, sizeof(g_sip_last_error), "%.128s", text);
        else
            snprintf(g_sip_last_error, sizeof(g_sip_last_error), "Registration failed");
        pthread_mutex_unlock(&g_sip_mutex);
        return;
    case PJSIP_IFACE_CALL_INCOMING:
        state_value = EVENT_CALL_STATE_INCOMING; label = "CALL_INCOMING"; break;
    case PJSIP_IFACE_CALL_ESTABLISHED:
        state_value = EVENT_CALL_STATE_ACTIVE; label = "CALL_ESTABLISHED"; break;
    case PJSIP_IFACE_CALL_CLOSED:
    default:
        state_value = EVENT_CALL_STATE_INVALID; label = "CALL_CLOSED"; break;
    }

    if (client) {
        call_event = call_state_event_create(label, NULL, state_value);
        if (call_event) {
            millennium_client_create_and_queue_event_ptr(
                (struct millennium_client *)client, (void *)call_event);
        }
    }
}

/* Audio device listing (delegates to the PJSIP layer). */
void list_audio_devices(void) {
    pjsip_iface_list_audio_devices();
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
static speed_t serial_speed_from_baud(int baud_rate) {
    switch (baud_rate) {
        case 1200: return B1200;
        case 2400: return B2400;
        case 4800: return B4800;
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
#ifdef B57600
        case 57600: return B57600;
#endif
#ifdef B115200
        case 115200: return B115200;
#endif
        default: return (speed_t)0;
    }
}

static int open_serial_port(struct millennium_client *client, const char *device) {
    int flags;
    struct termios options;
    speed_t speed;

    speed = serial_speed_from_baud(client->serial_baud_rate);
    if (speed == (speed_t)0) {
        logger_errorf_with_category("SDK", "Unsupported serial baud rate: %d",
                                    client->serial_baud_rate);
        return -1;
    }

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
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);
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

    mcu_decoder_init(&client->mcu_decoder);
    memset(&client->mcu_event_replay, 0, sizeof(client->mcu_event_replay));
    client->mcu_protocol_ready = 0;
    client->serial_replay_pending = 1;
    clock_gettime(CLOCK_MONOTONIC, &client->mcu_protocol_started_at);
    client->mcu_protocol_last_hello_at = client->mcu_protocol_started_at;
    client->pending_frame_length = 0;
    if (send_mcu_hello(client) != 0) {
        logger_error_with_category("SDK", "Failed to begin MCU protocol negotiation");
        close(client->display_fd);
        client->display_fd = -1;
        client->serial_healthy = 0;
        return -1;
    }

    return 0;
}

static int send_mcu_hello(struct millennium_client *client) {
    uint8_t hello[2] = {MCU_PROTOCOL_VERSION, MCU_PROTOCOL_VERSION};
    uint8_t frame[MCU_PROTOCOL_MAX_FRAME];
    size_t length;
    if (!client) return -1;
    length = mcu_protocol_encode(MCU_MSG_HELLO, client->mcu_tx_sequence++, hello,
                                 sizeof(hello), frame, sizeof(frame));
    if (!length || serial_write_all(client, frame, length) != 0) return -1;
    clock_gettime(CLOCK_MONOTONIC, &client->mcu_protocol_last_hello_at);
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
    mcu_decoder_init(&client->mcu_decoder);
    
    /* Set function pointers */
    client->create_and_queue_event_char = millennium_client_create_and_queue_event_char;
    client->create_and_queue_event_ptr = millennium_client_create_and_queue_event_ptr;
    
    /* Get current time */
    clock_gettime(CLOCK_MONOTONIC, &client->last_update_time);
    
    {
        config_data_t *cfg = config_get_instance();
        serial_settings_t settings;
        if (serial_settings_load(cfg, &settings) != 0) {
            millennium_client_destroy(client);
            logger_error_with_category("SDK", "Invalid serial configuration");
            return NULL;
        }
        strcpy(client->serial_device_path, settings.device_path);
        client->serial_baud_rate = settings.baud_rate;

        if (open_serial_port(client, client->serial_device_path) != 0) {
            millennium_client_destroy(client);
            return NULL;
        }
    }

    /* Build the SIP account from the daemon config and bring up PJSIP.
     * Credentials live in /etc/millennium/daemon.conf (sip.*), so there is no
     * separate accounts file and no need to run as a particular user. If SIP
     * isn't configured (or fails to start), the daemon still runs — the phone
     * just has no VoIP, which is fine for the games/plugins. */
    {
        config_data_t *cfg = config_get_instance();
        pjsip_iface_account_t acc;
        const char *transport;

        memset(&acc, 0, sizeof(acc));
        acc.id_uri      = config_get_string(cfg, "sip.id_uri", "");
        acc.reg_uri     = config_get_string(cfg, "sip.registrar", "");
        acc.realm       = config_get_string(cfg, "sip.realm", "*");
        acc.username    = config_get_string(cfg, "sip.username", "");
        acc.password    = config_get_string(cfg, "sip.password", "");
        acc.stun_server = config_get_string(cfg, "sip.stun_server", "");
        acc.local_port   = config_get_int(cfg, "sip.local_port", 0);
        /* ALSA device names; defaults route call audio to the handset earpiece
         * (right channel) and capture the C-Media mic via a plug device. */
        acc.snd_capture  = config_get_string(cfg, "sip.snd_capture",  "plughw:CARD=Device,DEV=0");
        acc.snd_playback = config_get_string(cfg, "sip.snd_playback", "out_right_solo");

        transport = config_get_string(cfg, "sip.transport", "tls");
        if (strcmp(transport, "udp") == 0)
            acc.transport = PJSIP_IFACE_TRANSPORT_UDP;
        else if (strcmp(transport, "tcp") == 0)
            acc.transport = PJSIP_IFACE_TRANSPORT_TCP;
        else
            acc.transport = PJSIP_IFACE_TRANSPORT_TLS;

        if (!acc.id_uri[0] || !acc.reg_uri[0]) {
            logger_warn_with_category("SDK",
                "SIP not configured (sip.id_uri/sip.registrar empty); VoIP disabled");
        } else if (pjsip_iface_start(&acc, sip_event_cb, client) != 0) {
            logger_error_with_category("SDK",
                "Failed to start PJSIP; continuing with VoIP disabled");
        }
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
        
        pjsip_iface_stop();

        client->is_open = 0;
        logger_info_with_category("SDK", "MillenniumClient closed.");
    }
}

void millennium_client_call(struct millennium_client *client, const char *number) {
    char user[64];
    (void)client;

    if (!number) return;

    /* North American dialing: prefix +1. The PJSIP layer appends the account
     * domain and chooses the transport. */
    snprintf(user, sizeof(user), "+1%s", number);
    logger_infof_with_category("SDK", "Initiating call to: %s", user);

    if (pjsip_iface_call(user) != 0) {
        logger_errorf_with_category("SDK", "Failed to initiate call to: %s", user);
    } else {
        logger_infof_with_category("SDK", "Calling: %s", number);
    }
}

void millennium_client_answer_call(struct millennium_client *client) {
    (void)client;
    if (pjsip_iface_answer() == 0) {
        logger_info_with_category("SDK", "Call answered.");
    } else {
        logger_error_with_category("SDK", "Cannot answer call: no active call");
    }
}

void millennium_client_hangup(struct millennium_client *client) {
    (void)client;
    pjsip_iface_hangup();
    logger_info_with_category("SDK", "Call terminated.");
}

int millennium_client_send_dtmf(struct millennium_client *client, char key) {
    (void)client;
    return pjsip_iface_send_dtmf(key);
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
#if SERIAL_WATCHDOG_ENABLED
    struct timespec now;
    serial_link_state_t st;
    serial_action_t action;
    int backoff;
#endif
    if (!client) return;

#if SERIAL_WATCHDOG_ENABLED
    clock_gettime(CLOCK_MONOTONIC, &now);

    /* The policy lives in serial_recovery.c so it can be unit-tested; this
     * function just samples the link and carries the verdict out (#247). */
    st.fd_open = (client->display_fd != -1);
    st.link_healthy = client->serial_healthy;
    st.idle_seconds = now.tv_sec - client->last_serial_activity.tv_sec;
    st.seconds_until_retry = client->next_reconnect_time.tv_sec - now.tv_sec;

    action = serial_recovery_next_action(&st);

    if (action == SERIAL_ACTION_MARK_DEAD) {
        if (st.fd_open) {
            logger_warnf_with_category("SDK",
                "Serial watchdog: no activity for %ld seconds, marking link dead",
                st.idle_seconds);
        } else {
            logger_warn_with_category("SDK", "Serial fd is closed, marking link dead");
        }
        client->serial_healthy = 0;
        metrics_increment_counter("serial_disconnects", 1);
        client->reconnect_attempts = 0;
        client->next_reconnect_time = now;

        /* Marking the link dead makes a retry due immediately, so ask again and
         * reconnect in this same pass -- which is what the daemon has always
         * done on the pass where the watchdog fires. */
        st.link_healthy = 0;
        st.seconds_until_retry = 0;
        action = serial_recovery_next_action(&st);
    }

    /* (#59) Send periodic keepalive when idle to avoid false watchdog triggers.
     * Arduino consumes CMD_KEEPALIVE (0x06) as no-op; write_command updates last_serial_activity. */
    if (action == SERIAL_ACTION_KEEPALIVE) {
        millennium_client_write_command(client, CMD_KEEPALIVE, NULL, 0);
    }

    if (action == SERIAL_ACTION_RECONNECT) {
        logger_infof_with_category("SDK",
            "Serial reconnect attempt %d", client->reconnect_attempts + 1);

        if (open_serial_port(client, client->serial_device_path) == 0) {
            logger_info_with_category("SDK", "Serial port reopened successfully");
            metrics_increment_counter("serial_reconnects", 1);

            /* Opening an Arduino Micro resets it. State replay is deferred
             * until the MCU completes protocol negotiation. */
        } else {
            client->reconnect_attempts++;
            backoff = serial_recovery_backoff_seconds(client->reconnect_attempts);
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
    struct timespec current_time;
    long elapsed_ms;

    /* (#263) NULL-guard to match check_serial. This is where a NULL client
     * from a failed millennium_client_create() used to land as a SEGV. */
    if (!client || client->display_fd == -1) return;

    /* Read directly from the file descriptor */
    while ((bytes_read = read(client->display_fd, buffer, sizeof(buffer))) > 0) {
        ssize_t index;
        millennium_client_serial_activity(client);
        for (index = 0; index < bytes_read; index++) {
            uint8_t byte = (uint8_t)buffer[index];
            if (client->mcu_decoder.used || byte == MCU_PROTOCOL_SOF) {
                mcu_frame_t frame;
                int decoded = mcu_decoder_feed(&client->mcu_decoder, byte, &frame);
                if (decoded == 1) process_mcu_frame(client, &frame);
                else if (decoded < 0) metrics_increment_counter("mcu_crc_or_frame_errors", 1);
            } else {
                string_buffer_append(client, (const char *)&byte, 1);
            }
        }
        millennium_client_process_event_buffer(client);
    }

    if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
        logger_errorf_with_category("SDK", "Error reading from display_fd: %s", strerror(errno));
    }

    clock_gettime(CLOCK_MONOTONIC, &current_time);

    if (!client->mcu_protocol_ready && client->serial_healthy) {
        serial_ready_action_t ready_action = serial_recovery_readiness_action(
            current_time.tv_sec - client->mcu_protocol_started_at.tv_sec,
            current_time.tv_sec - client->mcu_protocol_last_hello_at.tv_sec);
        if (ready_action == SERIAL_READY_SEND_HELLO) {
            if (send_mcu_hello(client) == 0)
                metrics_increment_counter("mcu_protocol_hello_retries", 1);
        } else if (ready_action == SERIAL_READY_FAIL) {
            logger_error_with_category("SDK", "MCU protocol negotiation timed out");
            metrics_increment_counter("mcu_protocol_negotiation_failures", 1);
            client->serial_healthy = 0;
        }
    }

    if (client->mcu_protocol_ready && client->serial_replay_pending) {
        uint8_t coin_seq[COIN_GATE_RESYNC_MAX];
        size_t coin_len = millennium_coin_gate_resync(
                client->coin_gate_cmd, coin_seq, sizeof(coin_seq));
        size_t i;
        client->serial_replay_pending = 0;
        for (i = 0; i < coin_len; i++)
            millennium_client_write_to_coin_validator(client, coin_seq[i]);
        if (client->display_message) client->display_dirty = 1;
    }

    if (client->pending_frame_length > 0) {
        long pending_ms = (current_time.tv_sec - client->pending_sent_at.tv_sec) * 1000 +
            (current_time.tv_nsec - client->pending_sent_at.tv_nsec) / 1000000;
        if (pending_ms >= 500) {
            if (client->pending_retries >= 3) {
                logger_errorf_with_category("SDK",
                    "MCU command sequence %u was not acknowledged",
                    (unsigned int)client->pending_sequence);
                metrics_increment_counter("mcu_command_timeouts", 1);
                client->pending_frame_length = 0;
                client->serial_healthy = 0;
            } else if (serial_write_all(client, client->pending_frame,
                                        client->pending_frame_length) == 0) {
                client->pending_retries++;
                client->pending_sent_at = current_time;
                metrics_increment_counter("mcu_command_retries", 1);
            }
        }
    }

    elapsed_ms = (current_time.tv_sec - client->last_update_time.tv_sec) * 1000 +
                     (current_time.tv_nsec - client->last_update_time.tv_nsec) / 1000000;
    
    if (client->mcu_protocol_ready && client->display_dirty && elapsed_ms > 33) {
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
        size_t event_start = 0;
        size_t i;
        char event_type;
        char *payload;
        size_t payload_len;
        size_t remove_len;
        logger_debug_with_category("SDK", client->input_buffer);

        for (i = 0; i < client->input_buffer_size; i++) {
            char c = client->input_buffer[i];
            if (c == '@' || c == 'K' || c == 'C' || c == 'V' || c == 'A' || 
                c == 'B' || c == 'D' || c == 'E' || c == 'F' || c == 'H' ||
                c == EVENT_TYPE_DIAG || c == EVENT_TYPE_HEARTBEAT) {
                event_start = i;
                break;
            }
        }
        
        if (i >= client->input_buffer_size) {
            return; /* No event marker found */
        }

        event_type = client->input_buffer[event_start];
        payload = millennium_client_extract_payload(client, event_type, event_start);
        
        logger_debugf_with_category("SDK", "Event type: %c", event_type);
        
        if (payload) {
            logger_debugf_with_category("SDK", "Payload: %s", payload);
        }

        millennium_client_create_and_queue_event_char(client, event_type, payload);
        
        /* Remove processed data from buffer */
        payload_len = payload ? strlen(payload) : 0;
        remove_len = event_start + payload_len + 1;
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
    case EVENT_TYPE_DIAG:
        payload_length = EVENT_DIAG_PAYLOAD_LEN;
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
    } else if (event_type == EVENT_TYPE_DIAG && payload &&
               strlen(payload) >= EVENT_DIAG_PAYLOAD_LEN) {
        /* (#230) An Arduino telling us it lost messages.  Not queued as a phone
         * event -- nothing acts on it -- but it must not stay invisible, which
         * was the whole complaint: keypresses went missing with nothing
         * anywhere reporting the loss.  Published as a gauge because the
         * Arduino reports a running total that restarts at 0 when it reboots. */
        const char *source;
        long count;
        if (event_diag_parse(payload, &source, &count)) {
            /* The Arduinos re-announce on a timer so a report cannot be lost to
             * a reconnect, and so the gauge follows a board reboot back down to
             * zero. Refresh the gauge every time, but only log when the number
             * actually moves -- otherwise a single drop would spam the log
             * forever. Remembered per source; two sources, so a tiny array. */
            static long last_logged[2] = { -1, -1 };
            int idx = (source[0] == 'a') ? 0 : 1;
            char metric[64];

            snprintf(metric, sizeof(metric), "arduino_i2c_drops_%s", source);
            metrics_set_gauge(metric, (double)count);

            if (count != last_logged[idx]) {
                if (count > 0) {
                    logger_warnf_with_category("SDK",
                        "Arduino %s reports %ld dropped I2C message(s)", source, count);
                } else if (last_logged[idx] > 0) {
                    logger_infof_with_category("SDK",
                        "Arduino %s I2C drop count reset to 0", source);
                }
                last_logged[idx] = count;
            }
        } else {
            const char *role;
            long cause;
            if (event_reset_parse(payload, &role, &cause)) {
                char metric[64];
                snprintf(metric, sizeof(metric), "mcu_reset_cause_%s", role);
                metrics_set_gauge(metric, (double)cause);
                snprintf(metric, sizeof(metric), "mcu_resets_%s", role);
                metrics_increment_counter(metric, 1);
                logger_infof_with_category("SDK",
                    "Arduino %s previous reset cause bitmask: %ld", role, cause);
            } else {
                logger_warn_with_category("SDK", "Malformed Arduino diagnostic payload");
            }
        }
    } else if (event_type == EVENT_TYPE_HEARTBEAT) {
        /* Silently consumed; serial_activity was already updated on read */
    } else {
        logger_warnf_with_category("SDK", "Unknown event type: %c", event_type);
    }
}

static void process_mcu_frame(struct millennium_client *client, const mcu_frame_t *frame) {
    char payload[MCU_PROTOCOL_MAX_PAYLOAD + 1];
    char legacy_type = 0;
    if (!client || !frame) return;
    if (frame->type == MCU_MSG_ACK) {
        if (frame->length >= 2 && frame->payload[1] == 0 &&
                client->pending_frame_length > 0 &&
                frame->payload[0] == client->pending_sequence) {
            client->pending_frame_length = 0;
            client->pending_retries = 0;
        } else if (frame->length >= 2 && frame->payload[1] != 0) {
            metrics_increment_counter("mcu_command_busy", 1);
        }
        return;
    }
    if (frame->type == MCU_MSG_HELLO) {
        if (frame->length >= 2 && frame->payload[0] <= MCU_PROTOCOL_VERSION &&
                frame->payload[1] >= MCU_PROTOCOL_VERSION) {
            client->mcu_protocol_ready = 1;
            metrics_set_gauge("mcu_protocol_version", MCU_PROTOCOL_VERSION);
        } else {
            client->serial_healthy = 0;
            logger_error_with_category("SDK", "MCU protocol versions are incompatible");
        }
        return;
    }
    /* Alpha and Beta have independent event sequence spaces, and Alpha frames
     * are transparently forwarded by Beta. Command replay is guarded on Beta;
     * do not apply one combined replay window to the interleaved event stream. */
    if (frame->length > MCU_PROTOCOL_MAX_PAYLOAD) return;
    memcpy(payload, frame->payload, frame->length);
    payload[frame->length] = '\0';
    switch (frame->type) {
    case MCU_EVT_KEY: legacy_type = EVENT_TYPE_KEYPAD; break;
    case MCU_EVT_HOOK: legacy_type = EVENT_TYPE_HOOK; break;
    case MCU_EVT_CARD: legacy_type = EVENT_TYPE_CARD; break;
    case MCU_EVT_COIN: legacy_type = EVENT_TYPE_COIN; break;
    case MCU_EVT_DIAGNOSTIC: legacy_type = EVENT_TYPE_DIAG; break;
    case MCU_EVT_HEARTBEAT: legacy_type = EVENT_TYPE_HEARTBEAT; break;
    case MCU_EVT_OPERATION:
        if (frame->length < 2) return;
        legacy_type = (char)frame->payload[0];
        metrics_set_gauge("mcu_last_operation_transaction", frame->payload[1]);
        if (legacy_type == 'R') {
            metrics_increment_counter("mcu_operations_completed", 1);
            return;
        }
        if (legacy_type == 'X') {
            metrics_increment_counter("mcu_operation_deadlines", 1);
            logger_errorf_with_category("SDK", "MCU operation %u exceeded its deadline",
                                        (unsigned int)frame->payload[1]);
            return;
        }
        memmove(payload, payload + 2, frame->length - 1);
        break;
    default:
        metrics_increment_counter("mcu_unknown_frames", 1);
        return;
    }
    millennium_client_create_and_queue_event_char(client, legacy_type, payload);
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
    size_t message_length;
    if (!message) return;

    logger_debugf_with_category("SDK", "Writing message to display: %s", message);

    /* Clamp once so the framed length and CRC cover exactly the sent bytes. */
    message_length = millennium_display_payload_len(message);
    if (strlen(message) > message_length) {
        logger_warnf_with_category("SDK",
                "Display message truncated from %lu to %d bytes",
                (unsigned long)strlen(message), DISPLAY_MAX_PAYLOAD);
    }

    millennium_client_write_command(client, 0x02, (const uint8_t *)message,
                                    message_length);
}

void millennium_client_write_to_coin_validator(struct millennium_client *client, uint8_t data) {
    logger_debugf_with_category("SDK", "Writing to coin validator: %d", data);

    /* (#239) Remember the gate state so a reconnect can restore it. */
    if (client) {
        client->coin_gate_cmd = millennium_coin_gate_track(client->coin_gate_cmd, data);
    }

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

static int serial_write_all(struct millennium_client *client, const uint8_t *data,
                            size_t length) {
    size_t written = 0;
    struct timespec start;
    struct timespec now;
    if (!client || client->display_fd < 0 || (!data && length)) return -1;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (written < length) {
        ssize_t result = write(client->display_fd, data + written, length - written);
        if (result > 0) {
            written += (size_t)result;
            continue;
        }
        if (result < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            logger_errorf_with_category("SDK", "Serial write failed: %s", strerror(errno));
            return -1;
        }
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - start.tv_sec >= 2) {
            logger_error_with_category("SDK", "Serial write deadline exceeded");
            metrics_increment_counter("mcu_write_deadlines", 1);
            return -1;
        }
        {
            struct timespec pause_time = {0, 200000};
            nanosleep(&pause_time, NULL);
        }
    }
    millennium_client_serial_activity(client);
    return 0;
}

void millennium_client_write_command(struct millennium_client *client, uint8_t command,
                                     const uint8_t *data, size_t data_size) {
    uint8_t type;
    uint8_t frame[MCU_PROTOCOL_MAX_FRAME];
    uint8_t sequence;
    size_t frame_length;
    struct timespec now;
    if (!client) return;
    switch (command) {
    case 0x02: type = MCU_CMD_DISPLAY; break;
    case 0x03: type = MCU_CMD_COIN_CONTROL; break;
    case 0x04: type = MCU_CMD_COIN_PROGRAM; break;
    case 0x05: type = MCU_CMD_COIN_VERIFY; break;
    case 0x06: type = MCU_CMD_KEEPALIVE; break;
    case 0x07: type = MCU_CMD_IDENTITY; break;
    default:
        logger_errorf_with_category("SDK", "Unsupported MCU command: %u", command);
        return;
    }
    /* Calls made during Arduino boot update their desired state, but no
     * command may reach the validator/display until HELLO confirms readiness. */
    if (!client->mcu_protocol_ready) return;
    sequence = client->mcu_tx_sequence++;
    frame_length = mcu_protocol_encode(type, sequence, data, data_size,
                                       frame, sizeof(frame));
    if (!frame_length) {
        logger_error_with_category("SDK", "MCU command payload is invalid or oversized");
        return;
    }
    if (serial_write_all(client, frame, frame_length) != 0) return;
    if (mcu_message_is_critical(type)) {
        memcpy(client->pending_frame, frame, frame_length);
        client->pending_frame_length = frame_length;
        client->pending_sequence = sequence;
        client->pending_retries = 0;
        clock_gettime(CLOCK_MONOTONIC, &now);
        client->pending_sent_at = now;
    }
}
