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

bool parseOnOff(const char* value, bool* out) {
  if (value == nullptr || out == nullptr) {
    return false;
  }
  if (strcmp(value, "1") == 0 || strcasecmp(value, "on") == 0 || strcasecmp(value, "true") == 0) {
    *out = true;
    return true;
  }
  if (strcmp(value, "0") == 0 || strcasecmp(value, "off") == 0 || strcasecmp(value, "false") == 0) {
    *out = false;
    return true;
  }
  return false;
}

void copyRestOfLine(const char* line, size_t prefixLen, char* dest, size_t destSize) {
  if (line == nullptr || dest == nullptr || destSize == 0) {
    return;
  }
  while (line[prefixLen] == ' ') {
    prefixLen++;
  }
  strlcpy(dest, line + prefixLen, destSize);
}

bool parseHexNibble(char c, uint8_t* out) {
  if (c >= '0' && c <= '9') {
    *out = static_cast<uint8_t>(c - '0');
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    *out = static_cast<uint8_t>(10 + c - 'a');
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    *out = static_cast<uint8_t>(10 + c - 'A');
    return true;
  }
  return false;
}

bool parseHexColor(const char* value, uint8_t* r, uint8_t* g, uint8_t* b) {
  if (value == nullptr || r == nullptr || g == nullptr || b == nullptr) {
    return false;
  }
  while (*value == ' ') {
    value++;
  }
  if (*value == '#') {
    value++;
  }
  if (strlen(value) != 6) {
    return false;
  }
  uint8_t nibbles[6];
  for (int i = 0; i < 6; ++i) {
    if (!parseHexNibble(value[i], &nibbles[i])) {
      return false;
    }
  }
  *r = static_cast<uint8_t>((nibbles[0] << 4) | nibbles[1]);
  *g = static_cast<uint8_t>((nibbles[2] << 4) | nibbles[3]);
  *b = static_cast<uint8_t>((nibbles[4] << 4) | nibbles[5]);
  return true;
}

}  // namespace

void ConfigStore::begin() { prefs_.begin(kNamespace, false); }

const char* ConfigStore::transportName(TransportMode mode) {
  switch (mode) {
    case TransportMode::Serial:
      return "serial";
    case TransportMode::Wifi:
      return "wifi";
    default:
      return "both";
  }
}

bool ConfigStore::parseTransportName(const char* name, TransportMode* out) {
  if (name == nullptr || out == nullptr) {
    return false;
  }
  if (strcasecmp(name, "serial") == 0) {
    *out = TransportMode::Serial;
    return true;
  }
  if (strcasecmp(name, "wifi") == 0) {
    *out = TransportMode::Wifi;
    return true;
  }
  if (strcasecmp(name, "both") == 0) {
    *out = TransportMode::Both;
    return true;
  }
  return false;
}

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
  config->wifiEnabled = prefs_.getBool("wifi_en", true);
  prefs_.getString("wifi_ssid", config->wifiSsid, sizeof(config->wifiSsid));
  prefs_.getString("wifi_pass", config->wifiPass, sizeof(config->wifiPass));
  prefs_.getString("host", config->host, sizeof(config->host));
  prefs_.getString("mdns_host", config->mdnsHostname, sizeof(config->mdnsHostname));
  config->oscPort = prefs_.getUShort("osc_port", 9000);
  config->listenPort = prefs_.getUShort("listen_port", 9001);
  config->pulseEnabled = prefs_.getBool("pulse", true);
  config->bpmStep = prefs_.getFloat("bpm_step", 1.0f);
  config->beatLedR = static_cast<uint8_t>(prefs_.getUChar("beat_r", 30));
  config->beatLedG = static_cast<uint8_t>(prefs_.getUChar("beat_g", 255));
  config->beatLedB = static_cast<uint8_t>(prefs_.getUChar("beat_b", 120));
}

void ConfigStore::save(const DeviceConfig& config) {
  prefs_.putInt("transport", static_cast<int>(config.transport));
  prefs_.putBool("wifi_en", config.wifiEnabled);
  prefs_.putString("wifi_ssid", config.wifiSsid);
  prefs_.putString("wifi_pass", config.wifiPass);
  prefs_.putString("host", config.host);
  prefs_.putString("mdns_host", config.mdnsHostname);
  prefs_.putUShort("osc_port", config.oscPort);
  prefs_.putUShort("listen_port", config.listenPort);
  prefs_.putBool("pulse", config.pulseEnabled);
  prefs_.putFloat("bpm_step", config.bpmStep);
  prefs_.putUChar("beat_r", config.beatLedR);
  prefs_.putUChar("beat_g", config.beatLedG);
  prefs_.putUChar("beat_b", config.beatLedB);
}

void ConfigStore::resetDefaults(DeviceConfig* config) {
  if (config == nullptr) {
    return;
  }
  *config = DeviceConfig{};
}

bool ConfigStore::stageSerialCommand(const char* line, DeviceConfig* config) {
  if (line == nullptr || config == nullptr) {
    return false;
  }

  if (strncmp(line, "host ", 5) == 0) {
    copyRestOfLine(line, 5, config->host, sizeof(config->host));
    return true;
  }
  if (strncmp(line, "mdns_hostname ", 14) == 0) {
    if (strcmp(line + 14, "clear") == 0) {
      config->mdnsHostname[0] = '\0';
      return true;
    }
    copyRestOfLine(line, 14, config->mdnsHostname, sizeof(config->mdnsHostname));
    return true;
  }
  if (strncmp(line, "port ", 5) == 0) {
    config->oscPort = static_cast<uint16_t>(atoi(line + 5));
    return true;
  }
  if (strncmp(line, "listen_port ", 12) == 0) {
    config->listenPort = static_cast<uint16_t>(atoi(line + 12));
    return true;
  }
  if (strncmp(line, "transport ", 10) == 0) {
    TransportMode mode;
    if (!parseTransportName(line + 10, &mode)) {
      return true;
    }
    config->transport = mode;
    return true;
  }
  if (strncmp(line, "wifi_enabled ", 13) == 0) {
    bool enabled = false;
    if (parseOnOff(line + 13, &enabled)) {
      config->wifiEnabled = enabled;
    }
    return true;
  }
  if (strncmp(line, "wifi ssid ", 10) == 0) {
    copyRestOfLine(line, 10, config->wifiSsid, sizeof(config->wifiSsid));
    return true;
  }
  if (strncmp(line, "wifi pass ", 10) == 0) {
    copyRestOfLine(line, 10, config->wifiPass, sizeof(config->wifiPass));
    return true;
  }
  if (strcmp(line, "wifi clear") == 0) {
    config->wifiSsid[0] = '\0';
    config->wifiPass[0] = '\0';
    return true;
  }
  if (strncmp(line, "beat_led_color ", 15) == 0) {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    if (parseHexColor(line + 15, &r, &g, &b)) {
      config->beatLedR = r;
      config->beatLedG = g;
      config->beatLedB = b;
    }
    return true;
  }

  return false;
}
