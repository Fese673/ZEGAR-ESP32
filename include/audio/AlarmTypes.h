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
  uint8_t dayMask;          // bit0=Pn bit1=Wt bit2=Śr bit3=Cz bit4=Pt bit5=So bit6=Nd
  uint8_t flags;            // bit0=singleShot
  uint16_t lastTriggerDay;

  // Domyślna inicjalizacja: wszystkie dni, brak singleShot
  AlarmEntry()
    : hour(7), minute(0), enabled(true),
      dayMask(0x7F), flags(0), lastTriggerDay(UINT16_MAX) {}
};
