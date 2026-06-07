#pragma once

#include <Arduino.h>
namespace BMP280Screen {

enum class SensorState : uint8_t {
  Init = 0,
  Ready,
  Stale,
  Error,
  Missing,
};

struct RuntimeData {
  SensorState sensorState;
  float temperatureC;
  float pressureHpa;
  float altitudeM;
  uint32_t lastSampleMs;
  uint32_t sampleRevision;
  bool hasSample;
  bool hasTemperature;
  bool hasPressure;
  bool hasAltitude;
  bool altitudeAvailable;
  char statusText[16];
};

extern RuntimeData runtimeData;
extern bool screenDirty;

void resetRuntimeData();
void markScreenDirty();

}  // namespace BMP280Screen
