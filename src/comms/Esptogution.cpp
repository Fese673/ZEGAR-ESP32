#include "Esptogution.h"
#include "esp_to_gution/EsptoGuitionState.h"
#include "esp_to_gution/EsptoGuitionTransport.h"
#include "AppSettings.h"

namespace EsptoGuition {
namespace {

uint32_t s_broadcastIntervalMs = kDefaultBroadcastIntervalMs;
uint32_t s_lastBroadcastMs = 0;
uint8_t s_sequence = 0;

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

void sendWeather(uint8_t sequence, unsigned long nowMs) {
  WeatherPayload payload;
  if (!buildWeatherPayload(payload, nowMs)) return;

  uint8_t buffer[16] = {};
  uint8_t *cursor = buffer;
  appendS16(cursor, payload.temperatureCx100);
  appendU16(cursor, payload.humidityPctX100);
  appendU16(cursor, payload.pressureHpaX10);
  appendU32(cursor, payload.sampleAgeMs);
  appendU8(cursor, payload.flags);

  sendRawFrame(kTypeWeather, sequence, buffer, static_cast<uint16_t>(cursor - buffer));
}

void sendPms(uint8_t sequence, unsigned long nowMs) {
  PmsPayload payload;
  if (!buildPmsPayload(payload, nowMs)) return;

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

void broadcastSnapshots(unsigned long nowMs) {
  if ((nowMs - s_lastBroadcastMs) < s_broadcastIntervalMs) return;
  s_lastBroadcastMs = nowMs;
  sendWeather(s_sequence++, nowMs);
  sendPms(s_sequence++, nowMs);
  sendTime(s_sequence++);
}

} // namespace

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
  uint8_t payload[5];
  payload[0] = state.buzzerEnabled ? 1 : 0;
  payload[1] = state.mqttEnabled ? 1 : 0;
  payload[2] = state.touchTestEnabled ? 1 : 0;
  payload[3] = state.backgroundMusicEnabled ? 1 : 0;
  payload[4] = true; // PMS always enabled / default true
  sendRawFrame(kTypeSettings, sequence, payload, sizeof(payload));
}

} // namespace EsptoGuition
