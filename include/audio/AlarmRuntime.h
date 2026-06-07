#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "AlarmTypes.h"
namespace AlarmRuntime {

static constexpr int kMaxAlarms = 8;

struct State {
  bool alarmRinging = false;
  unsigned long alarmStartTime = 0;
  AlarmEntry alarms[kMaxAlarms] = {};
  int alarmsCount = 0;
  int ringingAlarmIndex = -1;
};

State& mutableState();
const State& state();
void reset();

// Czy jakikolwiek alarm jest uzbrojony (enabled)
bool isAnyAlarmArmed();

// Persist pojedynczego alarmu do NVS
void saveAlarm(Preferences &prefs, int idx);
// Persist wszystkich alarmów
void saveAllAlarms(Preferences &prefs);
// Odczyt wszystkich alarmów z NVS (migracja starych kluczy)
void loadAllAlarms(Preferences &prefs);

}  // namespace AlarmRuntime
