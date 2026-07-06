#include <Arduino.h>

#include "config_store.h"
#include "display_ui.h"
#include "encoder_fsm.h"
#include "led_ring.h"
#include "touch_settings.h"
#include "transport.h"

namespace {

constexpr const char* kIntervals[] = {"sixteenth", "eighth", "quarter", "half", "whole"};
constexpr size_t kIntervalCount = sizeof(kIntervals) / sizeof(kIntervals[0]);

DisplayUi gDisplay;
EncoderFsm gEncoder;
LedRing gLeds;
TouchSettings gTouch;
Transport gTransport;
ConfigStore gConfigStore;
DeviceConfig gConfig;
UiState gUi;

const char* nextInterval(const char* current) {
  size_t index = 2;
  for (size_t i = 0; i < kIntervalCount; ++i) {
    if (strcmp(current, kIntervals[i]) == 0) {
      index = i;
      break;
    }
  }
  return kIntervals[(index + 1) % kIntervalCount];
}

void applySync(const SyncPayload& payload) {
  gUi.confirmedBpm = payload.bpm;
  if (!gUi.editing) {
    gUi.displayedBpm = payload.bpm;
  }
  gUi.running = payload.running;
  gUi.clickEnabled = payload.clickEnabled;
  gUi.clickInterval = payload.clickInterval;
  gEncoder.onSyncBpm(payload.bpm);
}

void onBeat(float beat) {
  gLeds.pulse(gUi.pulseEnabled, beat);
}

void handleSettingsAction(int page, bool tap) {
  if (!tap) {
    return;
  }
  switch (page) {
    case 0:
      gTransport.sendClickToggle();
      break;
    case 1:
      gUi.pulseEnabled = !gUi.pulseEnabled;
      gConfig.pulseEnabled = gUi.pulseEnabled;
      gConfigStore.save(gConfig);
      break;
    default:
      gUi.clickInterval = nextInterval(gUi.clickInterval);
      gTransport.sendInterval(gUi.clickInterval);
      break;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  gConfigStore.begin();
  gConfigStore.load(&gConfig);
  gUi.pulseEnabled = gConfig.pulseEnabled;
  gUi.displayedBpm = 120.0f;
  gUi.confirmedBpm = 120.0f;
  gUi.clickInterval = "quarter";

  gDisplay.begin();
  gEncoder.begin(40.0f, 240.0f, gConfig.bpmStep);
  gLeds.begin();
  gTouch.begin();
  gTransport.begin(
      gConfig,
      applySync,
      onBeat);
  gTransport.setConfigHandler([](const char* line) {
    gConfigStore.applySerialCommand(line, &gConfig);
  });
  gDisplay.render(gUi);
}

void loop() {
  gTransport.loop();
  gTouch.loop(&gUi, handleSettingsAction);

  const EncoderFsm::Result encoder = gEncoder.update();
  if (encoder.bpmChanged) {
    gUi.editing = true;
    gUi.displayedBpm = encoder.newBpm;
  }
  if (encoder.cancelEdit) {
    gUi.editing = false;
    gUi.displayedBpm = encoder.newBpm;
  }
  if (encoder.confirmEdit) {
    gUi.editing = false;
    gUi.displayedBpm = encoder.newBpm;
    gUi.confirmedBpm = encoder.newBpm;
    gTransport.sendBpm(encoder.newBpm);
  }
  if (encoder.toggleTransport) {
    gTransport.sendStartStop();
  }

  gLeds.tick();
  gDisplay.render(gUi);
  delay(10);
}
