#pragma once

#include <Arduino.h>

class LedRing {
 public:
  void begin();
  void setBeatColor(uint8_t r, uint8_t g, uint8_t b);
  void pulse(bool enabled, float beatValue);
  void tick();

 private:
  uint8_t beatR_ = 30;
  uint8_t beatG_ = 255;
  uint8_t beatB_ = 120;
  uint32_t offAtMs_ = 0;
};
