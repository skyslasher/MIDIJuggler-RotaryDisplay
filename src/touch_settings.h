#pragma once

#include <functional>

#include <Wire.h>

#include "display_ui.h"

class TouchSettings {
 public:
  using ActionCallback = std::function<void(int page, bool tap)>;

  void begin();
  void loop(UiState* state, const ActionCallback& onAction);

 private:
  bool readTouch(int* x, int* y);
  uint8_t readGesture();
};
