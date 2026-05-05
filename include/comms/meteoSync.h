#pragma once
#include <Arduino.h>

namespace meteoSync {

struct WeatherData {
  float temperature;     // °C
  uint8_t humidity;      // %
  float pressure;         // hPa
  uint8_t weatherCode;   // WMO code
  float windSpeed;       // m/s
  uint32_t timestamp;    // Unix time
  bool valid;            // true when at least one successful sample is available

  WeatherData();
};

void begin();
void update();
void triggerFetch();
bool getLatest(WeatherData& out);

} // namespace meteoSync
