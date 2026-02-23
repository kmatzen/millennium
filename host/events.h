#ifndef EVENTS_H
#define EVENTS_H

/* clang-format on */
#include <stdint.h>
#include <stddef.h>

struct call;

/* Event type constants */
#define EVENT_TYPE_KEYPAD 'K'
#define EVENT_TYPE_CARD 'C'
#define EVENT_TYPE_COIN 'V'
#define EVENT_TYPE_COIN_UPLOAD_START 'A'
#define EVENT_TYPE_COIN_UPLOAD_END 'B'
#define EVENT_TYPE_COIN_VALIDATION_START 'D'
#define EVENT_TYPE_COIN_VALIDATION_END 'F'
#define EVENT_TYPE_EEPROM_ERROR 'E'
#define EVENT_TYPE_HOOK 'H'
#define EVENT_TYPE_HEARTBEAT 'P'
#define EVENT_TYPE_CALL_STATE '1'

/* Event type enumeration */
typedef enum {
    EVENT_KEYPAD = 0,
    EVENT_CARD,
    EVENT_COIN,
    EVENT_HOOK_STATE_CHANGE,
    EVENT_COIN_EEPROM_UPLOAD_START,
    EVENT_COIN_EEPROM_UPLOAD_END,
    EVENT_COIN_EEPROM_VALIDATION_START,
    EVENT_COIN_EEPROM_VALIDATION_END,
    EVENT_COIN_EEPROM_VALIDATION_ERROR,
    EVENT_CALL_STATE
} event_type_t;

/* Call state enumeration */
typedef enum {
    EVENT_CALL_STATE_INVALID = 0,
    EVENT_CALL_STATE_INCOMING,
    EVENT_CALL_STATE_ACTIVE
} call_state_t;

/* Base event structure */
typedef struct {
    event_type_t type;
    void (*destroy)(void *event);
    const char *(*get_name)(void *event);
    char *(*get_repr)(void *event);
} event_t;

/* Keypad event structure */
typedef struct {
    event_t base;
    char key;
} keypad_event_t;

/* Card event structure */
typedef struct {
    event_t base;
    char card_number[17]; /* 16 chars + null terminator */
} card_event_t;

/* Coin event structure */
typedef struct {
    event_t base;
    uint8_t code;
} coin_event_t;

/* Hook state change event structure */
typedef struct {
    event_t base;
    char state;
} hook_state_change_event_t;

/* Coin EEPROM upload start event structure */
typedef struct {
    event_t base;
} coin_eeprom_upload_start_t;

/* Coin EEPROM upload end event structure */
typedef struct {
    event_t base;
} coin_eeprom_upload_end_t;

/* Coin EEPROM validation start event structure */
typedef struct {
    event_t base;
} coin_eeprom_validation_start_t;

/* Coin EEPROM validation end event structure */
typedef struct {
    event_t base;
} coin_eeprom_validation_end_t;

/* Coin EEPROM validation error event structure */
typedef struct {
    event_t base;
    uint8_t addr;
    uint8_t expected;
    uint8_t actual;
} coin_eeprom_validation_error_t;

/* Call state event structure */
typedef struct {
    event_t base;
    char state[32]; /* State string */
    struct call *call;
    call_state_t state_value;
} call_state_event_t;

/* Function declarations for event creation */
keypad_event_t *keypad_event_create(char key);
card_event_t *card_event_create(const char *card_number);
coin_event_t *coin_event_create(uint8_t code);
hook_state_change_event_t *hook_state_change_event_create(char state);
coin_eeprom_upload_start_t *coin_eeprom_upload_start_create(void);
coin_eeprom_upload_end_t *coin_eeprom_upload_end_create(void);
coin_eeprom_validation_start_t *coin_eeprom_validation_start_create(void);
coin_eeprom_validation_end_t *coin_eeprom_validation_end_create(void);
coin_eeprom_validation_error_t *coin_eeprom_validation_error_create(uint8_t addr, uint8_t expected, uint8_t actual);
call_state_event_t *call_state_event_create(const char *state, struct call *call, call_state_t state_value);

/* Function declarations for event operations */
void event_destroy(event_t *event);
const char *event_get_name(event_t *event);
char *event_get_repr(event_t *event);

/* Specific getter functions */
char keypad_event_get_key(keypad_event_t *event);
char hook_state_change_event_get_direction(hook_state_change_event_t *event);
call_state_t call_state_event_get_state(call_state_event_t *event);
struct call *call_state_event_get_call(call_state_event_t *event);

/* Coin event specific function */
char *coin_event_get_coin_code(coin_event_t *event);

#endif /* EVENTS_H */
