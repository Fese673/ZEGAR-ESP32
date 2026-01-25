#include "ModeManager.h"

#include <WiFi.h>
#include <Esp.h>
#include <Arduino.h>

#include "WiFiSync.h"
#include "Bluetooth/AudioBT.h"

namespace ModeManager {

// Lokalny stan – śledzimy czy radia są aktywne.
static bool wifiActive = false;
static bool btActive = false;
static AppState *pState = nullptr;

void begin(AppState *statePtr) { pState = statePtr; }

static void ensureHome() {
	if (pState && *pState != STATE_HOME) {
		*pState = STATE_HOME;
	}
}

void wifiOn() {
	// Najpierw wyłącz BT, aby uniknąć kolizji heap/IRQ.
	if (btActive) {
		btOff();
	}

	// Rozpocznij proces Wi-Fi (WiFiSync zajmie się połączeniem/NTP).
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

void btOn() {
	// BT tylko gdy Wi-Fi jest wyłączone.
	if (wifiActive) {
		wifiOff();
	}

	if (!btActive) {
		audioBT_init();
		btActive = true;
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

void transitionRadio(RadioMode mode) {
    if (mode == WIFI_ONLY) {
        wifiOn();
    } else {
        btOn();
    }
}

void logDiag(const char* msg) {
	// Minimalny diagnostyczny helper bez alokacji; używany w setup()
	if (msg) {
		Serial.print("[ModeManager] ");
		Serial.print(msg);
		Serial.print(" heap=");
		Serial.println(ESP.getFreeHeap());
	}
}

bool isWifiOn() { return wifiActive; }
bool isBtOn() { return btActive; }

} // namespace ModeManager
