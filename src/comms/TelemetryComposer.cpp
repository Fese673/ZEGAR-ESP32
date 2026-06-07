#include "TelemetryComposer.h"

#include <math.h>

#include "BMP280Sensor.h"
#include "ENS160AHT21Screen.h"
namespace TelemetryComposer {

void buildMqttTelemetrySample(Sample& out) {
  out.temperatureC = 0.0f;
  out.humidityPct = 0;
  out.pressureHpa = 0;
  out.aqi = 0;
  out.tvoc = 0;
  out.eco2 = 0;

  if (BMP280Screen::runtimeData.hasTemperature) {
    out.temperatureC = BMP280Screen::runtimeData.temperatureC;
  } else if (ENS160AHT21Screen::runtimeData.hasClimateSample) {
    out.temperatureC = ENS160AHT21Screen::runtimeData.temperatureC;
  }

  if (ENS160AHT21Screen::runtimeData.hasClimateSample) {
    out.humidityPct = (int)ENS160AHT21Screen::runtimeData.humidityPct;
  }

  if (BMP280Screen::runtimeData.hasPressure) {
    out.pressureHpa = (int)BMP280Screen::runtimeData.pressureHpa;
  }

  if (ENS160AHT21Screen::runtimeData.hasGasSample) {
    out.aqi = ENS160AHT21Screen::runtimeData.aqi;
    out.tvoc = ENS160AHT21Screen::runtimeData.tvoc;
    out.eco2 = ENS160AHT21Screen::runtimeData.eco2;
  }
}

float computeDewPoint(float temperatureC, float humidityPct) {
  const float b = 17.625f;
  const float c = 243.04f;

  if (!isfinite(temperatureC) || !isfinite(humidityPct) || humidityPct <= 0.0f || humidityPct > 100.0f) {
    return NAN;
  }

  const float gamma = logf(humidityPct / 100.0f) + (b * temperatureC) / (c + temperatureC);
  return (c * gamma) / (b - gamma);
}

float computeHumidex(float temperatureC, float humidityPct) {
  if (!isfinite(temperatureC) || !isfinite(humidityPct) || humidityPct <= 0.0f || humidityPct > 100.0f) {
    return NAN;
  }

  if (temperatureC < 20.0f || humidityPct < 40.0f) {
    return NAN;
  }

  const float e = 6.112f * powf(10.0f, (7.5f * temperatureC) / (237.7f + temperatureC)) * (humidityPct / 100.0f);
  return temperatureC + (5.0f / 9.0f) * (e - 10.0f);
}

float computeHeatIndexNWS(float temperatureC, float humidityPct) {
  if (!isfinite(temperatureC) || !isfinite(humidityPct) || temperatureC < 27.0f || humidityPct < 40.0f) {
    return NAN;
  }

  const float tF = temperatureC * 9.0f / 5.0f + 32.0f;
  const float rh = humidityPct;

  const float hiF = -42.379f + 2.04901523f * tF + 10.14333127f * rh - 0.22475541f * tF * rh
                  - 0.00683783f * tF * tF - 0.05481717f * rh * rh + 0.00122874f * tF * tF * rh
                  + 0.00085282f * tF * rh * rh - 0.00000199f * tF * tF * rh * rh;

  return (hiF - 32.0f) * 5.0f / 9.0f;
}

float computeAbsoluteHumidity(float temperatureC, float humidityPct) {
  if (!isfinite(temperatureC) || !isfinite(humidityPct) || humidityPct < 0.0f || humidityPct > 100.0f) {
    return NAN;
  }

  const float rW = 461.5f;  // J/(kg*K)
  const float tK = temperatureC + 273.15f;
  if (tK <= 0.0f) {
    return NAN;
  }

  const float saturationVaporPressure = 611.2f * expf((17.625f * temperatureC) / (243.04f + temperatureC));
  const float partialVaporPressure = (humidityPct / 100.0f) * saturationVaporPressure;
  return (partialVaporPressure / (rW * tK)) * 1000.0f;
}

}  // namespace TelemetryComposer
