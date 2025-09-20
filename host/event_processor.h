#pragma once

#include "events.h"
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>

class EventProcessor {
public:
    EventProcessor();
    void process_event(const std::shared_ptr<Event> &event);
    
    // Register event handler callbacks
    void register_coin_handler(std::function<void(const std::shared_ptr<CoinEvent> &)> handler);
    void register_call_state_handler(std::function<void(const std::shared_ptr<CallStateEvent> &)> handler);
    void register_hook_handler(std::function<void(const std::shared_ptr<HookStateChangeEvent> &)> handler);
    void register_keypad_handler(std::function<void(const std::shared_ptr<KeypadEvent> &)> handler);

private:
    void setup_dispatcher();
    
    // Event handler functions - these will be implemented as callbacks
    // that the daemon can register to handle events
    std::function<void(const std::shared_ptr<CoinEvent> &)> coin_handler_;
    std::function<void(const std::shared_ptr<CallStateEvent> &)> call_state_handler_;
    std::function<void(const std::shared_ptr<HookStateChangeEvent> &)> hook_handler_;
    std::function<void(const std::shared_ptr<KeypadEvent> &)> keypad_handler_;

private:
    std::unordered_map<std::type_index, std::function<void(const std::shared_ptr<Event> &)>> dispatcher;
};
