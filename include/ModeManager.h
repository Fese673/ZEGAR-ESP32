
#pragma once

#include <Arduino.h>

#include "AppState.h"

// Prosty menedżer trybów zapewniający wzajemne wykluczanie Wi-Fi i A2DP.
namespace ModeManager {

void begin(AppState *statePtr = nullptr);

// Manualne sterowanie Wi-Fi (synchronizacja / uploady).
void wifiOn();   // włączy Wi-Fi, zatrzyma BT jeśli działa
void wifiOff();  // rozłączy Wi-Fi i wyłączy radio

// Manualne sterowanie audio Bluetooth (A2DP Sink).
void btOn();     // włączy BT, wyłączy Wi-Fi jeśli aktywne
void btOff();    // rozłączy BT i zwolni pamięć stosu

bool isWifiOn();
bool isBtOn();

} // namespace ModeManager
