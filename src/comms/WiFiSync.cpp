#include "WiFiSync.h"
#include <WiFi.h>
#include <esp_system.h>
#include <stdlib.h>
#include <string.h>
#include "ModeManager.h"
#include "RamTelemetry.h"

namespace WiFiSync {

// wskaźniki na zmienne w main (ustawiane przez setTimeRefs)
static int* pHours = nullptr;
static int* pMinutes = nullptr;
static int* pSeconds = nullptr;
static unsigned long* pLastTick = nullptr;

// konfiguracja i stan wewnętrzny
static const char* ssid = nullptr;
static const char* pass = nullptr;
static const char* ntpServer = "pool.ntp.org";

// Safe copies of credentials for background task (avoids pointer lifetime issues)
static char ssidCopy[33] = {0};
static char passCopy[65] = {0};

// Flag: set by background task when WiFi connected
static volatile bool wifiConnectedByTask = false;
static volatile bool wifiFailedByTask = false;
static volatile uint16_t wifiDisconnectReason = 0;

// NTP tracking
static unsigned long lastNtpSyncMillis = 0;
static bool ntpSynced = false;

// Poland timezone with automatic DST switching:
// CET (UTC+1) in winter and CEST (UTC+2) in summer.
// NOTE: Use a POSIX TZ string format that is widely supported on embedded newlib.
static const char* TZ_POLAND = "CET-1CEST,M3.5.0,M10.5.0/3";

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

static constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;

static void ensureTzSet() {
  const char* current = getenv("TZ");
  if (current != nullptr && strcmp(current, TZ_POLAND) == 0) {
    return;
  }

  if (setenv("TZ", TZ_POLAND, 1) == 0) {
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

static constexpr unsigned long NTP_TIMEOUT_MS = 10000;
static unsigned long periodicSyncIntervalMs = 3600000UL; // default 60min

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

  ssid = _ssid;
  pass = _pass;
  // Safe copies for background task
  strncpy(ssidCopy, _ssid ? _ssid : "", sizeof(ssidCopy) - 1);
  ssidCopy[sizeof(ssidCopy) - 1] = '\0';
  strncpy(passCopy, _pass ? _pass : "", sizeof(passCopy) - 1);
  passCopy[sizeof(passCopy) - 1] = '\0';
  ntpServer = _ntp_server;

  // Keep system time in UTC and convert to local time via TZ.
  // Defensive: some components may overwrite TZ at runtime.
  ensureTzSet();

  state = SyncState::Idle;
  lastError = SyncError::None;
  timeSyncRequested = false;
  wifiConnectRequested = false;
  syncStartMillis = 0;
  backoffUntilMillis = 0;
  wifiConnectStartMillis = 0;
  wifiFailureCount = 0;
  ntpFailureCount = 0;
  sntpConfigured = false;

  wifiConnectedByTask = false;
  wifiFailedByTask = false;
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
  state = SyncState::Idle;
  wifiConnectedByTask = false;
  wifiFailedByTask = false;
  wifiDisconnectReason = 0;
  wifiConnectRequested = false;
  timeSyncRequested = false;
  backoffUntilMillis = 0;
  lastError = SyncError::None;
  wifiConnectStartMillis = 0;

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
  configTzTime(TZ_POLAND, ntpServer);
  sntpConfigured = true;
}

static void scheduleBackoff(unsigned long now, uint8_t failures) {
  const unsigned long delayMs = computeBackoffMs(failures);
  backoffUntilMillis = now + delayMs;
  state = (delayMs > 0) ? SyncState::Backoff : SyncState::Idle;
}

void update() {
  unsigned long now = millis();

  // Periodic sync: if device is in WiFi mode, connected and idle, run sync every interval
  if (ModeManager::isWifiOn() && WiFi.status() == WL_CONNECTED && state == SyncState::Idle) {
    if (now - lastPeriodicSync >= periodicSyncIntervalMs) {
      Serial.printf("[WiFiSync] Periodic time sync requested (interval=%lumin)", periodicSyncIntervalMs/60000UL);
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
        timeSyncRequested = false;
        wifiConnectRequested = false;
        return;
      }

      // Kill leftover task if any
      if (wifiBeginTaskHandle != NULL) {
        vTaskDelete(wifiBeginTaskHandle);
        wifiBeginTaskHandle = NULL;
      }

      wifiConnectedByTask = false;
      wifiFailedByTask = false;
        wifiDisconnectReason = 0;
        wifiConnectStartMillis = now;

      state = SyncState::WifiConnecting;
      if (onStartCb) onStartCb();
      xTaskCreatePinnedToCore(wifiInitTask, "wifiInit", 4096, NULL, 5, &wifiBeginTaskHandle, 1);
      Serial.println("[WiFiSync] update: WiFi init task spawned to Core 1");
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
      wifiConnectedByTask = false;
      wifiFailedByTask = false;
      wifiDisconnectReason = 0;
      wifiConnectStartMillis = 0;
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
        if (onDoneCb) onDoneCb();
      }
      return;
    }

    if (wifiFailedByTask) {
      wifiFailedByTask = false;
      lastError = SyncError::Wifi;
      wifiFailureCount++;
      Serial.printf("[WiFiSync] update: WiFi connect failed (reason=%u, failures=%u)\n",
                    wifiDisconnectReason, wifiFailureCount);
      wifiDisconnectReason = 0;
      wifiConnectStartMillis = 0;
      scheduleBackoff(now, wifiFailureCount);
      return;
    }

    if (wifiConnectStartMillis != 0 && now - wifiConnectStartMillis >= WIFI_CONNECT_TIMEOUT_MS) {
      lastError = SyncError::Wifi;
      wifiFailureCount++;
      Serial.printf("[WiFiSync] update: WiFi connect timeout (failures=%u)\n", wifiFailureCount);
      wifiConnectStartMillis = 0;
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

      lastNtpSyncMillis = millis();
      ntpSynced = true;
      lastError = SyncError::None;
      ntpFailureCount = 0;
      timeSyncRequested = false;

      Serial.printf("[WiFiSync] NTP synced: %02d:%02d:%02d (DST=%d)\n",
                    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, timeinfo.tm_isdst);

      state = SyncState::Idle;
      if (onDoneCb) onDoneCb();
      return;
    }

    if (now - syncStartMillis >= NTP_TIMEOUT_MS) {
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
  if (minutes < 10) minutes = 10;
  if (minutes > 360) minutes = 360;
  // granularity 1 minute is fine; caller ensures multiple-of-10 if desired
  periodicSyncIntervalMs = (unsigned long)minutes * 60000UL;
}

uint16_t getPeriodicSyncIntervalMinutes() {
  return (uint16_t)(periodicSyncIntervalMs / 60000UL);
}
} // namespace WiFiSync
