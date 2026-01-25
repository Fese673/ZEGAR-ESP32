#include "WiFiSync.h"
#include <WiFi.h>
#include "esp_wifi.h"

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

static constexpr unsigned long WIFI_RETRY_DELAY_MS = 500;
static constexpr int WIFI_MAX_RETRIES = 20;
static constexpr unsigned long NTP_TIMEOUT_MS = 5000;
static constexpr unsigned long MSG_DISPLAY_MS = 1500;

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
}

void setOnStart(void (*cb)()) { onStartCb = cb; }
void setOnDone(void (*cb)())  { onDoneCb  = cb; }

static void drawConnectingDots(int dots) {
  LCD_CLEAR();
  LCD_SET(0, 0);
  LCD_PRINT("Laczenie WiFi");
  LCD_SET(0, 1);
  for (int i = 0; i < dots; ++i) LCD_PRINT(".");
  LCD_DUMP();
}

static void drawMessage(const char* msg) {
  LCD_CLEAR();
  LCD_SET(0, 0);
  LCD_PRINT(msg);
  LCD_DUMP();
}

void startSync() {
  if (state != S_IDLE) return;

  // ustaw appState, jeśli przekazano wskaźnik
  if (pAppState) *pAppState = STATE_WIFI_SYNC;

  state = S_CONNECTING;
  retryCount = 0;
  lastCheck = millis();

  if (onStartCb) onStartCb();

  drawConnectingDots(0);

  if (ssid && pass) {
    WiFi.begin(ssid, pass);
  } else {
    drawMessage("Brak SSID/PASS");
    state = S_DONE;
    lastCheck = millis();
  }
}

void stop() {
  // Siłowe zatrzymanie procesu Wi-Fi/NTP i zamknięcie radia.
  state = S_IDLE;
  retryCount = 0;
  WiFi.disconnect(true);
  // ustaw tryb OFF zanim zatrzymamy/deinicjalizujemy sterownik
  WiFi.mode(WIFI_OFF);
  esp_wifi_stop();
  esp_wifi_deinit();
  if (pAppState) *pAppState = STATE_HOME;
  if (onDoneCb) onDoneCb();
}

bool isBusy() {
  return state != S_IDLE;
}

void update() {
  if (state == S_IDLE) return;

  unsigned long now = millis();

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
