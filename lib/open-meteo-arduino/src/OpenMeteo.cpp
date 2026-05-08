#include "OpenMeteo.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

static constexpr unsigned long kHttpTimeoutMs = 5000UL;

static bool fetchAndParseJson(const String& url, JsonDocument& doc, size_t docSize)
{
    WiFiClient client;
    client.setTimeout(kHttpTimeoutMs);

    HTTPClient http;
    if (!http.begin(client, url)) {
        return false;
    }

    http.useHTTP10(true);
    http.setReuse(false);
    http.setConnectTimeout(kHttpTimeoutMs);
    http.setTimeout(kHttpTimeoutMs);

    const int code = http.GET();
    if (code <= 0) {
        http.end();
        return false;
    }

    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    return err == DeserializationError::Ok;
}

bool getCurrentWeather(OM_CurrentWeather *structure, float latitude, float longitude, const char* apiLink)
{
    String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(latitude) + "&longitude=" + String(longitude) + apiLink;

    DynamicJsonDocument jsonDoc(3072);
    if (!fetchAndParseJson(url, jsonDoc, 3072) || jsonDoc["current"].isNull()) {
        return false;
    }

    structure->time = jsonDoc["current"]["time"] | 0;
    structure->temp = jsonDoc["current"]["temperature_2m"] | 0.0f;
    structure->humidity = jsonDoc["current"]["relative_humidity_2m"] | 0;
    structure->apparent_temp = jsonDoc["current"]["apparent_temperature"] | 0.0f;
    structure->is_day = jsonDoc["current"]["is_day"] | false;
    structure->weather_code = jsonDoc["current"]["weather_code"] | 0;
    structure->cloud_cover = jsonDoc["current"]["cloud_cover"] | 0;
    structure->pressure = jsonDoc["current"]["pressure_msl"] | 0.0f;
    structure->wind_speed = jsonDoc["current"]["wind_speed_10m"] | 0.0f;
    structure->wind_deg = jsonDoc["current"]["wind_direction_10m"] | 0;
    structure->wind_gust = jsonDoc["current"]["wind_gusts_10m"] | 0.0f;
    structure->precipitation = jsonDoc["current"]["precipitation"] | 0.0f;
    structure->uv_index = jsonDoc["current"]["uv_index"] | 0.0f;

    if (!jsonDoc["daily"].isNull()) {
        structure->sunrise = jsonDoc["daily"]["sunrise"][0] | 0U;
        structure->sunset = jsonDoc["daily"]["sunset"][0] | 0U;
    }

    return true;
}