#include "Esptogution.h"

#include <Arduino.h>

#include <math.h>
#include <string.h>

#include "esp_system.h"

#include "AppLog.h"
#include "BMP280Screen.h"
#include "ENS160AHT21Screen.h"
#include "PMS_Czujnik.h"
#include "RTCService.h"

namespace EsptoGuition {
namespace {

constexpr char TAG[] = "GUI";
constexpr uint8_t kFrameHeaderBytes = 4;
constexpr uint8_t kFrameCrcBytes = 2;
constexpr uint8_t kAckOk = 0x00;
constexpr uint8_t kAckBadType = 0x01;
constexpr uint8_t kAckBadLength = 0x02;
constexpr uint8_t kAckBadCrc = 0x03;
constexpr uint8_t kAckBadConfig = 0x04;
constexpr bool kUseSyntheticPayloads = true;

HardwareSerial *s_serial = nullptr;
uint32_t s_broadcastIntervalMs = kDefaultBroadcastIntervalMs;
uint32_t s_lastBroadcastMs = 0;
uint8_t s_sequence = 0;

struct RxState {
  enum class Mode : uint8_t {
    WaitStart = 0,
    Header,
    Payload,
  };

  Mode mode = Mode::WaitStart;
  uint8_t header[kFrameHeaderBytes] = {};
  uint16_t headerIndex = 0;
  uint16_t payloadLength = 0;
  uint16_t payloadIndex = 0;
  uint8_t payload[kMaxPayloadBytes + kFrameCrcBytes] = {};
  unsigned long lastByteMs = 0;
  uint8_t type = 0;
  uint8_t sequence = 0;
};

RxState s_rx;

struct WeatherPayload {
  int16_t temperatureCx100 = 0;
  uint16_t humidityPctX100 = 0;
  uint16_t pressureHpaX10 = 0;
  uint32_t sampleAgeMs = 0;
  uint8_t flags = 0;
};

struct PmsPayload {
  uint16_t pm01 = 0;
  uint16_t pm25 = 0;
  uint16_t pm10 = 0;
  uint16_t count0p3 = 0;
  uint16_t count0p5 = 0;
  uint16_t count1p0 = 0;
  uint16_t count2p5 = 0;
  uint16_t count5p0 = 0;
  uint16_t count10p0 = 0;
  uint32_t sampleAgeMs = 0;
  uint8_t flags = 0;
};

struct TimePayload {
  uint32_t unixSeconds = 0;
  uint8_t valid = 0;
};

void seedSyntheticRandom() {
  static bool s_seeded = false;
  if (!s_seeded) {
    randomSeed(esp_random());
    s_seeded = true;
  }
}

int16_t randomTemperatureCx100() {
  return static_cast<int16_t>(random(-400, 3801));
}

uint16_t randomHumidityPctX100() {
  return static_cast<uint16_t>(random(2000, 9001));
}

uint16_t randomPressureHpaX10() {
  return static_cast<uint16_t>(random(9800, 10461));
}

uint32_t randomSampleAgeMs() {
  return static_cast<uint32_t>(random(0, 120000));
}

void buildSyntheticWeatherPayload(WeatherPayload &out) {
  seedSyntheticRandom();
  out.temperatureCx100 = randomTemperatureCx100();
  out.humidityPctX100 = randomHumidityPctX100();
  out.pressureHpaX10 = randomPressureHpaX10();
  out.sampleAgeMs = randomSampleAgeMs();
  out.flags = 0x07U;
}

void buildSyntheticPmsPayload(PmsPayload &out) {
  seedSyntheticRandom();
  out.pm01 = static_cast<uint16_t>(random(0, 85));
  out.pm25 = static_cast<uint16_t>(random(0, 150));
  out.pm10 = static_cast<uint16_t>(random(0, 180));
  out.count0p3 = static_cast<uint16_t>(random(0, 5000));
  out.count0p5 = static_cast<uint16_t>(random(0, 4000));
  out.count1p0 = static_cast<uint16_t>(random(0, 3000));
  out.count2p5 = static_cast<uint16_t>(random(0, 2000));
  out.count5p0 = static_cast<uint16_t>(random(0, 1000));
  out.count10p0 = static_cast<uint16_t>(random(0, 500));
  out.sampleAgeMs = randomSampleAgeMs();
  out.flags = 0x01U;
}

uint16_t crc16Ccitt(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFFU;
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if ((crc & 0x8000U) != 0U) {
        crc = static_cast<uint16_t>((crc << 1) ^ 0x1021U);
      } else {
        crc = static_cast<uint16_t>(crc << 1);
      }
    }
  }
  return crc;
}

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

void resetRx() { s_rx = RxState{}; }

void sendFrame(uint8_t type, uint8_t sequence, const uint8_t *payload,
               uint16_t payloadLength) {
  if (s_serial == nullptr || payloadLength > kMaxPayloadBytes) {
    return;
  }

  uint8_t frame[1 + kFrameHeaderBytes + kMaxPayloadBytes + kFrameCrcBytes] = {};
  uint8_t *cursor = frame;
  *cursor++ = kFrameStart;
  *cursor++ = type;
  *cursor++ = sequence;
  *cursor++ = static_cast<uint8_t>(payloadLength & 0xFFU);
  *cursor++ = static_cast<uint8_t>((payloadLength >> 8) & 0xFFU);

  if (payloadLength > 0U && payload != nullptr) {
    memcpy(cursor, payload, payloadLength);
    cursor += payloadLength;
  }

  const uint16_t crc = crc16Ccitt(
      frame + 1, static_cast<size_t>(kFrameHeaderBytes + payloadLength));
  *cursor++ = static_cast<uint8_t>(crc & 0xFFU);
  *cursor++ = static_cast<uint8_t>((crc >> 8) & 0xFFU);

  s_serial->write(frame, static_cast<size_t>(cursor - frame));
}

void sendAck(uint8_t sequence, uint8_t ackCode, uint8_t relatedType) {
  const uint8_t payload[2] = {ackCode, relatedType};
  sendFrame(kTypeAck, sequence, payload, sizeof(payload));
}

float pickWeatherTemperature(bool &valid, bool &fromEns160) {
  valid = false;
  fromEns160 = false;

  if (ENS160AHT21Screen::runtimeData.hasClimateSample &&
      isfinite(ENS160AHT21Screen::runtimeData.temperatureC)) {
    valid = true;
    fromEns160 = true;
    return ENS160AHT21Screen::runtimeData.temperatureC;
  }

  if (BMP280Screen::runtimeData.hasTemperature &&
      isfinite(BMP280Screen::runtimeData.temperatureC)) {
    valid = true;
    return BMP280Screen::runtimeData.temperatureC;
  }

  return 0.0f;
}

float pickWeatherHumidity(bool &valid) {
  valid = false;
  if (ENS160AHT21Screen::runtimeData.hasClimateSample &&
      isfinite(ENS160AHT21Screen::runtimeData.humidityPct)) {
    valid = true;
    return ENS160AHT21Screen::runtimeData.humidityPct;
  }

  return 0.0f;
}

float pickWeatherPressure(bool &valid) {
  valid = false;
  if (BMP280Screen::runtimeData.hasPressure &&
      isfinite(BMP280Screen::runtimeData.pressureHpa)) {
    valid = true;
    return BMP280Screen::runtimeData.pressureHpa;
  }

  return 0.0f;
}

uint32_t latestWeatherAgeMs(unsigned long nowMs) {
  unsigned long lastSampleMs = 0;
  if (BMP280Screen::runtimeData.hasSample) {
    lastSampleMs = BMP280Screen::runtimeData.lastSampleMs;
  }
  if (ENS160AHT21Screen::runtimeData.hasSample &&
      ENS160AHT21Screen::runtimeData.lastUpdateMs > lastSampleMs) {
    lastSampleMs = ENS160AHT21Screen::runtimeData.lastUpdateMs;
  }

  if (lastSampleMs == 0UL || nowMs < lastSampleMs) {
    return 0;
  }

  return static_cast<uint32_t>(nowMs - lastSampleMs);
}

bool buildWeatherPayload(WeatherPayload &out, unsigned long nowMs) {
  if (kUseSyntheticPayloads) {
    buildSyntheticWeatherPayload(out);
    return true;
  }

  bool tempValid = false;
  bool tempFromEns = false;
  bool humValid = false;
  bool pressureValid = false;

  const float temperatureC = pickWeatherTemperature(tempValid, tempFromEns);
  const float humidityPct = pickWeatherHumidity(humValid);
  const float pressureHpa = pickWeatherPressure(pressureValid);

  out.temperatureCx100 =
      tempValid ? static_cast<int16_t>(lroundf(temperatureC * 100.0f)) : 0;
  out.humidityPctX100 =
      humValid ? static_cast<uint16_t>(lroundf(humidityPct * 100.0f)) : 0;
  out.pressureHpaX10 =
      pressureValid ? static_cast<uint16_t>(lroundf(pressureHpa * 10.0f)) : 0;
  out.sampleAgeMs = latestWeatherAgeMs(nowMs);
  out.flags = 0;
  if (tempValid) {
    out.flags |= 0x01U;
  }
  if (humValid) {
    out.flags |= 0x02U;
  }
  if (pressureValid) {
    out.flags |= 0x04U;
  }
  if (tempFromEns) {
    out.flags |= 0x08U;
  }
  if (ENS160AHT21Screen::runtimeData.hasClimateSample) {
    out.flags |= 0x10U;
  }
  if (BMP280Screen::runtimeData.hasPressure) {
    out.flags |= 0x20U;
  }

  return true;
}

bool buildPmsPayload(PmsPayload &out, unsigned long nowMs) {
  if (kUseSyntheticPayloads) {
    buildSyntheticPmsPayload(out);
    return true;
  }

  if (PMS5003Sensor::isOk()) {
    const PMS5003Sensor::MassReadings atmospheric =
        PMS5003Sensor::getAtmospheric();
    const PMS5003Sensor::ParticleCounts particles =
        PMS5003Sensor::getParticleCounts();
    const PMS5003Sensor::Stats stats = PMS5003Sensor::getStats();

    out.pm01 = atmospheric.pm01;
    out.pm25 = atmospheric.pm25;
    out.pm10 = atmospheric.pm10;
    out.count0p3 = particles.count0p3;
    out.count0p5 = particles.count0p5;
    out.count1p0 = particles.count1p0;
    out.count2p5 = particles.count2p5;
    out.count5p0 = particles.count5p0;
    out.count10p0 = particles.count10p0;
    out.sampleAgeMs =
        (stats.lastFrameTime != 0UL && nowMs >= stats.lastFrameTime)
            ? static_cast<uint32_t>(nowMs - stats.lastFrameTime)
            : 0U;
    out.flags = 0x01U;
  }

  return true;
}

bool buildTimePayload(TimePayload &out) {
  time_t epoch = 0;
  if (RTCService::getEpoch(&epoch) == RTCService::Status::Ok && epoch > 0) {
    out.unixSeconds = static_cast<uint32_t>(epoch);
    out.valid = 1U;
    return true;
  }

  const time_t systemEpoch = time(nullptr);
  if (systemEpoch > 0) {
    out.unixSeconds = static_cast<uint32_t>(systemEpoch);
    out.valid = 1U;
    return true;
  }

  out.unixSeconds = 0;
  out.valid = 0;
  return true;
}

void sendWeather(uint8_t sequence, unsigned long nowMs) {
  WeatherPayload payload;
  if (!buildWeatherPayload(payload, nowMs)) {
    sendAck(sequence, kAckBadType, kTypeWeather);
    return;
  }

  uint8_t buffer[16] = {};
  uint8_t *cursor = buffer;
  appendS16(cursor, payload.temperatureCx100);
  appendU16(cursor, payload.humidityPctX100);
  appendU16(cursor, payload.pressureHpaX10);
  appendU32(cursor, payload.sampleAgeMs);
  appendU8(cursor, payload.flags);
  sendFrame(kTypeWeather, sequence, buffer,
            static_cast<uint16_t>(cursor - buffer));
}

void sendPms(uint8_t sequence, unsigned long nowMs) {
  PmsPayload payload;
  if (!buildPmsPayload(payload, nowMs)) {
    sendAck(sequence, kAckBadType, kTypePms);
    return;
  }

  // PMS frame payload size:
  // 9xU16 (18B) + U32 (4B) + U8 (1B) = 23 bytes.
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
  sendFrame(kTypePms, sequence, buffer, static_cast<uint16_t>(cursor - buffer));
}

void sendTime(uint8_t sequence) {
  TimePayload payload;
  if (!buildTimePayload(payload)) {
    sendAck(sequence, kAckBadType, kTypeTime);
    return;
  }

  uint8_t buffer[5] = {};
  uint8_t *cursor = buffer;
  appendU32(cursor, payload.unixSeconds);
  appendU8(cursor, payload.valid);
  sendFrame(kTypeTime, sequence, buffer,
            static_cast<uint16_t>(cursor - buffer));
}

void sendType(uint8_t type, uint8_t sequence) {
  const unsigned long nowMs = millis();
  switch (type) {
  case kTypeWeather:
    sendWeather(sequence, nowMs);
    break;
  case kTypePms:
    sendPms(sequence, nowMs);
    break;
  case kTypeTime:
    sendTime(sequence);
    break;
  default:
    sendAck(sequence, kAckBadType, type);
    break;
  }
}

void handleFrame(uint8_t type, uint8_t sequence, const uint8_t *payload,
                 uint16_t payloadLength) {
  switch (type) {
  case kTypeRequest:
    if (payloadLength < 1U) {
      sendAck(sequence, kAckBadLength, type);
      return;
    }
    sendType(payload[0], sequence);
    return;

  case kTypeConfig:
    if (payloadLength != 4U) {
      sendAck(sequence, kAckBadConfig, type);
      return;
    }
    s_broadcastIntervalMs = static_cast<uint32_t>(payload[0]) |
                            (static_cast<uint32_t>(payload[1]) << 8U) |
                            (static_cast<uint32_t>(payload[2]) << 16U) |
                            (static_cast<uint32_t>(payload[3]) << 24U);
    if (s_broadcastIntervalMs < 1000UL) {
      s_broadcastIntervalMs = 1000UL;
    }
    sendAck(sequence, kAckOk, type);
    LOG_I(TAG, "config broadcast_interval_ms=%lu",
          static_cast<unsigned long>(s_broadcastIntervalMs));
    return;

  case kTypeAck:
    return;

  default:
    sendAck(sequence, kAckBadType, type);
    return;
  }
}

void parseIncoming() {
  if (s_serial == nullptr) {
    return;
  }

  while (s_serial->available() > 0) {
    const int rawByte = s_serial->read();
    if (rawByte < 0) {
      break;
    }

    const uint8_t byte = static_cast<uint8_t>(rawByte);
    const unsigned long nowMs = millis();

    if (s_rx.mode != RxState::Mode::WaitStart &&
        (nowMs - s_rx.lastByteMs) > kDefaultFrameTimeoutMs) {
      resetRx();
    }
    s_rx.lastByteMs = nowMs;

    switch (s_rx.mode) {
    case RxState::Mode::WaitStart:
      if (byte == kFrameStart) {
        s_rx.mode = RxState::Mode::Header;
        s_rx.headerIndex = 0;
      }
      break;

    case RxState::Mode::Header:
      s_rx.header[s_rx.headerIndex++] = byte;
      if (s_rx.headerIndex < kFrameHeaderBytes) {
        break;
      }

      s_rx.type = s_rx.header[0];
      s_rx.sequence = s_rx.header[1];
      s_rx.payloadLength = static_cast<uint16_t>(s_rx.header[2]) |
                           (static_cast<uint16_t>(s_rx.header[3]) << 8U);
      if (s_rx.payloadLength > kMaxPayloadBytes) {
        sendAck(s_rx.sequence, kAckBadLength, s_rx.type);
        resetRx();
        break;
      }

      s_rx.payloadIndex = 0;
      s_rx.mode = RxState::Mode::Payload;
      break;

    case RxState::Mode::Payload:
      s_rx.payload[s_rx.payloadIndex++] = byte;
      if (s_rx.payloadIndex <
          static_cast<uint16_t>(s_rx.payloadLength + kFrameCrcBytes)) {
        break;
      }

      {
        const uint16_t receivedCrc =
            static_cast<uint16_t>(s_rx.payload[s_rx.payloadLength]) |
            (static_cast<uint16_t>(s_rx.payload[s_rx.payloadLength + 1U])
             << 8U);
        uint8_t crcBuffer[4 + kMaxPayloadBytes] = {};
        crcBuffer[0] = s_rx.type;
        crcBuffer[1] = s_rx.sequence;
        crcBuffer[2] = static_cast<uint8_t>(s_rx.payloadLength & 0xFFU);
        crcBuffer[3] = static_cast<uint8_t>((s_rx.payloadLength >> 8U) & 0xFFU);
        if (s_rx.payloadLength > 0U) {
          memcpy(crcBuffer + 4, s_rx.payload, s_rx.payloadLength);
        }

        const uint16_t expectedCrc =
            crc16Ccitt(crcBuffer, static_cast<size_t>(4U + s_rx.payloadLength));
        if (receivedCrc == expectedCrc) {
          handleFrame(s_rx.type, s_rx.sequence, s_rx.payload,
                      s_rx.payloadLength);
        } else {
          sendAck(s_rx.sequence, kAckBadCrc, s_rx.type);
        }
      }

      resetRx();
      break;
    }
  }
}

void broadcastSnapshots(unsigned long nowMs) {
  if ((nowMs - s_lastBroadcastMs) < s_broadcastIntervalMs) {
    return;
  }

  s_lastBroadcastMs = nowMs;
  sendType(kTypeWeather, s_sequence++);
  sendType(kTypePms, s_sequence++);
  sendType(kTypeTime, s_sequence++);
}

} // namespace

void begin(HardwareSerial &serialPort, uint32_t baudRate, int rxPin,
           int txPin) {
  s_serial = &serialPort;
  s_serial->begin(baudRate, SERIAL_8N1, rxPin, txPin);
  resetRx();
  s_sequence = 0;
  s_broadcastIntervalMs = kDefaultBroadcastIntervalMs;
  s_lastBroadcastMs = millis() - s_broadcastIntervalMs;

  LOG_I(TAG, "begin baud=%lu rx=%d tx=%d interval_ms=%lu",
        static_cast<unsigned long>(baudRate), rxPin, txPin,
        static_cast<unsigned long>(s_broadcastIntervalMs));
}

void update() {
  if (s_serial == nullptr) {
    return;
  }

  parseIncoming();
  broadcastSnapshots(millis());
}

void setBroadcastIntervalMs(uint32_t intervalMs) {
  if (intervalMs < 1000UL) {
    intervalMs = 1000UL;
  }
  s_broadcastIntervalMs = intervalMs;
}

uint32_t getBroadcastIntervalMs() { return s_broadcastIntervalMs; }

bool isReady() { return s_serial != nullptr; }

} // namespace EsptoGuition
