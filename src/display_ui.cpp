#include "display_ui.h"

#include "board.h"

namespace {

constexpr uint16_t kBg = 0x0841;
constexpr uint16_t kPanel = 0x10A2;
constexpr uint16_t kAccent = 0x4CDF;
constexpr uint16_t kRunning = 0x8800;
constexpr uint16_t kOn = 0x47E0;
constexpr uint16_t kOff = 0xFC18;

const char* intervalLabel(const char* interval) {
  if (strcmp(interval, "sixteenth") == 0) return "1/16";
  if (strcmp(interval, "eighth") == 0) return "1/8";
  if (strcmp(interval, "half") == 0) return "1/2";
  if (strcmp(interval, "whole") == 0) return "1/1";
  return "1/4";
}

}  // namespace

void DisplayUi::begin() {
  pinMode(board::kDisplayBl, OUTPUT);
  digitalWrite(board::kDisplayBl, HIGH);
  lcd_.init();
  lcd_.setBrightness(255);
  lcd_.fillScreen(kBg);
}

void DisplayUi::setMessage(const char* message) {
  strlcpy(message_, message ? message : "", sizeof(message_));
}

void DisplayUi::render(const UiState& state) {
  const uint32_t now = millis();
  if (state.editing && now - lastBlinkMs_ > 400) {
    lastBlinkMs_ = now;
    blinkOn_ = !blinkOn_;
  }

  lcd_.fillScreen(kBg);
  const int cardR = 92;
  const int cx = board::kWidth / 2;
  const int cy = 78;
  const uint16_t cardColor = state.running ? kRunning : kPanel;
  lcd_.fillCircle(cx, cy, cardR, cardColor);
  lcd_.drawCircle(cx, cy, cardR, state.editing ? kAccent : cardColor);

  if (!state.editing || blinkOn_) {
    lcd_.setTextDatum(textdatum_t::middle_center);
    lcd_.setTextColor(TFT_WHITE, cardColor);
    lcd_.setFont(&fonts::Font4);
    char bpmText[8];
    snprintf(bpmText, sizeof(bpmText), "%.0f", state.displayedBpm);
    lcd_.drawString(bpmText, cx, cy);
  }

  lcd_.setFont(&fonts::Font2);
  lcd_.setTextDatum(textdatum_t::top_center);
  lcd_.setTextColor(TFT_WHITE, kBg);
  lcd_.drawString(state.running ? "RUN" : "STOP", cx, 8);

  const int settingsY = 168;
  lcd_.fillRoundRect(12, settingsY, 216, 56, 10, kPanel);
  lcd_.setTextDatum(textdatum_t::middle_center);
  switch (state.settingsPage) {
    case 0:
      lcd_.drawString("Klick", cx, settingsY + 12);
      lcd_.setTextColor(state.clickEnabled ? kOn : kOff, kPanel);
      lcd_.drawString(state.clickEnabled ? "Ein" : "Aus", cx, settingsY + 34);
      break;
    case 1:
      lcd_.drawString("Puls", cx, settingsY + 12);
      lcd_.setTextColor(state.pulseEnabled ? kOn : kOff, kPanel);
      lcd_.drawString(state.pulseEnabled ? "Ein" : "Aus", cx, settingsY + 34);
      break;
    default:
      lcd_.setTextColor(TFT_WHITE, kPanel);
      lcd_.drawString("Intervall", cx, settingsY + 12);
      lcd_.drawString(intervalLabel(state.clickInterval), cx, settingsY + 34);
      break;
  }

  lcd_.setTextColor(TFT_WHITE, kBg);
  lcd_.setFont(&fonts::Font0);
  lcd_.setTextDatum(textdatum_t::bottom_center);
  lcd_.drawString("< swipe >", cx, board::kHeight - 4);
  if (message_[0] != '\0') {
    lcd_.drawString(message_, cx, board::kHeight - 16);
  }
}
