#pragma once

#include <Arduino.h>

namespace AppSettings {

struct State {
  bool buzzerEnabled = true;
  bool backgroundMusicEnabled = true;
  bool mqttEnabled = true;
  bool showEpicIntro = true;
  int homeOverlaySeconds = 7;
  int homeUiProfile = 0;
  int ntpSyncMinutes = 60;
  int alarmMelodyIndex = 0;
};

State& mutableState();
const State& state();
void reset();

}  // namespace AppSettings