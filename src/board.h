#pragma once

#include <Arduino.h>

namespace board {

// Must be enabled before display init (Elecrow factory firmware).
constexpr int kPowerPin1 = 1;
constexpr int kPowerPin2 = 2;
constexpr int kPowerLightPin = 40;

constexpr int kDisplaySclk = 10;
constexpr int kDisplayMosi = 11;
constexpr int kDisplayDc = 3;
constexpr int kDisplayCs = 9;
constexpr int kDisplayRst = 14;
constexpr int kDisplayBl = 46;

constexpr int kTouchSda = 6;
constexpr int kTouchScl = 7;
constexpr int kTouchInt = 5;
constexpr int kTouchRst = 13;

constexpr int kLedPin = 48;
constexpr int kLedCount = 5;

constexpr int kEncoderA = 45;
constexpr int kEncoderB = 42;
constexpr int kEncoderSwitch = 41;

constexpr uint16_t kWidth = 240;
constexpr uint16_t kHeight = 240;

inline void powerOn() {
  pinMode(kPowerLightPin, OUTPUT);
  digitalWrite(kPowerLightPin, LOW);
  pinMode(kPowerPin1, OUTPUT);
  digitalWrite(kPowerPin1, HIGH);
  pinMode(kPowerPin2, OUTPUT);
  digitalWrite(kPowerPin2, HIGH);
}

}  // namespace board
