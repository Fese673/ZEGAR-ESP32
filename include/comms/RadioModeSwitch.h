/*
 * RadioModeSwitch.h
 * Przełączanie Wi-Fi / Bluetooth z zapisem decyzji w RTC memory.
 */
#pragma once

#include <stdint.h>

enum RadioModeSwitchState {
  RADIO_STATE_WIFI,
  RADIO_STATE_BT,
  RADIO_STATE_TRANSITIONING,
};

enum RadioModeSwitchNextMode {
  RADIO_NEXT_WIFI,
  RADIO_NEXT_BT,
  RADIO_NEXT_NONE,
};

namespace RadioModeSwitch {

// Init
void begin();
void update();
RadioModeSwitchState getCurrentState();
RadioModeSwitchNextMode getNextMode();
bool isInitializing();

// Mode switch requests
void requestModeSwitch_WiFi();
void requestModeSwitch_BT();
void cancelModeSwitch();

// Boot mode handling
void initializeStartMode();
bool isDefaultStartupWiFi();
bool wasBootHandoffDetected();
void forceMode(RadioModeSwitchState state, RadioModeSwitchNextMode nextMode = RADIO_NEXT_NONE);

// RTC snapshot
uint8_t getRTCHours();
uint8_t getRTCMinutes();
uint8_t getRTCSeconds();
void clearRTCTime();

// Diagnostics
void printDiagnostics();

}  // namespace RadioModeSwitch
