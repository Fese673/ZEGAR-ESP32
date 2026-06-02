#ifndef UI_DRAW_H
#define UI_DRAW_H


#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include "LCDMirror.h"
#include "AppState.h"
#include "Board_Pins.h"


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


extern int displayedBPM;
extern int displayedSPO2;
extern bool stm32Connected;
extern uint8_t heapUsagePercent;
extern uint8_t heapUsageCore0Percent;
extern uint8_t heapUsageCore1Percent;
extern uint32_t ramFreeBytes;
extern uint32_t ramTotalBytes;
extern uint32_t ramLargestBlockBytes;
extern uint32_t ramMinFreeBytes;
extern uint32_t ramDmaFreeBytes;
extern uint32_t flashFreeBytes;

// Minutnik (Timer) - ustawienia i status
extern int timerSetMinutes;
extern int timerSetSeconds;
extern bool timerRunning;

extern int timerSetHours;
extern unsigned long timerStartMillis;
extern unsigned long timerDurationMs;
extern int timerUiCursor;   // 0=CZAS, 1=PRESETY
extern int timerPresetIndex; // 0=2m, 1=15m, 2=45m

// Stoper (Stopwatch) — via StopwatchService
#include "StopwatchService.h"


// LCD object (your main must define it, e.g. LiquidCrystal_I2C lcd(...))
extern LiquidCrystal_I2C lcd;


// --- Prototypes (these are the functions moved from main) ---


// UI / LCD
void drawHome();
void invalidateHomeRenderCache();
void requestUiFullRedraw();
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
void drawBtMusicControl(); // UI sterowania muzyką BT



// 7-seg
uint8_t swapNibbles(uint8_t v);
void slowShiftOut(uint8_t v);
void initSevenSeg();
void updateSevenSeg();
void updateSevenSegStoper(int mins, int secs, int centisec);


#endif // UI_DRAW_H