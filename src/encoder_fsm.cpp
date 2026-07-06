#include "encoder_fsm.h"

#include "board.h"

namespace {

constexpr uint32_t kEditTimeoutMs = 5000;

}  // namespace

void EncoderFsm::begin(float bpmMin, float bpmMax, float step) {
  bpmMin_ = bpmMin;
  bpmMax_ = bpmMax;
  step_ = step;
  pinMode(board::kEncoderA, INPUT_PULLUP);
  pinMode(board::kEncoderB, INPUT_PULLUP);
  pinMode(board::kEncoderSwitch, INPUT_PULLUP);
  lastEncoded_ = (digitalRead(board::kEncoderA) << 1) | digitalRead(board::kEncoderB);
}

void EncoderFsm::onSyncBpm(float bpm) {
  if (editing_) {
    return;
  }
  confirmedBpm_ = bpm;
  editBpm_ = bpm;
}

EncoderFsm::Result EncoderFsm::update() {
  Result result;
  static uint8_t lastSwitch = HIGH;
  const uint8_t switchState = digitalRead(board::kEncoderSwitch);
  const int encoded = (digitalRead(board::kEncoderA) << 1) | digitalRead(board::kEncoderB);
  if (encoded != lastEncoded_) {
    const int delta = ((lastEncoded_ << 2) | encoded) & 0x0F;
    int direction = 0;
    if (delta == 0b1101 || delta == 0b0100 || delta == 0b0010 || delta == 0b1011) {
      direction = 1;
    } else if (delta == 0b1110 || delta == 0b0111 || delta == 0b0001 || delta == 0b1000) {
      direction = -1;
    }
    if (direction != 0) {
      if (!editing_) {
        editing_ = true;
        rotatedWhileEditing_ = true;
        editStartedMs_ = millis();
        editBpm_ = confirmedBpm_;
      }
      editBpm_ += direction * step_;
      if (editBpm_ < bpmMin_) editBpm_ = bpmMin_;
      if (editBpm_ > bpmMax_) editBpm_ = bpmMax_;
      result.bpmChanged = true;
      result.newBpm = editBpm_;
    }
    lastEncoded_ = encoded;
  }

  if (lastSwitch == HIGH && switchState == LOW) {
    if (editing_ && rotatedWhileEditing_) {
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

  if (editing_ && millis() - editStartedMs_ >= kEditTimeoutMs) {
    editing_ = false;
    rotatedWhileEditing_ = false;
    editBpm_ = confirmedBpm_;
    result.cancelEdit = true;
    result.newBpm = confirmedBpm_;
  }

  if (result.bpmChanged || result.cancelEdit) {
    result.newBpm = editBpm_;
  }
  return result;
}
