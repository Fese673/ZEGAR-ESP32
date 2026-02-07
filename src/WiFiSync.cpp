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
static long gmtOffsetSec = 3600;
static int dstOffsetSec = 3600;

enum InternalState { S_IDLE, S_CONNECTING, S_SYNCING_TIME, S_DONE };
static InternalState state = S_IDLE;

static unsigned long lastCheck = 0;
static int retryCount = 0;

static void (*onStartCb)() = nullptr;
static void (*onDoneCb)() = nullptr;
// Control whether sync-related UI (LCD) should be shown
static bool showSyncUi = false; // disabled by default per user request

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
  ntpServer = _ntp_server;
  gmtOffsetSec = _gmt_offset;
  dstOffsetSec = _dst_offset;
  state = S_IDLE;
  lastCheck = 0;
  retryCount = 0;
  // Initialize periodic sync timer to avoid immediate trigger after boot
  lastPeriodicSync = millis();

  // Auto-sync-on-connect feature removed per user request; no event handler registered
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

  // W przeciwnym razie rozpocznij łączenie
  state = S_CONNECTING;
  drawConnectingDots(0);

  if (ssid && pass) {
    WiFi.begin(ssid, pass);
  } else {
    drawMessage("Brak SSID/PASS");
    state = S_DONE;
    lastCheck = millis();
  }
}

void setAutoSyncOnConnect(bool enable) {
  // noop: auto-sync-on-connect feature disabled
}

void stop() {
  // Zatrzymanie procesu Wi-Fi/NTP bez całkowitego deinicjalizowania drivera
  // (bo to uniemożliwia handler events przy ponownym włączeniu WiFi)
  state = S_IDLE;
  retryCount = 0;
  
  WiFi.disconnect(true);    // Disconnect and turn off radio, ale nie deinicjalizuj driver
  WiFi.mode(WIFI_OFF);      // Set mode OFF
  
  // NIE wywoływać esp_wifi_stop() ani esp_wifi_deinit() - to blokuje handler events
  // przy ponownym włączeniu WiFi. Handler będzie działać gdy WiFi.begin() będzie wywoływane
  
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
    if (WiFi.status() == WL_CONNECTED) {
      state = S_SYNCING_TIME;
      lastCheck = now;

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
    if (getLocalTime(&timeinfo)) {
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
