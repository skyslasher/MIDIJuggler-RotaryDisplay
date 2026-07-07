#include "display_ui.h"

#include <LovyanGFX.hpp>
#include <LGFXScreenBuilder.h>

#include "RotaryUi.h"
#include "backlight.h"
#include "board.h"

#if defined(ROTARY_UI_REQUIRE_DYNAMIC) && \
    (!defined(ROTARY_UI_HOME_DYNAMIC) || !defined(ROTARY_UI_HOME_PART_COUNT))
#error "Patched include/RotaryUi.h required (ROTARY_UI_HOME_DYNAMIC + ROTARY_UI_HOME_PART_COUNT)."
#endif

#if (defined(ROTARY_UI_HOME_DYNAMIC) && defined(ROTARY_UI_HOME_PART_COUNT)) || defined(ROTARY_UI_HAS_SCENE_Home)
#define ROTARY_UI_USE_BUILDER_HOME 1
#endif

#if defined(ROTARY_UI_NO_DOUBLE_BUFFER)
#pragma message "ROTARY_UI_NO_DOUBLE_BUFFER: settings pages draw to lcd_ (no sprite pushSprite)"
#endif

#if defined(ROTARY_UI_HOME_DYNAMIC) && defined(ROTARY_UI_HOME_PART_COUNT)
#pragma message "Home render path: renderHomeScene (LGFXScreenBuilder direct lcd_)"
#elif defined(ROTARY_UI_HAS_SCENE_Home)
#pragma message "Home render path: show(Home) fallback (patched export missing ROTARY_UI_HOME_DYNAMIC?)"
#endif

namespace {

constexpr uint32_t kBootDurationMs = 2000;
constexpr int kCx = board::kWidth / 2;

constexpr uint16_t kBg = 0x0841;
constexpr uint16_t kPanel = 0x10A2;
constexpr uint16_t kAccent = 0x4CDF;
constexpr uint16_t kOn = 0x47E0;
constexpr uint16_t kOff = 0xFC18;

const char* intervalLabel(const char* interval) {
  if (strcmp(interval, "sixteenth") == 0) return "1/16";
  if (strcmp(interval, "eighth") == 0) return "1/8";
  if (strcmp(interval, "half") == 0) return "1/2";
  if (strcmp(interval, "whole") == 0) return "1/1";
  return "1/4";
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
  lcd.fillScreen(kBg);
  lcd.fillRoundRect(10, 44, 220, 108, 8, kPanel);
  if (state.editing) {
    lcd.drawRoundRect(10, 44, 220, 108, 8, kAccent);
  }

  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(TFT_WHITE, kPanel);
  char bpmText[8];
  snprintf(bpmText, sizeof(bpmText), "%.0f", state.displayedBpm);
  lcd.drawString(bpmText, kCx, 96);
  lcd.setFont(&fonts::Font0);
  lcd.drawString("BPM", kCx, 132);
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

void DisplayUi::begin() {
  lcd_.init();
  lcd_.initDMA();
  lcd_.startWrite();
  setBacklight(255);

  screen_ = new RotaryUi::Screen(lcd_);
  screen_->begin();
  screen_->setProfile(RotaryUi::Profile::Auto);

#if !defined(ROTARY_UI_NO_DOUBLE_BUFFER)
  canvas_.setColorDepth(16);
  canvas_.setPsram(true);
  canvas_.createSprite(board::kWidth, board::kHeight);
#endif

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

#ifdef ROTARY_UI_USE_BUILDER_HOME
#if defined(ROTARY_UI_HOME_DYNAMIC) && defined(ROTARY_UI_HOME_PART_COUNT)

namespace {

void fillHomeValues(lgfxsb::Value* v, const UiState& state, const char* bpmText, const char* intervalText) {
#ifdef ROTARY_UI_HOME_IDX_bpmAccent
  v[ROTARY_UI_HOME_IDX_bpmAccent] = lgfxsb::Value::boolean(state.editing);
#endif
#ifdef ROTARY_UI_HOME_IDX_bpmPanel
  v[ROTARY_UI_HOME_IDX_bpmPanel] = lgfxsb::Value::boolean(true);
#endif
#ifdef ROTARY_UI_HOME_IDX_bpmText
  v[ROTARY_UI_HOME_IDX_bpmText] = lgfxsb::Value::text(bpmText);
#endif
#ifdef ROTARY_UI_HOME_IDX_bpmLabel
  v[ROTARY_UI_HOME_IDX_bpmLabel] = lgfxsb::Value::boolean(true);
#endif
#ifdef ROTARY_UI_HOME_IDX_klickBgInactive
  v[ROTARY_UI_HOME_IDX_klickBgInactive] = lgfxsb::Value::boolean(!state.clickEnabled);
#endif
#ifdef ROTARY_UI_HOME_IDX_klickTextInactive
  v[ROTARY_UI_HOME_IDX_klickTextInactive] = lgfxsb::Value::boolean(!state.clickEnabled);
#endif
#ifdef ROTARY_UI_HOME_IDX_klickBgActive
  v[ROTARY_UI_HOME_IDX_klickBgActive] = lgfxsb::Value::boolean(state.clickEnabled);
#endif
#ifdef ROTARY_UI_HOME_IDX_klickTextActive
  v[ROTARY_UI_HOME_IDX_klickTextActive] = lgfxsb::Value::boolean(state.clickEnabled);
#endif
#ifdef ROTARY_UI_HOME_IDX_pulsBgInactive
  v[ROTARY_UI_HOME_IDX_pulsBgInactive] = lgfxsb::Value::boolean(!state.pulseEnabled);
#endif
#ifdef ROTARY_UI_HOME_IDX_pulsTextInactive
  v[ROTARY_UI_HOME_IDX_pulsTextInactive] = lgfxsb::Value::boolean(!state.pulseEnabled);
#endif
#ifdef ROTARY_UI_HOME_IDX_pulsBgActive
  v[ROTARY_UI_HOME_IDX_pulsBgActive] = lgfxsb::Value::boolean(state.pulseEnabled);
#endif
#ifdef ROTARY_UI_HOME_IDX_pulsTextActive
  v[ROTARY_UI_HOME_IDX_pulsTextActive] = lgfxsb::Value::boolean(state.pulseEnabled);
#endif
#ifdef ROTARY_UI_HOME_IDX_intervalBg
  v[ROTARY_UI_HOME_IDX_intervalBg] = lgfxsb::Value::boolean(true);
#endif
#ifdef ROTARY_UI_HOME_IDX_intervalText
  v[ROTARY_UI_HOME_IDX_intervalText] = lgfxsb::Value::text(intervalText);
#endif
}

}  // namespace
#endif

void DisplayUi::renderHome(const UiState& state) {
  snprintf(builderBpm_, sizeof(builderBpm_), "%.0f", state.displayedBpm);
  strlcpy(builderInterval_, intervalLabel(state.clickInterval), sizeof(builderInterval_));
#if defined(ROTARY_UI_HOME_DYNAMIC) && defined(ROTARY_UI_HOME_PART_COUNT)
  lgfxsb::Value values[ROTARY_UI_HOME_PART_COUNT]{};
  fillHomeValues(values, state, builderBpm_, builderInterval_);
#if defined(ROTARY_TRANSPORT_SERIAL)
  Serial.printf("renderHome: dynamic parts=%u editing=%d click=%d puls=%d\n",
                static_cast<unsigned>(ROTARY_UI_HOME_PART_COUNT), state.editing,
                state.clickEnabled, state.pulseEnabled);
#endif
  screen_->renderHomeScene(values, ROTARY_UI_HOME_PART_COUNT);
#else
#if defined(ROTARY_TRANSPORT_SERIAL)
  Serial.println("renderHome: FALLBACK show(Home)");
#endif
  RotaryUi::Scene::Home home;
  home.bpmText = builderBpm_;
  screen_->show(home);
#endif
#if !defined(ROTARY_UI_NO_DOUBLE_BUFFER)
  // Drop stale manual-page sprite pixels so a later pushFull() cannot resurrect "< swipe >".
  canvas_.fillScreen(kBg);
#endif
}
#endif

void DisplayUi::renderPage(const UiState& state, bool& usedBuilder) {
  usedBuilder = false;
  const auto page = static_cast<SettingsPage>(state.settingsPage);

  if (page == SettingsPage::Bpm) {
#ifdef ROTARY_UI_USE_BUILDER_HOME
    renderHome(state);
    usedBuilder = true;
    return;
#else
#if defined(ROTARY_TRANSPORT_SERIAL)
    Serial.println("renderPage: manual drawBpmPage (no ROTARY_UI_HOME_DYNAMIC)");
#endif
#endif
  }

#if defined(ROTARY_UI_NO_DOUBLE_BUFFER)
  lcd_.waitDMA();
  if (lcd_.getStartCount() > 0) {
    lcd_.endWrite();
  }
  lcd_.startWrite();
  lgfx::LovyanGFX& g = lcd_;
#else
  lgfx::LovyanGFX& g = canvas_;
#endif

  switch (page) {
    case SettingsPage::Bpm:
      drawBpmPage(g, state, message_);
      break;
    case SettingsPage::Click:
      drawClickPage(g, state, message_);
      break;
    case SettingsPage::Pulse:
      drawPulsePage(g, state, message_);
      break;
    case SettingsPage::Interval:
      drawIntervalPage(g, state, message_);
      break;
    case SettingsPage::Network:
      drawNetworkPage(g, state, message_);
      break;
    default:
      drawBpmPage(g, state, message_);
      break;
  }
}

void DisplayUi::pushFull() {
#if defined(ROTARY_UI_NO_DOUBLE_BUFFER)
  return;
#else
  lcd_.waitDMA();
  if (lcd_.getStartCount() > 0) {
    lcd_.endWrite();
  }
  canvas_.pushSprite(0, 0);
  lcd_.startWrite();
#endif
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
