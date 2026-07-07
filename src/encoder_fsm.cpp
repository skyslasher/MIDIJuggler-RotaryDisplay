#include "encoder_fsm.h"

#include <driver/pcnt.h>

#include "board.h"

namespace {

constexpr uint32_t kEditTimeoutMs = 5000;
constexpr pcnt_unit_t kPcntUnit = PCNT_UNIT_0;
constexpr int kPulsesPerDetent = 2;

void initPcnt() {
  pcnt_config_t config = {};
  config.pulse_gpio_num = board::kEncoderA;
  config.ctrl_gpio_num = board::kEncoderB;
  config.lctrl_mode = PCNT_MODE_REVERSE;
  config.hctrl_mode = PCNT_MODE_KEEP;
  config.pos_mode = PCNT_COUNT_INC;
  config.neg_mode = PCNT_COUNT_DEC;
  config.counter_h_lim = 32767;
  config.counter_l_lim = -32768;
  config.unit = kPcntUnit;
  config.channel = PCNT_CHANNEL_0;

  pcnt_unit_config(&config);
  pcnt_set_filter_value(kPcntUnit, 80);
  pcnt_filter_enable(kPcntUnit);
  pcnt_counter_pause(kPcntUnit);
  pcnt_counter_clear(kPcntUnit);
  pcnt_counter_resume(kPcntUnit);
}

}  // namespace

void EncoderFsm::begin(float bpmMin, float bpmMax, float step) {
  bpmMin_ = bpmMin;
  bpmMax_ = bpmMax;
  step_ = step;
  pinMode(board::kEncoderSwitch, INPUT_PULLUP);
  pcntRemainder_ = 0;
  initPcnt();
}

void EncoderFsm::onSyncBpm(float bpm) {
  if (editing_) {
    return;
  }
  confirmedBpm_ = bpm;
  editBpm_ = bpm;
}

void EncoderFsm::consumePcnt(Result* result, int settingsPage) {
  int16_t count = 0;
  pcnt_get_counter_value(kPcntUnit, &count);
  pcnt_counter_clear(kPcntUnit);
  if (count == 0) {
    return;
  }

  pcntRemainder_ -= count;
  int steps = 0;
  while (pcntRemainder_ >= kPulsesPerDetent) {
    ++steps;
    pcntRemainder_ -= kPulsesPerDetent;
  }
  while (pcntRemainder_ <= -kPulsesPerDetent) {
    --steps;
    pcntRemainder_ += kPulsesPerDetent;
  }
  if (steps == 0) {
    return;
  }

  if (settingsPage == 2) {
    result->intervalStep += steps;
    return;
  }

  if (!editing_) {
    editing_ = true;
    rotatedWhileEditing_ = true;
    editStartedMs_ = millis();
    editBpm_ = confirmedBpm_;
  }
  editStartedMs_ = millis();
  editBpm_ += steps * step_;
  if (editBpm_ < bpmMin_) editBpm_ = bpmMin_;
  if (editBpm_ > bpmMax_) editBpm_ = bpmMax_;
  result->bpmChanged = true;
  result->newBpm = editBpm_;
}

EncoderFsm::Result EncoderFsm::update(int settingsPage) {
  Result result;
  static uint8_t lastSwitch = HIGH;

  consumePcnt(&result, settingsPage);

  const uint8_t switchState = digitalRead(board::kEncoderSwitch);
  if (lastSwitch == HIGH && switchState == LOW) {
    if (settingsPage == 2) {
      // Interval page: rotation only; tap is handled via touch.
    } else if (editing_ && rotatedWhileEditing_) {
      confirmedBpm_ = editBpm_;
      editing_ = false;
      rotatedWhileEditing_ = false;
      result.confirmEdit = true;
      result.newBpm = confirmedBpm_;
    } else if (!rotatedWhileEditing_) {
      result.toggleTransport = true;
    }
  }
  lastSwitch = switchState;

  if (settingsPage != 2 && editing_ && millis() - editStartedMs_ >= kEditTimeoutMs) {
    editing_ = false;
    rotatedWhileEditing_ = false;
    editBpm_ = confirmedBpm_;
    result.cancelEdit = true;
    result.newBpm = confirmedBpm_;
  }

  return result;
}
