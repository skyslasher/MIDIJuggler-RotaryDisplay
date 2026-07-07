#include "touch_settings.h"

#include "board.h"

namespace {

constexpr uint8_t kTouchAddr = 0x15;
constexpr int kSwipeThreshold = 40;

}  // namespace

void TouchSettings::begin() {
  pinMode(board::kTouchInt, INPUT_PULLUP);
  pinMode(board::kTouchRst, OUTPUT);
  digitalWrite(board::kTouchRst, LOW);
  delay(10);
  digitalWrite(board::kTouchRst, HIGH);
  delay(50);
  Wire.begin(board::kTouchSda, board::kTouchScl);
}

bool TouchSettings::readTouch(int* x, int* y) {
  if (digitalRead(board::kTouchInt) != LOW) {
    return false;
  }
  Wire.beginTransmission(kTouchAddr);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(kTouchAddr, static_cast<uint8_t>(4)) != 4) {
    return false;
  }
  const uint8_t xHigh = Wire.read();
  const uint8_t xLow = Wire.read();
  const uint8_t yHigh = Wire.read();
  const uint8_t yLow = Wire.read();
  *x = ((xHigh & 0x0F) << 8) | xLow;
  *y = ((yHigh & 0x0F) << 8) | yLow;
  return true;
}

void TouchSettings::loop(UiState* state, const ActionCallback& onAction) {
  static int startX = 0;
  static int startY = 0;
  static int lastX = 0;
  static int lastY = 0;
  static bool touching = false;
  int x = 0;
  int y = 0;
  if (!readTouch(&x, &y)) {
    if (touching) {
      const int dx = lastX - startX;
      const int dy = lastY - startY;
      if (abs(dx) > kSwipeThreshold && abs(dx) >= abs(dy)) {
        if (dx > 0 && state->settingsPage > 0) {
          state->settingsPage -= 1;
        } else if (dx < 0 && state->settingsPage < 2) {
          state->settingsPage += 1;
        }
      } else if (abs(dx) <= kSwipeThreshold && lastY > 150) {
        onAction(state->settingsPage, true);
      }
    }
    touching = false;
    return;
  }

  lastX = x;
  lastY = y;
  if (!touching) {
    startX = x;
    startY = y;
    touching = true;
  }
}
