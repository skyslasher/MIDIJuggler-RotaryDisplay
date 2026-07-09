#pragma once

#include <Preferences.h>

enum class TransportMode : uint8_t { Both = 0, Serial = 1, Wifi = 2 };

struct DeviceConfig {
  TransportMode transport = TransportMode::Both;
  bool wifiEnabled = true;
  char wifiSsid[33] = "";
  char wifiPass[65] = "";
  char host[48] = "midijuggler.local";
  char mdnsHostname[32] = "";
  uint16_t oscPort = 9000;
  uint16_t listenPort = 9001;
  bool pulseEnabled = true;
  float bpmStep = 1.0f;
  uint8_t beatLedR = 30;
  uint8_t beatLedG = 255;
  uint8_t beatLedB = 120;
};

class ConfigStore {
 public:
  void begin();
  void load(DeviceConfig* config);
  void save(const DeviceConfig& config);
  void resetDefaults(DeviceConfig* config);

  // Updates in-memory config only (persist on config apply).
  bool stageSerialCommand(const char* line, DeviceConfig* config);

  static const char* transportName(TransportMode mode);
  static bool parseTransportName(const char* name, TransportMode* out);

 private:
  Preferences prefs_;
};
