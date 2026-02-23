#include "event_processor.h"
#include "logger.h"
#include <stdlib.h>
#include <stdio.h>

event_processor_t *event_processor_create(void) {
    event_processor_t *processor = malloc(sizeof(event_processor_t));
    if (!processor) {
        return NULL;
    }
    
    /* Initialize all handlers to NULL */
    processor->coin_handler = NULL;
    processor->call_state_handler = NULL;
    processor->hook_handler = NULL;
    processor->keypad_handler = NULL;
    processor->card_handler = NULL;
    
    return processor;
}

void event_processor_destroy(event_processor_t *processor) {
    if (processor) {
        free(processor);
    }
}

void event_processor_process_event(event_processor_t *processor, event_t *event) {
    if (!processor || !event) {
        return;
    }
    
    switch (event->type) {
    case EVENT_COIN:
        if (processor->coin_handler) {
            processor->coin_handler((coin_event_t *)event);
        } else {
            fprintf(stderr, "EventProcessor: No coin handler registered\n");
        }
        break;
        
    case EVENT_CALL_STATE:
        if (processor->call_state_handler) {
            processor->call_state_handler((call_state_event_t *)event);
        } else {
            fprintf(stderr, "EventProcessor: No call state handler registered\n");
        }
        break;
        
    case EVENT_HOOK_STATE_CHANGE:
        if (processor->hook_handler) {
            processor->hook_handler((hook_state_change_event_t *)event);
        } else {
            fprintf(stderr, "EventProcessor: No hook handler registered\n");
        }
        break;
        
    case EVENT_KEYPAD:
        if (processor->keypad_handler) {
            processor->keypad_handler((keypad_event_t *)event);
        } else {
            fprintf(stderr, "EventProcessor: No keypad handler registered\n");
        }
        break;
        
    case EVENT_CARD:
        if (processor->card_handler) {
            processor->card_handler((card_event_t *)event);
        } else {
            fprintf(stderr, "EventProcessor: No card handler registered\n");
        }
        break;

    case EVENT_COIN_EEPROM_UPLOAD_START:
    case EVENT_COIN_EEPROM_UPLOAD_END:
    case EVENT_COIN_EEPROM_VALIDATION_START:
    case EVENT_COIN_EEPROM_VALIDATION_END:
    case EVENT_COIN_EEPROM_VALIDATION_ERROR:
        /* These events don't have specific handlers yet */
        fprintf(stderr, "EventProcessor: Unhandled event type: %d\n", event->type);
        break;
        
    default:
        fprintf(stderr, "EventProcessor: Unknown event type: %d\n", event->type);
        break;
    }
}

void event_processor_register_coin_handler(event_processor_t *processor, coin_handler_func_t handler) {
    if (processor) {
        processor->coin_handler = handler;
    }
}

void event_processor_register_call_state_handler(event_processor_t *processor, call_state_handler_func_t handler) {
    if (processor) {
        processor->call_state_handler = handler;
    }
}

void event_processor_register_hook_handler(event_processor_t *processor, hook_handler_func_t handler) {
    if (processor) {
        processor->hook_handler = handler;
    }
}

void event_processor_register_keypad_handler(event_processor_t *processor, keypad_handler_func_t handler) {
    if (processor) {
        processor->keypad_handler = handler;
    }
}

void event_processor_register_card_handler(event_processor_t *processor, card_handler_func_t handler) {
    if (processor) {
        processor->card_handler = handler;
    }
}
