#include "transport.h"

#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>

namespace {

constexpr const char* kBpmAddress = "/midijuggler/clock/bpm";
constexpr const char* kStartStopAddress = "/midijuggler/clock/start_stop";
constexpr const char* kClickToggleAddress = "/midijuggler/clock/click_toggle";
constexpr const char* kIntervalAddress = "/midijuggler/clock/click_interval";
constexpr const char* kHelloAddress = "/midijuggler/rotary/hello";
constexpr const char* kSyncAddress = "/midijuggler/rotary/sync";
constexpr const char* kBeatAddress = "/midijuggler/rotary/beat";

WiFiUDP gTx;
WiFiUDP gRx;

size_t pad4(size_t size) { return (size + 3) & ~static_cast<size_t>(3); }

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
  memcpy(packet + offset, &value, sizeof(value));
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

void sendOscHello(const char* host, uint16_t port, const char* ip, uint16_t listenPort) {
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
  const int32_t portValue = listenPort;
  memcpy(packet + offset, &portValue, sizeof(portValue));
  offset += sizeof(portValue);
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
  const int32_t value = 1;
  memcpy(packet + offset, &value, sizeof(value));
  offset += sizeof(value);
  sendOscPacket(host, port, packet, offset);
}

}  // namespace

void Transport::begin(const DeviceConfig& config, SyncCallback onSync, BeatCallback onBeat) {
  onSync_ = std::move(onSync);
  onBeat_ = std::move(onBeat);
  strlcpy(host_, config.host, sizeof(host_));
  oscPort_ = config.oscPort;
  listenPort_ = config.listenPort;
  useSerial_ = config.transport != TransportMode::Wifi;
  useWifi_ = config.transport != TransportMode::Serial;

  if (useWifi_) {
    WiFiManager manager;
    manager.setConfigPortalTimeout(120);
    manager.autoConnect("RotaryDisplay-Setup");
    if (WiFi.status() == WL_CONNECTED) {
      gRx.begin(listenPort_);
      sendOscHello(host_, oscPort_, WiFi.localIP().toString().c_str(), listenPort_);
    }
  }
  if (useSerial_) {
    sendSerialLine("hello");
  }
}

void Transport::loop() {
  if (useSerial_) {
    pollSerial();
  }
  if (useWifi_ && WiFi.status() == WL_CONNECTED) {
    pollOsc();
  }
}

void Transport::sendBpm(float bpm) {
  char line[32];
  snprintf(line, sizeof(line), "bpm %.1f", bpm);
  sendSerialLine(line);
  if (useWifi_ && WiFi.status() == WL_CONNECTED) {
    sendOscFloat(host_, oscPort_, kBpmAddress, bpm);
  }
}

void Transport::sendStartStop() {
  sendSerialLine("start_stop");
  if (useWifi_ && WiFi.status() == WL_CONNECTED) {
    sendOscTrigger(host_, oscPort_, kStartStopAddress);
  }
}

void Transport::sendClickToggle() {
  sendSerialLine("click_toggle");
  if (useWifi_ && WiFi.status() == WL_CONNECTED) {
    sendOscTrigger(host_, oscPort_, kClickToggleAddress);
  }
}

void Transport::sendInterval(const char* interval) {
  char line[32];
  snprintf(line, sizeof(line), "interval %s", interval);
  sendSerialLine(line);
  if (useWifi_ && WiFi.status() == WL_CONNECTED) {
    sendOscString(host_, oscPort_, kIntervalAddress, interval);
  }
}

void Transport::sendHello() {
  if (useSerial_) {
    sendSerialLine("hello");
  }
  if (useWifi_ && WiFi.status() == WL_CONNECTED) {
    sendOscHello(host_, oscPort_, WiFi.localIP().toString().c_str(), listenPort_);
  }
}

void Transport::handleSerialConfigLine(const char* line) {
  if (configHandler_) {
    configHandler_(line);
  }
  if (strncmp(line, "host ", 5) == 0) {
    strlcpy(host_, line + 5, sizeof(host_));
  } else if (strncmp(line, "port ", 5) == 0) {
    oscPort_ = static_cast<uint16_t>(atoi(line + 5));
  }
}

void Transport::setConfigHandler(std::function<void(const char* line)> handler) {
  configHandler_ = std::move(handler);
}

void Transport::sendSerialLine(const char* line) {
  if (!useSerial_) {
    return;
  }
  Serial.println(line);
}

void Transport::sendOscAddress(const char* address) {
  if (useWifi_ && WiFi.status() == WL_CONNECTED) {
    sendOscTrigger(host_, oscPort_, address);
  }
}

void Transport::sendOscBpm(float bpm) { sendOscFloat(host_, oscPort_, kBpmAddress, bpm); }
void Transport::sendOscInterval(const char* interval) {
  sendOscString(host_, oscPort_, kIntervalAddress, interval);
}
void Transport::sendOscHello() {
  sendOscHello(host_, oscPort_, WiFi.localIP().toString().c_str(), listenPort_);
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
  const int size = gRx.parsePacket();
  if (size <= 0) {
    return;
  }
  uint8_t buffer[192];
  const int read = gRx.read(buffer, sizeof(buffer));
  if (read <= 0) {
    return;
  }
  char address[64] = {0};
  memcpy(address, buffer, min(static_cast<size_t>(read), sizeof(address) - 1));
  if (strcmp(address, kBeatAddress) == 0) {
    float beat = 0.0f;
    const size_t offset = pad4(strlen(address) + 1) + pad4(2);
    if (static_cast<size_t>(read) >= offset + sizeof(beat)) {
      memcpy(&beat, buffer + offset, sizeof(beat));
      if (onBeat_) {
        onBeat_(beat);
      }
    }
    return;
  }
  if (strcmp(address, kSyncAddress) != 0) {
    return;
  }
  SyncPayload payload;
  size_t offset = pad4(strlen(address) + 1) + pad4(2);
  if (static_cast<size_t>(read) < offset + 12) {
    return;
  }
  memcpy(&payload.bpm, buffer + offset, sizeof(payload.bpm));
  offset += 4;
  int32_t running = 0;
  int32_t clickEnabled = 0;
  memcpy(&running, buffer + offset, sizeof(running));
  offset += 4;
  memcpy(&clickEnabled, buffer + offset, sizeof(clickEnabled));
  offset += 4;
  payload.running = running != 0;
  payload.clickEnabled = clickEnabled != 0;
  if (offset < static_cast<size_t>(read)) {
    strlcpy(payload.clickInterval, reinterpret_cast<const char*>(buffer + offset),
            sizeof(payload.clickInterval));
  }
  if (onSync_) {
    onSync_(payload);
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
  if (strncmp(line, "host ", 5) == 0 || strncmp(line, "port ", 5) == 0 ||
      strncmp(line, "transport ", 10) == 0) {
    handleSerialConfigLine(line);
    return;
  }
  SyncPayload payload;
  float beat = 0.0f;
  if (parseSyncLine(line, &payload)) {
    if (onSync_) {
      onSync_(payload);
    }
    return;
  }
  if (parseBeatLine(line, &beat)) {
    if (onBeat_) {
      onBeat_(beat);
    }
  }
}
