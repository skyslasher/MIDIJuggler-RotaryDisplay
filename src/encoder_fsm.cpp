#include "encoder_fsm.h"

#include <driver/pcnt.h>

#include "board.h"
#include "display_ui.h"

namespace {

constexpr uint32_t kEditTimeoutMs = 5000;
constexpr pcnt_unit_t kPcntUnit = PCNT_UNIT_0;
constexpr int kPulsesPerDetent = 2;

constexpr const char* kIntervals[] = {"sixteenth", "eighth", "quarter", "half", "whole"};
constexpr size_t kIntervalCount = sizeof(kIntervals) / sizeof(kIntervals[0]);

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

const char* stepInterval(const char* current, int steps) {
  size_t index = 2;
  for (size_t i = 0; i < kIntervalCount; ++i) {
    if (strcmp(current, kIntervals[i]) == 0) {
      index = i;
      break;
    }
  }
  if (steps > 0) {
    index = (index + static_cast<size_t>(steps)) % kIntervalCount;
  } else {
    index = (index + kIntervalCount - static_cast<size_t>(-steps) % kIntervalCount) % kIntervalCount;
  }
  return kIntervals[index];
}

}  // namespace

void EncoderFsm::begin(float bpmMin, float bpmMax, float step) {
  bpmMin_ = bpmMin;
  bpmMax_ = bpmMax;
  step_ = step;
  pinMode(board::kEncoderSwitch, INPUT_PULLUP);
  pcntRemainder_ = 0;
  strlcpy(confirmedInterval_, "quarter", sizeof(confirmedInterval_));
  strlcpy(editInterval_, "quarter", sizeof(editInterval_));
  initPcnt();
}

void EncoderFsm::onSyncBpm(float bpm) {
  if (editing_) {
    return;
  }
  confirmedBpm_ = bpm;
  editBpm_ = bpm;
}

void EncoderFsm::onSyncInterval(const char* interval) {
  if (editingInterval_) {
    return;
  }
  strlcpy(confirmedInterval_, interval, sizeof(confirmedInterval_));
  strlcpy(editInterval_, interval, sizeof(editInterval_));
}

void EncoderFsm::cancelIntervalEdit() {
  editingInterval_ = false;
  rotatedWhileEditingInterval_ = false;
  strlcpy(editInterval_, confirmedInterval_, sizeof(editInterval_));
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

  if (settingsPage == static_cast<int>(SettingsPage::Interval)) {
    if (!editingInterval_) {
      editingInterval_ = true;
      rotatedWhileEditingInterval_ = true;
      editStartedMs_ = millis();
      strlcpy(editInterval_, confirmedInterval_, sizeof(editInterval_));
    }
    editStartedMs_ = millis();
    const char* next = stepInterval(editInterval_, steps);
    strlcpy(editInterval_, next, sizeof(editInterval_));
    result->intervalChanged = true;
    strlcpy(result->newInterval, editInterval_, sizeof(result->newInterval));
    return;
  }

  if (settingsPage != static_cast<int>(SettingsPage::Bpm)) {
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
    switch (static_cast<SettingsPage>(settingsPage)) {
      case SettingsPage::Bpm:
        if (editing_ && rotatedWhileEditing_) {
          confirmedBpm_ = editBpm_;
          editing_ = false;
          rotatedWhileEditing_ = false;
          result.confirmEdit = true;
          result.newBpm = confirmedBpm_;
        } else if (!rotatedWhileEditing_) {
          result.toggleTransport = true;
        }
        break;
      case SettingsPage::Click:
        result.toggleClick = true;
        break;
      case SettingsPage::Pulse:
        result.togglePulse = true;
        break;
      case SettingsPage::Interval:
        if (editingInterval_) {
          strlcpy(confirmedInterval_, editInterval_, sizeof(confirmedInterval_));
          editingInterval_ = false;
          rotatedWhileEditingInterval_ = false;
          result.confirmInterval = true;
          strlcpy(result.newInterval, confirmedInterval_, sizeof(result.newInterval));
        }
        break;
      default:
        break;
    }
  }
  lastSwitch = switchState;

  if (settingsPage == static_cast<int>(SettingsPage::Bpm) && editing_ &&
      millis() - editStartedMs_ >= kEditTimeoutMs) {
    editing_ = false;
    rotatedWhileEditing_ = false;
    editBpm_ = confirmedBpm_;
    result.cancelEdit = true;
    result.newBpm = confirmedBpm_;
  }

  if (settingsPage == static_cast<int>(SettingsPage::Interval) && editingInterval_ &&
      millis() - editStartedMs_ >= kEditTimeoutMs) {
    cancelIntervalEdit();
    result.cancelInterval = true;
    strlcpy(result.newInterval, confirmedInterval_, sizeof(result.newInterval));
  }

  return result;
}
