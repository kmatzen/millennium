# Event Ordering and Idempotency (#100)

Events are processed asynchronously: the Baresip thread queues call-state events to the main loop, while hook/coin/keypad events come from the serial/Arduino. Order is not guaranteed when multiple events occur close together.

## Hook + Call State Race

**Scenario**: User hangs up (hook_down) at roughly the same time the remote party hangs up (CALL_CLOSED → EVENT_CALL_STATE_INVALID).

### Order 1: Hook first, then INVALID

1. `handle_hook_event(hook_down)`: Transitions to IDLE_DOWN, clears keypad/coins, calls `millennium_client_hangup`, sends coin-validator commands.
2. `handle_call_state_event(INVALID)`: `current_state` is already IDLE_DOWN. Condition `current_state == CALL_ACTIVE || current_state == CALL_INCOMING` is false → **no action**. Baresip tolerates a second hangup if one was already sent.

### Order 2: INVALID first, then hook

1. `handle_call_state_event(INVALID)`: Transitions to IDLE_UP, clears keypad/coins, calls `millennium_client_hangup`, sends coin-validator commands.
2. `handle_hook_event(hook_down)`: Transitions to IDLE_DOWN again, clears keypad/coins (already clear), sends hangup and coin-validator commands again. **Idempotent** — redundant but safe.

### Idempotency

- **INVALID handler**: Only acts when `current_state` is CALL_ACTIVE or CALL_INCOMING. If already IDLE (from prior hook_down), does nothing.
- **Hook-down handler**: Always transitions to IDLE_DOWN and clears state. Safe to run multiple times.
- **Double-hangup**: Baresip tolerates. Coin-validator commands (`c`, `z`) sent twice are assumed harmless.

## Testing

Scenario tests `test_remote_hangup.scenario` and `test_incoming_handset_up.scenario` cover call-state transitions. Interleaved hook + call_ended could be added for more comprehensive race testing.
