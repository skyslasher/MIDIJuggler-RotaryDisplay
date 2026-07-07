#pragma once

#include <functional>

#include "config_store.h"

struct SyncPayload {
  float bpm = 120.0f;
  bool running = false;
  bool clickEnabled = false;
  char clickInterval[16] = "quarter";
};

using SyncCallback = std::function<void(const SyncPayload&)>;
using BeatCallback = std::function<void(float)>;

class Transport {
 public:
  void begin(const DeviceConfig& config, SyncCallback onSync, BeatCallback onBeat);
  void loop();
  void sendBpm(float bpm);
  void sendStartStop();
  void sendClickToggle();
  void sendInterval(const char* interval);
  void sendHello();
  void handleSerialConfigLine(const char* line);
  void setConfigHandler(std::function<void(const char* line)> handler);

 private:
  bool useWifi_ = true;
  bool useSerial_ = true;
  SyncCallback onSync_;
  BeatCallback onBeat_;
  char host_[48] = "";
  uint16_t oscPort_ = 9000;
  uint16_t listenPort_ = 9001;
  char serialLine_[96] = "";
  size_t serialLength_ = 0;
  uint32_t lastHelloMs_ = 0;
  std::function<void(const char* line)> configHandler_;

  void sendSerialLine(const char* line);
  void sendOscAddress(const char* address);
  void sendOscBpm(float bpm);
  void sendOscInterval(const char* interval);
  void sendOscHello();
  void pollSerial();
  void pollOsc();
  void dispatchLine(const char* line);
  static bool parseSyncLine(const char* line, SyncPayload* payload);
  static bool parseBeatLine(const char* line, float* beat);
};
