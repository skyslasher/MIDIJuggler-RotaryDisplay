#pragma once

#include "display_panel.h"

struct UiState {
  float displayedBpm = 120.0f;
  float confirmedBpm = 120.0f;
  bool editing = false;
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
  void drawStatus(const UiState& state);
  void drawBpmCard(const UiState& state);
  void drawSettings(const UiState& state);
  void drawFooter();
  void pushFull();

  PanelDisplay lcd_;
  lgfx::LGFX_Sprite canvas_{&lcd_};
  UiState lastRendered_{};
  char message_[48] = "";
  char lastMessage_[48] = "";
  bool ready_ = false;
};
