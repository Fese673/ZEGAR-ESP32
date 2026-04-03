#ifndef UI_DRAW_H
#define UI_DRAW_H


#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include "LCDMirror.h"
#include "AppState.h" // for EditState / AppState enums
#include "BoardPins.h"
#include "AlarmTypes.h"


// --- Fallback pin definitions (will not override existing defines in main) ---
#ifndef DATA_PIN
#define DATA_PIN BoardPins::kSevenSegData
#endif
#ifndef CLOCK_PIN
#define CLOCK_PIN BoardPins::kSevenSegClock
#endif
#ifndef LATCH_PIN
#define LATCH_PIN BoardPins::kSevenSegLatch
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
// === Flaga ekranów PMS5003 - wymusi rysowanie przy wejściu do podmenu ===
extern bool pmsScreenDirty;

// === Zmienne Ustawień (Settings) ===
extern int settingsMenuIndex;
extern const char* settingsMenuItems[];
extern int settingsMenuCount;
extern int settingsPmsMenuIndex;
extern const char* settingsPmsMenuItems[];
extern int settingsPmsMenuCount;
extern int settingsMqttMenuIndex;
extern const char* settingsMqttMenuItems[];
extern int settingsMqttMenuCount;
extern int settingsBuzzerMenuIndex;
extern const char* settingsBuzzerMenuItems[];
extern int settingsBuzzerMenuCount;
extern int settingsAlarmMelodyIndex;
extern bool pms5003Enabled;
extern bool buzzerEnabled;
extern bool mqttEnabled;
extern int settingsRotationSec;
extern int s_prevSettingsRotationSec;
extern int settingsSyncMinutes;
extern int s_prevSettingsSyncMin;
// Alarms
extern const int MAX_ALARMS;
extern AlarmEntry alarms[];
extern int alarmsCount;
extern int alarmsMenuIndex; // selection in list
extern int selectedAlarmIndex; // editing index
extern int alarmEditCursor; // 0=CZAS,1=STATUS,2=USUN

// Minutnik (Timer) - ustawienia i status
extern int timerSetMinutes;
extern int timerSetSeconds;
extern bool timerRunning;

extern int timerSetHours;
extern unsigned long timerStartMillis;
extern unsigned long timerDurationMs;
extern int timerUiCursor;   // 0=CZAS, 1=PRESETY
extern int timerPresetIndex; // 0=2m, 1=15m, 2=45m


// LCD object (your main must define it, e.g. LiquidCrystal_I2C lcd(...))
extern LiquidCrystal_I2C lcd;


// --- Prototypes (these are the functions moved from main) ---


// UI / LCD
void drawHome();
void drawAirScreen();
void drawIndoorWeatherScreen();
void drawExtremeEnvironmentScreen();
void drawExtremeAlgorithmScreen();
void setHomeUiProfile(uint8_t profileIndex);
void drawMenu();
void drawSetTime();
void drawAlarm();
void drawTimer();
void drawStoper();
void drawDebugSTM32();
void printTime(bool edit);
void printVal(int v, bool sel);
void drawStats();  // UI statystyk
void drawSystemResources(); // UI zasobów systemu (RAM/FLASH)
void drawModeTransition(); // UI przejścia trybu (WiFi ↔ Bluetooth)



// 7-seg
uint8_t swapNibbles(uint8_t v);
void slowShiftOut(uint8_t v);
void initSevenSeg();
void updateSevenSeg();
void updateSevenSegStoper(int mins, int secs, int centisec);


#endif // UI_DRAW_H