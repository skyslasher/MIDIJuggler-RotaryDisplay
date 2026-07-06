#pragma once

class LedRing {
 public:
  void begin();
  void pulse(bool enabled, float beatValue);
  void tick();

 private:
  uint32_t offAtMs_ = 0;
};
