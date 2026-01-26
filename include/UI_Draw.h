#ifndef UI_DRAW_H
#define UI_DRAW_H


#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include "LCDMirror.h"
#include "AppState.h" // for EditState / AppState enums


// --- Fallback pin definitions (will not override existing defines in main) ---
#ifndef DATA_PIN
#define DATA_PIN 23
#endif
#ifndef CLOCK_PIN
#define CLOCK_PIN 18
#endif
#ifndef LATCH_PIN
#define LATCH_PIN 5
#endif


// --- Externs: variables defined in your main program ---
extern int hours;
extern int minutes;
extern int seconds;


extern bool alarmEnabled;
extern int alarmHour;
extern int alarmMinute;


extern EditState editState; // from AppState.cpp/h


extern int menuIndex;
extern const char* menuItems[];
extern int menuCount;


extern bool stoperRunning;
extern unsigned long stoperStart;
extern unsigned long stoperElapsed;


extern int displayedBPM;
extern int displayedSPO2;
extern bool stm32Connected;

// === Zmienne dla DHT ===
extern float dhtTemperature;
extern float dhtHumidity;
extern bool dhtReady;
extern bool dhtScreenDirty;

// Zmienne do triku z zamrażaniem czasu na 7-seg
extern int savedHours, savedMinutes, savedSeconds;
extern bool timeSaved;


// LCD object (your main must define it, e.g. LiquidCrystal_I2C lcd(...))
extern LiquidCrystal_I2C lcd;


// --- Prototypes (these are the functions moved from main) ---


// UI / LCD
void drawHome();
void drawMenu();
void drawSetTime();
void drawAlarm();
void drawStoper();
void drawDebugSTM32();
void printTime(bool edit);
void printVal(int v, bool sel);
void drawStats();  // UI statystyk
void drawSystemResources(); // UI zasobów systemu (RAM/FLASH)
void drawModeTransition(); // UI przejścia trybu (WiFi ↔ Bluetooth)

// === Funkcje DHT ===
void drawTemperature();
void drawHumidity();
void showTemperature7Seg();
void showHumidity7Seg();



// 7-seg
uint8_t swapNibbles(uint8_t v);
void slowShiftOut(uint8_t v);
void initSevenSeg();
void updateSevenSeg();
void updateSevenSegStoper(int mins, int secs, int centisec);


#endif // UI_DRAW_H