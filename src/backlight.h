#pragma once

#include <Arduino.h>

#include "board.h"

inline void setBacklight(uint8_t brightness) {
  static bool configured = false;
  constexpr int kChannel = 0;
  if (!configured) {
    ledcSetup(kChannel, 5000, 8);
    ledcAttachPin(board::kDisplayBl, kChannel);
    configured = true;
  }
  ledcWrite(kChannel, brightness);
}
