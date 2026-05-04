#pragma once
#include <stdint.h>
#include <stdbool.h>

namespace EsptoGuition {

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

bool buildWeatherPayload(WeatherPayload &out, unsigned long nowMs);
bool buildPmsPayload(PmsPayload &out, unsigned long nowMs);
bool buildTimePayload(TimePayload &out);

void handleReceivedSettings(const uint8_t* payload, uint16_t payloadLength);

} // namespace EsptoGuition
