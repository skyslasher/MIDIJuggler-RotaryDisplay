#pragma once

#include "display_panel.h"

enum class SettingsPage : uint8_t {
  Bpm = 0,
  Click = 1,
  Pulse = 2,
  Interval = 3,
  Network = 4,
  Count = 5,
};

struct UiState {
  float displayedBpm = 120.0f;
  float confirmedBpm = 120.0f;
  bool editing = false;
  bool editingInterval = false;
  bool running = false;
  bool clickEnabled = false;
  bool pulseEnabled = true;
  int settingsPage = 0;
  char clickInterval[16] = "quarter";
  char displayedInterval[16] = "quarter";
  bool wifiConnected = false;
  char wifiSsid[33] = "";
  char oscHost[48] = "";
  bool oscConnected = false;
};

namespace RotaryUi {
class Screen;
}

class DisplayUi {
 public:
  void begin();
  void render(const UiState& state);
  void setMessage(const char* message);

 private:
  void renderBoot();
  void renderPage(const UiState& state);
  void pushFull();
  bool stateChanged(const UiState& state) const;

  PanelDisplay lcd_;
  lgfx::LGFX_Sprite canvas_{&lcd_};
  RotaryUi::Screen* screen_ = nullptr;
  UiState lastRendered_{};
  char message_[48] = "";
  char lastMessage_[48] = "";
  bool ready_ = false;
  bool showingBoot_ = true;
  uint32_t bootStartedMs_ = 0;
  static DisplayUi* instance_;
};
