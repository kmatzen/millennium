#ifndef MILLENNIUM_SDK_H
#define MILLENNIUM_SDK_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

/* Forward declarations */
struct millennium_client;
struct millennium_logger;

/* Logger level enumeration */
typedef enum {
    LOGGER_VERBOSE = 0,
    LOGGER_DEBUG,
    LOGGER_INFO,
    LOGGER_WARN,
    LOGGER_ERROR
} logger_level_t;

/* Logger structure */
typedef struct millennium_logger {
    logger_level_t current_level;
} millennium_logger_t;

/* Millennium client structure */
typedef struct millennium_client {
    int display_fd;
    int is_open;
    char *input_buffer;
    size_t input_buffer_size;
    size_t input_buffer_capacity;
    
    /* Event queue - simple linked list implementation */
    struct event_queue_node {
        void *event;
        struct event_queue_node *next;
    } *event_queue_head, *event_queue_tail;
    
    /* Thread handle - using pthread for C89 compatibility */
    void *thread_handle;
    
    char *display_message;
    int display_dirty;
    struct timespec last_update_time;
    void *ua; /* struct ua * */
    
    /* Function pointers for event handling */
    void (*create_and_queue_event_char)(struct millennium_client *client, char event_type, const char *payload);
    void (*create_and_queue_event_ptr)(struct millennium_client *client, void *event);

    /* Serial health tracking */
    struct timespec last_serial_activity;
    int serial_healthy;
    int reconnect_attempts;
    struct timespec next_reconnect_time;
    char serial_device_path[256];
} millennium_client_t;

/* Function declarations */

/* Logger functions */
logger_level_t millennium_logger_parse_level(const char *level_str);
void millennium_logger_set_level(logger_level_t level);
void millennium_logger_log(logger_level_t level, const char *message);
logger_level_t millennium_logger_get_current_level(void);

/* Millennium client functions */
struct millennium_client *millennium_client_create(void);
void millennium_client_destroy(struct millennium_client *client);
void millennium_client_close(struct millennium_client *client);

/* Event handling */
void millennium_client_create_and_queue_event_char(struct millennium_client *client, char event_type, const char *payload);
void millennium_client_create_and_queue_event_ptr(struct millennium_client *client, void *event);

/* Display and communication */
void millennium_client_set_display(struct millennium_client *client, const char *message);
void millennium_client_write_to_coin_validator(struct millennium_client *client, uint8_t data);
void millennium_client_update(struct millennium_client *client);
void *millennium_client_next_event(struct millennium_client *client);

/* Call functions */
void millennium_client_call(struct millennium_client *client, const char *number);
void millennium_client_answer_call(struct millennium_client *client);
void millennium_client_hangup(struct millennium_client *client);
void millennium_client_set_ua(struct millennium_client *client, void *ua);

/* Internal functions */
void millennium_client_write_command(struct millennium_client *client, uint8_t command, const uint8_t *data, size_t data_size);
void millennium_client_process_event_buffer(struct millennium_client *client);
char *millennium_client_extract_payload(struct millennium_client *client, char event_type, size_t event_start);
void millennium_client_write_to_display(struct millennium_client *client, const char *message);

/* Serial health and reconnection */
int millennium_client_serial_is_healthy(struct millennium_client *client);
void millennium_client_check_serial(struct millennium_client *client);
void millennium_client_serial_activity(struct millennium_client *client);

/* Utility functions */
void list_audio_devices(void);

/* Constants */
#define BAUD_RATE B9600
#define ASYNC_WORKERS 4
#define SERIAL_WATCHDOG_SECONDS 300
#define SERIAL_MAX_BACKOFF_SECONDS 60

#endif /* MILLENNIUM_SDK_H */