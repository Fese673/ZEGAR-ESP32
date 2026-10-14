#pragma once
#include <Arduino.h>
#include <stdint.h>

enum class StopwatchState : uint8_t {
  IDLE     = 0,
  RUNNING  = 1,
  STOPPED  = 2,
};

// Tryb wyswietlania stopera — ujednolicony z Gution (EEZ):
//   < 60 min  -> MS: pole setnych (MM:SS.CS) + etykieta "(MS)"
//   >= 60 min -> S:  sekundy (HH:MM:SS) bez setnych + etykieta "(S)"
enum class StopwatchDisplayMode : uint8_t {
  MS = 0,
  S  = 1,
};

namespace StopwatchService {

// Progi trybow wyswietlania (jedno zrodlo prawdy na Zegarze).
constexpr uint32_t kSecondsModeThresholdMs = 3600000UL;  // 60 min
constexpr uint32_t kMaxDisplayHours        = 99UL;       // cap HH na 7-seg/LCD

// State machine
void start();
void stop();
void reset();

// Read-only
StopwatchState getState();
uint32_t getElapsedMs();
bool isRunning();
StopwatchDisplayMode getDisplayMode();

}  // namespace StopwatchService
