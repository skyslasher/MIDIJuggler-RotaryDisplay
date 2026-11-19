#include "touch_settings.h"

#include <Arduino.h>

#include "board.h"
#include "display_ui.h"

namespace {

constexpr uint8_t kTouchAddr = 0x15;
// 240px panel. The CST816 reports slide gestures for small finger shifts, so a
// press must travel a large fraction of the screen before it is a swipe.
constexpr int kSwipeThreshold = 80;
constexpr uint32_t kSwipeMinDurationMs = 80;
constexpr uint32_t kReleaseDebounceMs = 30;
constexpr int kPageCount = static_cast<int>(SettingsPage::Count);

int wrapPage(int page, int delta) {
  return (page + delta + kPageCount) % kPageCount;
}

bool i2cWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kTouchAddr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool i2cRead(uint8_t reg, uint8_t* value) {
  Wire.beginTransmission(kTouchAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(kTouchAddr, static_cast<uint8_t>(1)) != 1) {
    return false;
  }
  *value = Wire.read();
  return true;
}

}  // namespace

void TouchSettings::begin() {
  pinMode(board::kTouchInt, INPUT_PULLUP);
  pinMode(board::kTouchRst, OUTPUT);
  digitalWrite(board::kTouchRst, LOW);
  delay(10);
  digitalWrite(board::kTouchRst, HIGH);
  delay(50);
  Wire.begin(board::kTouchSda, board::kTouchScl);
  Wire.setClock(400000);
  i2cWrite(0xFE, 0xFF);
}

bool TouchSettings::readTouch(int* x, int* y) {
  uint8_t fingers = 0;
  if (!i2cRead(0x02, &fingers) || fingers == 0) {
    return false;
  }

  Wire.beginTransmission(kTouchAddr);
  Wire.write(0x03);
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
  static uint32_t touchStartedMs = 0;
  static uint32_t lostContactMs = 0;
  static bool touching = false;
  int x = 0;
  int y = 0;

  if (!readTouch(&x, &y)) {
    if (!touching) {
      return;
    }
    const uint32_t now = millis();
    if (lostContactMs == 0) {
      lostContactMs = now;
      return;
    }
    if (now - lostContactMs < kReleaseDebounceMs) {
      return;
    }

    const int dx = lastX - startX;
    const int dy = lastY - startY;
    const int travelX = abs(dx);
    const int travelY = abs(dy);
    const uint32_t elapsed = now - touchStartedMs;
    // The CST816 slide gesture fires on a small finger shift during a press.
    // Page changes follow measured travel only.
    const bool swipe = travelX >= kSwipeThreshold && travelX >= travelY &&
                       elapsed >= kSwipeMinDurationMs;
    if (swipe) {
      state->settingsPage = wrapPage(state->settingsPage, dx > 0 ? 1 : -1);
    } else if (travelX < kSwipeThreshold && travelY < kSwipeThreshold &&
               state->settingsPage == static_cast<int>(SettingsPage::Bpm)) {
      onAction(state->settingsPage, true);
    }
    touching = false;
    lostContactMs = 0;
    return;
  }

  lostContactMs = 0;
  lastX = x;
  lastY = y;
  if (!touching) {
    startX = x;
    startY = y;
    touchStartedMs = millis();
    touching = true;
  }
}
