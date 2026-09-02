#include "sip_call_slot.h"

void sip_call_slot_set(sip_call_slot_t *slot, int call_id) {
    if (!slot) return;
    pthread_mutex_lock(&slot->mutex);
    slot->call_id = call_id;
    pthread_mutex_unlock(&slot->mutex);
}

int sip_call_slot_snapshot(sip_call_slot_t *slot) {
    int call_id;
    if (!slot) return -1;
    pthread_mutex_lock(&slot->mutex);
    call_id = slot->call_id;
    pthread_mutex_unlock(&slot->mutex);
    return call_id;
}

int sip_call_slot_clear_if(sip_call_slot_t *slot, int expected, int invalid_id) {
    int cleared = 0;
    if (!slot) return 0;
    pthread_mutex_lock(&slot->mutex);
    if (slot->call_id == expected) {
        slot->call_id = invalid_id;
        cleared = 1;
    }
    pthread_mutex_unlock(&slot->mutex);
    return cleared;
}
