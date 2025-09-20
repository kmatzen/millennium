#include "event_processor.h"
#include <iostream>

EventProcessor::EventProcessor() {
    setup_dispatcher();
}

void EventProcessor::register_coin_handler(std::function<void(const std::shared_ptr<CoinEvent> &)> handler) {
    coin_handler_ = handler;
}

void EventProcessor::register_call_state_handler(std::function<void(const std::shared_ptr<CallStateEvent> &)> handler) {
    call_state_handler_ = handler;
}

void EventProcessor::register_hook_handler(std::function<void(const std::shared_ptr<HookStateChangeEvent> &)> handler) {
    hook_handler_ = handler;
}

void EventProcessor::register_keypad_handler(std::function<void(const std::shared_ptr<KeypadEvent> &)> handler) {
    keypad_handler_ = handler;
}


void EventProcessor::process_event(const std::shared_ptr<Event> &event) {
    auto it = dispatcher.find(typeid(*event));
    if (it != dispatcher.end()) {
        try {
            it->second(event);
        } catch (const std::exception& e) {
            // Log error to stderr since we don't have access to logger
            std::cerr << "EventProcessor error: " << e.what() << std::endl;
        }
    } else {
        // Log warning to stderr since we don't have access to logger
        std::cerr << "EventProcessor: Unhandled event type: " << event->name() << std::endl;
    }
}


void EventProcessor::setup_dispatcher() {
    dispatcher[typeid(CoinEvent)] = [&](const std::shared_ptr<Event> &e) {
        auto coin_event = std::dynamic_pointer_cast<CoinEvent>(e);
        if (coin_handler_) {
            coin_handler_(coin_event);
        }
    };
    
    dispatcher[typeid(CallStateEvent)] = [&](const std::shared_ptr<Event> &e) {
        auto call_state_event = std::dynamic_pointer_cast<CallStateEvent>(e);
        if (call_state_handler_) {
            call_state_handler_(call_state_event);
        }
    };
    
    dispatcher[typeid(HookStateChangeEvent)] = [&](const std::shared_ptr<Event> &e) {
        auto hook_event = std::dynamic_pointer_cast<HookStateChangeEvent>(e);
        if (hook_handler_) {
            hook_handler_(hook_event);
        }
    };
    
    dispatcher[typeid(KeypadEvent)] = [&](const std::shared_ptr<Event> &e) {
        auto keypad_event = std::dynamic_pointer_cast<KeypadEvent>(e);
        if (keypad_handler_) {
            keypad_handler_(keypad_event);
        }
    };
}