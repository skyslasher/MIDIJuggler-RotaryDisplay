#include <Arduino.h>

#include "board.h"
#include "config_store.h"
#include "display_ui.h"
#include "encoder_fsm.h"
#include "led_ring.h"
#include "touch_settings.h"
#include "transport.h"

namespace {

DisplayUi gDisplay;
EncoderFsm gEncoder;
LedRing gLeds;
TouchSettings gTouch;
Transport gTransport;
ConfigStore gConfigStore;
DeviceConfig gConfig;
UiState gUi;
bool gRenderDirty = true;

void markDirty() { gRenderDirty = true; }

void updateNetworkStatus() {
  const bool wifiConnected = gTransport.isWifiConnected();
  const char* ssid = gTransport.wifiSsid();
  const char* host = gTransport.oscHost();
  const bool oscConnected = gTransport.isOscConnected();
  if (gUi.wifiConnected != wifiConnected || strcmp(gUi.wifiSsid, ssid) != 0 ||
      strcmp(gUi.oscHost, host) != 0 || gUi.oscConnected != oscConnected) {
    gUi.wifiConnected = wifiConnected;
    strlcpy(gUi.wifiSsid, ssid, sizeof(gUi.wifiSsid));
    strlcpy(gUi.oscHost, host, sizeof(gUi.oscHost));
    gUi.oscConnected = oscConnected;
    markDirty();
  }
}

void applySync(const SyncPayload& payload) {
  if (!gEncoder.isEditing() && !gEncoder.shouldRejectSyncBpm(payload.bpm)) {
    if (gUi.displayedBpm != payload.bpm || gUi.confirmedBpm != payload.bpm) {
      markDirty();
    }
    gUi.confirmedBpm = payload.bpm;
    gUi.displayedBpm = payload.bpm;
    gEncoder.onSyncBpm(payload.bpm);
  }
  if (!gEncoder.isEditingInterval()) {
    if (strcmp(gUi.clickInterval, payload.clickInterval) != 0 ||
        strcmp(gUi.displayedInterval, payload.clickInterval) != 0) {
      markDirty();
    }
    strlcpy(gUi.clickInterval, payload.clickInterval, sizeof(gUi.clickInterval));
    strlcpy(gUi.displayedInterval, payload.clickInterval, sizeof(gUi.displayedInterval));
    gEncoder.onSyncInterval(payload.clickInterval);
  }
  if (gUi.running != payload.running || gUi.clickEnabled != payload.clickEnabled) {
    markDirty();
  }
  gUi.running = payload.running;
  gUi.clickEnabled = payload.clickEnabled;
}

void onBeat(float beat) {
  gLeds.pulse(gUi.pulseEnabled, beat);
}

void handleTouchAction(int page, bool tap) {
  if (!tap || page != static_cast<int>(SettingsPage::Bpm)) {
    return;
  }
  gTransport.sendTapTempo();
}

void cancelIntervalEditOnPageChange(int previousPage) {
  if (previousPage != static_cast<int>(SettingsPage::Interval) ||
      gUi.settingsPage == previousPage) {
    return;
  }
  if (gEncoder.isEditingInterval()) {
    gEncoder.cancelIntervalEdit();
    strlcpy(gUi.displayedInterval, gUi.clickInterval, sizeof(gUi.displayedInterval));
    markDirty();
  }
}

}  // namespace

void setup() {
  board::powerOn();
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("MIDIJuggler-RotaryDisplay boot");

  gConfigStore.begin();
  gConfigStore.load(&gConfig);
  gUi.pulseEnabled = gConfig.pulseEnabled;
  gUi.displayedBpm = 120.0f;
  gUi.confirmedBpm = 120.0f;
  strlcpy(gUi.clickInterval, "quarter", sizeof(gUi.clickInterval));
  strlcpy(gUi.displayedInterval, "quarter", sizeof(gUi.displayedInterval));
  strlcpy(gUi.oscHost, gConfig.host, sizeof(gUi.oscHost));

  Serial.println("init display");
  gDisplay.begin();
  Serial.println("display init done");
  gDisplay.render(gUi);
  Serial.println("init encoder");
  gEncoder.begin(40.0f, 240.0f, gConfig.bpmStep);
  Serial.println("init leds");
  gLeds.begin();
  Serial.println("init touch");
  gTouch.begin();
  Serial.println("init transport");
  gTransport.setConfigStore(&gConfigStore, &gConfig);
  gTransport.begin(gConfig, applySync, onBeat);
  updateNetworkStatus();
  gRenderDirty = true;
  Serial.println("ready");
}

void loop() {
  const int previousPage = gUi.settingsPage;

  const EncoderFsm::Result encoder = gEncoder.update(gUi.settingsPage);
  gUi.editing = gEncoder.isEditing();
  gUi.editingInterval = gEncoder.isEditingInterval();

  if (encoder.bpmChanged) {
    gUi.displayedBpm = encoder.newBpm;
    markDirty();
  }
  if (encoder.cancelEdit) {
    gUi.displayedBpm = encoder.newBpm;
    markDirty();
  }
  if (encoder.confirmEdit) {
    gUi.displayedBpm = encoder.newBpm;
    gUi.confirmedBpm = encoder.newBpm;
    gEncoder.confirmLocalBpm(encoder.newBpm);
    gTransport.sendBpm(encoder.newBpm);
    markDirty();
  }
  if (encoder.toggleTransport) {
    gTransport.sendStartStop();
  }
  if (encoder.toggleClick) {
    gTransport.sendClickToggle();
  }
  if (encoder.togglePulse) {
    gUi.pulseEnabled = !gUi.pulseEnabled;
    gConfig.pulseEnabled = gUi.pulseEnabled;
    gConfigStore.save(gConfig);
    markDirty();
  }
  if (encoder.intervalChanged) {
    strlcpy(gUi.displayedInterval, encoder.newInterval, sizeof(gUi.displayedInterval));
    markDirty();
  }
  if (encoder.cancelInterval) {
    strlcpy(gUi.displayedInterval, encoder.newInterval, sizeof(gUi.displayedInterval));
    markDirty();
  }
  if (encoder.confirmInterval) {
    strlcpy(gUi.displayedInterval, encoder.newInterval, sizeof(gUi.displayedInterval));
    strlcpy(gUi.clickInterval, encoder.newInterval, sizeof(gUi.clickInterval));
    gTransport.sendInterval(encoder.newInterval);
    markDirty();
  }

  gTouch.loop(&gUi, handleTouchAction);
  if (gUi.settingsPage != previousPage) {
    cancelIntervalEditOnPageChange(previousPage);
    gUi.editingInterval = gEncoder.isEditingInterval();
    markDirty();
  }

  if (gRenderDirty || gDisplay.shouldRender(gUi)) {
    gDisplay.render(gUi);
    gRenderDirty = false;
  }

  gTransport.loop();
  updateNetworkStatus();

  gLeds.tick();
  delay(4);
}
