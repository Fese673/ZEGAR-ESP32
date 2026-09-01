#pragma once

#include <Arduino.h>
namespace AppSettings {

struct State {
  bool buzzerEnabled = true;
  bool touchTestEnabled = true;
  bool backgroundMusicEnabled = true;
  bool mqttEnabled = true;
  bool showEpicIntro = true;
  int homeOverlaySeconds = 7;
  int homeUiProfile = 0;
  int ntpSyncMinutes = 60;
  int alarmMelodyIndex = 0;
  /* Etap 2: jasnosc 7-seg (0..100), forward do STM32 jako BRT:XX.
   * 100 = pelna, 0 = zgaszone. Persistence: NVS "segBrightness". */
  uint8_t sevenSegBrightness = 100;
};

State& mutableState();
const State& state();
void reset();

}  // namespace AppSettings