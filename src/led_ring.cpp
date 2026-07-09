#include "led_ring.h"

#include <Adafruit_NeoPixel.h>

#include "board.h"

namespace {

Adafruit_NeoPixel strip(board::kLedCount, board::kLedPin, NEO_GRB + NEO_KHZ800);

}  // namespace

void LedRing::begin() {
  strip.begin();
  strip.setBrightness(80);
  strip.show();
}

void LedRing::pulse(bool enabled, float beatValue) {
  if (!enabled || beatValue <= 0.5f) {
    return;
  }
  for (int i = 0; i < board::kLedCount; ++i) {
    strip.setPixelColor(i, strip.Color(30, 255, 120));
  }
  strip.show();
  offAtMs_ = millis() + 80;
}

void LedRing::tick() {
  if (offAtMs_ != 0 && millis() >= offAtMs_) {
    offAtMs_ = 0;
    for (int i = 0; i < board::kLedCount; ++i) {
      strip.setPixelColor(i, 0);
    }
    strip.show();
  }
}
