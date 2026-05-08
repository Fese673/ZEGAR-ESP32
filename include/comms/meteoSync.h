#pragma once
#include <Arduino.h>

namespace meteoSync {

struct WeatherData {
  float temperature;     // °C
  uint8_t humidity;      // %
  float pressure;         // hPa
  uint8_t weatherCode;   // WMO code
  float windSpeed;       // m/s
  float apparentTemp;    // °C feels like
  uint8_t cloudCover;    // %
  uint16_t windDeg;      // degrees
  float windGust;        // m/s
  float precipitation;   // mm
  float uvIndex;         // UV index
  uint32_t sunrise;      // unix time
  uint32_t sunset;       // unix time
  uint32_t timestamp;    // Unix time
  bool valid;

  WeatherData();
};

struct AirQualityData {
  uint16_t europeanAqi;
  float pm25;
  float pm10;
  float co2;
  uint32_t timestamp;
  bool valid;

  AirQualityData();
};

void begin();
void update();
void triggerFetch();
bool getLatest(WeatherData& out);
bool getLatestAirQuality(AirQualityData& out);

} // namespace meteoSync
