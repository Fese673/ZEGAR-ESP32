#include "ModeManager.h"

#include <Esp.h>
#include <Arduino.h>

#include "RadioModeSwitch.h"
#include "WiFiSync.h"
#include "RamTelemetry.h"
#include "bluetooth/AudioBT.h"
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
  if (btActive) {
    Serial.println("[ModeManager] wifiOn() refused: BT is still active");
    return;
  }

  if (wifiActive) {
    ensureHome();
    return;
  }

  // ALL WiFi hardware init is now in WiFiSync background task (Core 1)
  // No WiFi.mode/begin/disconnect here — prevents blocking main loop
  wifiActive = true;
  radioMode = WIFI_ONLY;
  ensureHome();
  WiFiSync::startSync();
  RAM_CHECKPOINT("WIFI_ON");
}

void wifiOff() {
  const bool wasActive = wifiActive;
  WiFiSync::stop();
  wifiActive = false;
  ensureHome();
  if (wasActive) {
    RAM_CHECKPOINT("WIFI_OFF");
  }
}

// ============================================================================
// ZARZĄDZANIE BLUETOOTH
// ============================================================================
void btOn() {
  if (wifiActive) {
    Serial.println("[ModeManager] btOn() refused: Wi-Fi is still active");
    return;
  }

  if (btActive) {
    ensureHome();
    return;
  }

  if (!btActive) {
    if (!audioBT_init()) {
      Serial.println("[ModeManager] BT init failed (BT mode retained, no WiFi fallback)");
      btActive = false;
      radioMode = BT_ONLY;
      RadioModeSwitch::forceMode(RADIO_STATE_BT, RADIO_NEXT_BT);
      ensureHome();
      return;
    }
    btActive = true;
    radioMode = BT_ONLY;
    RadioModeSwitch::forceMode(RADIO_STATE_BT, RADIO_NEXT_BT);
    // BEZPIECZEŃSTWO: Przywróć piny enkodera (GPIO 25, 26) do INPUT_PULLUP
    // I2S teraz używa GPIO 33/32 zamiast 25/26 - konflikt ROZWIĄZANY
    // encoder_reinit_pins() zapewnia stabilną reinicjalizację po I2S init
    encoder_reinit_pins();
    RAM_CHECKPOINT("BT_ON");
  }
  ensureHome();
}

void btOff() {
  const bool wasActive = btActive;
  if (btActive) {
    audioBT_deinit();
    btActive = false;
  }
  ensureHome();
  if (wasActive) {
    RAM_CHECKPOINT("BT_OFF");
  }
}

// ============================================================================
// PRZEŁĄCZANIE TRYBU RADIA
// ============================================================================
void transitionRadio(RadioMode mode) {
  if (mode == WIFI_ONLY) {
    btOff();
    wifiOn();
  } else {
    wifiOff();
    btOn();
  }
  RAM_CHECKPOINT("MODE_SWITCH_DONE");
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
