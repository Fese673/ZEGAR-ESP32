#include "ModeManager.h"

#include <WiFi.h>
#include <Esp.h>
#include <Arduino.h>

#include "WiFiSync.h"
#include "Bluetooth/AudioBT.h"
#include "Encoder.h"

namespace ModeManager {

// ============================================================================
// ZMIENNE STANU
// ============================================================================
static bool      wifiActive = false;
static bool      btActive   = false;
static AppState* pState     = nullptr;

// ============================================================================
// INICJALIZACJA
// ============================================================================
void begin(AppState* statePtr) {
  pState = statePtr;
}

// ============================================================================
// FUNKCJE POMOCNICZE
// ============================================================================
static void ensureHome() {
  if (pState && *pState != STATE_HOME) {
    *pState = STATE_HOME;
  }
}

// ============================================================================
// ZARZĄDZANIE WiFi
// ============================================================================
void wifiOn() {
  // Najpierw wyłącz BT, aby uniknąć kolizji heap/IRQ
  if (btActive) {
    btOff();
  }

  // Rozpocznij proces Wi-Fi (WiFiSync zajmie się połączeniem/NTP)
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.disconnect(true);

  wifiActive = true;
  ensureHome();
  WiFiSync::startSync();
}

void wifiOff() {
  WiFiSync::stop();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiActive = false;
  ensureHome();
}

// ============================================================================
// ZARZĄDZANIE BLUETOOTH
// ============================================================================
void btOn() {
  // BT tylko gdy Wi-Fi jest wyłączone
  if (wifiActive) {
    wifiOff();
  }

  if (!btActive) {
    audioBT_init();
    btActive = true;
    // BEZPIECZEŃSTWO: Przywróć piny enkodera (GPIO 25, 26) do INPUT_PULLUP
    // I2S teraz używa GPIO 33/32 zamiast 25/26 - konflikt ROZWIĄZANY
    // encoder_reinit_pins() zapewnia stabilną reinicjalizację po I2S init
    encoder_reinit_pins();
  }
  ensureHome();
}

void btOff() {
  if (btActive) {
    audioBT_deinit();
    btActive = false;
  }
  ensureHome();
}

// ============================================================================
// PRZEŁĄCZANIE TRYBU RADIA
// ============================================================================
void transitionRadio(RadioMode mode) {
  if (mode == WIFI_ONLY) {
    wifiOn();
  } else {
    btOn();
  }
}

// ============================================================================
// DIAGNOSTYKA
// ============================================================================
void logDiag(const char* msg) {
  // Minimalny diagnostyczny helper bez alokacji; używany w setup()
  if (msg) {
    Serial.print("[ModeManager] ");
    Serial.print(msg);
    Serial.print(" heap=");
    Serial.println(ESP.getFreeHeap());
  }
}

// ============================================================================
// GETTERY STANU
// ============================================================================
bool isWifiOn() { return wifiActive; }
bool isBtOn()   { return btActive; }

} // namespace ModeManager
