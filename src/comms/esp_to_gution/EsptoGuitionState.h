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
struct SystemResourcesPayload {
  uint32_t freeRam = 0;
  uint32_t heapRam = 0;
  uint32_t dmaRam = 0;
  uint8_t core0Cpu = 0;
  uint8_t core1Cpu = 0;
  uint32_t freeFlash = 0;
  uint32_t usedFlash = 0;
  uint16_t underrunsAudio = 0;
  uint16_t overflowAudio = 0;
  uint16_t dropsAudio = 0;
  uint16_t errorsI2c = 0;
  uint16_t timeoutsI2c = 0;
};

struct WifiPayload {
  bool connected = false;
  int8_t rssi = 0;
  uint8_t ip[4] = {0, 0, 0, 0};
};

bool buildWeatherPayload(WeatherPayload &out, unsigned long nowMs);
bool buildPmsPayload(PmsPayload &out, unsigned long nowMs);
bool buildTimePayload(TimePayload &out);
bool buildWifiPayload(WifiPayload &out);
bool buildSystemResourcesPayload(SystemResourcesPayload &out);

void handleReceivedSettings(const uint8_t* payload, uint16_t payloadLength);

} // namespace EsptoGuition
