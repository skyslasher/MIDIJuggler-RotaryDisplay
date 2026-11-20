#include "transport.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <esp_netif.h>
#include <esp_wifi.h>

#include <cstring>

namespace {

constexpr const char* kBpmAddress = "/midijuggler/clock/bpm";
constexpr const char* kStartStopAddress = "/midijuggler/clock/start_stop";
constexpr const char* kClickToggleAddress = "/midijuggler/clock/click_toggle";
constexpr const char* kIntervalAddress = "/midijuggler/clock/click_interval";
constexpr const char* kTapTempoAddress = "/midijuggler/clock/tap_tempo";
constexpr const char* kHelloAddress = "/midijuggler/rotary/hello";
constexpr const char* kSyncAddress = "/midijuggler/rotary/sync";
constexpr const char* kBeatAddress = "/midijuggler/rotary/beat";
constexpr const char* kFeedbackBpmAddress = "/midijuggler/rotary/bpm";
constexpr const char* kFeedbackRunningAddress = "/midijuggler/rotary/running";
constexpr const char* kFeedbackClickEnabledAddress = "/midijuggler/rotary/click_enabled";
constexpr const char* kFeedbackClickIntervalAddress = "/midijuggler/rotary/click_interval";

WiFiUDP gTx;
WiFiUDP gRx;

// Back off only after a disconnect. A timer measured from scan/join start
// used to call begin() again and abort association (the old 8s restart).
constexpr uint32_t kWifiFailBackoffMs = 3000;
constexpr uint32_t kWifiScanTimeoutMs = 20000;
constexpr uint32_t kWifiJoinGiveUpMs = 20000;
constexpr uint32_t kWifiBusyRetryMs = 1000;
constexpr uint32_t kMdnsRetryMs = 2000;

// Set from the Wi-Fi event task. Loop re-announces mDNS; MDNS.begin from the
// event callback deadlocks the mdns task against the event loop.
volatile bool gStaGotIp = false;
volatile bool gScanDone = false;
volatile bool gJoinPending = false;
volatile uint32_t gWifiRetryAfterMs = 0;
volatile uint32_t gIgnoreDisconnectUntil = 0;
volatile uint32_t gJoinStartedMs = 0;

bool isMdnsLabelChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
}

bool sanitizeMdnsLabel(char* dest, size_t destSize, const char* raw) {
  if (dest == nullptr || destSize < 2) {
    return false;
  }
  dest[0] = '\0';
  if (raw == nullptr) {
    return false;
  }
  while (*raw == ' ') {
    raw++;
  }
  char buf[32];
  strlcpy(buf, raw, sizeof(buf));
  size_t len = strlen(buf);
  while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '.')) {
    buf[--len] = '\0';
  }
  if (len >= 6 && strcasecmp(buf + len - 6, ".local") == 0) {
    len -= 6;
    buf[len] = '\0';
  }
  if (len == 0 || len >= destSize || buf[0] == '-' || buf[len - 1] == '-') {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    char c = buf[i];
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
    if (!isMdnsLabelChar(c)) {
      return false;
    }
    buf[i] = c;
  }
  strlcpy(dest, buf, destSize);
  return true;
}

bool sameSsid(const char* configured, const String& scanned) {
  if (configured == nullptr) {
    return false;
  }
  // Byte length, not a tokenizing compare. A %s / strtok match stops at the
  // space and would treat "Lunatic Network" as a different AP.
  const size_t len = strnlen(configured, 32);
  return len > 0 && scanned.length() == len && memcmp(scanned.c_str(), configured, len) == 0;
}

void formatSsidHead(char* dest, size_t destSize, const char* ssid, size_t len) {
  if (dest == nullptr || destSize == 0) {
    return;
  }
  size_t n = len;
  if (n > 16) {
    n = 16;
  }
  if (n >= destSize) {
    n = destSize - 1;
  }
  for (size_t i = 0; i < n; ++i) {
    const unsigned char c = static_cast<unsigned char>(ssid == nullptr ? 0 : ssid[i]);
    dest[i] = (c >= 32 && c < 127) ? static_cast<char>(c) : '?';
  }
  dest[n] = '\0';
}

size_t pad4(size_t size) { return (size + 3) & ~static_cast<size_t>(3); }

uint32_t readU32BE(const uint8_t* data) {
  return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
}

int32_t readI32BE(const uint8_t* data) { return static_cast<int32_t>(readU32BE(data)); }

float readFloatBE(const uint8_t* data) {
  union {
    uint32_t u;
    float f;
  } value;
  value.u = readU32BE(data);
  return value.f;
}

void writeU32BE(uint8_t* data, uint32_t value) {
  data[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
  data[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
  data[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
  data[3] = static_cast<uint8_t>(value & 0xFF);
}

void writeI32BE(uint8_t* data, int32_t value) {
  writeU32BE(data, static_cast<uint32_t>(value));
}

void writeFloatBE(uint8_t* data, float value) {
  union {
    float f;
    uint32_t u;
  } raw;
  raw.f = value;
  writeU32BE(data, raw.u);
}

size_t oscTagsOffset(const char* address) { return pad4(strlen(address) + 1); }

const char* oscTags(const uint8_t* buffer, const char* address) {
  return reinterpret_cast<const char*>(buffer + oscTagsOffset(address));
}

size_t oscFirstArgOffset(const uint8_t* buffer, const char* address) {
  const char* tags = oscTags(buffer, address);
  return oscTagsOffset(address) + pad4(strlen(tags) + 1);
}

void sendOscPacket(const IPAddress& ip, uint16_t port, const uint8_t* data, size_t length) {
  if (!gTx.beginPacket(ip, port)) {
    return;
  }
  gTx.write(data, length);
  gTx.endPacket();
}

void sendOscFloat(const IPAddress& ip, uint16_t port, const char* address, float value) {
  uint8_t packet[96] = {0};
  size_t offset = 0;
  const size_t addrLen = pad4(strlen(address) + 1);
  memcpy(packet + offset, address, strlen(address) + 1);
  offset += addrLen;
  const char* tags = ",f";
  memcpy(packet + offset, tags, 3);
  offset += pad4(3);
  writeFloatBE(packet + offset, value);
  offset += sizeof(value);
  sendOscPacket(ip, port, packet, offset);
}

void sendOscString(const IPAddress& ip, uint16_t port, const char* address, const char* value) {
  uint8_t packet[128] = {0};
  size_t offset = 0;
  const size_t addrLen = pad4(strlen(address) + 1);
  memcpy(packet + offset, address, strlen(address) + 1);
  offset += addrLen;
  const char* tags = ",s";
  memcpy(packet + offset, tags, 3);
  offset += pad4(3);
  const size_t valueLen = pad4(strlen(value) + 1);
  memcpy(packet + offset, value, strlen(value) + 1);
  offset += valueLen;
  sendOscPacket(ip, port, packet, offset);
}

void sendOscHelloPacket(const IPAddress& ip, uint16_t port, const char* feedbackHost,
                        uint16_t listenPort) {
  uint8_t packet[128] = {0};
  size_t offset = 0;
  const size_t addrLen = pad4(strlen(kHelloAddress) + 1);
  memcpy(packet + offset, kHelloAddress, strlen(kHelloAddress) + 1);
  offset += addrLen;
  const char* tags = ",si";
  memcpy(packet + offset, tags, 4);
  offset += pad4(4);
  const size_t ipLen = pad4(strlen(feedbackHost) + 1);
  memcpy(packet + offset, feedbackHost, strlen(feedbackHost) + 1);
  offset += ipLen;
  writeI32BE(packet + offset, static_cast<int32_t>(listenPort));
  offset += sizeof(int32_t);
  sendOscPacket(ip, port, packet, offset);
}

void sendOscTrigger(const IPAddress& ip, uint16_t port, const char* address) {
  uint8_t packet[64] = {0};
  size_t offset = 0;
  const size_t addrLen = pad4(strlen(address) + 1);
  memcpy(packet + offset, address, strlen(address) + 1);
  offset += addrLen;
  const char* tags = ",i";
  memcpy(packet + offset, tags, 3);
  offset += pad4(3);
  writeI32BE(packet + offset, 1);
  offset += sizeof(int32_t);
  sendOscPacket(ip, port, packet, offset);
}

}  // namespace

void Transport::setConfigStore(ConfigStore* store, DeviceConfig* config) {
  configStore_ = store;
  deviceConfig_ = config;
}

void Transport::setConfigAppliedCallback(ConfigAppliedCallback callback) {
  configAppliedCallback_ = std::move(callback);
}

void Transport::applyConfig(const DeviceConfig& config, bool runWifiPortal) {
  const bool wasSerialClock = useSerialClock_;
  const bool wasWifiClock = useWifiClock_;
  const bool hostChanged =
      strncmp(host_, config.host, sizeof(host_)) != 0 || oscPort_ != config.oscPort;
  strlcpy(host_, config.host, sizeof(host_));
  oscPort_ = config.oscPort;
  listenPort_ = config.listenPort;
  useSerialClock_ = config.transport != TransportMode::Wifi;
  useWifiClock_ = config.transport != TransportMode::Serial;
  wifiEnabled_ = config.wifiEnabled;
  if (hostChanged) {
    hostIpValid_ = false;
    lastHostResolveMs_ = 0;
  }

  if (useWifiClock_ && wifiEnabled_) {
    connectWifi(config, runWifiPortal);
  } else {
    disconnectWifi();
  }

  const bool transportChanged = wasSerialClock != useSerialClock_ || wasWifiClock != useWifiClock_;
  if (transportChanged || hostChanged) {
    // Serial sync must not count as OSC registration. Re-hello when the path changes.
    oscSyncReceived_ = false;
    lastOscSyncMs_ = 0;
    lastHelloMs_ = millis();
    lastOscHelloMs_ = millis();
    sendHello();
  }
}

void Transport::begin(const DeviceConfig& config, SyncCallback onSync, BeatCallback onBeat) {
  onSync_ = std::move(onSync);
  onBeat_ = std::move(onBeat);
  applyConfig(config, false);

  if (useSerialClock_) {
    lastHelloMs_ = millis();
    sendSerialLine("hello");
  }
  if (useWifiClock_ && WiFi.status() == WL_CONNECTED) {
    sendOscHello();
  }
}

void Transport::loop() {
  pollSerial();

  if (wifiPortalPending_ && deviceConfig_ != nullptr) {
    wifiPortalPending_ = false;
    connectWifi(*deviceConfig_, true);
  } else {
    maintainWifi();
  }

  if (useSerialClock_) {
    const uint32_t now = millis();
    if (now - lastHelloMs_ >= 3000) {
      lastHelloMs_ = now;
      sendSerialLine("hello");
    }
  }
  if (useWifiClock_ && WiFi.status() == WL_CONNECTED) {
    maintainHostAddress();
    pollOsc();
    if (needsOscHello()) {
      const uint32_t now = millis();
      if (now - lastOscHelloMs_ >= 3000) {
        lastOscHelloMs_ = now;
        sendOscHello();
      }
    }
  }
}

void Transport::sendBpm(float bpm) {
  if (useSerialClock_) {
    char line[32];
    snprintf(line, sizeof(line), "bpm %.1f", bpm);
    sendSerialLine(line);
  }
  if (readyToSendOsc()) {
    sendOscFloat(IPAddress(hostIp_), oscPort_, kBpmAddress, bpm);
  }
}

void Transport::sendStartStop() {
  if (useSerialClock_) {
    sendSerialLine("start_stop");
  }
  if (readyToSendOsc()) {
    sendOscTrigger(IPAddress(hostIp_), oscPort_, kStartStopAddress);
  }
}

void Transport::sendClickToggle() {
  if (useSerialClock_) {
    sendSerialLine("click_toggle");
  }
  if (readyToSendOsc()) {
    sendOscTrigger(IPAddress(hostIp_), oscPort_, kClickToggleAddress);
  }
}

void Transport::sendInterval(const char* interval) {
  if (useSerialClock_) {
    char line[32];
    snprintf(line, sizeof(line), "interval %s", interval);
    sendSerialLine(line);
  }
  if (readyToSendOsc()) {
    sendOscString(IPAddress(hostIp_), oscPort_, kIntervalAddress, interval);
  }
}

void Transport::sendTapTempo() {
  if (useSerialClock_) {
    sendSerialLine("tap_tempo");
  }
  if (readyToSendOsc()) {
    sendOscTrigger(IPAddress(hostIp_), oscPort_, kTapTempoAddress);
  }
}

void Transport::sendHello() {
  if (useSerialClock_) {
    sendSerialLine("hello");
  }
  if (useWifiClock_ && WiFi.status() == WL_CONNECTED) {
    sendOscHello();
  }
}

bool Transport::isWifiConnected() const {
  return useWifiClock_ && wifiEnabled_ && WiFi.status() == WL_CONNECTED;
}

bool Transport::isWifiEnabled() const { return wifiEnabled_; }

const char* Transport::wifiSsid() const {
  if (!isWifiConnected()) {
    return "";
  }
  return connectedSsid_;
}

const char* Transport::wifiStatus() const {
  if (!wifiEnabled_) {
    return "disabled";
  }
  if (!useWifiClock_) {
    return "transport-serial";
  }
  if (WiFi.status() == WL_CONNECTED) {
    return "connected";
  }
  return "disconnected";
}

const char* Transport::oscHost() const { return host_; }

bool Transport::isOscConnected() const {
  if (!isWifiConnected()) {
    return false;
  }
  if (!oscSyncReceived_ || lastOscSyncMs_ == 0) {
    return false;
  }
  return millis() - lastOscSyncMs_ < 10000;
}

void Transport::markOscSyncReceived() {
  oscSyncReceived_ = true;
  lastOscSyncMs_ = millis();
}

bool Transport::needsOscHello() const {
  if (!useWifiClock_ || WiFi.status() != WL_CONNECTED) {
    return false;
  }
  if (!oscSyncReceived_) {
    return true;
  }
  return millis() - lastOscSyncMs_ >= 10000;
}

bool Transport::readyToSendOsc() const {
  return useWifiClock_ && hostIpValid_ && WiFi.status() == WL_CONNECTED;
}

void Transport::sendSerialLine(const char* line) {
  if (!useSerialClock_) {
    return;
  }
  Serial.println(line);
}

void Transport::sendConfigLine(const char* line) { Serial.println(line); }

void Transport::sendConfigError(const char* reason) {
  char line[96];
  snprintf(line, sizeof(line), "err %s", reason);
  sendConfigLine(line);
}

void Transport::emitConfigGet() {
  if (deviceConfig_ == nullptr) {
    sendConfigError("no config");
    return;
  }
  const DeviceConfig& config = *deviceConfig_;
  char line[96];

  snprintf(line, sizeof(line), "cfg transport=%s", ConfigStore::transportName(config.transport));
  sendConfigLine(line);
  snprintf(line, sizeof(line), "cfg wifi_enabled=%s", config.wifiEnabled ? "on" : "off");
  sendConfigLine(line);
  snprintf(line, sizeof(line), "cfg wifi_ssid=%s", config.wifiSsid);
  sendConfigLine(line);
  sendConfigLine("cfg wifi_pass=***");
  snprintf(line, sizeof(line), "cfg host=%s", config.host);
  sendConfigLine(line);
  if (config.mdnsHostname[0] != '\0') {
    snprintf(line, sizeof(line), "cfg mdns_hostname=%s", config.mdnsHostname);
  } else {
    snprintf(line, sizeof(line), "cfg mdns_hostname=(auto)");
  }
  sendConfigLine(line);
  snprintf(line, sizeof(line), "cfg port=%u", config.oscPort);
  sendConfigLine(line);
  snprintf(line, sizeof(line), "cfg listen_port=%u", config.listenPort);
  sendConfigLine(line);
  snprintf(line, sizeof(line), "cfg pulse_enabled=%s", config.pulseEnabled ? "on" : "off");
  sendConfigLine(line);
  snprintf(line, sizeof(line), "cfg bpm_step=%.1f", config.bpmStep);
  sendConfigLine(line);
  snprintf(line, sizeof(line), "cfg beat_led_color=#%02X%02X%02X", config.beatLedR, config.beatLedG,
           config.beatLedB);
  sendConfigLine(line);
}

bool Transport::handleConfigLine(const char* line) {
  if (line == nullptr) {
    return false;
  }

  if (strcmp(line, "config get") == 0) {
    emitConfigGet();
    sendConfigLine("ok");
    return true;
  }

  if (strcmp(line, "config apply") == 0) {
    if (configStore_ == nullptr || deviceConfig_ == nullptr) {
      sendConfigError("no store");
      return true;
    }
    configStore_->save(*deviceConfig_);
    applyConfig(*deviceConfig_, false);
    if (configAppliedCallback_) {
      configAppliedCallback_(*deviceConfig_);
    }
    sendConfigLine("ok");
    return true;
  }

  if (strcmp(line, "config reset") == 0) {
    if (configStore_ == nullptr || deviceConfig_ == nullptr) {
      sendConfigError("no store");
      return true;
    }
    configStore_->resetDefaults(deviceConfig_);
    configStore_->save(*deviceConfig_);
    applyConfig(*deviceConfig_, false);
    if (configAppliedCallback_) {
      configAppliedCallback_(*deviceConfig_);
    }
    sendConfigLine("ok");
    return true;
  }

  if (strcmp(line, "reboot") == 0) {
    sendConfigLine("ok");
    delay(100);
    ESP.restart();
    return true;
  }

  if (strcmp(line, "wifi portal") == 0) {
    wifiPortalPending_ = true;
    sendConfigLine("ok");
    return true;
  }

  if (deviceConfig_ != nullptr && configStore_ != nullptr &&
      configStore_->stageSerialCommand(line, deviceConfig_)) {
    sendConfigLine("ok");
    return true;
  }

  return false;
}

void Transport::ensureWifiHooks() {
  static bool hooked = false;
  if (hooked) {
    return;
  }
  hooked = true;
  WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
    if (event == ARDUINO_EVENT_WIFI_SCAN_DONE) {
      gScanDone = true;
      return;
    }
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      gStaGotIp = true;
      gJoinPending = false;
      gJoinStartedMs = 0;
      return;
    }
    if (event != ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      return;
    }
    gStaGotIp = false;
    const uint8_t len = info.wifi_sta_disconnected.ssid_len;
    char seen[33] = {};
    if (len > 0 && len < sizeof(seen)) {
      memcpy(seen, info.wifi_sta_disconnected.ssid, len);
    }
    Serial.printf("wifi: disconnected reason %u ssid [%s] len %u\n",
                  info.wifi_sta_disconnected.reason, seen, len);
    // A disconnect we issued to unstick the STA is not a failed join.
    if (static_cast<int32_t>(gIgnoreDisconnectUntil - millis()) > 0) {
      return;
    }
    gJoinPending = false;
    gJoinStartedMs = 0;
    gWifiRetryAfterMs = millis() + kWifiFailBackoffMs;
  });
}

bool Transport::buildMdnsHostname(char* dest, size_t destSize) const {
  if (deviceConfig_ != nullptr && deviceConfig_->mdnsHostname[0] != '\0' &&
      sanitizeMdnsLabel(dest, destSize, deviceConfig_->mdnsHostname)) {
    mdnsRejectLogged_ = false;
    return true;
  }
  if (deviceConfig_ != nullptr && deviceConfig_->mdnsHostname[0] != '\0' && !mdnsRejectLogged_) {
    mdnsRejectLogged_ = true;
    Serial.printf("mdns: hostname=%s rejected\n", deviceConfig_->mdnsHostname);
  }
  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  snprintf(dest, destSize, "rotary-%02x%02x%02x", mac[3], mac[4], mac[5]);
  return dest[0] != '\0';
}

void Transport::applyStaHostname() {
  char hostname[32] = {};
  if (!buildMdnsHostname(hostname, sizeof(hostname))) {
    return;
  }
  // mode(WIFI_STA) copies this into the netif only when the mode changes.
  // Set it first, then push it onto a netif that is already up.
  WiFi.setHostname(hostname);
  esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (netif != nullptr) {
    esp_netif_set_hostname(netif, hostname);
  }
}

void Transport::prepareWifiRadio() {
  // Keep the STA radio up. disconnect(true) powers it off and races with begin().
  WiFi.persistent(false);
  ensureWifiHooks();
  applyStaHostname();
  WiFi.mode(WIFI_STA);
  applyStaHostname();
  WiFi.setSleep(false);
  // Reason 201 is reconnectable. The core would call WiFi.begin() with no
  // BSSID, which searches by SSID and blocks esp_wifi_scan_start
  // (ESP_ERR_WIFI_STATE). That search is the path that misses a space.
  WiFi.setAutoReconnect(false);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.setMinSecurity(WIFI_AUTH_OPEN);
}

void Transport::joinWifi(const char* ssid, const char* pass, int32_t channel, const uint8_t* bssid,
                         int32_t rssi) {
  if (ssid == nullptr || bssid == nullptr || channel <= 0) {
    Serial.println("wifi: join skipped, scan had no bssid");
    gWifiRetryAfterMs = millis() + kWifiFailBackoffMs;
    return;
  }
  if (pass == nullptr) {
    pass = "";
  }
  const size_t ssidLen = strnlen(ssid, 32);
  const size_t passLen = strnlen(pass, 64);
  if (passLen == 33) {
    // 33 is a surprising stored length. Reason 201 is still "AP not found".
    static bool noted = false;
    if (!noted) {
      noted = true;
      Serial.println("wifi: passLen=33 noted; reason 201 is NO_AP_FOUND, not a bad password");
    }
  }

  // Copy SSID bytes explicitly. Do not hand the name to WiFi.begin(), whose
  // no-BSSID path lets the driver match the SSID and stop at the first space.
  wifi_config_t conf = {};
  memcpy(conf.sta.ssid, ssid, ssidLen);
  if (passLen > 0) {
    memcpy(conf.sta.password, pass, passLen);
  }
  conf.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  conf.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  conf.sta.threshold.rssi = -127;
  conf.sta.threshold.authmode = WIFI_AUTH_OPEN;
  conf.sta.pmf_cfg.capable = true;
  conf.sta.bssid_set = true;
  memcpy(conf.sta.bssid, bssid, 6);
  conf.sta.channel = static_cast<uint8_t>(channel);
  conf.sta.failure_retry_cnt = 3;

  applyStaHostname();
  const esp_err_t setErr = esp_wifi_set_config(WIFI_IF_STA, &conf);
  esp_err_t connErr = ESP_FAIL;
  if (setErr == ESP_OK) {
    gJoinStartedMs = millis();
    gJoinPending = true;
    connErr = esp_wifi_connect();
    if (connErr != ESP_OK) {
      gJoinPending = false;
      gJoinStartedMs = 0;
      gWifiRetryAfterMs = millis() + kWifiFailBackoffMs;
    }
  } else {
    gWifiRetryAfterMs = millis() + kWifiFailBackoffMs;
  }
  Serial.printf(
      "wifi: join %s len=%u passLen=%u ch=%ld rssi=%ld bssid=%02X:%02X:%02X:%02X:%02X:%02X set=0x%x conn=0x%x\n",
      ssid, static_cast<unsigned>(ssidLen), static_cast<unsigned>(passLen), static_cast<long>(channel),
      static_cast<long>(rssi), bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
      static_cast<unsigned>(setErr), static_cast<unsigned>(connErr));
  wifiScanActive_ = false;
}

void Transport::finishWifiScan(const char* ssid, const char* pass) {
  const int16_t count = WiFi.scanComplete();
  if (count < 0 && scanStartMs_ != 0 && millis() - scanStartMs_ < kWifiScanTimeoutMs) {
    // SCAN_DONE arrived before the result list is readable. Keep waiting.
    return;
  }
  wifiScanActive_ = false;
  gScanDone = false;

  int best = -1;
  int32_t bestRssi = -1000;
  const size_t ssidLen = ssid == nullptr ? 0 : strnlen(ssid, 32);
  if (count > 0 && ssidLen > 0) {
    for (int16_t i = 0; i < count; ++i) {
      const String ap = WiFi.SSID(static_cast<uint8_t>(i));
      if (!sameSsid(ssid, ap)) {
        continue;
      }
      const int32_t rssi = WiFi.RSSI(static_cast<uint8_t>(i));
      if (best < 0 || rssi > bestRssi) {
        best = i;
        bestRssi = rssi;
      }
    }
  }

  if (best >= 0) {
    const String ap = WiFi.SSID(static_cast<uint8_t>(best));
    char head[17] = {};
    formatSsidHead(head, sizeof(head), ap.c_str(), ap.length());
    const int32_t channel = WiFi.channel(static_cast<uint8_t>(best));
    Serial.printf("wifi: scan count=%d matched=1 len=%u head=[%s] ch=%ld rssi=%ld\n",
                  static_cast<int>(count), static_cast<unsigned>(ap.length()), head,
                  static_cast<long>(channel), static_cast<long>(bestRssi));
    uint8_t bssid[6] = {};
    const uint8_t* found = WiFi.BSSID(static_cast<uint8_t>(best));
    if (found != nullptr && channel > 0) {
      memcpy(bssid, found, sizeof(bssid));
      WiFi.scanDelete();
      joinWifi(ssid, pass, channel, bssid, bestRssi);
      return;
    }
  }

  Serial.printf("wifi: scan count=%d matched=0 wantLen=%u\n", static_cast<int>(count),
                static_cast<unsigned>(ssidLen));
  const int16_t shown = count > 4 ? 4 : (count > 0 ? count : 0);
  for (int16_t i = 0; i < shown; ++i) {
    const String ap = WiFi.SSID(static_cast<uint8_t>(i));
    char head[17] = {};
    formatSsidHead(head, sizeof(head), ap.c_str(), ap.length());
    Serial.printf("wifi: scan ap%d len=%u head=[%s]\n", static_cast<int>(i),
                  static_cast<unsigned>(ap.length()), head);
  }
  WiFi.scanDelete();
  gWifiRetryAfterMs = millis() + kWifiFailBackoffMs;
}

void Transport::startWifiScan() {
  prepareWifiRadio();
  if (gJoinPending) {
    return;
  }
  WiFi.scanDelete();
  gScanDone = false;

  // Channel 0 scans every channel. ssid NULL is a broadcast probe so the
  // driver does not filter the name with a match that stops at a space.
  // Arduino's scanNetworks leaves home_chan_dwell_time uninitialized.
  wifi_scan_config_t config = {};
  config.ssid = nullptr;
  config.bssid = nullptr;
  config.channel = 0;
  config.show_hidden = true;
  config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  config.scan_time.active.min = 100;
  config.scan_time.active.max = 300;

  const esp_err_t err = esp_wifi_scan_start(&config, false);
  if (err == ESP_ERR_WIFI_STATE) {
    if (staBusySinceMs_ == 0) {
      staBusySinceMs_ = millis();
      Serial.println("wifi: scan deferred sta busy");
    } else if (millis() - staBusySinceMs_ > kWifiJoinGiveUpMs) {
      gIgnoreDisconnectUntil = millis() + 1000;
      esp_wifi_disconnect();
      staBusySinceMs_ = 0;
      Serial.println("wifi: cleared stuck join");
    }
    gWifiRetryAfterMs = millis() + kWifiBusyRetryMs;
    wifiScanActive_ = false;
    return;
  }
  if (err != ESP_OK) {
    Serial.printf("wifi: scan start err=0x%x\n", static_cast<unsigned>(err));
    gWifiRetryAfterMs = millis() + kWifiFailBackoffMs;
    wifiScanActive_ = false;
    return;
  }
  staBusySinceMs_ = 0;
  wifiScanActive_ = true;
  scanStartMs_ = millis();
  const char* ssid = deviceConfig_ == nullptr ? "" : deviceConfig_->wifiSsid;
  Serial.printf("wifi: scanning all channels for %s len=%u\n", ssid,
                static_cast<unsigned>(strnlen(ssid, 32)));
}

void Transport::connectWifi(const DeviceConfig& config, bool runPortal) {
  gRx.stop();
  wifiRxReady_ = false;
  connectedSsid_[0] = '\0';
  hostIpValid_ = false;
  wifiScanActive_ = false;
  gScanDone = false;
  gJoinPending = false;
  gJoinStartedMs = 0;
  gWifiRetryAfterMs = 0;
  staBusySinceMs_ = 0;

  if (!wifiEnabled_) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return;
  }

  if (runPortal) {
    ensureWifiHooks();
    applyStaHostname();
    WiFi.scanDelete();
    WiFiManager manager;
    manager.setConfigPortalTimeout(120);
    manager.autoConnect("RotaryDisplay-Setup");
    if (WiFi.status() == WL_CONNECTED) {
      onWifiReady();
    }
    return;
  }

  if (config.wifiSsid[0] == '\0') {
    Serial.println("wifi: no SSID configured, skipping connect (use wifi portal)");
    return;
  }

  startWifiScan();
}

void Transport::maintainWifi() {
  if (!useWifiClock_ || !wifiEnabled_) {
    return;
  }
  const int status = static_cast<int>(WiFi.status());
  if (status != lastWifiStatus_) {
    Serial.printf("wifi: status %d\n", status);
    lastWifiStatus_ = status;
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiRxReady_) {
      gRx.stop();
      wifiRxReady_ = false;
      connectedSsid_[0] = '\0';
      hostIpValid_ = false;
    }
    // Leave the mDNS responder initialized. mdns_free() drops the IP-event
    // handler, and the next MDNS.begin often never binds the STA again.
    // GOT_IP re-announces once the address is back.
    if (mdnsOk_) {
      mdnsOk_ = false;
      mdnsIp_ = 0;
    }
    if (deviceConfig_ == nullptr || deviceConfig_->wifiSsid[0] == '\0') {
      return;
    }
    if (wifiScanActive_) {
      if (!gScanDone) {
        if (scanStartMs_ != 0 && millis() - scanStartMs_ > kWifiScanTimeoutMs) {
          esp_wifi_scan_stop();
          wifiScanActive_ = false;
          gScanDone = false;
          gWifiRetryAfterMs = millis() + kWifiFailBackoffMs;
          Serial.println("wifi: scan timeout");
        }
        return;
      }
      finishWifiScan(deviceConfig_->wifiSsid, deviceConfig_->wifiPass);
      return;
    }
    if (gJoinPending) {
      if (gJoinStartedMs != 0 && millis() - gJoinStartedMs > kWifiJoinGiveUpMs) {
        Serial.println("wifi: join still pending, allowing retry");
        gIgnoreDisconnectUntil = millis() + 1000;
        gJoinPending = false;
        gJoinStartedMs = 0;
        esp_wifi_disconnect();
      }
      return;
    }
    const uint32_t now = millis();
    if (gWifiRetryAfterMs != 0 && static_cast<int32_t>(gWifiRetryAfterMs - now) > 0) {
      return;
    }
    if (staBusySinceMs_ == 0) {
      Serial.printf("wifi: retry %s len=%u\n", deviceConfig_->wifiSsid,
                    static_cast<unsigned>(strnlen(deviceConfig_->wifiSsid, 32)));
    }
    startWifiScan();
    return;
  }
  if (gStaGotIp) {
    gStaGotIp = false;
    mdnsOk_ = false;
    mdnsIp_ = 0;
  }
  if (!wifiRxReady_) {
    onWifiReady();
    return;
  }
  if (mdnsOk_) {
    return;
  }
  const uint32_t now = millis();
  if (lastMdnsAttemptMs_ != 0 && now - lastMdnsAttemptMs_ < kMdnsRetryMs) {
    return;
  }
  startMdns();
  if (mdnsOk_) {
    sendOscHello();
  }
}

bool Transport::resolveOscHost(uint32_t* ip) const {
  if (ip == nullptr || host_[0] == '\0') {
    return false;
  }
  IPAddress literal;
  if (literal.fromString(host_)) {
    *ip = static_cast<uint32_t>(literal);
    return *ip != 0;
  }

  char name[48];
  strlcpy(name, host_, sizeof(name));
  const size_t len = strlen(name);
  if (len > 6 && strcasecmp(name + len - 6, ".local") == 0) {
    name[len - 6] = '\0';
    const IPAddress found = MDNS.queryHost(name, 800);
    if (found == IPAddress()) {
      return false;
    }
    *ip = static_cast<uint32_t>(found);
    return true;
  }

  IPAddress found;
  if (WiFi.hostByName(host_, found) != 1 || found == IPAddress()) {
    return false;
  }
  *ip = static_cast<uint32_t>(found);
  return true;
}

void Transport::maintainHostAddress() {
  if (!useWifiClock_ || WiFi.status() != WL_CONNECTED || host_[0] == '\0') {
    return;
  }
  const uint32_t now = millis();
  const uint32_t refreshMs = hostIpValid_ ? 60000 : 3000;
  if (lastHostResolveMs_ != 0 && now - lastHostResolveMs_ < refreshMs) {
    return;
  }
  lastHostResolveMs_ = now;
  uint32_t resolved = 0;
  if (!resolveOscHost(&resolved)) {
    if (!hostIpValid_) {
      Serial.printf("wifi: OSC host %s did not resolve\n", host_);
    }
    return;
  }
  if (!hostIpValid_ || resolved != hostIp_) {
    hostIp_ = resolved;
    hostIpValid_ = true;
    Serial.printf("wifi: OSC host %s -> %s\n", host_, IPAddress(hostIp_).toString().c_str());
    return;
  }
  hostIpValid_ = true;
}

void Transport::onWifiReady() {
  strlcpy(connectedSsid_, WiFi.SSID().c_str(), sizeof(connectedSsid_));
  gRx.begin(listenPort_);
  wifiRxReady_ = true;
  startMdns();
  maintainHostAddress();
  lastOscHelloMs_ = millis();
  sendOscHello();
}

void Transport::startMdns() {
  lastMdnsAttemptMs_ = millis();
  if (!useWifiClock_ || !wifiEnabled_ || WiFi.status() != WL_CONNECTED) {
    return;
  }

  char hostname[32] = {};
  if (!buildMdnsHostname(hostname, sizeof(hostname))) {
    mdnsOk_ = false;
    Serial.println("mdns: hostname= begin failed");
    return;
  }
  const IPAddress ip = WiFi.localIP();
  if (ip == IPAddress(0U)) {
    mdnsOk_ = false;
    Serial.printf("mdns: hostname=%s.local begin failed\n", hostname);
    return;
  }
  const uint32_t ipValue = static_cast<uint32_t>(ip);
  if (mdnsOk_ && mdnsServiceAdded_ && mdnsPort_ == listenPort_ && mdnsIp_ == ipValue &&
      strcmp(mdnsAdvertised_, hostname) == 0) {
    return;
  }

  // Modem sleep drops multicast replies. Re-apply after the station has an IP.
  WiFi.setSleep(false);
  applyStaHostname();

  if (mdnsServiceAdded_ && (strcmp(mdnsAdvertised_, hostname) != 0 || mdnsPort_ != listenPort_)) {
    MDNS.end();
    mdnsServiceAdded_ = false;
    mdnsAdvertised_[0] = '\0';
  }

  if (!MDNS.begin(hostname)) {
    mdnsOk_ = false;
    Serial.printf("mdns: hostname=%s.local begin failed\n", hostname);
    return;
  }
  if (!mdnsServiceAdded_) {
    if (!MDNS.addService("midijuggler-rotary", "udp", listenPort_)) {
      mdnsOk_ = false;
      Serial.printf("mdns: hostname=%s.local service failed\n", hostname);
      return;
    }
    mdnsServiceAdded_ = true;
    mdnsPort_ = listenPort_;
    strlcpy(mdnsAdvertised_, hostname, sizeof(mdnsAdvertised_));
  }
  mdnsOk_ = true;
  mdnsIp_ = ipValue;
  Serial.printf("mdns: hostname=%s.local\n", hostname);
}

void Transport::stopMdns() {
  if (mdnsServiceAdded_ || mdnsOk_) {
    MDNS.end();
  }
  mdnsServiceAdded_ = false;
  mdnsOk_ = false;
  mdnsPort_ = 0;
  mdnsIp_ = 0;
  mdnsAdvertised_[0] = '\0';
}

void Transport::feedbackAddress(char* buffer, size_t length) const {
  if (buffer == nullptr || length == 0) {
    return;
  }
  buffer[0] = '\0';

  char hostname[32] = {};
  if (!mdnsOk_ || !buildMdnsHostname(hostname, sizeof(hostname)) || hostname[0] == '\0') {
    if (WiFi.status() == WL_CONNECTED) {
      strlcpy(buffer, WiFi.localIP().toString().c_str(), length);
    }
    return;
  }
  snprintf(buffer, length, "%s.local", hostname);
}

void Transport::disconnectWifi() {
  gRx.stop();
  wifiRxReady_ = false;
  wifiScanActive_ = false;
  gScanDone = false;
  gJoinPending = false;
  gJoinStartedMs = 0;
  gWifiRetryAfterMs = 0;
  connectedSsid_[0] = '\0';
  hostIpValid_ = false;
  stopMdns();
  WiFi.scanDelete();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void Transport::sendOscBpm(float bpm) {
  if (!readyToSendOsc()) {
    return;
  }
  sendOscFloat(IPAddress(hostIp_), oscPort_, kBpmAddress, bpm);
}

void Transport::sendOscInterval(const char* interval) {
  if (!readyToSendOsc()) {
    return;
  }
  sendOscString(IPAddress(hostIp_), oscPort_, kIntervalAddress, interval);
}

void Transport::sendOscHello() {
  if (!readyToSendOsc()) {
    return;
  }
  char feedbackHost[64] = {0};
  feedbackAddress(feedbackHost, sizeof(feedbackHost));
  Serial.printf("wifi: OSC hello -> %s:%u as %s:%u\n", IPAddress(hostIp_).toString().c_str(),
                oscPort_, feedbackHost, listenPort_);
  sendOscHelloPacket(IPAddress(hostIp_), oscPort_, feedbackHost, listenPort_);
}

bool Transport::shouldAcceptSerialBeat() const {
  // Host sends beats on serial when OSC feedback is not registered, even with
  // WiFi up. Rejecting serial beats while connected silenced the LED entirely.
  return useSerialClock_;
}

bool Transport::shouldAcceptOscBeat() const {
  return useWifiClock_;
}

void Transport::deliverBeat(float beat) {
  if (beat <= 0.5f || !onBeat_) {
    return;
  }
  const uint32_t now = millis();
  // Collapse duplicate serial/OSC delivery and same-loop serial bursts only.
  constexpr uint32_t kDuplicateBeatDedupMs = 25;
  if (lastBeatPulseMs_ != 0 && now - lastBeatPulseMs_ < kDuplicateBeatDedupMs) {
    return;
  }
  lastBeatPulseMs_ = now;
  onBeat_(beat);
}

void Transport::emitSync() {
  if (!onSync_) {
    return;
  }
  onSync_(pendingSync_);
}

void Transport::pollSerial() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      if (serialLength_ > 0) {
        serialLine_[serialLength_] = '\0';
        dispatchLine(serialLine_);
        serialLength_ = 0;
      }
      continue;
    }
    if (serialLength_ + 1 < sizeof(serialLine_)) {
      serialLine_[serialLength_++] = c;
    }
  }
}

void Transport::pollOsc() {
  while (true) {
    const int size = gRx.parsePacket();
    if (size <= 0) {
      return;
    }
    uint8_t buffer[192];
    const int read = gRx.read(buffer, sizeof(buffer));
    if (read <= 0) {
      continue;
    }
    char address[64] = {0};
    memcpy(address, buffer, min(static_cast<size_t>(read), sizeof(address) - 1));
    if (strcmp(address, kBeatAddress) == 0) {
      const size_t offset = oscFirstArgOffset(buffer, address);
      if (static_cast<size_t>(read) >= offset + sizeof(float)) {
        const float beat = readFloatBE(buffer + offset);
        if (shouldAcceptOscBeat()) {
          deliverBeat(beat);
        }
      }
      continue;
    }
    if (strcmp(address, kFeedbackBpmAddress) == 0) {
      const size_t offset = oscFirstArgOffset(buffer, address);
      if (static_cast<size_t>(read) >= offset + sizeof(float)) {
        pendingSync_.bpm = readFloatBE(buffer + offset);
        emitSync();
      }
      continue;
    }
    if (strcmp(address, kFeedbackRunningAddress) == 0) {
      const size_t offset = oscFirstArgOffset(buffer, address);
      if (static_cast<size_t>(read) >= offset + sizeof(int32_t)) {
        pendingSync_.running = readI32BE(buffer + offset) != 0;
        emitSync();
      }
      continue;
    }
    if (strcmp(address, kFeedbackClickEnabledAddress) == 0) {
      const size_t offset = oscFirstArgOffset(buffer, address);
      if (static_cast<size_t>(read) >= offset + sizeof(int32_t)) {
        pendingSync_.clickEnabled = readI32BE(buffer + offset) != 0;
        emitSync();
      }
      continue;
    }
    if (strcmp(address, kFeedbackClickIntervalAddress) == 0) {
      const char* tags = oscTags(buffer, address);
      if (tags[1] != 's') {
        continue;
      }
      const size_t offset = oscFirstArgOffset(buffer, address);
      if (offset < static_cast<size_t>(read)) {
        strlcpy(pendingSync_.clickInterval, reinterpret_cast<const char*>(buffer + offset),
                sizeof(pendingSync_.clickInterval));
        emitSync();
      }
      continue;
    }
    if (strcmp(address, kSyncAddress) != 0) {
      continue;
    }
    const char* tags = oscTags(buffer, address);
    size_t offset = oscFirstArgOffset(buffer, address);
    SyncPayload payload;
    int intArgIndex = 0;
    bool valid = true;
    for (size_t i = 1; tags[i] != '\0'; ++i) {
      switch (tags[i]) {
        case 'f':
          if (static_cast<size_t>(read) < offset + sizeof(float)) {
            valid = false;
          } else {
            payload.bpm = readFloatBE(buffer + offset);
            offset += sizeof(float);
          }
          break;
        case 'i': {
          if (static_cast<size_t>(read) < offset + sizeof(int32_t)) {
            valid = false;
          } else {
            const int32_t value = readI32BE(buffer + offset);
            offset += sizeof(int32_t);
            if (intArgIndex == 0) {
              payload.running = value != 0;
            } else {
              payload.clickEnabled = value != 0;
            }
            ++intArgIndex;
          }
          break;
        }
        case 's':
          if (offset >= static_cast<size_t>(read)) {
            valid = false;
          } else {
            strlcpy(payload.clickInterval, reinterpret_cast<const char*>(buffer + offset),
                    sizeof(payload.clickInterval));
            offset += pad4(strlen(reinterpret_cast<const char*>(buffer + offset)) + 1);
          }
          break;
        default:
          valid = false;
          break;
      }
      if (!valid) {
        break;
      }
    }
    if (valid) {
      pendingSync_ = payload;
      markOscSyncReceived();
      if (onSync_) {
        onSync_(payload);
      }
    }
  }
}

bool Transport::parseSyncLine(const char* line, SyncPayload* payload) {
  if (payload == nullptr || strncmp(line, "sync ", 5) != 0) {
    return false;
  }
  int running = 0;
  int clickEnabled = 0;
  if (sscanf(line + 5, "%f %d %d %15s", &payload->bpm, &running, &clickEnabled,
             payload->clickInterval) < 4) {
    return false;
  }
  payload->running = running != 0;
  payload->clickEnabled = clickEnabled != 0;
  return true;
}

bool Transport::parseBeatLine(const char* line, float* beat) {
  if (beat == nullptr || strncmp(line, "beat ", 5) != 0) {
    return false;
  }
  *beat = static_cast<float>(atof(line + 5));
  return true;
}

void Transport::dispatchLine(const char* line) {
  if (line == nullptr || line[0] == '#') {
    return;
  }
  if (handleConfigLine(line)) {
    return;
  }

  if (strcmp(line, "hello") == 0) {
    // Host may poke hello over USB when switching to OSC feedback.
    sendHello();
    return;
  }

  SyncPayload payload;
  float beat = 0.0f;
  if (parseSyncLine(line, &payload)) {
    pendingSync_ = payload;
    // USB sync keeps the display alive, but it is not OSC registration.
    // Counting it here stopped hello retries while the host stayed on serial.
    if (onSync_) {
      onSync_(payload);
    }
    return;
  }
  if (parseBeatLine(line, &beat)) {
    if (shouldAcceptSerialBeat()) {
      deliverBeat(beat);
    }
  }
}
