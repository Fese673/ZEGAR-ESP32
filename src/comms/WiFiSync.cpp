#include "WiFiSync.h"
#include <WiFi.h>
#include <esp_system.h>
#include <atomic>
#include <stdlib.h>
#include <string.h>

#include "AppLog.h"
#include "ClockService.h"
#include "ModeManager.h"
#include "TaskConfig.h"
#include "RamTelemetry.h"
#include "meteoSync.h"

namespace WiFiSync {

namespace {

constexpr char TAG[] = "WIFI";


constexpr char kDefaultNtpServer[] = "pool.ntp.org";
constexpr char kPolandTimezone[] = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr unsigned long kWifiConnectTimeoutMs = 15000UL;
constexpr unsigned long kNtpTimeoutMs = 10000UL;
constexpr unsigned long kDefaultPeriodicSyncIntervalMs = 3600000UL;
constexpr uint16_t kMinPeriodicSyncMinutes = 10;
constexpr uint16_t kMaxPeriodicSyncMinutes = 360;
constexpr uint8_t kSsidCopySize = 33;
constexpr uint8_t kPassCopySize = 65;
constexpr uint8_t kNtpServerCopySize = 64;

}  // namespace

// Safe copies for the background task and SNTP configuration.
static char ssidCopy[kSsidCopySize] = {0};
static char passCopy[kPassCopySize] = {0};
static char ntpServerCopy[kNtpServerCopySize] = {0};

// Background-task and sync tracking.
static std::atomic<bool> wifiConnectedByTask{false};
static std::atomic<bool> wifiFailedByTask{false};
static std::atomic<int8_t> wifiRssi{0};
static std::atomic<uint16_t> wifiDisconnectReason{0};
static unsigned long lastNtpSyncMillis = 0;
static bool ntpSynced = false;

static SyncState state = SyncState::Idle;
static SyncError lastError = SyncError::None;
static bool timeSyncRequested = false;
static bool wifiConnectRequested = false;
static unsigned long syncStartMillis = 0;
static unsigned long backoffUntilMillis = 0;
static unsigned long wifiConnectStartMillis = 0;
static uint8_t wifiFailureCount = 0;
static uint8_t ntpFailureCount = 0;
static bool sntpConfigured = false;
static wifi_event_id_t wifiEventHandler = 0;
static bool wifiEventHandlerInstalled = false;

static void ensureTzSet() {
  const char* current = getenv("TZ");
  if (current != nullptr && strcmp(current, kPolandTimezone) == 0) {
    return;
  }

  if (setenv("TZ", kPolandTimezone, 1) == 0) {
    tzset();
  } else {
    LOG_W(TAG, "Failed to set TZ localtime_may_be_incorrect=true");
  }
}

static void (*onStartCb)() = nullptr;
static void (*onDoneCb)() = nullptr;

static void handleWiFiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      wifiConnectedByTask.store(true);
      wifiFailedByTask.store(false);
      wifiDisconnectReason.store(0);
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      wifiDisconnectReason.store(info.wifi_sta_disconnected.reason);
      wifiConnectedByTask.store(false);
      wifiFailedByTask.store(true);
      break;

    default:
      break;
  }
}

static void ensureWiFiEventHandler() {
  if (wifiEventHandlerInstalled) {
    return;
  }

  wifiEventHandler = WiFi.onEvent(handleWiFiEvent);
  wifiEventHandlerInstalled = true;
}

static void resetConnectionTracking() {
  wifiConnectedByTask.store(false);
  wifiFailedByTask.store(false);
  wifiDisconnectReason.store(0);
  wifiConnectStartMillis = 0;
}

static void resetRequestFlags() {
  timeSyncRequested = false;
  wifiConnectRequested = false;
}

static void resetBackoff() {
  backoffUntilMillis = 0;
}

static void resetSyncState() {
  state = SyncState::Idle;
  lastError = SyncError::None;
  resetRequestFlags();
  resetBackoff();
  resetConnectionTracking();
  syncStartMillis = 0;
}

static void copyCredential(char* destination, uint8_t destinationSize, const char* source) {
  if (destinationSize == 0) {
    return;
  }

  strncpy(destination, source != nullptr ? source : "", destinationSize - 1);
  destination[destinationSize - 1] = '\0';
}

static void copyNtpServer(const char* source) {
  copyCredential(ntpServerCopy, kNtpServerCopySize, (source != nullptr && source[0] != '\0') ? source : kDefaultNtpServer);
}

static unsigned long computeBackoffMs(uint8_t failures);

static void applyBackoff(unsigned long now, uint8_t failures) {
  const unsigned long delayMs = computeBackoffMs(failures);
  backoffUntilMillis = now + delayMs;
  state = (delayMs > 0) ? SyncState::Backoff : SyncState::Idle;
}

// Task handle for WiFi.begin() offload
static std::atomic<TaskHandle_t> wifiBeginTaskHandle{NULL};

// Complete WiFi init task — runs ALL WiFi hardware on Core 1
// WiFi.mode(), WiFi.begin(), and connection wait — fully non-blocking for Core 0
static void wifiInitTask(void* param) {
  LOG_I(TAG, "WiFi init task started core=%d", (int)TaskConfig::WifiInitTask::kCore);

  // Step 1: WiFi driver init (this is the 2-8s blocker on Core 0 — now safe here)
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  LOG_I(TAG, "WiFi init task mode=STA done=true");
  RAM_CHECKPOINT("WIFI_DRIVER_ON");

  vTaskDelay(50 / portTICK_PERIOD_MS);

  // Step 2: Start connection
  WiFi.begin(ssidCopy, passCopy);
  LOG_I(TAG, "WiFi begin called waiting_for_events=true");

  wifiBeginTaskHandle.store(NULL, std::memory_order_release);
  vTaskDelete(NULL);
}

static bool startWifiConnectionTask(unsigned long now) {
  TaskHandle_t oldHandle = wifiBeginTaskHandle.exchange(NULL, std::memory_order_acq_rel);
  if (oldHandle != NULL) {
    vTaskDelete(oldHandle);
  }

  resetConnectionTracking();
  state = SyncState::WifiConnecting;
  wifiConnectStartMillis = now;

  TaskHandle_t newHandle = NULL;
  if (xTaskCreatePinnedToCore(wifiInitTask,
                              "wifiInit",
                              TaskConfig::WifiInitTask::kStackBytes,
                              NULL,
                              TaskConfig::WifiInitTask::kPriority,
                              &newHandle,
                              TaskConfig::WifiInitTask::kCore) != pdPASS) {
    LOG_E(TAG, "Failed to spawn WiFi init task");
    lastError = SyncError::Wifi;
    wifiFailureCount++;
    wifiConnectStartMillis = 0;
    applyBackoff(now, wifiFailureCount);
    return false;
  }

  wifiBeginTaskHandle.store(newHandle, std::memory_order_release);

  if (onStartCb) {
    onStartCb();
  }

  LOG_I(TAG, "WiFi init task spawned core=%d", (int)TaskConfig::WifiInitTask::kCore);
  return true;
}

static unsigned long periodicSyncIntervalMs = kDefaultPeriodicSyncIntervalMs;

static unsigned long lastPeriodicSync = 0;

static unsigned long computeBackoffMs(uint8_t failures) {
  // Exponential-ish backoff with upper bound.
  // 0->0ms, 1->1s, 2->2s, 3->4s, 4->8s, 5->16s, 6->30s, >=7->60s
  if (failures == 0) return 0;
  if (failures == 1) return 1000;
  if (failures == 2) return 2000;
  if (failures == 3) return 4000;
  if (failures == 4) return 8000;
  if (failures == 5) return 16000;
  if (failures == 6) return 30000;
  return 60000;
}

static void wifiMonitorTask(void* param) {
  while (true) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiRssi.store(static_cast<int8_t>(WiFi.RSSI()), std::memory_order_relaxed);
    } else {
      wifiRssi.store(0, std::memory_order_relaxed);
    }
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}

void begin(const char* _ssid, const char* _pass,
           const char* _ntp_server) {
  ensureWiFiEventHandler();

  copyCredential(ssidCopy, kSsidCopySize, _ssid);
  copyCredential(passCopy, kPassCopySize, _pass);
  copyNtpServer(_ntp_server);

  ensureTzSet();

  resetSyncState();
  wifiFailureCount = 0;
  ntpFailureCount = 0;
  sntpConfigured = false;

  resetConnectionTracking();
  lastNtpSyncMillis = 0;
  ntpSynced = false;
  lastPeriodicSync = millis();

  static TaskHandle_t monitorHandle = NULL;
  if (monitorHandle == NULL) {
    xTaskCreatePinnedToCore(
        wifiMonitorTask, "wifiMonitor", TaskConfig::WifiMonitorTask::kStackBytes,
        NULL, TaskConfig::WifiMonitorTask::kPriority, &monitorHandle,
        TaskConfig::WifiMonitorTask::kCore);
  }
}

void setOnStart(void (*cb)()) { onStartCb = cb; }
void setOnDone(void (*cb)())  { onDoneCb  = cb; }

TaskHandle_t getInitTaskHandle() {
  return wifiBeginTaskHandle.load(std::memory_order_acquire);
}

void requestTimeSync() {
  timeSyncRequested = true;
}

static void requestWifiConnect() {
  wifiConnectRequested = true;
}

void startSync() {
  LOG_I(TAG, "startSync request_wifi=true request_time_sync=true");
  requestWifiConnect();
  requestTimeSync();
}

void stop() {
  resetSyncState();
  wifiFailureCount = 0;
  ntpFailureCount = 0;

  TaskHandle_t killHandle = wifiBeginTaskHandle.exchange(NULL, std::memory_order_acq_rel);
  if (killHandle != NULL) {
    vTaskDelete(killHandle);
  }

  if (wifiEventHandlerInstalled) {
    WiFi.removeEvent(wifiEventHandler);
    wifiEventHandlerInstalled = false;
    wifiEventHandler = 0;
  }

  const wifi_mode_t currentMode = WiFi.getMode();
  if (currentMode != WIFI_MODE_NULL) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  RAM_CHECKPOINT("WIFI_SHUTDOWN");
}

bool isBusy() {
  return state != SyncState::Idle;
}

SyncState getState() {
  return state;
}

SyncError getLastError() {
  return lastError;
}

static void ensureSntpConfigured() {
  if (sntpConfigured) return;
  ensureTzSet();
  LOG_I(TAG, "action=configTzTime server=%s", ntpServerCopy[0] != '\0' ? ntpServerCopy : kDefaultNtpServer);
  configTzTime(kPolandTimezone, ntpServerCopy[0] != '\0' ? ntpServerCopy : kDefaultNtpServer);
  sntpConfigured = true;
}

static void scheduleBackoff(unsigned long now, uint8_t failures) {
  applyBackoff(now, failures);
}

void update() {
  unsigned long now = millis();

  if (ModeManager::isWifiOn() && WiFi.status() == WL_CONNECTED && state == SyncState::Idle) {
    if (now - lastPeriodicSync >= periodicSyncIntervalMs) {
      LOG_I(TAG, "Periodic time sync requested interval_min=%lu", periodicSyncIntervalMs / 60000UL);
      lastPeriodicSync = now;
      requestTimeSync();
    }
  }

  if (state == SyncState::Backoff && now >= backoffUntilMillis) {
    state = SyncState::Idle;
  }

  if (state == SyncState::Idle) {
    if ((timeSyncRequested || wifiConnectRequested) && now < backoffUntilMillis) {
      state = SyncState::Backoff;
      return;
    }

    if (!ModeManager::isWifiOn()) {
      return;
    }

    if (timeSyncRequested && WiFi.status() != WL_CONNECTED) {
      requestWifiConnect();
    }

    if (wifiConnectRequested && WiFi.status() != WL_CONNECTED) {
      if (!ssidCopy[0]) {
        lastError = SyncError::MissingCredentials;
        resetRequestFlags();
        return;
      }

      if (!startWifiConnectionTask(now)) {
        return;
      }
      return;
    }

    if (timeSyncRequested && WiFi.status() == WL_CONNECTED) {
      state = SyncState::TimeSyncing;
      syncStartMillis = now;
      ensureSntpConfigured();
      LOG_I(TAG, "Time syncing started");
      return;
    }

    return;
  }

  if (state == SyncState::WifiConnecting) {
    if (wifiConnectedByTask.load() || WiFi.status() == WL_CONNECTED) {
      resetConnectionTracking();
      wifiConnectRequested = false;
      wifiFailureCount = 0;
      lastError = SyncError::None;
      LOG_I(TAG, "WiFi connected");

      if (timeSyncRequested) {
        state = SyncState::TimeSyncing;
        syncStartMillis = now;
        ensureSntpConfigured();
      } else {
        state = SyncState::Idle;
        lastPeriodicSync = now;
        if (onDoneCb) onDoneCb();
      }
      return;
    }

    if (wifiFailedByTask.load()) {
      const uint16_t reason = wifiDisconnectReason.load();
      resetConnectionTracking();
      lastError = SyncError::Wifi;
      wifiFailureCount++;
      LOG_W(TAG, "WiFi connect failed reason=%u failures=%u", reason, wifiFailureCount);
      scheduleBackoff(now, wifiFailureCount);
      return;
    }

    if (wifiConnectStartMillis != 0 && now - wifiConnectStartMillis >= kWifiConnectTimeoutMs) {
      lastError = SyncError::Wifi;
      wifiFailureCount++;
      LOG_W(TAG, "WiFi connect timeout failures=%u", wifiFailureCount);
      resetConnectionTracking();
      WiFi.disconnect(true);
      scheduleBackoff(now, wifiFailureCount);
      return;
    }

    return;
  }

  if (state == SyncState::TimeSyncing) {
    ensureTzSet();
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
      Clock::set(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      Clock::setLastTick(millis());

      lastNtpSyncMillis = now;
      ntpSynced = true;
      lastError = SyncError::None;
      ntpFailureCount = 0;
      timeSyncRequested = false;
      lastPeriodicSync = now;

       LOG_I(TAG,
         "NTP synced time=%02d:%02d:%02d dst=%d",
         timeinfo.tm_hour,
         timeinfo.tm_min,
         timeinfo.tm_sec,
         timeinfo.tm_isdst);

      state = SyncState::Idle;
      if (onDoneCb) onDoneCb();

#if defined(METEO_ENABLED)
      meteoSync::triggerFetch();
#endif
      return;
    }

    if (now - syncStartMillis >= kNtpTimeoutMs) {
      lastError = SyncError::Ntp;
      ntpFailureCount++;
      LOG_W(TAG, "NTP sync timeout failures=%u", ntpFailureCount);
      scheduleBackoff(now, ntpFailureCount);
      timeSyncRequested = true;
      return;
    }

    return;
  }
}

unsigned long getLastNtpSyncTime() {
  return lastNtpSyncMillis;
}

bool hasNtpSynced() {
  return ntpSynced;
}

int8_t getRssi() {
  return wifiRssi.load(std::memory_order_relaxed);
}

void setPeriodicSyncIntervalMinutes(uint16_t minutes) {
  if (minutes < kMinPeriodicSyncMinutes) minutes = kMinPeriodicSyncMinutes;
  if (minutes > kMaxPeriodicSyncMinutes) minutes = kMaxPeriodicSyncMinutes;
  periodicSyncIntervalMs = (unsigned long)minutes * 60000UL;
}

uint16_t getPeriodicSyncIntervalMinutes() {
  return (uint16_t)(periodicSyncIntervalMs / 60000UL);
}

} // namespace WiFiSync
