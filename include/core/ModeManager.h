#pragma once

#include <Arduino.h>

#include "AppState.h"

// Prosty menedżer trybów zapewniający wzajemne wykluczanie Wi-Fi i A2DP.
namespace ModeManager {

void begin(AppState *statePtr = nullptr);

// Manualne sterowanie Wi-Fi (synchronizacja / uploady).
// Wywołujący odpowiada za uprzednie wygaszenie przeciwnego stosu.
void wifiOn();
void wifiOff();

// Manualne sterowanie audio Bluetooth (A2DP Sink).
void btOn();
void btOff();

void transitionRadio(RadioMode mode);

// Diagnostic logging helper (bez alokacji dynamicznych)
void logDiag(const char* msg);

bool isWifiOn();
bool isBtOn();

} // namespace ModeManager
