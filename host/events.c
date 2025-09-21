#include "events.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Helper function to create a string copy */
static char *strdup_safe(const char *src) {
    if (!src) return NULL;
    size_t len = strlen(src);
    char *dst = malloc(len + 1);
    if (dst) {
        memcpy(dst, src, len);
        dst[len] = '\0';
    }
    return dst;
}

/* Keypad event functions */
static void keypad_event_destroy(void *event) {
    free(event);
}

static const char *keypad_event_get_name(void *event) {
    (void)event;
    return "KeypadEvent";
}

static char *keypad_event_get_repr(void *event) {
    keypad_event_t *ke = (keypad_event_t *)event;
    char *repr = malloc(2);
    if (repr) {
        repr[0] = ke->key;
        repr[1] = '\0';
    }
    return repr;
}

keypad_event_t *keypad_event_create(char key) {
    keypad_event_t *event = malloc(sizeof(keypad_event_t));
    if (!event) return NULL;
    
    event->base.type = EVENT_KEYPAD;
    event->base.destroy = keypad_event_destroy;
    event->base.get_name = keypad_event_get_name;
    event->base.get_repr = keypad_event_get_repr;
    event->key = key;
    
    return event;
}

char keypad_event_get_key(keypad_event_t *event) {
    return event ? event->key : 0;
}

/* Card event functions */
static void card_event_destroy(void *event) {
    free(event);
}

static const char *card_event_get_name(void *event) {
    (void)event;
    return "CardEvent";
}

static char *card_event_get_repr(void *event) {
    card_event_t *ce = (card_event_t *)event;
    return strdup_safe(ce->card_number);
}

card_event_t *card_event_create(const char *card_number) {
    card_event_t *event = malloc(sizeof(card_event_t));
    if (!event) return NULL;
    
    event->base.type = EVENT_CARD;
    event->base.destroy = card_event_destroy;
    event->base.get_name = card_event_get_name;
    event->base.get_repr = card_event_get_repr;
    
    if (card_number) {
        strncpy(event->card_number, card_number, sizeof(event->card_number) - 1);
        event->card_number[sizeof(event->card_number) - 1] = '\0';
    } else {
        event->card_number[0] = '\0';
    }
    
    return event;
}

/* Coin event functions */
static void coin_event_destroy(void *event) {
    free(event);
}

static const char *coin_event_get_name(void *event) {
    (void)event;
    return "CoinEvent";
}

static char *coin_event_get_repr(void *event) {
    return coin_event_get_coin_code((coin_event_t *)event);
}

char *coin_event_get_coin_code(coin_event_t *event) {
    if (!event) return NULL;
    
    switch (event->code) {
    case 0x30:
        return strdup_safe("INVALID_COIN");
    case 0x31:
        return strdup_safe("COIN_1");
    case 0x32:
        return strdup_safe("COIN_2");
    case 0x33:
        return strdup_safe("COIN_3");
    case 0x34:
        return strdup_safe("COIN_4");
    case 0x35:
        return strdup_safe("COIN_5");
    case 0x36:
        return strdup_safe("COIN_6");
    case 0x37:
        return strdup_safe("COIN_7");
    case 0x38:
        return strdup_safe("COIN_8");
    default:
        return strdup_safe("UNKNOWN_COIN");
    }
}

coin_event_t *coin_event_create(uint8_t code) {
    coin_event_t *event = malloc(sizeof(coin_event_t));
    if (!event) return NULL;
    
    event->base.type = EVENT_COIN;
    event->base.destroy = coin_event_destroy;
    event->base.get_name = coin_event_get_name;
    event->base.get_repr = coin_event_get_repr;
    event->code = code;
    
    return event;
}

/* Hook state change event functions */
static void hook_state_change_event_destroy(void *event) {
    free(event);
}

static const char *hook_state_change_event_get_name(void *event) {
    (void)event;
    return "HookStateChangeEvent";
}

static char *hook_state_change_event_get_repr(void *event) {
    hook_state_change_event_t *he = (hook_state_change_event_t *)event;
    return strdup_safe(he->state == 'U' ? "Up" : "Down");
}

hook_state_change_event_t *hook_state_change_event_create(char state) {
    hook_state_change_event_t *event = malloc(sizeof(hook_state_change_event_t));
    if (!event) return NULL;
    
    event->base.type = EVENT_HOOK_STATE_CHANGE;
    event->base.destroy = hook_state_change_event_destroy;
    event->base.get_name = hook_state_change_event_get_name;
    event->base.get_repr = hook_state_change_event_get_repr;
    event->state = state;
    
    return event;
}

char hook_state_change_event_get_direction(hook_state_change_event_t *event) {
    return event ? event->state : 0;
}

/* Coin EEPROM upload start event functions */
static void coin_eeprom_upload_start_destroy(void *event) {
    free(event);
}

static const char *coin_eeprom_upload_start_get_name(void *event) {
    (void)event;
    return "CoinEepromUploadStart";
}

static char *coin_eeprom_upload_start_get_repr(void *event) {
    (void)event;
    return strdup_safe("");
}

coin_eeprom_upload_start_t *coin_eeprom_upload_start_create(void) {
    coin_eeprom_upload_start_t *event = malloc(sizeof(coin_eeprom_upload_start_t));
    if (!event) return NULL;
    
    event->base.type = EVENT_COIN_EEPROM_UPLOAD_START;
    event->base.destroy = coin_eeprom_upload_start_destroy;
    event->base.get_name = coin_eeprom_upload_start_get_name;
    event->base.get_repr = coin_eeprom_upload_start_get_repr;
    
    return event;
}

/* Coin EEPROM upload end event functions */
static void coin_eeprom_upload_end_destroy(void *event) {
    free(event);
}

static const char *coin_eeprom_upload_end_get_name(void *event) {
    (void)event;
    return "CoinEepromUploadEnd";
}

static char *coin_eeprom_upload_end_get_repr(void *event) {
    (void)event;
    return strdup_safe("");
}

coin_eeprom_upload_end_t *coin_eeprom_upload_end_create(void) {
    coin_eeprom_upload_end_t *event = malloc(sizeof(coin_eeprom_upload_end_t));
    if (!event) return NULL;
    
    event->base.type = EVENT_COIN_EEPROM_UPLOAD_END;
    event->base.destroy = coin_eeprom_upload_end_destroy;
    event->base.get_name = coin_eeprom_upload_end_get_name;
    event->base.get_repr = coin_eeprom_upload_end_get_repr;
    
    return event;
}

/* Coin EEPROM validation start event functions */
static void coin_eeprom_validation_start_destroy(void *event) {
    free(event);
}

static const char *coin_eeprom_validation_start_get_name(void *event) {
    (void)event;
    return "CoinEepromValidationStart";
}

static char *coin_eeprom_validation_start_get_repr(void *event) {
    (void)event;
    return strdup_safe("");
}

coin_eeprom_validation_start_t *coin_eeprom_validation_start_create(void) {
    coin_eeprom_validation_start_t *event = malloc(sizeof(coin_eeprom_validation_start_t));
    if (!event) return NULL;
    
    event->base.type = EVENT_COIN_EEPROM_VALIDATION_START;
    event->base.destroy = coin_eeprom_validation_start_destroy;
    event->base.get_name = coin_eeprom_validation_start_get_name;
    event->base.get_repr = coin_eeprom_validation_start_get_repr;
    
    return event;
}

/* Coin EEPROM validation end event functions */
static void coin_eeprom_validation_end_destroy(void *event) {
    free(event);
}

static const char *coin_eeprom_validation_end_get_name(void *event) {
    (void)event;
    return "CoinEepromValidationEnd";
}

static char *coin_eeprom_validation_end_get_repr(void *event) {
    (void)event;
    return strdup_safe("");
}

coin_eeprom_validation_end_t *coin_eeprom_validation_end_create(void) {
    coin_eeprom_validation_end_t *event = malloc(sizeof(coin_eeprom_validation_end_t));
    if (!event) return NULL;
    
    event->base.type = EVENT_COIN_EEPROM_VALIDATION_END;
    event->base.destroy = coin_eeprom_validation_end_destroy;
    event->base.get_name = coin_eeprom_validation_end_get_name;
    event->base.get_repr = coin_eeprom_validation_end_get_repr;
    
    return event;
}

/* Coin EEPROM validation error event functions */
static void coin_eeprom_validation_error_destroy(void *event) {
    free(event);
}

static const char *coin_eeprom_validation_error_get_name(void *event) {
    (void)event;
    return "CoinEepromValidationError";
}

static char *coin_eeprom_validation_error_get_repr(void *event) {
    coin_eeprom_validation_error_t *ee = (coin_eeprom_validation_error_t *)event;
    char *repr = malloc(64);
    if (repr) {
        sprintf(repr, "Addr: %d, Expected: %d, Actual: %d", 
                ee->addr, ee->expected, ee->actual);
    }
    return repr;
}

coin_eeprom_validation_error_t *coin_eeprom_validation_error_create(uint8_t addr, uint8_t expected, uint8_t actual) {
    coin_eeprom_validation_error_t *event = malloc(sizeof(coin_eeprom_validation_error_t));
    if (!event) return NULL;
    
    event->base.type = EVENT_COIN_EEPROM_VALIDATION_ERROR;
    event->base.destroy = coin_eeprom_validation_error_destroy;
    event->base.get_name = coin_eeprom_validation_error_get_name;
    event->base.get_repr = coin_eeprom_validation_error_get_repr;
    event->addr = addr;
    event->expected = expected;
    event->actual = actual;
    
    return event;
}

/* Call state event functions */
static void call_state_event_destroy(void *event) {
    free(event);
}

static const char *call_state_event_get_name(void *event) {
    (void)event;
    return "CallStateEvent";
}

static char *call_state_event_get_repr(void *event) {
    call_state_event_t *ce = (call_state_event_t *)event;
    return strdup_safe(ce->state);
}

call_state_event_t *call_state_event_create(const char *state, struct call *call, call_state_t state_value) {
    call_state_event_t *event = malloc(sizeof(call_state_event_t));
    if (!event) return NULL;
    
    event->base.type = EVENT_CALL_STATE;
    event->base.destroy = call_state_event_destroy;
    event->base.get_name = call_state_event_get_name;
    event->base.get_repr = call_state_event_get_repr;
    
    if (state) {
        strncpy(event->state, state, sizeof(event->state) - 1);
        event->state[sizeof(event->state) - 1] = '\0';
    } else {
        event->state[0] = '\0';
    }
    
    event->call = call;
    event->state_value = state_value;
    
    return event;
}

call_state_t call_state_event_get_state(call_state_event_t *event) {
    return event ? event->state_value : EVENT_CALL_STATE_INVALID;
}

struct call *call_state_event_get_call(call_state_event_t *event) {
    return event ? event->call : NULL;
}

/* Generic event functions */
void event_destroy(event_t *event) {
    if (event && event->destroy) {
        event->destroy(event);
    }
}

const char *event_get_name(event_t *event) {
    if (event && event->get_name) {
        return event->get_name(event);
    }
    return "UnknownEvent";
}

char *event_get_repr(event_t *event) {
    if (event && event->get_repr) {
        return event->get_repr(event);
    }
    return strdup_safe("Unknown");
}
