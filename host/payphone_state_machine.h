#pragma once

#include "millennium_sdk.h"
#include <memory>
#include <functional>
#include <map>
#include <string>
#include <chrono>
#include <typeindex>
#include <sstream>
#include <iomanip>

class PayPhoneStateMachine {
public:
    enum class State {
        INVALID = 0,
        IDLE_DOWN,
        IDLE_UP,
        CALL_INCOMING,
        CALL_ACTIVE
    };
    
    struct StateData {
        std::vector<char> keypad_buffer;
        int inserted_cents = 0;
        std::chrono::steady_clock::time_point last_activity;
        std::string display_line1;
        std::string display_line2;
        std::string caller_number;
        std::string dialed_number;
        
        void reset() {
            keypad_buffer.clear();
            inserted_cents = 0;
            last_activity = std::chrono::steady_clock::now();
            display_line1.clear();
            display_line2.clear();
            caller_number.clear();
            dialed_number.clear();
        }
        
        void updateActivity() {
            last_activity = std::chrono::steady_clock::now();
        }
        
        std::string getFormattedKeypadBuffer() const {
            std::vector<char> filled(10, '_');
            std::copy(keypad_buffer.begin(), keypad_buffer.end(), filled.begin());
            
            std::stringstream ss;
            ss << "(";
            for (int i = 0; i < 3; ++i)
                ss << filled[i];
            ss << ") ";
            for (int i = 3; i < 6; ++i)
                ss << filled[i];
            ss << "-";
            for (int i = 6; i < 10; ++i)
                ss << filled[i];
            return ss.str();
        }
        
        std::string getCoinMessage(int cost_cents) const {
            std::ostringstream message;
            message << "Insert " << std::setfill('0') << std::setw(2)
                    << (cost_cents - inserted_cents) << " cents";
            return message.str();
        }
    };
    
    using StateTransitionCallback = std::function<void(State from, State to, const StateData& data)>;
    using EventHandler = std::function<void(const std::shared_ptr<Event>& event, StateData& data)>;
    using CallActionCallback = std::function<void()>;
    using DisplayUpdateCallback = std::function<void(const StateData& data)>;
    
    PayPhoneStateMachine();
    ~PayPhoneStateMachine() = default;
    
    // State management
    State getCurrentState() const { return current_state_; }
    const StateData& getStateData() const { return state_data_; }
    StateData& getStateData() { return state_data_; }
    
    // State transitions
    bool transitionTo(State new_state);
    bool canTransitionTo(State new_state) const;
    
    // Event handling
    void handleEvent(const std::shared_ptr<Event>& event);
    
    // Callbacks
    void setStateTransitionCallback(StateTransitionCallback callback) {
        state_transition_callback_ = callback;
    }
    
    void setAnswerCallCallback(CallActionCallback callback) {
        answer_call_callback_ = callback;
    }
    
    void setHangupCallCallback(CallActionCallback callback) {
        hangup_call_callback_ = callback;
    }
    
    void setDisplayUpdateCallback(DisplayUpdateCallback callback) {
        display_update_callback_ = callback;
    }
    
    // Validation
    bool isValidState(State state) const;
    std::string getStateName(State state) const;
    std::string getCurrentStateName() const;
    
    // Utility methods
    void reset();
    bool isInCall() const;
    bool isRinging() const;
    bool isIdle() const;
    
    // Configuration
    void setCallCostCents(int cost_cents) { call_cost_cents_ = cost_cents; }
    
private:
    State current_state_;
    StateData state_data_;
    StateTransitionCallback state_transition_callback_;
    CallActionCallback answer_call_callback_;
    CallActionCallback hangup_call_callback_;
    DisplayUpdateCallback display_update_callback_;
    int call_cost_cents_ = 50; // Default call cost
    
    // Event handlers for each state
    std::map<State, std::map<std::type_index, EventHandler>> event_handlers_;
    
    // State transition rules
    std::map<State, std::vector<State>> valid_transitions_;
    
    void setupStateTransitions();
    void setupEventHandlers();
    void logStateTransition(State from, State to);
    void triggerDisplayUpdate();
    
    // Event handler methods
    void handleCoinEvent(const std::shared_ptr<CoinEvent>& event, StateData& data);
    void handleKeypadEvent(const std::shared_ptr<KeypadEvent>& event, StateData& data);
    void handleHookEvent(const std::shared_ptr<HookStateChangeEvent>& event, StateData& data);
    void handleCallStateEvent(const std::shared_ptr<CallStateEvent>& event, StateData& data);
    
    // State-specific logic
    void onEnterIdleDown(StateData& data);
    void onEnterIdleUp(StateData& data);
    void onEnterCallIncoming(StateData& data);
    void onEnterCallActive(StateData& data);
    void onExitState(State state, StateData& data);
};
