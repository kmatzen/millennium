#include "payphone_state_machine.h"
#include "logger.h"
#include "config.h"
#include <sstream>
#include <iomanip>

PayPhoneStateMachine::PayPhoneStateMachine() 
    : current_state_(State::IDLE_DOWN) {
    state_data_.reset();
    setupStateTransitions();
    setupEventHandlers();
}

void PayPhoneStateMachine::setupStateTransitions() {
    // Define valid state transitions
    valid_transitions_[State::IDLE_DOWN] = {State::IDLE_UP, State::CALL_INCOMING};
    valid_transitions_[State::IDLE_UP] = {State::IDLE_DOWN, State::CALL_ACTIVE};
    valid_transitions_[State::CALL_INCOMING] = {State::CALL_ACTIVE, State::IDLE_DOWN};
    valid_transitions_[State::CALL_ACTIVE] = {State::IDLE_DOWN};
    valid_transitions_[State::INVALID] = {State::IDLE_DOWN};
}

void PayPhoneStateMachine::setupEventHandlers() {
    // Set up event handlers for each state
    auto& idle_down_handlers = event_handlers_[State::IDLE_DOWN];
    idle_down_handlers[typeid(HookStateChangeEvent)] = [this](const std::shared_ptr<Event>& event, StateData& data) {
        auto hook_event = std::dynamic_pointer_cast<HookStateChangeEvent>(event);
        if (hook_event && hook_event->get_direction() == 'U') {
            handleHookEvent(hook_event, data);
        }
    };
    idle_down_handlers[typeid(CallStateEvent)] = [this](const std::shared_ptr<Event>& event, StateData& data) {
        auto call_event = std::dynamic_pointer_cast<CallStateEvent>(event);
        if (call_event && call_event->get_state() == CALL_INCOMING) {
            handleCallStateEvent(call_event, data);
        }
    };
    
    auto& idle_up_handlers = event_handlers_[State::IDLE_UP];
    idle_up_handlers[typeid(HookStateChangeEvent)] = [this](const std::shared_ptr<Event>& event, StateData& data) {
        auto hook_event = std::dynamic_pointer_cast<HookStateChangeEvent>(event);
        if (hook_event && hook_event->get_direction() == 'D') {
            handleHookEvent(hook_event, data);
        }
    };
    idle_up_handlers[typeid(CoinEvent)] = [this](const std::shared_ptr<Event>& event, StateData& data) {
        auto coin_event = std::dynamic_pointer_cast<CoinEvent>(event);
        handleCoinEvent(coin_event, data);
    };
    idle_up_handlers[typeid(KeypadEvent)] = [this](const std::shared_ptr<Event>& event, StateData& data) {
        auto keypad_event = std::dynamic_pointer_cast<KeypadEvent>(event);
        handleKeypadEvent(keypad_event, data);
    };
    
    auto& call_incoming_handlers = event_handlers_[State::CALL_INCOMING];
    call_incoming_handlers[typeid(HookStateChangeEvent)] = [this](const std::shared_ptr<Event>& event, StateData& data) {
        auto hook_event = std::dynamic_pointer_cast<HookStateChangeEvent>(event);
        if (hook_event && hook_event->get_direction() == 'U') {
            handleHookEvent(hook_event, data);
        }
    };
    call_incoming_handlers[typeid(CallStateEvent)] = [this](const std::shared_ptr<Event>& event, StateData& data) {
        auto call_event = std::dynamic_pointer_cast<CallStateEvent>(event);
        if (call_event && call_event->get_state() == CALL_ACTIVE) {
            handleCallStateEvent(call_event, data);
        }
    };
    
    auto& call_active_handlers = event_handlers_[State::CALL_ACTIVE];
    call_active_handlers[typeid(HookStateChangeEvent)] = [this](const std::shared_ptr<Event>& event, StateData& data) {
        auto hook_event = std::dynamic_pointer_cast<HookStateChangeEvent>(event);
        if (hook_event && hook_event->get_direction() == 'D') {
            handleHookEvent(hook_event, data);
        }
    };
    call_active_handlers[typeid(CallStateEvent)] = [this](const std::shared_ptr<Event>& event, StateData& data) {
        auto call_event = std::dynamic_pointer_cast<CallStateEvent>(event);
        handleCallStateEvent(call_event, data);
    };
}

bool PayPhoneStateMachine::transitionTo(State new_state) {
    if (!canTransitionTo(new_state)) {
        MillenniumLogger::getInstance().warn("StateMachine", 
            "Invalid transition from " + getStateName(current_state_) + 
            " to " + getStateName(new_state));
        return false;
    }
    
    State old_state = current_state_;
    onExitState(old_state, state_data_);
    
    current_state_ = new_state;
    state_data_.updateActivity();
    
    // Call state-specific entry logic
    switch (new_state) {
        case State::IDLE_DOWN:
            onEnterIdleDown(state_data_);
            break;
        case State::IDLE_UP:
            onEnterIdleUp(state_data_);
            break;
        case State::CALL_INCOMING:
            onEnterCallIncoming(state_data_);
            break;
        case State::CALL_ACTIVE:
            onEnterCallActive(state_data_);
            break;
        case State::INVALID:
            // No special entry logic for invalid state
            break;
    }
    
    logStateTransition(old_state, new_state);
    
    if (state_transition_callback_) {
        state_transition_callback_(old_state, new_state, state_data_);
    }
    
    return true;
}

bool PayPhoneStateMachine::canTransitionTo(State new_state) const {
    auto it = valid_transitions_.find(current_state_);
    if (it == valid_transitions_.end()) {
        return false;
    }
    
    const auto& valid_states = it->second;
    return std::find(valid_states.begin(), valid_states.end(), new_state) != valid_states.end();
}

void PayPhoneStateMachine::handleEvent(const std::shared_ptr<Event>& event) {
    if (!event) {
        return;
    }
    
    auto state_handlers_it = event_handlers_.find(current_state_);
    if (state_handlers_it == event_handlers_.end()) {
        return;
    }
    
    auto event_handler_it = state_handlers_it->second.find(typeid(*event));
    if (event_handler_it != state_handlers_it->second.end()) {
        event_handler_it->second(event, state_data_);
    }
}

bool PayPhoneStateMachine::isValidState(State state) const {
    return state != State::INVALID;
}

std::string PayPhoneStateMachine::getStateName(State state) const {
    switch (state) {
        case State::INVALID: return "INVALID";
        case State::IDLE_DOWN: return "IDLE_DOWN";
        case State::IDLE_UP: return "IDLE_UP";
        case State::CALL_INCOMING: return "CALL_INCOMING";
        case State::CALL_ACTIVE: return "CALL_ACTIVE";
        default: return "UNKNOWN";
    }
}

std::string PayPhoneStateMachine::getCurrentStateName() const {
    return getStateName(current_state_);
}

void PayPhoneStateMachine::reset() {
    State old_state = current_state_;
    onExitState(old_state, state_data_);
    
    current_state_ = State::IDLE_DOWN;
    state_data_.reset();
    
    onEnterIdleDown(state_data_);
    
    if (state_transition_callback_) {
        state_transition_callback_(old_state, current_state_, state_data_);
    }
}

bool PayPhoneStateMachine::isInCall() const {
    return current_state_ == State::CALL_ACTIVE;
}

bool PayPhoneStateMachine::isRinging() const {
    return current_state_ == State::CALL_INCOMING;
}

bool PayPhoneStateMachine::isIdle() const {
    return current_state_ == State::IDLE_DOWN || current_state_ == State::IDLE_UP;
}

void PayPhoneStateMachine::logStateTransition(State from, State to) {
    MillenniumLogger::getInstance().info("StateMachine", 
        "State transition: " + getStateName(from) + " -> " + getStateName(to));
}

void PayPhoneStateMachine::triggerDisplayUpdate() {
    if (display_update_callback_) {
        display_update_callback_(state_data_);
    }
}

void PayPhoneStateMachine::onEnterIdleDown(StateData& data) {
    data.display_line1 = "Ready";
    data.display_line2 = "Lift handset";
}

void PayPhoneStateMachine::onEnterIdleUp(StateData& data) {
    data.display_line1 = data.getFormattedKeypadBuffer();
    data.display_line2 = "Insert coins";
}

void PayPhoneStateMachine::onEnterCallIncoming(StateData& data) {
    if (!data.caller_number.empty()) {
        data.display_line1 = "Call from: " + data.caller_number;
    } else {
        data.display_line1 = "Call incoming...";
    }
    data.display_line2 = "Lift handset";
}

void PayPhoneStateMachine::onEnterCallActive(StateData& data) {
    data.display_line1 = "Call active";
    data.display_line2 = "Audio connected";
}

void PayPhoneStateMachine::onExitState(State state, StateData& data) {
    // Clean up state-specific data if needed
    switch (state) {
        case State::IDLE_UP:
        case State::CALL_ACTIVE:
            // Clear keypad buffer and coins when exiting these states
            data.keypad_buffer.clear();
            data.inserted_cents = 0;
            break;
        default:
            break;
    }
}

void PayPhoneStateMachine::handleCoinEvent(const std::shared_ptr<CoinEvent>& event, StateData& data) {
    if (!event) return;
    
    const auto& code = event->coin_code();
    int coin_value = 0;
    
    if (code == "COIN_6") {
        coin_value = 5;
    } else if (code == "COIN_7") {
        coin_value = 10;
    } else if (code == "COIN_8") {
        coin_value = 25;
    }
    
    if (coin_value > 0) {
        data.inserted_cents += coin_value;
        data.updateActivity();
        
        // Update display to show coin message
        if (current_state_ == State::IDLE_UP) {
            data.display_line2 = data.getCoinMessage(call_cost_cents_);
            // Trigger immediate display update
            triggerDisplayUpdate();
        }
        
        MillenniumLogger::getInstance().info("StateMachine", 
            "Coin inserted: " + code + ", value: " + std::to_string(coin_value) + 
            " cents, total: " + std::to_string(data.inserted_cents) + " cents");
    }
}

void PayPhoneStateMachine::handleKeypadEvent(const std::shared_ptr<KeypadEvent>& event, StateData& data) {
    if (!event) return;
    
    if (data.keypad_buffer.size() < 10) {
        char key = event->get_key();
        if (std::isdigit(key)) {
            data.keypad_buffer.push_back(key);
            data.updateActivity();
            
            // Update display to show the current number being dialed
            if (current_state_ == State::IDLE_UP) {
                data.display_line1 = data.getFormattedKeypadBuffer();
                MillenniumLogger::getInstance().debug("StateMachine", 
                    "Updated display line1 to: " + data.display_line1);
                // Trigger immediate display update
                triggerDisplayUpdate();
            }
            
            MillenniumLogger::getInstance().debug("StateMachine", 
                "Key pressed: " + std::string(1, key));
        }
    }
}

void PayPhoneStateMachine::handleHookEvent(const std::shared_ptr<HookStateChangeEvent>& event, StateData& data) {
    if (!event) return;
    
    MillenniumLogger::getInstance().debug("StateMachine", 
        "Hook event: direction=" + std::string(1, event->get_direction()) + 
        ", current_state=" + getStateName(current_state_));
    
    if (event->get_direction() == 'U') {
        if (current_state_ == State::CALL_INCOMING) {
            MillenniumLogger::getInstance().info("StateMachine", "Answering incoming call");
            // Answer the call before transitioning state
            if (answer_call_callback_) {
                answer_call_callback_();
            }
            transitionTo(State::CALL_ACTIVE);
        } else if (current_state_ == State::IDLE_DOWN) {
            transitionTo(State::IDLE_UP);
        }
    } else if (event->get_direction() == 'D') {
        if (current_state_ == State::CALL_ACTIVE || current_state_ == State::IDLE_UP) {
            MillenniumLogger::getInstance().info("StateMachine", "Hanging up call");
            // Hangup the call before transitioning state
            if (hangup_call_callback_) {
                hangup_call_callback_();
            }
            transitionTo(State::IDLE_DOWN);
        }
    }
}

void PayPhoneStateMachine::handleCallStateEvent(const std::shared_ptr<CallStateEvent>& event, StateData& data) {
    if (!event) return;
    
    if (event->get_state() == CALL_INCOMING && current_state_ == State::IDLE_DOWN) {
        // Store the caller number before transitioning
        data.caller_number = event->get_caller_number();
        transitionTo(State::CALL_INCOMING);
    } else if (event->get_state() == CALL_ACTIVE && current_state_ == State::CALL_INCOMING) {
        transitionTo(State::CALL_ACTIVE);
    }
}
