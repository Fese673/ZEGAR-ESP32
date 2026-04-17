#pragma once

#include <Arduino.h>

#include "AlarmTypes.h"

namespace AlarmRuntime {

static constexpr int kMaxAlarms = 8;

struct State {
  int alarmHour = 7;
  int alarmMinute = 0;
  bool alarmEnabled = false;
  bool alarmRinging = false;
  unsigned long alarmStartTime = 0;
  AlarmEntry alarms[kMaxAlarms] = {};
  int alarmsCount = 0;
};

State& mutableState();
const State& state();
void reset();

}  // namespace AlarmRuntime