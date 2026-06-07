#include "meteoSync.h"

#include <ArduinoJson.h>
#include <atomic>
#include <HTTPClient.h>
#include <time.h>
#include <WiFi.h>

#include "AppLog.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "OpenMeteo.h"
#include "Task_Config.h"
namespace meteoSync {

static constexpr char TAG_METEO[] = "METEO";

constexpr float kLatitude = 54.60568f;
constexpr float kLongitude = 18.23559f;
constexpr unsigned long kFetchIntervalMs = 60000UL;
constexpr unsigned long kUartSendIntervalMs = 5000UL;

constexpr size_t kTaskStackSize = TaskConfig::MeteoSyncTask::kStackBytes;
constexpr UBaseType_t kTaskPriority = TaskConfig::MeteoSyncTask::kPriority;
constexpr BaseType_t kTaskCore = TaskConfig::MeteoSyncTask::kCore;

static constexpr char kCurrentApiLink[] = CURRENT_API_LINK;
static constexpr char kAirQualityApiLink[] = "&current=european_aqi,pm2_5,pm10,carbon_dioxide,nitrogen_dioxide&timeformat=unixtime";

static portMUX_TYPE s_stateMux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_aqMux = portMUX_INITIALIZER_UNLOCKED;
WeatherData s_latest;
AirQualityData s_latestAirQuality;
unsigned long s_lastUartSendMs = 0;
bool s_initialized = false;
TaskHandle_t s_fetchTaskHandle = nullptr;
std::atomic<bool> s_pendingInitialFetch{false};

WeatherData::WeatherData()
  : temperature(0), humidity(0), pressure(0), weatherCode(0),
    windSpeed(0), apparentTemp(0), cloudCover(0), windDeg(0), windGust(0),
    precipitation(0), uvIndex(0), sunrise(0), sunset(0),
    timestamp(0), valid(false) {}

AirQualityData::AirQualityData()
  : europeanAqi(0), pm25(0), pm10(0), co2(0), no2UgM3(0), timestamp(0), valid(false) {}

static bool copyLatestSnapshot(WeatherData& out) {
  taskENTER_CRITICAL(&s_stateMux);
  out = s_latest;
  const bool isValid = s_latest.valid;
  taskEXIT_CRITICAL(&s_stateMux);
  return isValid;
}

static void storeLatestSnapshot(const OM_CurrentWeather& currentWeather) {
  taskENTER_CRITICAL(&s_stateMux);
  s_latest.temperature = currentWeather.temp;
  s_latest.humidity = currentWeather.humidity;
  s_latest.pressure = currentWeather.pressure;
  s_latest.weatherCode = currentWeather.weather_code;
  s_latest.windSpeed = currentWeather.wind_speed;
  s_latest.apparentTemp = currentWeather.apparent_temp;
  s_latest.cloudCover = currentWeather.cloud_cover;
  s_latest.windDeg = currentWeather.wind_deg;
  s_latest.windGust = currentWeather.wind_gust;
  s_latest.precipitation = currentWeather.precipitation;
  s_latest.uvIndex = currentWeather.uv_index;
  s_latest.sunrise = currentWeather.sunrise;
  s_latest.sunset = currentWeather.sunset;
  s_latest.timestamp = static_cast<uint32_t>(currentWeather.time);
  s_latest.valid = true;
  taskEXIT_CRITICAL(&s_stateMux);
}

static bool doFetch() {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_W(TAG_METEO, "WiFi not connected");
    return false;
  }

  time_t now = time(nullptr);
  if (now < 100000UL) {
    LOG_W(TAG_METEO, "Time not synced");
    return false;
  }

  LOG_I(TAG_METEO, "Fetching current weather");

  OM_CurrentWeather currentWeather;
  if (!getCurrentWeather(&currentWeather, kLatitude, kLongitude, kCurrentApiLink)) {
    LOG_W(TAG_METEO, "Fetch failed, keeping last known sample");
    return false;
  }

  storeLatestSnapshot(currentWeather);
  LOG_I(TAG_METEO,
        "Fetched OK: T=%.1f H=%u P=%.1f W=%.1f Code=%u Rain=%.1f UV=%.1f",
        currentWeather.temp,
        currentWeather.humidity,
        currentWeather.pressure,
        currentWeather.wind_speed,
        currentWeather.weather_code,
        currentWeather.precipitation,
        currentWeather.uv_index);
  return true;
}

static void storeLatestAirQuality() {
  char urlBuf[256];
  snprintf(urlBuf, sizeof(urlBuf), "http://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f%s",
           kLatitude, kLongitude, kAirQualityApiLink);
  String url = urlBuf;

  WiFiClient client;
  client.setTimeout(5000UL);
  HTTPClient http;
  if (!http.begin(client, url)) {
    LOG_W(TAG_METEO, "AQ HTTP begin failed");
    return;
  }
  http.useHTTP10(true);
  http.setReuse(false);
  http.setConnectTimeout(5000UL);
  http.setTimeout(5000UL);

  const int code = http.GET();
#if defined(ENABLE_WIFI_DIAGNOSTICS)
  LOG_D(TAG_METEO, "HTTP code=%d err=%s", code, http.errorToString(code).c_str());
#endif
  if (code <= 0) {
    LOG_W(TAG_METEO, "AQ fetch failed, code=%d", code);
    http.end();
    return;
  }

  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();

  if (err != DeserializationError::Ok || doc["current"].isNull()) {
    LOG_W(TAG_METEO, "AQ JSON parse failed");
    return;
  }

  AirQualityData aq;
  aq.europeanAqi = doc["current"]["european_aqi"] | 0U;
  aq.pm25 = doc["current"]["pm2_5"] | 0.0f;
  aq.pm10 = doc["current"]["pm10"] | 0.0f;
  aq.co2 = doc["current"]["carbon_dioxide"] | 0.0f;
  aq.no2UgM3 = doc["current"]["nitrogen_dioxide"] | 0.0f;
  aq.timestamp = doc["current"]["time"] | 0U;
  aq.valid = true;

  taskENTER_CRITICAL(&s_aqMux);
  s_latestAirQuality = aq;
  taskEXIT_CRITICAL(&s_aqMux);

  LOG_I(TAG_METEO, "AQ OK: AQI=%u PM2.5=%.1f PM10=%.1f CO2=%.0f NO2=%.1f",
        (unsigned)aq.europeanAqi, aq.pm25, aq.pm10, aq.co2, aq.no2UgM3);
}

static void fetchTask(void* param) {
  (void)param;
  LOG_I(TAG_METEO, "Task started on core %d", xPortGetCoreID());

  bool haveFetchedOnce = false;
  for (;;) {
    if (!haveFetchedOnce) {
      if (!s_pendingInitialFetch.exchange(false, std::memory_order_acq_rel)) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      }
    } else {
      (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kFetchIntervalMs));
    }

    doFetch();
    storeLatestAirQuality();
    haveFetchedOnce = true;

    // Drain any request that arrived while the fetch was still running so we do not double-fetch.
    (void)ulTaskNotifyTake(pdTRUE, 0);
  }
}

void begin() {
  s_initialized = true;
  s_lastUartSendMs = millis();

#if defined(METEO_ENABLED)
  LOG_I(TAG_METEO, "MeteoSync initialized");

  if (s_fetchTaskHandle != nullptr) {
    LOG_W(TAG_METEO, "Task already running");
    return;
  }

  if (xTaskCreatePinnedToCore(fetchTask,
                              "meteoFetch",
                              kTaskStackSize,
                              nullptr,
                              kTaskPriority,
                              &s_fetchTaskHandle,
                              kTaskCore) != pdPASS) {
    LOG_E(TAG_METEO, "Failed to create task");
    s_fetchTaskHandle = nullptr;
  } else {
    LOG_I(TAG_METEO, "Task created on core %d priority=%u stack=%u",
          (int)kTaskCore,
          (unsigned)kTaskPriority,
          (unsigned)kTaskStackSize);
  }
#endif
}

void triggerFetch() {
#if defined(METEO_ENABLED)
  if (s_fetchTaskHandle == nullptr) {
    s_pendingInitialFetch.store(true, std::memory_order_release);
    LOG_I(TAG_METEO, "Queued fetch request until worker starts");
    return;
  }

  xTaskNotifyGive(s_fetchTaskHandle);
#endif
}

void update() {
  if (!s_initialized) return;

#if defined(METEO_ENABLED)
  const unsigned long now = millis();
  if (now - s_lastUartSendMs >= kUartSendIntervalMs) {
    s_lastUartSendMs = now;

    WeatherData latest;
    if (!copyLatestSnapshot(latest)) {
      return;
    }

    LOG_I(TAG_METEO,
          "Weather T=%.1fC H=%u%% P=%.1fhPa W=%.1fm/s Code=%u",
          latest.temperature,
          latest.humidity,
          latest.pressure,
          latest.windSpeed,
          latest.weatherCode);
  }
#endif
}

bool getLatest(WeatherData& out) {
  return copyLatestSnapshot(out);
}

bool getLatestAirQuality(AirQualityData& out) {
  taskENTER_CRITICAL(&s_aqMux);
  out = s_latestAirQuality;
  const bool valid = s_latestAirQuality.valid;
  taskEXIT_CRITICAL(&s_aqMux);
  return valid;
}

} // namespace meteoSync