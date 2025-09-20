#include "events.h"
#include "logger.h"
#include <iostream>

std::string CoinEvent::coin_code() const {
  switch (code_) {
  case 0x30:
    return "INVALID_COIN";
  case 0x31:
    return "COIN_1";
  case 0x32:
    return "COIN_2";
  case 0x33:
    return "COIN_3";
  case 0x34:
    return "COIN_4";
  case 0x35:
    return "COIN_5";
  case 0x36:
    return "COIN_6";
  case 0x37:
    return "COIN_7";
  case 0x38:
    return "COIN_8";
  default:
    return "UNKNOWN_COIN";
  }
}

std::string CallStateEvent::repr() const {
  return state_;
}

struct call *CallStateEvent::get_call() const {
  return call_;
}

enum CallStateEvent::State CallStateEvent::get_state() const {
  return state_value_;
}
