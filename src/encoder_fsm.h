#pragma once

#include "display_ui.h"

class EncoderFsm {
 public:
  struct Result {
    bool bpmChanged = false;
    bool confirmEdit = false;
    bool cancelEdit = false;
    bool toggleTransport = false;
    bool toggleClick = false;
    bool togglePulse = false;
    bool intervalChanged = false;
    bool confirmInterval = false;
    bool cancelInterval = false;
    float newBpm = 0.0f;
    char newInterval[16] = "";
  };

  void begin(float bpmMin, float bpmMax, float step);
  void onSyncBpm(float bpm);
  void onSyncInterval(const char* interval);
  void cancelIntervalEdit();
  void confirmLocalBpm(float bpm);
  void onTransportToggle();
  bool shouldRejectSyncBpm(float bpm);
  bool isBpmTransferPending();
  bool isEditing() const { return editing_; }
  bool isEditingInterval() const { return editingInterval_; }
  Result update(int settingsPage = 0);

 private:
  void beginBpmEditIfNeeded();
  void beginIntervalEditIfNeeded();
  void clearExpiredPendingLocalBpm();
  void consumePcnt(Result* result, int settingsPage);

  float bpmMin_ = 40.0f;
  float bpmMax_ = 240.0f;
  float step_ = 1.0f;
  float confirmedBpm_ = 120.0f;
  float editBpm_ = 120.0f;
  char confirmedInterval_[16] = "quarter";
  char editInterval_[16] = "quarter";
  bool editing_ = false;
  bool editingInterval_ = false;
  bool rotatedWhileEditing_ = false;
  bool rotatedWhileEditingInterval_ = false;
  uint32_t editStartedMs_ = 0;
  int32_t pcntRemainder_ = 0;
  float pendingLocalBpm_ = -1.0f;
  uint32_t pendingLocalBpmMs_ = 0;
  uint32_t tapTempoBlockedUntilMs_ = 0;
};
