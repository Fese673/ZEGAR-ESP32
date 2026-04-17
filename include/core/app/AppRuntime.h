#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "BoardPins.h"

static constexpr unsigned long CLOCK_TICK_MS = 1000UL;
static constexpr unsigned long ALARM_DURATION_MS = 60000UL;
static constexpr unsigned long STM32_UPDATE_MS = 500UL;
static constexpr unsigned long STM32_TIMEOUT_MS = 3000UL;
static constexpr unsigned long STOPER_DRAW_MS = 100UL;
static constexpr uint8_t BUZZER_PIN = BoardPins::kBuzzer;

struct MainRuntimeState {
  bool bootDiagReprinted = false;
  unsigned long lastSTM32Update = 0;
  unsigned long lastSTM32DataReceived = 0;
  unsigned long lastStoperDraw = 0;
};

struct RuntimeContext {
  int& hours;
  int& minutes;
  int& seconds;
  unsigned long& lastTick;
  bool& mqttEnabled;
  int& settingsRotationSec;
  int& settingsUiScreenIndex;
  int& settingsMqttMenuIndex;
  int& settingsAlarmMelodyIndex;
  int& prevSettingsAlarmMelodyIndex;
  int& prevSettingsRotationSec;
  int& prevSettingsUiScreenIndex;
  int& alarmsCount;
  MainRuntimeState& state;
};

extern Preferences s_prefs;

extern int timerSetMinutes;
extern int timerSetSeconds;
extern bool timerRunning;
extern unsigned long timerStartMillis;
extern unsigned long timerDurationMs;
extern int timerSetHours;
extern int timerUiCursor;
extern int timerPresetIndex;

extern int displayedBPM;
extern int displayedSPO2;
extern bool stm32Connected;

extern bool stoperRunning;
extern unsigned long stoperStart;
extern unsigned long stoperElapsed;

extern int hours;
extern int minutes;
extern int seconds;
extern unsigned long lastTick;

extern MainRuntimeState g_mainRuntimeState;

RuntimeContext makeRuntimeContext();
void setHomeUiProfile(uint8_t profileIndex);
