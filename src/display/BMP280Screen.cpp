#include "BMP280Screen.h"

#include <cstring>
namespace BMP280Screen {

RuntimeData runtimeData = {
    SensorState::Init,
    0.0f,
    0.0f,
    0.0f,
    0,
    0,
    false,
    false,
    false,
    false,
    false,
    "INIT"
};

bool screenDirty = true;

void resetRuntimeData() {
  runtimeData.sensorState = SensorState::Init;
  runtimeData.temperatureC = 0.0f;
  runtimeData.pressureHpa = 0.0f;
  runtimeData.altitudeM = 0.0f;
  runtimeData.lastSampleMs = 0;
  runtimeData.sampleRevision = 0;
  runtimeData.hasSample = false;
  runtimeData.hasTemperature = false;
  runtimeData.hasPressure = false;
  runtimeData.hasAltitude = false;
  runtimeData.altitudeAvailable = false;
  std::strncpy(runtimeData.statusText, "INIT", sizeof(runtimeData.statusText));
  runtimeData.statusText[sizeof(runtimeData.statusText) - 1] = '\0';
  screenDirty = true;
}

void markScreenDirty() {
  screenDirty = true;
}

}  // namespace BMP280Screen
