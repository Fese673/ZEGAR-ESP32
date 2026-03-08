#include "ENS160AHT21Screen.h"

#include <cstring>

namespace ENS160AHT21Screen {

RuntimeData runtimeData = {
  0,
  0,
  0,
  0.0f,
  0.0f,
  0.0f,
  "INIT",
  0,
  false,
  false,
  false
};

bool screenDirty = true;

void resetRuntimeData() {
  runtimeData.aqi = 0;
  runtimeData.tvoc = 0;
  runtimeData.eco2 = 0;
  runtimeData.temperatureC = 0.0f;
  runtimeData.rawTemperatureC = 0.0f;
  runtimeData.humidityPct = 0.0f;
  std::strncpy(runtimeData.statusText, "INIT", sizeof(runtimeData.statusText));
  runtimeData.statusText[sizeof(runtimeData.statusText) - 1] = '\0';
  runtimeData.lastUpdateMs = 0;
  runtimeData.hasSample = false;
  runtimeData.hasGasSample = false;
  runtimeData.hasClimateSample = false;
  screenDirty = true;
}

void markScreenDirty() {
  screenDirty = true;
}

}  // namespace ENS160AHT21Screen