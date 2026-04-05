#include "ModeManager.h"

#include <Arduino.h>
#include <Esp.h>

#include "AppLog.h"

#include "Encoder.h"
#include "RadioModeSwitch.h"
#include "RamTelemetry.h"
#include "WiFiSync.h"
#include "bluetooth/AudioBT.h"

namespace ModeManager {
namespace {
constexpr char TAG[] = "MODE";

bool s_wifiActive = false;
bool s_btActive = false;
AppState* s_appState = nullptr;

void keepHomeScreen() {
  if (s_appState != nullptr && *s_appState != STATE_HOME) {
    *s_appState = STATE_HOME;
  }
}

void setRadioMode(RadioMode mode) {
  radioMode = mode;
}

void resetRuntimeState() {
  s_wifiActive = false;
  s_btActive = false;
  setRadioMode(WIFI_ONLY);
}

void stopBluetoothStack() {
  if (!s_btActive) {
    return;
  }

  audioBT_deinit();
  s_btActive = false;
  RAM_CHECKPOINT("BT_OFF");
}

bool startBluetoothStack() {
  if (!audioBT_init()) {
    LOG_E(TAG, "BT init failed bt_mode_retained=true no_wifi_fallback=true");
    s_btActive = false;
    setRadioMode(BT_ONLY);
    RadioModeSwitch::forceMode(RADIO_STATE_BT, RADIO_NEXT_BT);
    keepHomeScreen();
    return false;
  }

  s_btActive = true;
  setRadioMode(BT_ONLY);
  RadioModeSwitch::forceMode(RADIO_STATE_BT, RADIO_NEXT_BT);
  encoder_reinit_pins();
  RAM_CHECKPOINT("BT_ON");
  return true;
}

void logHeapSnapshot(const char* label) {
  if (label == nullptr) {
    return;
  }

  LOG_I(TAG,
        "Heap snapshot checkpoint=%s heap_b=%lu wifi=%s bt=%s mode=%u",
        label,
        (unsigned long)ESP.getFreeHeap(),
        s_wifiActive ? "ON" : "OFF",
        s_btActive ? "ON" : "OFF",
        (unsigned int)radioMode);
}

}  // namespace

void begin(AppState* statePtr) {
  s_appState = statePtr;
  resetRuntimeState();
}

void wifiOn() {
  if (s_btActive) {
    stopBluetoothStack();
  }

  if (s_wifiActive) {
    keepHomeScreen();
    return;
  }

  s_wifiActive = true;
  setRadioMode(WIFI_ONLY);
  keepHomeScreen();
  WiFiSync::startSync();
  RAM_CHECKPOINT("WIFI_ON");
}

void wifiOff() {
  const bool wasActive = s_wifiActive;

  WiFiSync::stop();
  s_wifiActive = false;
  keepHomeScreen();

  if (wasActive) {
    RAM_CHECKPOINT("WIFI_OFF");
  }
}

void btOn() {
  if (s_wifiActive) {
    wifiOff();
  }

  if (s_btActive) {
    keepHomeScreen();
    return;
  }

  if (!startBluetoothStack()) {
    return;
  }

  keepHomeScreen();
}

void btOff() {
  const bool wasActive = s_btActive;

  stopBluetoothStack();
  keepHomeScreen();

  if (!wasActive) {
    return;
  }
}

void transitionRadio(RadioMode mode) {
  switch (mode) {
    case WIFI_ONLY:
      wifiOn();
      break;
    case BT_ONLY:
      btOn();
      break;
    default:
      wifiOn();
      break;
  }
  RAM_CHECKPOINT("MODE_SWITCH_DONE");
}

void logDiag(const char* msg) {
  logHeapSnapshot(msg);
}

bool isWifiOn() {
  return s_wifiActive;
}

bool isBtOn() {
  return s_btActive;
}

}  // namespace ModeManager
