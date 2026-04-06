#pragma once

#include <Arduino.h>

#include "AppState.h"

namespace UIState {

struct MenuState {
  int index = 0;
  int count = 0;
  const char* const* items = nullptr;
};

struct State {
  MenuState mainMenu;
  MenuState gamesMenu;
  MenuState statsMenu;
  MenuState resourcesMenu;
  MenuState pmsMenu;
  MenuState pmsCf1Menu;
  MenuState pmsAtmMenu;
  MenuState pmsParticlesMenu;
  MenuState ens160Menu;
  MenuState bmp280Menu;
  MenuState settingsMenu;
  MenuState settingsPmsMenu;
  MenuState settingsBuzzerMenu;
  MenuState settingsMqttMenu;
  MenuState settingsAlarmMelodyMenu;
  MenuState settingsBootIntroMenu;
  MenuState settingsUiScreenMenu;
  MenuState alarmsMenu;

  int selectedAlarmIndex = 0;
  int alarmEditCursor = 0;
  bool pmsScreenDirty = true;
  AppState alarmReturnState = STATE_MENU;

  int prevSettingsAlarmMelodyIndex = 0;
  int prevSettingsRotationSec = 7;
  int prevSettingsSyncMin = 60;
  int prevSettingsUiScreenIndex = 0;
};

State& mutableState();
const State& state();
void reset();

}  // namespace UIState