#include "config_store.h"

#include <cstring>

namespace {

constexpr const char* kNamespace = "rotary";

TransportMode parseTransport(int value) {
  if (value == 1) {
    return TransportMode::Serial;
  }
  if (value == 2) {
    return TransportMode::Wifi;
  }
  return TransportMode::Both;
}

}  // namespace

void ConfigStore::begin() { prefs_.begin(kNamespace, false); }

void ConfigStore::load(DeviceConfig* config) {
  if (config == nullptr) {
    return;
  }
#if defined(ROTARY_TRANSPORT_SERIAL)
  config->transport = TransportMode::Serial;
#elif defined(ROTARY_TRANSPORT_WIFI)
  config->transport = TransportMode::Wifi;
#else
  config->transport = parseTransport(prefs_.getInt("transport", 0));
#endif
  prefs_.getString("host", config->host, sizeof(config->host));
  config->oscPort = prefs_.getUShort("osc_port", 9000);
  config->listenPort = prefs_.getUShort("listen_port", 9001);
  config->pulseEnabled = prefs_.getBool("pulse", true);
  config->bpmStep = prefs_.getFloat("bpm_step", 1.0f);
}

void ConfigStore::save(const DeviceConfig& config) {
  prefs_.putInt("transport", static_cast<int>(config.transport));
  prefs_.putString("host", config.host);
  prefs_.putUShort("osc_port", config.oscPort);
  prefs_.putUShort("listen_port", config.listenPort);
  prefs_.putBool("pulse", config.pulseEnabled);
  prefs_.putFloat("bpm_step", config.bpmStep);
}

void ConfigStore::applySerialCommand(const char* line, DeviceConfig* config) {
  if (line == nullptr || config == nullptr) {
    return;
  }
  if (strncmp(line, "host ", 5) == 0) {
    strlcpy(config->host, line + 5, sizeof(config->host));
    save(*config);
  } else if (strncmp(line, "port ", 5) == 0) {
    config->oscPort = static_cast<uint16_t>(atoi(line + 5));
    save(*config);
  } else if (strncmp(line, "transport ", 10) == 0) {
    if (strstr(line + 10, "serial") != nullptr) {
      config->transport = TransportMode::Serial;
    } else if (strstr(line + 10, "wifi") != nullptr) {
      config->transport = TransportMode::Wifi;
    } else {
      config->transport = TransportMode::Both;
    }
    save(*config);
  }
}
