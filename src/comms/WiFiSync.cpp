#include "WiFiSync.h"
#include <WiFi.h>
#include <esp_system.h>
#include <stdlib.h>
#include <string.h>
#include "ModeManager.h"
#include "RamTelemetry.h"

namespace WiFiSync {

namespace {

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

// pointers to time variables in main (set by setTimeRefs)
static int* pHours = nullptr;
static int* pMinutes = nullptr;
static int* pSeconds = nullptr;
static unsigned long* pLastTick = nullptr;

// Safe copies for the background task and SNTP configuration.
static char ssidCopy[kSsidCopySize] = {0};
static char passCopy[kPassCopySize] = {0};
static char ntpServerCopy[kNtpServerCopySize] = {0};

// Background-task and sync tracking.
static volatile bool wifiConnectedByTask = false;
static volatile bool wifiFailedByTask = false;
static volatile uint16_t wifiDisconnectReason = 0;
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
    Serial.println("[WiFiSync] WARNING: failed to set TZ, localtime may be incorrect");
  }
}

static void (*onStartCb)() = nullptr;
static void (*onDoneCb)() = nullptr;

static void handleWiFiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      wifiConnectedByTask = true;
      wifiFailedByTask = false;
      wifiDisconnectReason = 0;
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      wifiDisconnectReason = info.wifi_sta_disconnected.reason;
      wifiConnectedByTask = false;
      wifiFailedByTask = true;
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
  wifiConnectedByTask = false;
  wifiFailedByTask = false;
  wifiDisconnectReason = 0;
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
static TaskHandle_t wifiBeginTaskHandle = NULL;

// Complete WiFi init task — runs ALL WiFi hardware on Core 1
// WiFi.mode(), WiFi.begin(), and connection wait — fully non-blocking for Core 0
static void wifiInitTask(void* param) {
  Serial.println("[WiFiSync] wifiInitTask: started on Core 1");

  // Step 1: WiFi driver init (this is the 2-8s blocker on Core 0 — now safe here)
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  Serial.println("[WiFiSync] wifiInitTask: WiFi.mode(STA) done");
  RAM_CHECKPOINT("WIFI_DRIVER_ON");

  vTaskDelay(50 / portTICK_PERIOD_MS);

  // Step 2: Start connection
  WiFi.begin(ssidCopy, passCopy);
  Serial.println("[WiFiSync] wifiInitTask: WiFi.begin() called, waiting for WiFi events");

  wifiBeginTaskHandle = NULL;
  vTaskDelete(NULL);
}

static bool startWifiConnectionTask(unsigned long now) {
  if (wifiBeginTaskHandle != NULL) {
    vTaskDelete(wifiBeginTaskHandle);
    wifiBeginTaskHandle = NULL;
  }

  resetConnectionTracking();
  state = SyncState::WifiConnecting;
  wifiConnectStartMillis = now;

  if (xTaskCreatePinnedToCore(wifiInitTask, "wifiInit", 4096, NULL, 5, &wifiBeginTaskHandle, 1) != pdPASS) {
    Serial.println("[WiFiSync] ERROR: failed to spawn WiFi init task");
    wifiBeginTaskHandle = NULL;
    lastError = SyncError::Wifi;
    wifiFailureCount++;
    wifiConnectStartMillis = 0;
    applyBackoff(now, wifiFailureCount);
    return false;
  }

  if (onStartCb) {
    onStartCb();
  }

  Serial.println("[WiFiSync] update: WiFi init task spawned to Core 1");
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

void setTimeRefs(int &hoursRef, int &minutesRef, int &secondsRef, unsigned long &lastTickRef) {
  pHours = &hoursRef;
  pMinutes = &minutesRef;
  pSeconds = &secondsRef;
  pLastTick = &lastTickRef;
}

void begin(const char* _ssid, const char* _pass,
           const char* _ntp_server) {
  ensureWiFiEventHandler();

  copyCredential(ssidCopy, kSsidCopySize, _ssid);
  copyCredential(passCopy, kPassCopySize, _pass);
  copyNtpServer(_ntp_server);

  // Keep system time in UTC and convert to local time via TZ.
  // Defensive: some components may overwrite TZ at runtime.
  ensureTzSet();

  resetSyncState();
  wifiFailureCount = 0;
  ntpFailureCount = 0;
  sntpConfigured = false;

  resetConnectionTracking();
  lastNtpSyncMillis = 0;
  ntpSynced = false;
  lastPeriodicSync = millis();
}

void setOnStart(void (*cb)()) { onStartCb = cb; }
void setOnDone(void (*cb)())  { onDoneCb  = cb; }

TaskHandle_t getInitTaskHandle() {
  return wifiBeginTaskHandle;
}

void requestTimeSync() {
  timeSyncRequested = true;
}

static void requestWifiConnect() {
  wifiConnectRequested = true;
}

void startSync() {
  Serial.println("[WiFiSync] startSync() called");
  // Backwards compatible: request both WiFi connect and time sync.
  requestWifiConnect();
  requestTimeSync();
}

void stop() {
  resetSyncState();
  wifiFailureCount = 0;
  ntpFailureCount = 0;

  // Kill background init task if still running
  if (wifiBeginTaskHandle != NULL) {
    vTaskDelete(wifiBeginTaskHandle);
    wifiBeginTaskHandle = NULL;
  }

  if (wifiEventHandlerInstalled) {
    WiFi.removeEvent(wifiEventHandler);
    wifiEventHandlerInstalled = false;
    wifiEventHandler = 0;
  }

  // Avoid disconnect noise when WiFi driver is already off/uninitialized.
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
  Serial.println("[WiFiSync] Configuring SNTP via configTzTime()");
  configTzTime(kPolandTimezone, ntpServerCopy[0] != '\0' ? ntpServerCopy : kDefaultNtpServer);
  sntpConfigured = true;
}

static void scheduleBackoff(unsigned long now, uint8_t failures) {
  applyBackoff(now, failures);
}

void update() {
  unsigned long now = millis();

  // Periodic sync: if device is in WiFi mode, connected and idle, run sync every interval
  if (ModeManager::isWifiOn() && WiFi.status() == WL_CONNECTED && state == SyncState::Idle) {
    if (now - lastPeriodicSync >= periodicSyncIntervalMs) {
      Serial.printf("[WiFiSync] Periodic time sync requested (interval=%lu min)\n",
                    periodicSyncIntervalMs / 60000UL);
      lastPeriodicSync = now;
      requestTimeSync();
    }
  }

  // Transition out of Backoff when time elapsed
  if (state == SyncState::Backoff && now >= backoffUntilMillis) {
    state = SyncState::Idle;
  }

  // Nothing to do
  if (state == SyncState::Idle) {
    // Respect backoff window if requests are pending
    if ((timeSyncRequested || wifiConnectRequested) && now < backoffUntilMillis) {
      state = SyncState::Backoff;
      return;
    }

    // If WiFi mode is off, don't try to connect/sync.
    if (!ModeManager::isWifiOn()) {
      return;
    }

    // If time sync requested, ensure WiFi connect is requested too when not connected.
    if (timeSyncRequested && WiFi.status() != WL_CONNECTED) {
      requestWifiConnect();
    }

    // Connect WiFi if requested and not connected.
    if (wifiConnectRequested && WiFi.status() != WL_CONNECTED) {
      if (!ssidCopy[0]) {
        lastError = SyncError::MissingCredentials;
        // Keep state idle; nothing to retry.
        resetRequestFlags();
        return;
      }

      if (!startWifiConnectionTask(now)) {
        return;
      }
      return;
    }

    // Start time sync if requested and WiFi is connected.
    if (timeSyncRequested && WiFi.status() == WL_CONNECTED) {
      state = SyncState::TimeSyncing;
      syncStartMillis = now;
      ensureSntpConfigured();
      Serial.println("[WiFiSync] update: Time syncing started");
      return;
    }

    return;
  }

  if (state == SyncState::WifiConnecting) {
    if (wifiConnectedByTask || WiFi.status() == WL_CONNECTED) {
      resetConnectionTracking();
      wifiConnectRequested = false;
      wifiFailureCount = 0;
      lastError = SyncError::None;
      Serial.println("[WiFiSync] update: WiFi connected");

      // If time sync is requested, go straight to time sync.
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

    if (wifiFailedByTask) {
      const uint16_t reason = wifiDisconnectReason;
      resetConnectionTracking();
      lastError = SyncError::Wifi;
      wifiFailureCount++;
      Serial.printf("[WiFiSync] update: WiFi connect failed (reason=%u, failures=%u)\n",
                    reason, wifiFailureCount);
      scheduleBackoff(now, wifiFailureCount);
      return;
    }

    if (wifiConnectStartMillis != 0 && now - wifiConnectStartMillis >= kWifiConnectTimeoutMs) {
      lastError = SyncError::Wifi;
      wifiFailureCount++;
      Serial.printf("[WiFiSync] update: WiFi connect timeout (failures=%u)\n", wifiFailureCount);
      resetConnectionTracking();
      WiFi.disconnect(true);
      scheduleBackoff(now, wifiFailureCount);
      return;
    }

    return;
  }

  if (state == SyncState::TimeSyncing) {
    // Defensive: ensure TZ wasn't overwritten between sync cycles.
    ensureTzSet();
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {  // 10ms timeout — non-blocking poll
      if (pHours)   *pHours   = timeinfo.tm_hour;
      if (pMinutes) *pMinutes = timeinfo.tm_min;
      if (pSeconds) *pSeconds = timeinfo.tm_sec;
      if (pLastTick) *pLastTick = millis();

      lastNtpSyncMillis = now;
      ntpSynced = true;
      lastError = SyncError::None;
      ntpFailureCount = 0;
      timeSyncRequested = false;
      lastPeriodicSync = now;

      Serial.printf("[WiFiSync] NTP synced: %02d:%02d:%02d (DST=%d)\n",
                    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, timeinfo.tm_isdst);

      state = SyncState::Idle;
      if (onDoneCb) onDoneCb();
      return;
    }

    if (now - syncStartMillis >= kNtpTimeoutMs) {
      lastError = SyncError::Ntp;
      ntpFailureCount++;
      Serial.printf("[WiFiSync] NTP sync timeout (failures=%u)\n", ntpFailureCount);
      scheduleBackoff(now, ntpFailureCount);
      // Keep timeSyncRequested=true to retry later.
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

} // namespace WiFiSync

// --- API: configure periodic sync interval (minutes) ---
namespace WiFiSync {
void setPeriodicSyncIntervalMinutes(uint16_t minutes) {
  if (minutes < kMinPeriodicSyncMinutes) minutes = kMinPeriodicSyncMinutes;
  if (minutes > kMaxPeriodicSyncMinutes) minutes = kMaxPeriodicSyncMinutes;
  // granularity 1 minute is fine; caller ensures multiple-of-10 if desired
  periodicSyncIntervalMs = (unsigned long)minutes * 60000UL;
}

uint16_t getPeriodicSyncIntervalMinutes() {
  return (uint16_t)(periodicSyncIntervalMs / 60000UL);
}
} // namespace WiFiSync
