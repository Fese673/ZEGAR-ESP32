/*
 * ModeManager.h
 * Menedżer trybów Wi-Fi / Bluetooth dla aplikacji.
 */
#pragma once

#include "AppState.h"

namespace ModeManager {

// --- Inicjalizacja managera trybów ---
void begin(AppState* statePtr = nullptr);

// --- Wi-Fi ---
void wifiOn();
void wifiOff();

// --- Bluetooth A2DP ---
void btOn();
void btOff();

// --- Przełączenie trybu radia ---
void transitionRadio(RadioMode mode);

// --- Diagnostyka bez alokacji ---
void logDiag(const char* msg);

// --- Stan radiowy ---
bool isWifiOn();
bool isBtOn();

}  // namespace ModeManager
