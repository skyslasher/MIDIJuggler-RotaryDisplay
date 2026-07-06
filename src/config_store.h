#pragma once

#include <Preferences.h>

enum class TransportMode : uint8_t { Both = 0, Serial = 1, Wifi = 2 };

struct DeviceConfig {
  TransportMode transport = TransportMode::Both;
  char host[48] = "midijuggler.local";
  uint16_t oscPort = 9000;
  uint16_t listenPort = 9001;
  bool pulseEnabled = true;
  float bpmStep = 1.0f;
};

class ConfigStore {
 public:
  void begin();
  void load(DeviceConfig* config);
  void save(const DeviceConfig& config);
  void applySerialCommand(const char* line, DeviceConfig* config);

 private:
  Preferences prefs_;
};
