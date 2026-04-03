#pragma once

#include <Arduino.h>

struct AlarmEntry {
  uint8_t hour;
  uint8_t minute;
  bool enabled;
  uint16_t lastTriggerDay;
};
