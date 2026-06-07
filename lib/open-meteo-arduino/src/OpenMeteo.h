#ifndef OPENMETEO_H
#define OPENMETEO_H

#include <ArduinoJson.h>
#include <HTTPClient.h>

#define OM_LOGS_ENABLED 0

typedef struct OM_CurrentWeather
{
    time_t time = 0;

    float temp = 0;
    uint8_t humidity = 0;
    float apparent_temp = 0;

    bool is_day = 0;

    uint8_t weather_code = 0;

    uint8_t cloud_cover = 0;
    float pressure = 0;

    float wind_speed = 0;
    uint16_t wind_deg = 0;
    float wind_gust = 0;

    float precipitation = 0;
    float uv_index = 0;

    uint32_t sunrise = 0;
    uint32_t sunset = 0;

} OM_CurrentWeather;

#define CURRENT_API_LINK "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,cloud_cover,pressure_msl,wind_speed_10m,wind_direction_10m,wind_gusts_10m,precipitation,uv_index&daily=sunrise,sunset&timeformat=unixtime&timezone=auto"

bool getCurrentWeather(OM_CurrentWeather *structure, float latitude, float longitude, const char* apiLink = CURRENT_API_LINK);

#endif