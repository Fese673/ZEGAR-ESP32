#pragma once
#include <stdint.h>

// OSOBNY moduł komunikacji dla alarmów i timera
// Nie miesza się z esp_to_gution/ — czysta separacja odpowiedzialności

namespace TimeSync {

// Nowe typy ramek (pasmo 0x0D-0x16, wolne)
constexpr uint8_t kTypeAlarmList      = 0x0D;  // pełna lista alarmów (sync list)
constexpr uint8_t kTypeSetAlarm       = 0x0E;  // G→Z: edycja pojedynczego alarmu
constexpr uint8_t kTypeTimerState     = 0x0F;  // Z→G: stan minutnika
constexpr uint8_t kTypeTimerCmd       = 0x12;  // G→Z: komenda minutnika
constexpr uint8_t kTypeEditLock       = 0x13;  // Z→G: blokada edycji alarmów
constexpr uint8_t kTypeAlarmAction    = 0x14;  // G→Z: drzemka/wyłącz
constexpr uint8_t kTypeStopwatchState = 0x15;  // Z→G: stan stopera
constexpr uint8_t kTypeStopwatchCmd   = 0x16;  // G→Z: komenda stopera

// --- Send functions (Zegar → Gution) ---
void sendAlarmList();
void sendTimerState();
void sendEditLock(bool locked);
void sendStopwatchState();

// --- Handler functions (Gution → Zegar) ---
void handleAlarmListSync(const uint8_t* payload, uint16_t len);
void handleSetAlarm(const uint8_t* payload, uint16_t len);
void handleTimerCmd(const uint8_t* payload, uint16_t len);
void handleAlarmAction(const uint8_t* payload, uint16_t len);
void handleStopwatchCmd(const uint8_t* payload, uint16_t len);

} // namespace TimeSync
