#include "WiFiSync.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include "ModeManager.h"

namespace WiFiSync {

// wskaźniki na zmienne w main (ustawiane przez setTimeRefs)
static int* pHours = nullptr;
static int* pMinutes = nullptr;
static int* pSeconds = nullptr;
static unsigned long* pLastTick = nullptr;

// wskaźnik na appState (opcjonalny)
static AppState* pAppState = nullptr;
void setAppStatePtr(AppState* ptr) { pAppState = ptr; }

// konfiguracja i stan wewnętrzny
static const char* ssid = nullptr;
static const char* pass = nullptr;
static const char* ntpServer = "pool.ntp.org";

// Safe copies of credentials for background task (avoids pointer lifetime issues)
static char ssidCopy[33] = {0};
static char passCopy[65] = {0};

// Flag: set by background task when WiFi connected
static volatile bool wifiConnectedByTask = false;
static long gmtOffsetSec = 3600;
static int dstOffsetSec = 3600;

enum InternalState { S_IDLE, S_CONNECTING, S_SYNCING_TIME, S_DONE };
static InternalState state = S_IDLE;

static unsigned long lastCheck = 0;
static int retryCount = 0;

static void (*onStartCb)() = nullptr;
static void (*onDoneCb)() = nullptr;
// Flag: whether to show WiFi sync UI on LCD
static bool showSyncUi = true;  // enabled by default

// Task handle for WiFi.begin() offload
static TaskHandle_t wifiBeginTaskHandle = NULL;

// Complete WiFi init task — runs ALL WiFi hardware on Core 1
// WiFi.mode(), WiFi.begin(), and connection wait — fully non-blocking for Core 0
static void wifiInitTask(void* param) {
  Serial.println("[WiFiSync] wifiInitTask: started on Core 1");

  // Step 1: WiFi driver init (this is the 2-8s blocker on Core 0 — now safe here)
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(false);
  Serial.println("[WiFiSync] wifiInitTask: WiFi.mode(STA) done");

  vTaskDelay(50 / portTICK_PERIOD_MS);

  // Step 2: Start connection
  WiFi.begin(ssidCopy, passCopy);
  Serial.println("[WiFiSync] wifiInitTask: WiFi.begin() called");

  // Step 3: Wait for connection — use brief delay then check, avoid polling
  // ESP32 WiFi connects asynchronously; we just need to wait a moment and check statuscpfornonce
  // Avoid frequent WiFi.status() calls to prevent lock contention with main loop
  vTaskDelay(5000 / portTICK_PERIOD_MS);  // Wait 5s for WiFi async connection

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnectedByTask = true;
    Serial.printf("[WiFiSync] wifiInitTask: connected! IP=%s\n",
                  WiFi.localIP().toString().c_str());
  } else {
    Serial.printf("[WiFiSync] wifiInitTask: not connected after 5s (status=%d)\n", WiFi.status());
  }

  wifiBeginTaskHandle = NULL;
  vTaskDelete(NULL);
}

void setShowSyncUi(bool enable) {
  showSyncUi = enable;
}

// (Auto-sync-on-connect removed - feature disabled)

static constexpr unsigned long WIFI_RETRY_DELAY_MS = 500;
static constexpr int WIFI_MAX_RETRIES = 20;
static constexpr unsigned long NTP_TIMEOUT_MS = 5000;
static constexpr unsigned long MSG_DISPLAY_MS = 1500;
// Periodic automatic sync interval (1 hour)
static constexpr unsigned long PERIODIC_SYNC_INTERVAL_MS = 3600000UL;
static unsigned long lastPeriodicSync = 0;

void setTimeRefs(int &hoursRef, int &minutesRef, int &secondsRef, unsigned long &lastTickRef) {
  pHours = &hoursRef;
  pMinutes = &minutesRef;
  pSeconds = &secondsRef;
  pLastTick = &lastTickRef;
}

void begin(const char* _ssid, const char* _pass,
           const char* _ntp_server, long _gmt_offset, int _dst_offset) {
  ssid = _ssid;
  pass = _pass;
  // Safe copies for background task
  strncpy(ssidCopy, _ssid ? _ssid : "", sizeof(ssidCopy) - 1);
  ssidCopy[sizeof(ssidCopy) - 1] = '\0';
  strncpy(passCopy, _pass ? _pass : "", sizeof(passCopy) - 1);
  passCopy[sizeof(passCopy) - 1] = '\0';
  ntpServer = _ntp_server;
  gmtOffsetSec = _gmt_offset;
  dstOffsetSec = _dst_offset;
  state = S_IDLE;
  lastCheck = 0;
  retryCount = 0;
  wifiConnectedByTask = false;
  lastPeriodicSync = millis();
}

void setOnStart(void (*cb)()) { onStartCb = cb; }
void setOnDone(void (*cb)())  { onDoneCb  = cb; }

static void drawConnectingDots(int dots) {
  if (!showSyncUi) return;
  LCD_CLEAR();
  LCD_SET(0, 0);
  LCD_PRINT("Laczenie WiFi");
  LCD_SET(0, 1);
  for (int i = 0; i < dots; ++i) LCD_PRINT(".");
  LCD_DUMP();
}

static void drawMessage(const char* msg) {
  if (!showSyncUi) return;
  LCD_CLEAR();
  LCD_SET(0, 0);
  LCD_PRINT(msg);
  LCD_DUMP();
}

void startSync() {
  Serial.println("[WiFiSync] startSync() called");
  if (state != S_IDLE) {
    Serial.println("[WiFiSync] startSync() aborted because not in S_IDLE");
    return;
  }

  // ustaw appState, jeśli przekazano wskaźnik
  if (pAppState) *pAppState = STATE_WIFI_SYNC;

  retryCount = 0;
  lastCheck = millis();

  if (onStartCb) onStartCb();

  // Jeśli już jesteśmy połączeni, przejdź od razu do synchronizacji czasu
  if (WiFi.status() == WL_CONNECTED) {
    state = S_SYNCING_TIME;
    lastCheck = millis();
    Serial.println("[WiFiSync] WiFi already connected -> S_SYNCING_TIME");
    configTime(gmtOffsetSec, dstOffsetSec, ntpServer);
    drawMessage("Połączono, NTP...");
    return;
  }

  // Spawn FULL WiFi init on Core 1 — zero blocking on Core 0
  state = S_CONNECTING;
  drawConnectingDots(0);  // Show initial "Łaczenie WiFi" state

  if (!ssidCopy[0]) {
    drawMessage("Brak SSID/PASS");
    state = S_DONE;
    lastCheck = millis();
    return;
  }

  wifiConnectedByTask = false;

  // Kill leftover task if any
  if (wifiBeginTaskHandle != NULL) {
    vTaskDelete(wifiBeginTaskHandle);
    wifiBeginTaskHandle = NULL;
  }

  xTaskCreatePinnedToCore(wifiInitTask, "wifiInit", 4096, NULL, 5, &wifiBeginTaskHandle, 1);
  Serial.println("[WiFiSync] startSync: WiFi init task spawned to Core 1");
}

void setAutoSyncOnConnect(bool enable) {
  // noop: auto-sync-on-connect feature disabled
}

void stop() {
  state = S_IDLE;
  retryCount = 0;
  wifiConnectedByTask = false;

  // Kill background init task if still running
  if (wifiBeginTaskHandle != NULL) {
    vTaskDelete(wifiBeginTaskHandle);
    wifiBeginTaskHandle = NULL;
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  if (pAppState) *pAppState = STATE_HOME;
  if (onDoneCb) onDoneCb();
}

bool isBusy() {
  return state != S_IDLE;
}

void update() {
  unsigned long now = millis();

  // Periodic sync: if device is in WiFi mode, connected and idle, run sync every interval
  if (ModeManager::isWifiOn() && WiFi.status() == WL_CONNECTED && state == S_IDLE) {
    if (now - lastPeriodicSync >= PERIODIC_SYNC_INTERVAL_MS) {
      Serial.println("[WiFiSync] Periodic autoSync triggered (hourly)");
      lastPeriodicSync = now;
      startSync();
      return;
    }
  }

  if (state == S_IDLE) return;

  if (state == S_CONNECTING) {
    // Background task (Core 1) handles WiFi.mode + WiFi.begin + wait
    // Poll wifiConnectedByTask flag set by background task — avoids lock contention
    if (wifiConnectedByTask) {
      state = S_SYNCING_TIME;
      lastCheck = now;
      wifiConnectedByTask = false;
      Serial.println("[WiFiSync] WiFi connected, calling configTime()");
      configTime(gmtOffsetSec, dstOffsetSec, ntpServer);
      drawMessage("Połączono, NTP...");
    } else if (now - lastCheck >= WIFI_RETRY_DELAY_MS) {
      retryCount++;
      drawConnectingDots(retryCount % 5);
      if (retryCount >= WIFI_MAX_RETRIES) {
        drawMessage("Blad WiFi");
        state = S_DONE;
        lastCheck = now;
      } else {
        lastCheck = now;
      }
    }
  }
  else if (state == S_SYNCING_TIME) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {  // 10ms timeout — non-blocking poll
      // jeśli przekazano referencje do zmiennych czasu, ustaw je
      if (pHours)   *pHours   = timeinfo.tm_hour;
      if (pMinutes) *pMinutes = timeinfo.tm_min;
      if (pSeconds) *pSeconds = timeinfo.tm_sec;
      if (pLastTick) *pLastTick = millis();

      drawMessage("Czas ustawiony");
      state = S_DONE;
      lastCheck = now;
    } else if (now - lastCheck >= NTP_TIMEOUT_MS) {
      drawMessage("Blad NTP");
      state = S_DONE;
      lastCheck = now;
    }
  }
  else if (state == S_DONE) {
    if (now - lastCheck >= MSG_DISPLAY_MS) {
      state = S_IDLE;
      if (pAppState) *pAppState = STATE_HOME;
      if (onDoneCb) onDoneCb();
    }
  }
}

} // namespace WiFiSync
