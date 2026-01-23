#pragma once
#include "Encoder.h"

// Pointery na funkcje renderowania z main.cpp
typedef void (*DrawFn)();
typedef void (*Update7SegFn)();
typedef void (*Update7SegStoperFn)(int mins, int secs, int centisec);

// Struktura zawierająca wszystkie callbacki UI
// Przekazujesz je w ui_begin() - wszystkie funkcje są zdefiniowane w main.cpp
typedef struct {
  DrawFn drawHome;                         // Ekran główny (godzina)
  DrawFn drawMenu;                         // Menu z opcjami
  DrawFn drawSetTime;                      // Ekran edycji czasu
  DrawFn drawAlarm;                        // Ekran edycji alarmu
  DrawFn drawStoper;                       // Ekran stopera
  DrawFn drawDebugSTM32;                   // Debug panel
  Update7SegFn updateSevenSeg;             // Odśwież wyświetlacz 7-seg
  Update7SegStoperFn updateSevenSegStoper; // Odśwież 7-seg dla stopera (min, sec, centisec)
  DrawFn drawStats; // UI statystyk
  DrawFn drawSystemResources; // Rysowanie zasobów systemu (RAM/FLASH)
} UI_Callbacks;

// Inicjalizacja kontrolera UI
// Przekaż strukturę ze wszystkimi callbackami
// Wywoływane raz w setup()
void ui_begin(const UI_Callbacks &callbacks);

// Obsługa zdarzenia enkodera
// Zmienia stany aplikacji na podstawie ENC_LEFT, ENC_RIGHT, ENC_CLICK, ENC_LONG
// Wywoływane w loop() gdy encoder_update() != ENC_NONE
void ui_handleEvent(EncoderEvent e);

// Periodyczne zadania UI
// Wywoływane w loop() - opcjonalne, można pustą zostawić
void ui_tick();