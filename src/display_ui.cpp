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

constexpr int kCx = board::kWidth / 2;

const char* intervalLabel(const char* interval) {
  if (strcmp(interval, "sixteenth") == 0) return "1/16";
  if (strcmp(interval, "eighth") == 0) return "1/8";
  if (strcmp(interval, "half") == 0) return "1/2";
  if (strcmp(interval, "whole") == 0) return "1/1";
  return "1/4";
}

void drawStatusChip(lgfx::LGFX_Sprite* canvas, int x, int y, int w, const char* label,
                    const char* value, uint16_t valueColor) {
  canvas->fillRoundRect(x, y, w, 36, 8, kPanel);
  canvas->setFont(&fonts::Font0);
  canvas->setTextDatum(textdatum_t::top_center);
  canvas->setTextColor(TFT_WHITE, kPanel);
  canvas->drawString(label, x + w / 2, y + 4);
  canvas->setTextColor(valueColor, kPanel);
  canvas->drawString(value, x + w / 2, y + 20);
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

void DisplayUi::drawBpmPage(const UiState& state) {
  canvas_.setFont(&fonts::Font2);
  canvas_.setTextDatum(textdatum_t::top_center);
  canvas_.setTextColor(TFT_WHITE, kBg);
  canvas_.drawString(state.running ? "RUN" : "STOP", kCx, 8);

  constexpr int cardCy = 88;
  constexpr int cardR = 72;
  const uint16_t cardColor = state.running ? kRunning : kPanel;
  canvas_.fillCircle(kCx, cardCy, cardR, cardColor);
  const uint16_t ringColor = state.editing ? kAccent : cardColor;
  canvas_.drawCircle(kCx, cardCy, cardR, ringColor);

  canvas_.setTextDatum(textdatum_t::middle_center);
  canvas_.setTextColor(TFT_WHITE, cardColor);
  canvas_.setFont(&fonts::Font4);
  char bpmText[8];
  snprintf(bpmText, sizeof(bpmText), "%.0f", state.displayedBpm);
  canvas_.drawString(bpmText, kCx, cardCy);

  canvas_.setFont(&fonts::Font0);
  canvas_.setTextColor(TFT_WHITE, kBg);
  canvas_.drawString("BPM", kCx, cardCy + cardR + 6);

  const int chipY = 168;
  const int chipW = 68;
  const int gap = 6;
  const int totalW = chipW * 3 + gap * 2;
  const int startX = (board::kWidth - totalW) / 2;
  drawStatusChip(&canvas_, startX, chipY, chipW, "Klick",
                 state.clickEnabled ? "Ein" : "Aus",
                 state.clickEnabled ? kOn : kOff);
  drawStatusChip(&canvas_, startX + chipW + gap, chipY, chipW, "Puls",
                 state.pulseEnabled ? "Ein" : "Aus",
                 state.pulseEnabled ? kOn : kOff);
  drawStatusChip(&canvas_, startX + (chipW + gap) * 2, chipY, chipW, "Intv",
                 intervalLabel(state.clickInterval), kAccent);
}

void DisplayUi::drawClickPage(const UiState& state) {
  canvas_.setFont(&fonts::Font2);
  canvas_.setTextDatum(textdatum_t::top_center);
  canvas_.setTextColor(TFT_WHITE, kBg);
  canvas_.drawString("Audio-Klick", kCx, 24);

  canvas_.fillRoundRect(24, 72, 192, 96, 12, kPanel);
  canvas_.setTextDatum(textdatum_t::middle_center);
  canvas_.setTextColor(TFT_WHITE, kPanel);
  canvas_.setFont(&fonts::Font4);
  canvas_.drawString(state.clickEnabled ? "Ein" : "Aus", kCx, 120);
  canvas_.setFont(&fonts::Font0);
  canvas_.setTextColor(state.clickEnabled ? kOn : kOff, kPanel);
  canvas_.drawString(state.clickEnabled ? "Aktiv" : "Inaktiv", kCx, 148);
}

void DisplayUi::drawPulsePage(const UiState& state) {
  canvas_.setFont(&fonts::Font2);
  canvas_.setTextDatum(textdatum_t::top_center);
  canvas_.setTextColor(TFT_WHITE, kBg);
  canvas_.drawString("Puls", kCx, 24);

  canvas_.fillRoundRect(24, 72, 192, 96, 12, kPanel);
  canvas_.setTextDatum(textdatum_t::middle_center);
  canvas_.setTextColor(TFT_WHITE, kPanel);
  canvas_.setFont(&fonts::Font4);
  canvas_.drawString(state.pulseEnabled ? "Ein" : "Aus", kCx, 120);
  canvas_.setFont(&fonts::Font0);
  canvas_.setTextColor(state.pulseEnabled ? kOn : kOff, kPanel);
  canvas_.drawString(state.pulseEnabled ? "LED-Ring aktiv" : "LED-Ring aus", kCx, 148);
}

void DisplayUi::drawIntervalPage(const UiState& state) {
  canvas_.setFont(&fonts::Font2);
  canvas_.setTextDatum(textdatum_t::top_center);
  canvas_.setTextColor(TFT_WHITE, kBg);
  canvas_.drawString("Intervall", kCx, 24);

  const uint16_t panelColor = state.editingInterval ? kAccent : kPanel;
  canvas_.fillRoundRect(24, 72, 192, 96, 12, panelColor);
  canvas_.setTextDatum(textdatum_t::middle_center);
  canvas_.setTextColor(TFT_WHITE, panelColor);
  canvas_.setFont(&fonts::Font7);
  canvas_.drawString(intervalLabel(state.displayedInterval), kCx, 118);
  canvas_.setFont(&fonts::Font0);
  if (state.editingInterval) {
    canvas_.drawString("Drehen / Druecken", kCx, 152);
  }
}

void DisplayUi::drawNetworkPage(const UiState& state) {
  canvas_.setFont(&fonts::Font2);
  canvas_.setTextDatum(textdatum_t::top_center);
  canvas_.setTextColor(TFT_WHITE, kBg);
  canvas_.drawString("Netzwerk", kCx, 12);

  canvas_.fillRoundRect(12, 36, 216, 88, 10, kPanel);
  canvas_.setFont(&fonts::Font0);
  canvas_.setTextDatum(textdatum_t::top_left);
  canvas_.setTextColor(TFT_WHITE, kPanel);
  canvas_.drawString("WiFi", 22, 44);
  canvas_.drawString(state.wifiSsid[0] ? state.wifiSsid : "-", 22, 58);
  canvas_.setTextColor(state.wifiConnected ? kOn : kOff, kPanel);
  canvas_.drawString(state.wifiConnected ? "Verbunden" : "Getrennt", 22, 74);

  canvas_.fillRoundRect(12, 132, 216, 88, 10, kPanel);
  canvas_.setTextColor(TFT_WHITE, kPanel);
  canvas_.drawString("OSC", 22, 140);
  canvas_.drawString(state.oscHost[0] ? state.oscHost : "-", 22, 154);
  canvas_.setTextColor(state.oscConnected ? kOn : kOff, kPanel);
  canvas_.drawString(state.oscConnected ? "Sync aktiv" : "Kein Sync", 22, 170);
}

void DisplayUi::drawPage(const UiState& state) {
  canvas_.fillScreen(kBg);
  switch (static_cast<SettingsPage>(state.settingsPage)) {
    case SettingsPage::Bpm:
      drawBpmPage(state);
      break;
    case SettingsPage::Click:
      drawClickPage(state);
      break;
    case SettingsPage::Pulse:
      drawPulsePage(state);
      break;
    case SettingsPage::Interval:
      drawIntervalPage(state);
      break;
    case SettingsPage::Network:
      drawNetworkPage(state);
      break;
    default:
      drawBpmPage(state);
      break;
  }
  drawFooter();
}

void DisplayUi::drawFooter() {
  const int footerY = board::kHeight - 20;
  canvas_.fillRect(0, footerY, board::kWidth, 20, kBg);
  canvas_.setFont(&fonts::Font0);
  canvas_.setTextDatum(textdatum_t::bottom_center);
  canvas_.setTextColor(TFT_WHITE, kBg);
  canvas_.drawString("< swipe >", kCx, board::kHeight - 4);
  if (message_[0] != '\0') {
    canvas_.drawString(message_, kCx, board::kHeight - 16);
  }
}

void DisplayUi::render(const UiState& state) {
  const bool pageChanged = lastRendered_.settingsPage != state.settingsPage;
  const bool changed =
      pageChanged || !ready_ ||
      lastRendered_.displayedBpm != state.displayedBpm ||
      lastRendered_.confirmedBpm != state.confirmedBpm ||
      lastRendered_.editing != state.editing ||
      lastRendered_.editingInterval != state.editingInterval ||
      lastRendered_.running != state.running ||
      lastRendered_.clickEnabled != state.clickEnabled ||
      lastRendered_.pulseEnabled != state.pulseEnabled ||
      strcmp(lastRendered_.clickInterval, state.clickInterval) != 0 ||
      strcmp(lastRendered_.displayedInterval, state.displayedInterval) != 0 ||
      lastRendered_.wifiConnected != state.wifiConnected ||
      strcmp(lastRendered_.wifiSsid, state.wifiSsid) != 0 ||
      strcmp(lastRendered_.oscHost, state.oscHost) != 0 ||
      lastRendered_.oscConnected != state.oscConnected ||
      strcmp(lastMessage_, message_) != 0;

  if (!changed) {
    return;
  }

  drawPage(state);
  pushFull();
  lastRendered_ = state;
  strlcpy(lastMessage_, message_, sizeof(lastMessage_));
  ready_ = true;
}
