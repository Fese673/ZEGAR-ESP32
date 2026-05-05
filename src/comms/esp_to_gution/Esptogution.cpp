#include "Esptogution.h"
#include "Config.h"
#include "EsptoGuitionState.h"
#include "EsptoGuitionTransport.h"
#include "AppSettings.h"

namespace EsptoGuition {

using namespace Config;

uint32_t s_broadcastIntervalMs = kDefaultBroadcastIntervalMs;
uint32_t s_lastBroadcastMs = 0;
uint8_t s_sequence = 0;

WeatherPayload s_lastSentWeather;
PmsPayload s_lastSentPms;
WifiPayload s_lastSentWifi;
SystemResourcesPayload s_lastSentResources;
unsigned long s_lastTimeSentS = 0;

namespace {

void appendU8(uint8_t *&cursor, uint8_t value) { *cursor++ = value; }

void appendU16(uint8_t *&cursor, uint16_t value) {
  *cursor++ = static_cast<uint8_t>(value & 0xFFU);
  *cursor++ = static_cast<uint8_t>((value >> 8) & 0xFFU);
}

void appendS16(uint8_t *&cursor, int16_t value) {
  appendU16(cursor, static_cast<uint16_t>(value));
}

void appendU32(uint8_t *&cursor, uint32_t value) {
  *cursor++ = static_cast<uint8_t>(value & 0xFFU);
  *cursor++ = static_cast<uint8_t>((value >> 8) & 0xFFU);
  *cursor++ = static_cast<uint8_t>((value >> 16) & 0xFFU);
  *cursor++ = static_cast<uint8_t>((value >> 24) & 0xFFU);
}

} // namespace

void sendWeather(uint8_t sequence) {
  WeatherPayload payload;
  if (!buildWeatherPayload(payload, millis())) return;

  uint8_t buffer[16] = {};
  uint8_t *cursor = buffer;
  appendS16(cursor, payload.temperatureCx100);
  appendU16(cursor, payload.humidityPctX100);
  appendU16(cursor, payload.pressureHpaX10);
  appendU32(cursor, payload.sampleAgeMs);
  appendU8(cursor, payload.flags);

  sendRawFrame(kTypeWeather, sequence, buffer, static_cast<uint16_t>(cursor - buffer));
}

void sendPms(uint8_t sequence) {
  PmsPayload payload;
  if (!buildPmsPayload(payload, millis())) return;

  uint8_t buffer[23] = {};
  uint8_t *cursor = buffer;
  appendU16(cursor, payload.pm01);
  appendU16(cursor, payload.pm25);
  appendU16(cursor, payload.pm10);
  appendU16(cursor, payload.count0p3);
  appendU16(cursor, payload.count0p5);
  appendU16(cursor, payload.count1p0);
  appendU16(cursor, payload.count2p5);
  appendU16(cursor, payload.count5p0);
  appendU16(cursor, payload.count10p0);
  appendU32(cursor, payload.sampleAgeMs);
  appendU8(cursor, payload.flags);

  sendRawFrame(kTypePms, sequence, buffer, static_cast<uint16_t>(cursor - buffer));
}

void sendTime(uint8_t sequence) {
  TimePayload payload;
  if (!buildTimePayload(payload)) return;

  uint8_t buffer[5] = {};
  uint8_t *cursor = buffer;
  appendU32(cursor, payload.unixSeconds);
  appendU8(cursor, payload.valid);

  sendRawFrame(kTypeTime, sequence, buffer, static_cast<uint16_t>(cursor - buffer));
}

void sendWifiStatus(uint8_t sequence) {
  WifiPayload payload;
  if (!buildWifiPayload(payload)) return;

  uint8_t buffer[6] = {};
  uint8_t *cursor = buffer;
  appendU8(cursor, payload.connected ? 1 : 0);
  appendU8(cursor, static_cast<uint8_t>(payload.rssi));
  appendU8(cursor, payload.ip[0]);
  appendU8(cursor, payload.ip[1]);
  appendU8(cursor, payload.ip[2]);
  appendU8(cursor, payload.ip[3]);

  sendRawFrame(kTypeWifiStatus, sequence, buffer, static_cast<uint16_t>(cursor - buffer));
}

void broadcastSnapshots(unsigned long nowMs) {
  static unsigned long lastKeepaliveMs = 0;
  static unsigned long lastSafetyRefreshMs = 0;

  constexpr unsigned long kKeepaliveIntervalMs = Config::kKeepaliveIntervalMs;
  constexpr unsigned long kSafetyRefreshIntervalMs = Config::kSafetyRefreshIntervalMs;

  WeatherPayload currentWeather;
  if (buildWeatherPayload(currentWeather, nowMs)) {
    bool changed = (currentWeather.temperatureCx100 != s_lastSentWeather.temperatureCx100) ||
                  (currentWeather.humidityPctX100 != s_lastSentWeather.humidityPctX100) ||
                  (currentWeather.pressureHpaX10 != s_lastSentWeather.pressureHpaX10);
    if (changed) {
      sendWeather(s_sequence++);
      s_lastSentWeather = currentWeather;
      lastKeepaliveMs = nowMs;
    }
  }

  PmsPayload currentPms;
  if (buildPmsPayload(currentPms, nowMs)) {
    bool changed = (currentPms.pm01 != s_lastSentPms.pm01) ||
                 (currentPms.pm25 != s_lastSentPms.pm25) ||
                 (currentPms.pm10 != s_lastSentPms.pm10);
    if (changed) {
      sendPms(s_sequence++);
      s_lastSentPms = currentPms;
      lastKeepaliveMs = nowMs;
    }
  }

  TimePayload currentTime;
  if (buildTimePayload(currentTime)) {
    if (currentTime.unixSeconds != s_lastTimeSentS) {
      sendTime(s_sequence++);
      s_lastTimeSentS = currentTime.unixSeconds;
      lastKeepaliveMs = nowMs;
    }
  }

  WifiPayload currentWifi;
  if (buildWifiPayload(currentWifi)) {
    bool changed = (currentWifi.connected != s_lastSentWifi.connected) ||
                 (currentWifi.rssi != s_lastSentWifi.rssi) ||
                 (currentWifi.ip[0] != s_lastSentWifi.ip[0]) ||
                 (currentWifi.ip[1] != s_lastSentWifi.ip[1]) ||
                 (currentWifi.ip[2] != s_lastSentWifi.ip[2]) ||
                 (currentWifi.ip[3] != s_lastSentWifi.ip[3]);
    if (changed) {
      sendWifiStatus(s_sequence++);
      s_lastSentWifi = currentWifi;
      lastKeepaliveMs = nowMs;
    }
  }

  static unsigned long lastResourcesCheckMs = 0;
  if (nowMs - lastResourcesCheckMs >= Config::kSignals[Config::SIGNAL_SYSTEM_RESOURCES].periodicIntervalMs) {
    lastResourcesCheckMs = nowMs;
    SystemResourcesPayload currentRes;
    if (buildSystemResourcesPayload(currentRes)) {
      bool changed = (currentRes.core0Cpu != s_lastSentResources.core0Cpu) ||
                    (currentRes.core1Cpu != s_lastSentResources.core1Cpu) ||
                    (currentRes.freeRam != s_lastSentResources.freeRam);
      if (changed) {
        sendSystemResources(s_sequence++);
        s_lastSentResources = currentRes;
        lastKeepaliveMs = nowMs;
      }
    }
  }

  if (nowMs - lastKeepaliveMs >= kKeepaliveIntervalMs) {
    lastKeepaliveMs = nowMs;
    sendHelloAck(s_sequence++);
  }

  if (nowMs - lastSafetyRefreshMs >= kSafetyRefreshIntervalMs) {
    lastSafetyRefreshMs = nowMs;
    sendWeather(s_sequence++);
    sendPms(s_sequence++);
    sendTime(s_sequence++);
    sendWifiStatus(s_sequence++);
    sendSystemResources(s_sequence++);
    sendSettings(s_sequence++);
    buildWeatherPayload(s_lastSentWeather, nowMs);
    buildPmsPayload(s_lastSentPms, nowMs);
    buildWifiPayload(s_lastSentWifi);
    buildSystemResourcesPayload(s_lastSentResources);
  }
}

void sendSystemResources(uint8_t sequence) {
  SystemResourcesPayload payload;
  if (!buildSystemResourcesPayload(payload)) return;

  uint8_t buffer[32] = {};
  uint8_t *cursor = buffer;
  appendU32(cursor, payload.freeRam);
  appendU32(cursor, payload.heapRam);
  appendU32(cursor, payload.dmaRam);
  appendU8(cursor, payload.core0Cpu);
  appendU8(cursor, payload.core1Cpu);
  appendU32(cursor, payload.freeFlash);
  appendU32(cursor, payload.usedFlash);
  appendU16(cursor, payload.underrunsAudio);
  appendU16(cursor, payload.overflowAudio);
  appendU16(cursor, payload.dropsAudio);
  appendU16(cursor, payload.errorsI2c);
  appendU16(cursor, payload.timeoutsI2c);

  sendRawFrame(kTypeSystemResources, sequence, buffer, static_cast<uint16_t>(cursor - buffer));
}

void begin(HardwareSerial &serialPort, uint32_t baudRate, int rxPin, int txPin) {
  beginSerial(serialPort, baudRate, rxPin, txPin);
}

void update() {
  ingestSerialBytes();
  broadcastSnapshots(millis());
}

void setBroadcastIntervalMs(uint32_t intervalMs) {
  s_broadcastIntervalMs = intervalMs;
}

uint32_t getBroadcastIntervalMs() {
  return s_broadcastIntervalMs;
}

bool isReady() {
  return true;
}

void sendSettings(uint8_t sequence) {
  AppSettings::State& state = AppSettings::mutableState();
  uint8_t payload[6];
  payload[0] = state.buzzerEnabled ? 1 : 0;
  payload[1] = state.mqttEnabled ? 1 : 0;
  payload[2] = state.touchTestEnabled ? 1 : 0;
  payload[3] = state.backgroundMusicEnabled ? 1 : 0;
  payload[4] = true; // PMS always enabled / default true
  payload[5] = static_cast<uint8_t>(state.alarmMelodyIndex);
  sendRawFrame(kTypeSettings, sequence, payload, sizeof(payload));
}

uint8_t nextSequence() {
  return s_sequence++;
}

} // namespace EsptoGuition
