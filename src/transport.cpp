#include "transport.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>

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

void sendOscPacket(const char* host, uint16_t port, const uint8_t* data, size_t length) {
  gTx.beginPacket(host, port);
  gTx.write(data, length);
  gTx.endPacket();
}

void sendOscFloat(const char* host, uint16_t port, const char* address, float value) {
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
  sendOscPacket(host, port, packet, offset);
}

void sendOscString(const char* host, uint16_t port, const char* address, const char* value) {
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
  sendOscPacket(host, port, packet, offset);
}

void sendOscHelloPacket(const char* host, uint16_t port, const char* ip, uint16_t listenPort) {
  uint8_t packet[128] = {0};
  size_t offset = 0;
  const size_t addrLen = pad4(strlen(kHelloAddress) + 1);
  memcpy(packet + offset, kHelloAddress, strlen(kHelloAddress) + 1);
  offset += addrLen;
  const char* tags = ",si";
  memcpy(packet + offset, tags, 4);
  offset += pad4(4);
  const size_t ipLen = pad4(strlen(ip) + 1);
  memcpy(packet + offset, ip, strlen(ip) + 1);
  offset += ipLen;
  writeI32BE(packet + offset, static_cast<int32_t>(listenPort));
  offset += sizeof(int32_t);
  sendOscPacket(host, port, packet, offset);
}

void sendOscTrigger(const char* host, uint16_t port, const char* address) {
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
  sendOscPacket(host, port, packet, offset);
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
  strlcpy(host_, config.host, sizeof(host_));
  oscPort_ = config.oscPort;
  listenPort_ = config.listenPort;
  useSerialClock_ = config.transport != TransportMode::Wifi;
  useWifiClock_ = config.transport != TransportMode::Serial;
  wifiEnabled_ = config.wifiEnabled;

  if (useWifiClock_ && wifiEnabled_) {
    connectWifi(config, runWifiPortal);
  } else {
    disconnectWifi();
  }

  const bool transportChanged = wasSerialClock != useSerialClock_ || wasWifiClock != useWifiClock_;
  if (transportChanged) {
    // Force OSC hello after serial->wifi switches; stale serial sync masked registration.
    lastSyncMs_ = 0;
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
    pollOsc();
    if (!isOscConnected()) {
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
  if (useWifiClock_ && WiFi.status() == WL_CONNECTED) {
    sendOscFloat(host_, oscPort_, kBpmAddress, bpm);
  }
}

void Transport::sendStartStop() {
  if (useSerialClock_) {
    sendSerialLine("start_stop");
  }
  if (useWifiClock_ && WiFi.status() == WL_CONNECTED) {
    sendOscTrigger(host_, oscPort_, kStartStopAddress);
  }
}

void Transport::sendClickToggle() {
  if (useSerialClock_) {
    sendSerialLine("click_toggle");
  }
  if (useWifiClock_ && WiFi.status() == WL_CONNECTED) {
    sendOscTrigger(host_, oscPort_, kClickToggleAddress);
  }
}

void Transport::sendInterval(const char* interval) {
  if (useSerialClock_) {
    char line[32];
    snprintf(line, sizeof(line), "interval %s", interval);
    sendSerialLine(line);
  }
  if (useWifiClock_ && WiFi.status() == WL_CONNECTED) {
    sendOscString(host_, oscPort_, kIntervalAddress, interval);
  }
}

void Transport::sendTapTempo() {
  if (useSerialClock_) {
    sendSerialLine("tap_tempo");
  }
  if (useWifiClock_ && WiFi.status() == WL_CONNECTED) {
    sendOscTrigger(host_, oscPort_, kTapTempoAddress);
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
  if (lastSyncMs_ == 0) {
    return false;
  }
  return millis() - lastSyncMs_ < 10000;
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

void Transport::connectWifi(const DeviceConfig& config, bool runPortal) {
  gRx.stop();
  wifiRxReady_ = false;
  connectedSsid_[0] = '\0';
  WiFi.disconnect(true);

  if (!wifiEnabled_) {
    WiFi.mode(WIFI_OFF);
    return;
  }

  WiFi.mode(WIFI_STA);

  if (runPortal) {
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

  WiFi.begin(config.wifiSsid, config.wifiPass);
  Serial.printf("wifi: connecting to %s (non-blocking)\n", config.wifiSsid);
}

void Transport::maintainWifi() {
  if (!useWifiClock_ || !wifiEnabled_) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiRxReady_) {
      gRx.stop();
      wifiRxReady_ = false;
      connectedSsid_[0] = '\0';
      stopMdns();
    }
    return;
  }
  if (wifiRxReady_) {
    return;
  }
  onWifiReady();
}

void Transport::onWifiReady() {
  strlcpy(connectedSsid_, WiFi.SSID().c_str(), sizeof(connectedSsid_));
  gRx.begin(listenPort_);
  wifiRxReady_ = true;
  startMdns();
  lastOscHelloMs_ = millis();
  sendOscHello();
}

void Transport::startMdns() {
  stopMdns();
  if (!useWifiClock_ || !wifiEnabled_ || WiFi.status() != WL_CONNECTED) {
    return;
  }

  char hostname[32] = {0};
  if (deviceConfig_ != nullptr && deviceConfig_->mdnsHostname[0] != '\0') {
    strlcpy(hostname, deviceConfig_->mdnsHostname, sizeof(hostname));
  } else {
    uint8_t mac[6] = {0};
    WiFi.macAddress(mac);
    snprintf(hostname, sizeof(hostname), "rotary-%02x%02x%02x", mac[3], mac[4], mac[5]);
  }

  if (!MDNS.begin(hostname)) {
    Serial.println("mdns: begin failed");
    return;
  }
  MDNS.addService("midijuggler-rotary", "udp", listenPort_);
  Serial.printf("mdns: advertised %s.local\n", hostname);
}

void Transport::stopMdns() { MDNS.end(); }

void Transport::feedbackAddress(char* buffer, size_t length) const {
  if (buffer == nullptr || length == 0) {
    return;
  }
  buffer[0] = '\0';

  char hostname[32] = {0};
  if (deviceConfig_ != nullptr && deviceConfig_->mdnsHostname[0] != '\0') {
    strlcpy(hostname, deviceConfig_->mdnsHostname, sizeof(hostname));
  } else if (WiFi.status() == WL_CONNECTED) {
    uint8_t mac[6] = {0};
    WiFi.macAddress(mac);
    snprintf(hostname, sizeof(hostname), "rotary-%02x%02x%02x", mac[3], mac[4], mac[5]);
  }

  if (hostname[0] == '\0') {
    strlcpy(buffer, WiFi.localIP().toString().c_str(), length);
    return;
  }
  snprintf(buffer, length, "%s.local", hostname);
}

void Transport::disconnectWifi() {
  gRx.stop();
  wifiRxReady_ = false;
  connectedSsid_[0] = '\0';
  stopMdns();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void Transport::sendOscBpm(float bpm) { sendOscFloat(host_, oscPort_, kBpmAddress, bpm); }

void Transport::sendOscInterval(const char* interval) {
  sendOscString(host_, oscPort_, kIntervalAddress, interval);
}

void Transport::sendOscHello() {
  char feedbackHost[64] = {0};
  feedbackAddress(feedbackHost, sizeof(feedbackHost));
  sendOscHelloPacket(host_, oscPort_, feedbackHost, listenPort_);
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
  lastSyncMs_ = millis();
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
      if (onSync_) {
        lastSyncMs_ = millis();
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

  SyncPayload payload;
  float beat = 0.0f;
  if (parseSyncLine(line, &payload)) {
    pendingSync_ = payload;
    lastSyncMs_ = millis();
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
