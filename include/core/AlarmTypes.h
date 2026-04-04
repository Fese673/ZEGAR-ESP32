/*
  * AlarmTypes.h
  * Definicje typów związanych z alarmami
*/
#pragma once

#include <Arduino.h>

//--- Struktura pojedynczego wpisu alarmu ---
struct AlarmEntry {
  uint8_t hour;
  uint8_t minute;
  bool enabled;
  uint16_t lastTriggerDay;
};
