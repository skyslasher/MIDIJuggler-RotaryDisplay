#include "touch_settings.h"

#include "board.h"
#include "display_ui.h"

namespace {

constexpr uint8_t kTouchAddr = 0x15;
constexpr int kSwipeThreshold = 35;
constexpr int kPageCount = static_cast<int>(SettingsPage::Count);

constexpr uint8_t kGestureNone = 0x00;
constexpr uint8_t kGestureSlideLeft = 0x03;
constexpr uint8_t kGestureSlideRight = 0x04;
constexpr uint8_t kGestureSingleTap = 0x05;

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

uint8_t TouchSettings::readGesture() {
  uint8_t gesture = kGestureNone;
  i2cRead(0x01, &gesture);
  return gesture;
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
      const uint8_t gesture = readGesture();
      const int dx = lastX - startX;
      const int dy = lastY - startY;

      if (gesture == kGestureSlideRight) {
        state->settingsPage = wrapPage(state->settingsPage, 1);
      } else if (gesture == kGestureSlideLeft) {
        state->settingsPage = wrapPage(state->settingsPage, -1);
      } else if (abs(dx) > kSwipeThreshold && abs(dx) >= abs(dy)) {
        if (dx > 0) {
          state->settingsPage = wrapPage(state->settingsPage, 1);
        } else {
          state->settingsPage = wrapPage(state->settingsPage, -1);
        }
      } else if ((gesture == kGestureSingleTap ||
                  (abs(dx) <= kSwipeThreshold && abs(dy) <= kSwipeThreshold)) &&
                 state->settingsPage == static_cast<int>(SettingsPage::Bpm)) {
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
