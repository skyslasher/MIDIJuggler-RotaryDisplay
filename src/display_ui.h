#pragma once

#include "display_panel.h"

struct UiState {
  float displayedBpm = 120.0f;
  float confirmedBpm = 120.0f;
  bool editing = false;
  bool blinkOn = true;
  bool running = false;
  bool clickEnabled = false;
  bool pulseEnabled = true;
  int settingsPage = 0;
  const char* clickInterval = "quarter";
};

class DisplayUi {
 public:
  void begin();
  void render(const UiState& state);
  void setMessage(const char* message);

 private:
  PanelDisplay lcd_;
  char message_[48] = "";
  uint32_t lastBlinkMs_ = 0;
  bool blinkOn_ = true;
};
