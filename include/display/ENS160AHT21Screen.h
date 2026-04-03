#pragma once

#include <Arduino.h>

namespace ENS160AHT21Screen {

struct RuntimeData {
  uint8_t aqi;
  uint16_t tvoc;
  uint16_t eco2;
  float temperatureC;
  float rawTemperatureC;
  float humidityPct;
  char statusText[16];
  uint32_t lastUpdateMs;
  bool hasSample;
  bool hasGasSample;
  bool hasClimateSample;
};

extern RuntimeData runtimeData;
extern bool screenDirty;

void resetRuntimeData();
void markScreenDirty();

}  // namespace ENS160AHT21Screen