#include <Arduino.h>

#include "board.h"
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
bool gRenderDirty = true;

void markDirty() { gRenderDirty = true; }

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

const char* prevInterval(const char* current) {
  size_t index = 2;
  for (size_t i = 0; i < kIntervalCount; ++i) {
    if (strcmp(current, kIntervals[i]) == 0) {
      index = i;
      break;
    }
  }
  return kIntervals[(index + kIntervalCount - 1) % kIntervalCount];
}

void applyIntervalStep(int steps) {
  if (steps == 0) {
    return;
  }
  const char* current = gUi.clickInterval;
  if (steps > 0) {
    for (int i = 0; i < steps; ++i) {
      current = nextInterval(current);
    }
  } else {
    for (int i = 0; i > steps; --i) {
      current = prevInterval(current);
    }
  }
  strlcpy(gUi.clickInterval, current, sizeof(gUi.clickInterval));
  gTransport.sendInterval(gUi.clickInterval);
  markDirty();
}

void applySync(const SyncPayload& payload) {
  if (!gEncoder.isEditing()) {
    if (gUi.displayedBpm != payload.bpm || gUi.confirmedBpm != payload.bpm) {
      markDirty();
    }
    gUi.confirmedBpm = payload.bpm;
    gUi.displayedBpm = payload.bpm;
    gEncoder.onSyncBpm(payload.bpm);
  }
  if (gUi.running != payload.running || gUi.clickEnabled != payload.clickEnabled ||
      strcmp(gUi.clickInterval, payload.clickInterval) != 0) {
    markDirty();
  }
  gUi.running = payload.running;
  gUi.clickEnabled = payload.clickEnabled;
  strlcpy(gUi.clickInterval, payload.clickInterval, sizeof(gUi.clickInterval));
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
      markDirty();
      break;
    default:
      applyIntervalStep(1);
      break;
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
  gTransport.begin(
      gConfig,
      applySync,
      onBeat);
  gTransport.setConfigHandler([](const char* line) {
    gConfigStore.applySerialCommand(line, &gConfig);
  });
  gRenderDirty = true;
  Serial.println("ready");
}

void loop() {
  const int previousPage = gUi.settingsPage;

  const EncoderFsm::Result encoder = gEncoder.update(gUi.settingsPage);
  gUi.editing = gEncoder.isEditing();
  if (encoder.intervalStep != 0) {
    applyIntervalStep(encoder.intervalStep);
  }
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
    gTransport.sendBpm(encoder.newBpm);
    markDirty();
  }
  if (encoder.toggleTransport) {
    gTransport.sendStartStop();
  }

  gTouch.loop(&gUi, handleSettingsAction);
  if (gUi.settingsPage != previousPage) {
    markDirty();
  }

  gTransport.loop();

  if (gRenderDirty) {
    gDisplay.render(gUi);
    gRenderDirty = false;
  }

  gLeds.tick();
  delay(4);
}
