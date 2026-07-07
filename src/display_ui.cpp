#include "display_ui.h"

#include <LovyanGFX.hpp>
#if __has_include(<LGFXVirtualCanvas.h>)
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <LGFXVirtualCanvas.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
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
constexpr uint16_t kOn = 0x47E0;
constexpr uint16_t kOff = 0xFC18;

constexpr uint16_t makeRgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t kChipInactiveBg = makeRgb565(29, 34, 51);
constexpr uint16_t kChipActiveBg = makeRgb565(62, 79, 117);
constexpr uint16_t kChipInactiveText = makeRgb565(118, 121, 132);
constexpr uint16_t kChipActiveText = makeRgb565(201, 204, 215);

constexpr int kHomeChipY = 168;
constexpr int kHomeChipW = 68;
constexpr int kHomeChipH = 40;
constexpr int kHomeChipGap = 6;
constexpr int kHomeChipX = (board::kWidth - (kHomeChipW * 3 + kHomeChipGap * 2)) / 2;

const char* intervalLabel(const char* interval) {
  if (strcmp(interval, "sixteenth") == 0) return "1/16";
  if (strcmp(interval, "eighth") == 0) return "1/8";
  if (strcmp(interval, "half") == 0) return "1/2";
  if (strcmp(interval, "whole") == 0) return "1/1";
  return "1/4";
}

template <typename Gfx>
void drawHomeStatusChip(Gfx& g, int x, int y, int w, int h, const char* text, uint16_t bg, uint16_t fg) {
  g.fillRoundRect(x, y, w, h, 4, bg);
  g.setFont(&fonts::Font0);
  g.setTextDatum(textdatum_t::middle_center);
  g.setTextColor(fg, bg);
  g.drawString(text, x + w / 2, y + h / 2);
}

template <typename Gfx>
void drawHomeStatusRow(Gfx& g, const UiState& state) {
  const int x0 = kHomeChipX;
  const int x1 = x0 + kHomeChipW + kHomeChipGap;
  const int x2 = x1 + kHomeChipW + kHomeChipGap;
  const uint16_t clickBg = state.clickEnabled ? kChipActiveBg : kChipInactiveBg;
  const uint16_t clickFg = state.clickEnabled ? kChipActiveText : kChipInactiveText;
  const uint16_t pulseBg = state.pulseEnabled ? kChipActiveBg : kChipInactiveBg;
  const uint16_t pulseFg = state.pulseEnabled ? kChipActiveText : kChipInactiveText;
  drawHomeStatusChip(g, x0, kHomeChipY, kHomeChipW, kHomeChipH, "Klick", clickBg, clickFg);
  drawHomeStatusChip(g, x1, kHomeChipY, kHomeChipW, kHomeChipH, "Puls", pulseBg, pulseFg);
  drawHomeStatusChip(g, x2, kHomeChipY, kHomeChipW, kHomeChipH, intervalLabel(state.clickInterval),
                     kChipInactiveBg, kChipInactiveText);
}

template <typename Gfx>
void drawFooter(Gfx& g, const char* message, uint16_t bg = kBg) {
  g.setFont(&fonts::Font0);
  g.setTextDatum(textdatum_t::bottom_center);
  g.setTextColor(static_cast<uint16_t>(TFT_WHITE), bg);
  g.drawString("< swipe >", kCx, board::kHeight - 4);
  if (message && message[0] != '\0') {
    g.drawString(message, kCx, board::kHeight - 16);
  }
}

void drawBpmPage(lgfx::LovyanGFX& lcd, const UiState& state, const char* message) {
  constexpr uint16_t kHomeBg = makeRgb565(16, 16, 24);
  lcd.fillScreen(kHomeBg);
  lcd.fillRoundRect(10, 44, 220, 108, 8, kChipInactiveBg);
  if (state.editing) {
    lcd.drawRoundRect(10, 44, 220, 108, 8, kAccent);
  }

  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(kChipActiveText, kChipInactiveBg);
  char bpmText[8];
  snprintf(bpmText, sizeof(bpmText), "%.0f", state.displayedBpm);
  lcd.drawString(bpmText, kCx, 96);
  lcd.setFont(&fonts::Font0);
  lcd.setTextColor(kChipInactiveText, kChipInactiveBg);
  lcd.drawString("BPM", kCx, 132);

  drawHomeStatusRow(lcd, state);
  drawFooter(lcd, message, kHomeBg);
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

void homeOverlay(RotaryUi::Canvas& g, const RotaryUi::Scene::Home&) {
  DisplayUi* ui = DisplayUi::instance();
  if (!ui) {
    return;
  }
  const UiState& state = ui->overlayState();
  drawHomeStatusRow(g, state);
  if (state.editing) {
    g.drawRoundRect(10, 44, 220, 108, 8, kAccent);
  }
  constexpr uint16_t kHomeBg = makeRgb565(16, 16, 24);
  drawFooter(g, ui->footerMessage(), kHomeBg);
}

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
#ifdef ROTARY_UI_HAS_SCENE_Home
  screen_->setOverlay(&homeOverlay);
#endif

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

#ifdef ROTARY_UI_HAS_SCENE_Home
void DisplayUi::renderHome(const UiState& state) {
  overlayState_ = state;
  RotaryUi::Scene::Home home;
  snprintf(builderBpm_, sizeof(builderBpm_), "%.0f", state.displayedBpm);
  home.bpmText = builderBpm_;
  screen_->show(home);
}
#endif

void DisplayUi::renderPage(const UiState& state, bool& usedBuilder) {
  usedBuilder = false;
  switch (static_cast<SettingsPage>(state.settingsPage)) {
    case SettingsPage::Bpm:
#ifdef ROTARY_UI_HAS_SCENE_Home
      renderHome(state);
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
