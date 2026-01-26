#include "UI_Controller.h"
#include <Arduino.h>
#include "AppState.h"
#include "ModeManager.h"
#include "RadioModeSwitch.h"

// ============================================================================
// ZMIENNE GLOBALNE (extern z main.cpp)
// ============================================================================

// --- Menu ---
extern int menuIndex;
extern int menuCount;
extern enum AppState appState;
extern enum EditState editState;

// --- Budzik ---
extern int  alarmHour;
extern int  alarmMinute;
extern bool alarmEnabled;
extern bool alarmRinging;
extern unsigned long alarmStartTime;
extern unsigned long lastMelodyStep;
extern int  melodyStep;

// --- Stoper ---
extern bool stoperRunning;
extern unsigned long stoperStart;
extern unsigned long stoperElapsed;

// --- Czas ---
extern int hours;
extern int minutes;
extern int seconds;
extern unsigned long lastTick;

// --- Statystyki ---
extern int statsMenuIndex;
extern int statsMenuCount;

// ============================================================================
// FUNKCJE EXTERN (z main.cpp)
// ============================================================================
extern void syncTimeFromWiFi();
extern void updateSevenSeg();
extern void updateSevenSegStoper(int mins, int secs, int centisec);
extern void drawHome();
extern void drawMenu();
extern void drawSetTime();
extern void drawAlarm();
extern void drawStoper();
extern void drawDebugSTM32();
extern void drawStats();
extern void drawTemperature();
extern void drawHumidity();
extern void showTemperature7Seg();
extern void showHumidity7Seg();

// --- DHT (potrzebne do przywracania czasu) ---
extern int  savedHours, savedMinutes, savedSeconds;
extern bool timeSaved;

// ============================================================================
// UI CONTROLLER - IMPLEMENTACJA
// ============================================================================

static UI_Callbacks s_callbacks;

void ui_begin(const UI_Callbacks& callbacks) {
  s_callbacks = callbacks;
  if (s_callbacks.drawHome) {
    s_callbacks.drawHome();
  }
}

// Pomocnicza: zmiana czasu w trybie edycji
static void adjustTime_internal(int dir) {
  switch (editState) {
    case EDIT_HOURS:
      hours = (hours + dir + 24) % 24;
      break;
    case EDIT_MINUTES:
      minutes = (minutes + dir + 60) % 60;
      break;
    case EDIT_SECONDS:
      seconds = (seconds + dir + 60) % 60;
      break;
    default:
      break;
  }

  if (s_callbacks.drawSetTime) s_callbacks.drawSetTime();
  if (s_callbacks.updateSevenSeg) s_callbacks.updateSevenSeg();
}

void ui_handleEvent(EncoderEvent e) {
  if (e == ENC_NONE) return;

  // ==========================================================================
  // 1. OBRÓT ENKODERA (lewo/prawo)
  // ==========================================================================
  if (e == ENC_LEFT || e == ENC_RIGHT) {
    const int dir = (e == ENC_RIGHT) ? 1 : -1;

    switch (appState) {
      case STATE_MENU:
        menuIndex = constrain(menuIndex + dir, 0, menuCount - 1);
        if (s_callbacks.drawMenu) s_callbacks.drawMenu();
        break;

      case STATE_STATS:
        statsMenuIndex = constrain(statsMenuIndex + dir, 0, statsMenuCount - 1);
        if (s_callbacks.drawStats) s_callbacks.drawStats();
        break;

      case STATE_SET_TIME:
        adjustTime_internal(dir);
        break;

      case STATE_ALARM:
        if (editState == EDIT_HOURS) {
          alarmHour = (alarmHour + dir + 24) % 24;
        } else {
          alarmMinute = (alarmMinute + dir + 60) % 60;
        }
        if (s_callbacks.drawAlarm) s_callbacks.drawAlarm();
        break;

      case STATE_STATS_RESOURCES:
        if (s_callbacks.drawSystemResources) s_callbacks.drawSystemResources();
        break;

      default:
        break;
    }
    return;
  }

  // ==========================================================================
  // 2. KRÓTKIE KLIKNIĘCIE (enter/select)
  // ==========================================================================
  if (e == ENC_CLICK) {
    // --- HOME -> MENU ---
    if (appState == STATE_HOME) {
      appState = STATE_MENU;
      if (s_callbacks.drawMenu) s_callbacks.drawMenu();
      return;
    }

    // --- GŁÓWNE MENU (Wybór opcji) ---
    if (appState == STATE_MENU) {
      switch (menuIndex) {
        case 0:  // Ustaw czas
          appState  = STATE_SET_TIME;
          editState = EDIT_HOURS;
          if (s_callbacks.drawSetTime) s_callbacks.drawSetTime();
          return;

        case 1:  // Stoper
          appState      = STATE_STOPER;
          stoperRunning = false;
          stoperElapsed = 0;
          if (s_callbacks.drawStoper) s_callbacks.drawStoper();
          return;

        case 2:  // Budzik
          appState  = STATE_ALARM;
          editState = EDIT_HOURS;
          if (s_callbacks.drawAlarm) s_callbacks.drawAlarm();
          return;

        case 3:  // Czas z WiFi
          syncTimeFromWiFi();
          if (s_callbacks.updateSevenSeg) s_callbacks.updateSevenSeg();
          if (s_callbacks.drawMenu) s_callbacks.drawMenu();
          return;

        case 4:  // Statystyki
          appState       = STATE_STATS;
          statsMenuIndex = 0;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          return;

        case 5:  // Debug STM32
          appState = STATE_DEBUG_STM32;
          if (s_callbacks.updateSevenSeg) s_callbacks.updateSevenSeg();
          if (s_callbacks.drawDebugSTM32) s_callbacks.drawDebugSTM32();
          return;

        case 6:  // Temperatura
          appState = STATE_TEMPERATURE;
          drawTemperature();
          showTemperature7Seg();
          return;

        case 7:  // Wilgotność
          appState = STATE_HUMIDITY;
          drawHumidity();
          showHumidity7Seg();
          return;

        case 8:  // Wyjście
          appState = STATE_HOME;
          if (s_callbacks.updateSevenSeg) s_callbacks.updateSevenSeg();
          if (s_callbacks.drawHome) s_callbacks.drawHome();
          return;

        case 9:  // Radio Toggle (WiFi ↔ Bluetooth)
          // Przełącz na inny tryb z resetem - BEZ żadnych operacji LCD!
          if (radioMode == WIFI_ONLY) {
            // Przejdź na Bluetooth
            radioMode = BT_ONLY;
            // Od razu restart - nie rysuj nic na LCD
            RadioModeSwitch::requestModeSwitch_BT();
          } else {
            // Przejdź na WiFi
            radioMode = WIFI_ONLY;
            // Od razu restart - nie rysuj nic na LCD
            RadioModeSwitch::requestModeSwitch_WiFi();
          }
          return;

        default:
          break;
      }
    }

    // --- LOGIKA MENU STATYSTYK ---
    if (appState == STATE_STATS) {
      switch (statsMenuIndex) {
        case 0:
          appState = STATE_STATS_CLICKS;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          break;
        case 1:
          appState = STATE_STATS_STEPS;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          break;
        case 2:
          appState = STATE_STATS_TEMP;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          break;
        case 3:
          appState = STATE_STATS_HUM;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          break;
        case 4:  // Zasoby
          appState = STATE_STATS_RESOURCES;
          if (s_callbacks.drawSystemResources) s_callbacks.drawSystemResources();
          break;
        case 5:  // Wyjście
          appState = STATE_MENU;
          if (s_callbacks.drawMenu) s_callbacks.drawMenu();
          break;
        default:
          break;
      }
      return;
    }

    // --- LOGIKA POZOSTAŁYCH STANÓW ---

    if (appState == STATE_STOPER) {
      if (!stoperRunning) {
        stoperRunning = true;
        stoperStart   = millis();
      } else {
        stoperRunning  = false;
        stoperElapsed += millis() - stoperStart;
      }
      if (s_callbacks.drawStoper) s_callbacks.drawStoper();
      return;
    }

    if (appState == STATE_DEBUG_STM32) {
      appState = STATE_HOME;
      if (s_callbacks.updateSevenSeg) s_callbacks.updateSevenSeg();
      if (s_callbacks.drawHome) s_callbacks.drawHome();
      return;
    }

    if (appState == STATE_SET_TIME) {
      editState = static_cast<EditState>(editState + 1);
      if (editState == EDIT_DONE) {
        lastTick = millis();
        appState = STATE_HOME;
        if (s_callbacks.updateSevenSeg) s_callbacks.updateSevenSeg();
        if (s_callbacks.drawHome) s_callbacks.drawHome();
      } else {
        if (s_callbacks.drawSetTime) s_callbacks.drawSetTime();
      }
      return;
    }

    if (appState == STATE_ALARM) {
      editState = static_cast<EditState>(editState + 1);
      if (editState > EDIT_MINUTES) {
        alarmEnabled = true;
        appState     = STATE_HOME;
        if (s_callbacks.updateSevenSeg) s_callbacks.updateSevenSeg();
        if (s_callbacks.drawHome) s_callbacks.drawHome();
      } else {
        if (s_callbacks.drawAlarm) s_callbacks.drawAlarm();
      }
      return;
    }

    return;
  } // koniec: if (e == ENC_CLICK)

  // ==========================================================================
  // 3. DŁUGIE KLIKNIĘCIE (back/escape)
  // ==========================================================================
  if (e == ENC_LONG) {
    // Pomocnicza funkcja do powrotu do menu statystyk
    auto returnToStatsMenu = [](int menuIdx) {
      appState       = STATE_STATS;
      statsMenuIndex = menuIdx;
      if (s_callbacks.drawStats) s_callbacks.drawStats();
    };

    // Statystyki: ekrany szczegółowe -> powrót do menu statystyk
    switch (appState) {
      case STATE_STATS_CLICKS:
        returnToStatsMenu(0);
        return;
      case STATE_STATS_STEPS:
        returnToStatsMenu(1);
        return;
      case STATE_STATS_TEMP:
        returnToStatsMenu(2);
        return;
      case STATE_STATS_HUM:
        returnToStatsMenu(3);
        return;
      case STATE_STATS_RESOURCES:
        returnToStatsMenu(4);
        return;

      case STATE_STATS:
        // Menu statystyk -> menu główne
        appState = STATE_MENU;
        if (s_callbacks.drawMenu) s_callbacks.drawMenu();
        return;

      case STATE_TEMPERATURE:
      case STATE_HUMIDITY:
        // DHT (temp/wilg) -> powrót do MENU + przywrócenie czasu
        if (timeSaved) {
          hours     = savedHours;
          minutes   = savedMinutes;
          seconds   = savedSeconds;
          timeSaved = false;
        }
        appState = STATE_MENU;
        if (s_callbacks.drawMenu) s_callbacks.drawMenu();
        return;

      case STATE_STOPER:
      case STATE_DEBUG_STM32:
        // Inne wyjścia -> MENU
        appState = STATE_MENU;
        if (s_callbacks.drawMenu) s_callbacks.drawMenu();
        return;

      default:
        // Fallback (cokolwiek innego) -> MENU
        appState = STATE_MENU;
        if (s_callbacks.drawMenu) s_callbacks.drawMenu();
        return;
    }
  }
} // koniec: ui_handleEvent(...)