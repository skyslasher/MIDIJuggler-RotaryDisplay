#pragma once

#include "display_ui.h"

class EncoderFsm {
 public:
  struct Result {
    bool bpmChanged = false;
    bool confirmEdit = false;
    bool cancelEdit = false;
    bool toggleTransport = false;
    float newBpm = 0.0f;
  };

  void begin(float bpmMin, float bpmMax, float step);
  void onSyncBpm(float bpm);
  bool isEditing() const { return editing_; }
  Result update();

 private:
  void consumePcnt(Result* result);

  float bpmMin_ = 40.0f;
  float bpmMax_ = 240.0f;
  float step_ = 1.0f;
  float confirmedBpm_ = 120.0f;
  float editBpm_ = 120.0f;
  bool editing_ = false;
  bool rotatedWhileEditing_ = false;
  uint32_t editStartedMs_ = 0;
  int32_t pcntRemainder_ = 0;
};
