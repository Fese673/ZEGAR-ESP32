#include "AppRuntime.h"

#include "AlarmRuntime.h"
#include "AppSettings.h"
#include "HomeRuntime.h"
#include "UIState.h"
Preferences s_prefs;

volatile bool g_alarmEditActive = false;
volatile bool g_nvsAlarmsDirty = false;

int timerSetMinutes = 0;
int timerSetSeconds = 0;
bool timerRunning = false;
unsigned long timerStartMillis = 0;
unsigned long timerDurationMs = 0;
int timerSetHours = 0;
int timerUiCursor = 0;
int timerPresetIndex = 1;

int displayedBPM = 0;
int displayedSPO2 = 0;
bool stm32Connected = false;

MainRuntimeState g_mainRuntimeState;

RuntimeContext makeRuntimeContext() {
  AppSettings::State& appSettings = AppSettings::mutableState();
  UIState::State& uiState = UIState::mutableState();
  AlarmRuntime::State& alarmRuntime = AlarmRuntime::mutableState();

  return RuntimeContext{
      appSettings.mqttEnabled,
      appSettings.homeOverlaySeconds,
      appSettings.homeUiProfile,
      uiState.settingsMqttMenu.index,
      appSettings.alarmMelodyIndex,
      uiState.prevSettingsAlarmMelodyIndex,
      uiState.prevSettingsRotationSec,
      uiState.prevSettingsUiScreenIndex,
      alarmRuntime.alarmsCount,
      g_mainRuntimeState,
  };
}

void setHomeUiProfile(uint8_t profileIndex) {
  HomeRuntime::setProfile(profileIndex);
}