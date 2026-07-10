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
using ConfigAppliedCallback = std::function<void(const DeviceConfig&)>;

class Transport {
 public:
  void begin(const DeviceConfig& config, SyncCallback onSync, BeatCallback onBeat);
  void loop();
  void applyConfig(const DeviceConfig& config, bool runWifiPortal = false);
  void setConfigAppliedCallback(ConfigAppliedCallback callback);
  void sendBpm(float bpm);
  void sendStartStop();
  void sendClickToggle();
  void sendInterval(const char* interval);
  void sendTapTempo();
  void sendHello();
  void setConfigStore(ConfigStore* store, DeviceConfig* config);
  bool isWifiConnected() const;
  bool isWifiEnabled() const;
  const char* wifiSsid() const;
  const char* wifiStatus() const;
  const char* oscHost() const;
  bool isOscConnected() const;

 private:
  ConfigStore* configStore_ = nullptr;
  DeviceConfig* deviceConfig_ = nullptr;
  bool useWifiClock_ = true;
  bool useSerialClock_ = true;
  bool wifiEnabled_ = true;
  bool wifiPortalPending_ = false;
  SyncCallback onSync_;
  BeatCallback onBeat_;
  ConfigAppliedCallback configAppliedCallback_;
  char host_[48] = "";
  uint16_t oscPort_ = 9000;
  uint16_t listenPort_ = 9001;
  char serialLine_[128] = "";
  size_t serialLength_ = 0;
  uint32_t lastHelloMs_ = 0;
  uint32_t lastOscHelloMs_ = 0;
  uint32_t lastHostSyncMs_ = 0;
  bool hostSyncReceived_ = false;
  uint32_t lastBeatPulseMs_ = 0;
  bool wifiRxReady_ = false;
  char connectedSsid_[33] = "";
  SyncPayload pendingSync_;

  void sendSerialLine(const char* line);
  void maintainWifi();
  void sendConfigLine(const char* line);
  void sendConfigError(const char* reason);
  void emitConfigGet();
  bool handleConfigLine(const char* line);
  void connectWifi(const DeviceConfig& config, bool runPortal);
  void disconnectWifi();
  void onWifiReady();
  void startMdns();
  void stopMdns();
  void feedbackAddress(char* buffer, size_t length) const;
  void sendOscBpm(float bpm);
  void sendOscInterval(const char* interval);
  void sendOscHello();
  void pollSerial();
  void pollOsc();
  void dispatchLine(const char* line);
  void emitSync();
  void markHostSyncReceived();
  bool needsOscHello() const;
  bool shouldAcceptSerialBeat() const;
  bool shouldAcceptOscBeat() const;
  void deliverBeat(float beat);
  static bool parseSyncLine(const char* line, SyncPayload* payload);
  static bool parseBeatLine(const char* line, float* beat);
};
