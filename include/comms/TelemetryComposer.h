#pragma once

#include <Arduino.h>
namespace TelemetryComposer {

struct Sample {
  float temperatureC;
  int humidityPct;
  int pressureHpa;
  uint8_t aqi;
  uint16_t tvoc;
  uint16_t eco2;
};

void buildMqttTelemetrySample(Sample& out);

float computeDewPoint(float temperatureC, float humidityPct);
float computeHumidex(float temperatureC, float humidityPct);
float computeHeatIndexNWS(float temperatureC, float humidityPct);
float computeAbsoluteHumidity(float temperatureC, float humidityPct);

}  // namespace TelemetryComposer
