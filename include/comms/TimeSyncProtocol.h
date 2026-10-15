#pragma once
#include <stdint.h>
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
// Tryb 7-seg (przyciski kolowe na ekranie ZEGAR w Guitonie):
constexpr uint8_t kTypeSegModeCmd     = 0x17;  // G→Z: [mode 0/1/2] wybor zawartosci 7-seg
constexpr uint8_t kTypeSegModeState   = 0x18;  // Z→G: [mode] potwierdzenie (zmiana/HELLO/refresh)

// Tryby 7-seg (wspolne z Guitonem — lustro w timer_synchro.h).
constexpr uint8_t kSegModeClock    = 0;  // zwykly zegar GG:MM:SS
constexpr uint8_t kSegModeStopwatch = 1; // czas stopera MM:SS,CS (albo HH:MM:SS >= 60 min)
constexpr uint8_t kSegModeTimer    = 2;  // odliczanie minutnika

// --- Send functions (Zegar → Gution) ---
void sendAlarmList();
void sendTimerState();
void sendEditLock(bool locked);
void sendStopwatchState();
void sendSegModeState();

// --- Handler functions (Gution → Zegar) ---
void handleAlarmListSync(const uint8_t* payload, uint16_t len);
void handleSetAlarm(const uint8_t* payload, uint16_t len);
void handleTimerCmd(const uint8_t* payload, uint16_t len);
void handleAlarmAction(const uint8_t* payload, uint16_t len);
void handleStopwatchCmd(const uint8_t* payload, uint16_t len);
// Zwraca true gdy tryb przyjety (valid 0..2) — false = NACK (dane odrzucone).
bool handleSegModeCmd(const uint8_t* payload, uint16_t len);

// Aktualny tryb 7-seg (0=clock, 1=stoper, 2=minutnik). Wlascicielem jest
// ZEGAR; Guition tylko klika i odzwierciedla echo. Bez persistencji —
// po restarcie zawsze zegar (decyzja produktowa).
uint8_t getSegMode();

} // namespace TimeSync
