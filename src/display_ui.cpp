#include "display_ui.h"

#include <LovyanGFX.hpp>
#if __has_include(<LGFXVirtualCanvas.h>)
#include <LGFXVirtualCanvas.h>
#endif
#include <LGFXScreenBuilder.h>

#include "RotaryUi.h"
#include "backlight.h"
#include "board.h"

namespace {

constexpr uint32_t kBootDurationMs = 2000;
constexpr int kCx = board::kWidth / 2;

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

void drawStatusChip(lgfx::LovyanGFX& lcd, int x, int y, int w, const char* label, const char* value,
                    uint16_t valueColor) {
  lcd.fillRoundRect(x, y, w, 36, 8, kPanel);
  lcd.setFont(&fonts::Font0);
  lcd.setTextDatum(textdatum_t::top_center);
  lcd.setTextColor(TFT_WHITE, kPanel);
  lcd.drawString(label, x + w / 2, y + 4);
  lcd.setTextColor(valueColor, kPanel);
  lcd.drawString(value, x + w / 2, y + 20);
}

void drawFooter(lgfx::LovyanGFX& lcd, const char* message) {
  lcd.setFont(&fonts::Font0);
  lcd.setTextDatum(textdatum_t::bottom_center);
  lcd.setTextColor(TFT_WHITE, kBg);
  lcd.drawString("< swipe >", kCx, board::kHeight - 4);
  if (message && message[0] != '\0') {
    lcd.drawString(message, kCx, board::kHeight - 16);
  }
}

void drawBpmPage(lgfx::LovyanGFX& lcd, const UiState& state, const char* message) {
  lcd.fillScreen(kBg);
  lcd.fillRect(0, 0, board::kWidth, 22, kBg);
  lcd.setFont(&fonts::Font2);
  lcd.setTextDatum(textdatum_t::top_center);
  lcd.setTextColor(TFT_WHITE, kBg);
  lcd.drawString(state.running ? "RUN" : "STOP", kCx, 4);

  constexpr int cardCy = 100;
  constexpr int cardR = 68;
  const uint16_t cardColor = state.running ? kRunning : kPanel;
  lcd.fillCircle(kCx, cardCy, cardR, cardColor);
  lcd.drawCircle(kCx, cardCy, cardR, state.editing ? kAccent : cardColor);

  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(TFT_WHITE, cardColor);
  lcd.setFont(&fonts::Font4);
  char bpmText[8];
  snprintf(bpmText, sizeof(bpmText), "%.0f", state.displayedBpm);
  lcd.drawString(bpmText, kCx, cardCy);
  lcd.setFont(&fonts::Font0);
  lcd.setTextColor(TFT_WHITE, kBg);
  lcd.drawString("BPM", kCx, cardCy + cardR + 6);

  const int chipY = 168;
  const int chipW = 68;
  const int gap = 6;
  const int startX = (board::kWidth - (chipW * 3 + gap * 2)) / 2;
  drawStatusChip(lcd, startX, chipY, chipW, "Klick", state.clickEnabled ? "Ein" : "Aus",
                 state.clickEnabled ? kOn : kOff);
  drawStatusChip(lcd, startX + chipW + gap, chipY, chipW, "Puls", state.pulseEnabled ? "Ein" : "Aus",
                 state.pulseEnabled ? kOn : kOff);
  drawStatusChip(lcd, startX + (chipW + gap) * 2, chipY, chipW, "Intv", intervalLabel(state.clickInterval),
                 kAccent);
  drawFooter(lcd, message);
}

void drawClickPage(lgfx::LovyanGFX& lcd, const UiState& state, const char* message) {
  lcd.fillScreen(kBg);
  lcd.setFont(&fonts::Font2);
  lcd.setTextDatum(textdatum_t::top_center);
  lcd.setTextColor(TFT_WHITE, kBg);
  lcd.drawString("Audio-Klick", kCx, 24);
  lcd.fillRoundRect(24, 72, 192, 96, 12, kPanel);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(TFT_WHITE, kPanel);
  lcd.setFont(&fonts::Font4);
  lcd.drawString(state.clickEnabled ? "Ein" : "Aus", kCx, 120);
  lcd.setFont(&fonts::Font0);
  lcd.setTextColor(state.clickEnabled ? kOn : kOff, kPanel);
  lcd.drawString(state.clickEnabled ? "Aktiv" : "Inaktiv", kCx, 148);
  drawFooter(lcd, message);
}

void drawPulsePage(lgfx::LovyanGFX& lcd, const UiState& state, const char* message) {
  lcd.fillScreen(kBg);
  lcd.setFont(&fonts::Font2);
  lcd.setTextDatum(textdatum_t::top_center);
  lcd.setTextColor(TFT_WHITE, kBg);
  lcd.drawString("Puls", kCx, 24);
  lcd.fillRoundRect(24, 72, 192, 96, 12, kPanel);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(TFT_WHITE, kPanel);
  lcd.setFont(&fonts::Font4);
  lcd.drawString(state.pulseEnabled ? "Ein" : "Aus", kCx, 120);
  lcd.setFont(&fonts::Font0);
  lcd.setTextColor(state.pulseEnabled ? kOn : kOff, kPanel);
  lcd.drawString(state.pulseEnabled ? "LED-Ring aktiv" : "LED-Ring aus", kCx, 148);
  drawFooter(lcd, message);
}

void drawIntervalPage(lgfx::LovyanGFX& lcd, const UiState& state, const char* message) {
  lcd.fillScreen(kBg);
  lcd.setFont(&fonts::Font2);
  lcd.setTextDatum(textdatum_t::top_center);
  lcd.setTextColor(TFT_WHITE, kBg);
  lcd.drawString("Intervall", kCx, 24);
  const uint16_t panelColor = state.editingInterval ? kAccent : kPanel;
  lcd.fillRoundRect(24, 72, 192, 96, 12, panelColor);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(TFT_WHITE, panelColor);
  lcd.setFont(&fonts::Font4);
  lcd.drawString(intervalLabel(state.displayedInterval), kCx, 118);
  if (state.editingInterval) {
    lcd.setFont(&fonts::Font0);
    lcd.drawString("Drehen / Druecken", kCx, 152);
  }
  drawFooter(lcd, message);
}

void drawNetworkPage(lgfx::LovyanGFX& lcd, const UiState& state, const char* message) {
  lcd.fillScreen(kBg);
  lcd.setFont(&fonts::Font2);
  lcd.setTextDatum(textdatum_t::top_center);
  lcd.setTextColor(TFT_WHITE, kBg);
  lcd.drawString("Netzwerk", kCx, 12);
  lcd.fillRoundRect(12, 36, 216, 88, 10, kPanel);
  lcd.setFont(&fonts::Font0);
  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setTextColor(TFT_WHITE, kPanel);
  lcd.drawString("WiFi", 22, 44);
  lcd.drawString(state.wifiSsid[0] ? state.wifiSsid : "-", 22, 58);
  lcd.setTextColor(state.wifiConnected ? kOn : kOff, kPanel);
  lcd.drawString(state.wifiConnected ? "Verbunden" : "Getrennt", 22, 74);
  lcd.fillRoundRect(12, 132, 216, 88, 10, kPanel);
  lcd.setTextColor(TFT_WHITE, kPanel);
  lcd.drawString("OSC", 22, 140);
  lcd.drawString(state.oscHost[0] ? state.oscHost : "-", 22, 154);
  lcd.setTextColor(state.oscConnected ? kOn : kOff, kPanel);
  lcd.drawString(state.oscConnected ? "Sync aktiv" : "Kein Sync", 22, 170);
  drawFooter(lcd, message);
}

}  // namespace

DisplayUi* DisplayUi::instance_ = nullptr;

void DisplayUi::begin() {
  instance_ = this;
  lcd_.init();
  lcd_.initDMA();
  lcd_.startWrite();
  setBacklight(255);

  screen_ = new RotaryUi::Screen(lcd_);
  screen_->begin();
  screen_->setProfile(RotaryUi::Profile::Auto);

  canvas_.setColorDepth(16);
  canvas_.setPsram(true);
  canvas_.createSprite(board::kWidth, board::kHeight);

  bootStartedMs_ = millis();
  showingBoot_ = true;
  ready_ = false;
  renderBoot();
}

void DisplayUi::setMessage(const char* message) {
  strlcpy(message_, message ? message : "", sizeof(message_));
}

void DisplayUi::renderBoot() {
  RotaryUi::Scene::Boot boot;
  boot.title = "MIDIJuggler";
  boot.subtitle = "Rotary Display";
  screen_->show(boot);
}

#ifdef ROTARY_UI_HAS_SCENE_Main
void DisplayUi::renderMainBuilder(const UiState& state) {
  RotaryUi::Scene::Main main;
  snprintf(builderBpm_, sizeof(builderBpm_), "%.0f", state.displayedBpm);
  strlcpy(builderClick_, state.clickEnabled ? "Ein" : "Aus", sizeof(builderClick_));
  strlcpy(builderPulse_, state.pulseEnabled ? "Ein" : "Aus", sizeof(builderPulse_));
  strlcpy(builderInterval_, intervalLabel(state.clickInterval), sizeof(builderInterval_));
  main.bpmtext = builderBpm_;
  main.clicktext = builderClick_;
  main.pulsetext = builderPulse_;
  main.intervaltext = builderInterval_;
  screen_->show(main);
}
#endif

void DisplayUi::renderPage(const UiState& state, bool& usedBuilder) {
  usedBuilder = false;
  switch (static_cast<SettingsPage>(state.settingsPage)) {
    case SettingsPage::Bpm:
#ifdef ROTARY_UI_HAS_SCENE_Main
      renderMainBuilder(state);
      usedBuilder = true;
#else
      drawBpmPage(canvas_, state, message_);
#endif
      break;
    case SettingsPage::Click:
      drawClickPage(canvas_, state, message_);
      break;
    case SettingsPage::Pulse:
      drawPulsePage(canvas_, state, message_);
      break;
    case SettingsPage::Interval:
      drawIntervalPage(canvas_, state, message_);
      break;
    case SettingsPage::Network:
      drawNetworkPage(canvas_, state, message_);
      break;
    default:
      drawBpmPage(canvas_, state, message_);
      break;
  }
}

void DisplayUi::pushFull() {
  if (lcd_.getStartCount() > 0) {
    lcd_.endWrite();
  }
  canvas_.pushSprite(0, 0);
  lcd_.startWrite();
}

bool DisplayUi::stateChanged(const UiState& state) const {
  return !ready_ || lastRendered_.settingsPage != state.settingsPage ||
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
}

bool DisplayUi::shouldRender(const UiState& state) const {
  if (showingBoot_) {
    return millis() - bootStartedMs_ >= kBootDurationMs;
  }
  return stateChanged(state);
}

void DisplayUi::render(const UiState& state) {
  if (showingBoot_) {
    if (millis() - bootStartedMs_ < kBootDurationMs) {
      return;
    }
    showingBoot_ = false;
    ready_ = false;
  }

  if (!stateChanged(state)) {
    return;
  }

  bool usedBuilder = false;
  renderPage(state, usedBuilder);
  if (!usedBuilder) {
    pushFull();
  }
  lastRendered_ = state;
  strlcpy(lastMessage_, message_, sizeof(lastMessage_));
  ready_ = true;
}
