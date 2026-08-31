#ifndef SIP_CALL_SLOT_H
#define SIP_CALL_SLOT_H

#include <pthread.h>

typedef struct {
    pthread_mutex_t mutex;
    int call_id;
} sip_call_slot_t;

#define SIP_CALL_SLOT_INITIALIZER(initial_id) { PTHREAD_MUTEX_INITIALIZER, (initial_id) }

void sip_call_slot_set(sip_call_slot_t *slot, int call_id);
int sip_call_slot_snapshot(sip_call_slot_t *slot);
int sip_call_slot_clear_if(sip_call_slot_t *slot, int expected, int invalid_id);

#endif
