#include "display_ui.h"

#include "backlight.h"
#include "board.h"

namespace {

constexpr uint16_t kBg = 0x0841;
constexpr uint16_t kPanel = 0x10A2;
constexpr uint16_t kAccent = 0x4CDF;
constexpr uint16_t kRunning = 0x8800;
constexpr uint16_t kOn = 0x47E0;
constexpr uint16_t kOff = 0xFC18;

constexpr int kCardCx = board::kWidth / 2;
constexpr int kCardCy = 78;
constexpr int kCardR = 92;
constexpr int kSettingsY = 168;
constexpr int kSettingsH = 72;
constexpr int kStatusY = 0;
constexpr int kStatusH = 28;

const char* intervalLabel(const char* interval) {
  if (strcmp(interval, "sixteenth") == 0) return "1/16";
  if (strcmp(interval, "eighth") == 0) return "1/8";
  if (strcmp(interval, "half") == 0) return "1/2";
  if (strcmp(interval, "whole") == 0) return "1/1";
  return "1/4";
}

}  // namespace

void DisplayUi::begin() {
  lcd_.init();
  lcd_.initDMA();
  lcd_.startWrite();
  setBacklight(255);

  canvas_.setColorDepth(16);
  canvas_.setPsram(true);
  canvas_.createSprite(board::kWidth, board::kHeight);
  ready_ = false;
}

void DisplayUi::setMessage(const char* message) {
  strlcpy(message_, message ? message : "", sizeof(message_));
}

void DisplayUi::pushFull() {
  if (lcd_.getStartCount() > 0) {
    lcd_.endWrite();
  }
  canvas_.pushSprite(0, 0);
  lcd_.startWrite();
}

void DisplayUi::drawStatus(const UiState& state) {
  canvas_.fillRect(0, kStatusY, board::kWidth, kStatusH, kBg);
  canvas_.setFont(&fonts::Font2);
  canvas_.setTextDatum(textdatum_t::top_center);
  canvas_.setTextColor(TFT_WHITE, kBg);
  canvas_.drawString(state.running ? "RUN" : "STOP", kCardCx, 8);
}

void DisplayUi::drawBpmCard(const UiState& state) {
  const int left = kCardCx - kCardR;
  const int top = kCardCy - kCardR;
  const int size = kCardR * 2;
  canvas_.fillRect(left, top, size, size, kBg);

  const uint16_t cardColor = state.running ? kRunning : kPanel;
  canvas_.fillCircle(kCardCx, kCardCy, kCardR, cardColor);
  const uint16_t ringColor = state.editing ? kAccent : cardColor;
  canvas_.drawCircle(kCardCx, kCardCy, kCardR, ringColor);

  canvas_.setTextDatum(textdatum_t::middle_center);
  canvas_.setTextColor(TFT_WHITE, cardColor);
  canvas_.setFont(&fonts::Font4);
  char bpmText[8];
  snprintf(bpmText, sizeof(bpmText), "%.0f", state.displayedBpm);
  canvas_.drawString(bpmText, kCardCx, kCardCy);
}

void DisplayUi::drawSettings(const UiState& state) {
  const int cx = kCardCx;
  canvas_.fillRect(0, kSettingsY, board::kWidth, kSettingsH, kBg);
  canvas_.fillRoundRect(12, kSettingsY, 216, 56, 10, kPanel);
  canvas_.setTextDatum(textdatum_t::middle_center);
  switch (state.settingsPage) {
    case 0:
      canvas_.setTextColor(TFT_WHITE, kPanel);
      canvas_.drawString("Klick", cx, kSettingsY + 12);
      canvas_.setTextColor(state.clickEnabled ? kOn : kOff, kPanel);
      canvas_.drawString(state.clickEnabled ? "Ein" : "Aus", cx, kSettingsY + 34);
      break;
    case 1:
      canvas_.setTextColor(TFT_WHITE, kPanel);
      canvas_.drawString("Puls", cx, kSettingsY + 12);
      canvas_.setTextColor(state.pulseEnabled ? kOn : kOff, kPanel);
      canvas_.drawString(state.pulseEnabled ? "Ein" : "Aus", cx, kSettingsY + 34);
      break;
    default:
      canvas_.setTextColor(TFT_WHITE, kPanel);
      canvas_.drawString("Intervall", cx, kSettingsY + 12);
      canvas_.drawString(intervalLabel(state.clickInterval), cx, kSettingsY + 34);
      break;
  }
}

void DisplayUi::drawFooter() {
  const int footerY = board::kHeight - 20;
  canvas_.fillRect(0, footerY, board::kWidth, 20, kBg);
  canvas_.setFont(&fonts::Font0);
  canvas_.setTextDatum(textdatum_t::bottom_center);
  canvas_.setTextColor(TFT_WHITE, kBg);
  canvas_.drawString("< swipe >", kCardCx, board::kHeight - 4);
  if (message_[0] != '\0') {
    canvas_.drawString(message_, kCardCx, board::kHeight - 16);
  }
}

void DisplayUi::render(const UiState& state) {
  if (!ready_) {
    canvas_.fillScreen(kBg);
    drawStatus(state);
    drawBpmCard(state);
    drawSettings(state);
    drawFooter();
    pushFull();
    lastRendered_ = state;
    strlcpy(lastMessage_, message_, sizeof(lastMessage_));
    ready_ = true;
    return;
  }

  const bool statusChanged = lastRendered_.running != state.running;
  const bool bpmChanged = lastRendered_.displayedBpm != state.displayedBpm ||
                          lastRendered_.editing != state.editing || statusChanged;
  const bool settingsChanged =
      lastRendered_.settingsPage != state.settingsPage ||
      lastRendered_.clickEnabled != state.clickEnabled ||
      lastRendered_.pulseEnabled != state.pulseEnabled ||
      strcmp(lastRendered_.clickInterval, state.clickInterval) != 0;
  const bool footerChanged = strcmp(lastMessage_, message_) != 0;

  bool changed = false;
  if (statusChanged) {
    drawStatus(state);
    changed = true;
  }
  if (bpmChanged) {
    drawBpmCard(state);
    changed = true;
  }
  if (settingsChanged) {
    drawSettings(state);
    changed = true;
  }
  if (footerChanged) {
    drawFooter();
    changed = true;
  }

  if (changed) {
    pushFull();
  }

  lastRendered_ = state;
  strlcpy(lastMessage_, message_, sizeof(lastMessage_));
}
