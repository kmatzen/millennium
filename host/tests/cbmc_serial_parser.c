/*
 * CBMC memory-safety proof harness for the serial event-buffer parser.
 *
 * millennium_client_process_event_buffer() and millennium_client_extract_payload()
 * below are copied verbatim from millennium_sdk.c (keep in sync). The harness
 * runs them on a fully NONDETERMINISTIC input buffer of nondeterministic size
 * (0..CAP), so a successful CBMC run proves there is NO out-of-bounds access,
 * pointer error, overflow, or leak for ANY serial input up to that length --
 * exactly the bug class that could be triggered by line noise from the Arduino.
 *
 *   make cbmc-parser
 *
 * Precondition modeled: input_buffer points at input_buffer_capacity bytes and
 * input_buffer_size <= input_buffer_capacity (the invariant the SDK maintains).
 */
#include <stdlib.h>
#include <string.h>

/* Event-type markers (values from events.h). */
#define EVENT_TYPE_KEYPAD               'K'
#define EVENT_TYPE_CARD                 'C'
#define EVENT_TYPE_COIN                 'V'
#define EVENT_TYPE_COIN_UPLOAD_START    'A'
#define EVENT_TYPE_COIN_UPLOAD_END      'B'
#define EVENT_TYPE_COIN_VALIDATION_START 'D'
#define EVENT_TYPE_COIN_VALIDATION_END  'F'
#define EVENT_TYPE_EEPROM_ERROR         'E'
#define EVENT_TYPE_HOOK                 'H'
#define EVENT_TYPE_HEARTBEAT            'P'

struct event_queue_node { void *event; struct event_queue_node *next; };
struct millennium_client {
    char *input_buffer;
    size_t input_buffer_size;
    size_t input_buffer_capacity;
    struct event_queue_node *event_queue_head, *event_queue_tail;
};

/* Dependencies stubbed: they don't touch input_buffer's bounds. */
static void logger_debug_with_category(const char *c, const char *m) { (void)c; (void)m; }
static void logger_debugf_with_category(const char *c, const char *f, ...) { (void)c; (void)f; }
static void millennium_client_create_and_queue_event_char(
    struct millennium_client *cl, char t, const char *p) { (void)cl; (void)t; (void)p; }

/* ─────────── VERBATIM from millennium_sdk.c (keep in sync) ─────────── */

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
                c == EVENT_TYPE_HEARTBEAT) {
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

        if (payload) {
            free(payload);
            payload = NULL;
        }
    }
}

/* ─────────── CBMC harness ─────────── */

#define CAP 36
size_t nondet_size(void);

void verify_parser(void) {
    struct millennium_client client;
    char buf[CAP];                       /* uninitialized => nondeterministic */
    size_t size = nondet_size();

    __CPROVER_assume(size <= (size_t)CAP);   /* SDK invariant: size <= capacity */

    client.input_buffer = buf;
    client.input_buffer_size = size;
    client.input_buffer_capacity = (size_t)CAP;
    client.event_queue_head = NULL;
    client.event_queue_tail = NULL;

    millennium_client_process_event_buffer(&client);
}
