#include "meteoSync.h"
#include "OpenMeteo.h"
#include "TaskConfig.h"
#include <WiFi.h>
#include <atomic>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "AppLog.h"

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

static portMUX_TYPE s_stateMux = portMUX_INITIALIZER_UNLOCKED;
WeatherData s_latest;
unsigned long s_lastUartSendMs = 0;
bool s_initialized = false;
TaskHandle_t s_fetchTaskHandle = nullptr;
std::atomic<bool> s_pendingInitialFetch{false};

WeatherData::WeatherData()
  : temperature(0), humidity(0), pressure(0), weatherCode(0), windSpeed(0), timestamp(0), valid(false) {}

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
        "Fetched OK: T=%.1f H=%u P=%.1f W=%.1f Code=%u",
        currentWeather.temp,
        currentWeather.humidity,
        currentWeather.pressure,
        currentWeather.wind_speed,
        currentWeather.weather_code);
  return true;
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

} // namespace meteoSync