#include "UI_Controller.h"
#include <Arduino.h>
#include "AppState.h"

// Zmienne globalne z main.cpp
extern int menuIndex;
extern const int menuCount;
extern enum AppState appState;
extern enum EditState editState;

extern int alarmHour;
extern int alarmMinute;
extern bool alarmEnabled;
extern bool alarmRinging;
extern unsigned long alarmStartTime;
extern unsigned long lastMelodyStep;
extern int melodyStep;

extern bool stoperRunning;
extern unsigned long stoperStart;
extern unsigned long stoperElapsed;

extern int hours;
extern int minutes;
extern int seconds;
extern unsigned long lastTick;

// info nowe 
extern int statsMenuIndex;
extern int statsMenuCount;

// Funkcje z main.cpp
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

// potrzebne do DHT
extern int savedHours, savedMinutes, savedSeconds;
extern bool timeSaved;


static UI_Callbacks s_callbacks;

void ui_begin(const UI_Callbacks &callbacks) {
  s_callbacks = callbacks;
  if (s_callbacks.drawHome) s_callbacks.drawHome();
}

// Pomocnicza: zmiana czasu
static void adjustTime_internal(int dir) {
  if (editState == EDIT_HOURS) {
    hours = (hours + dir + 24) % 24;
  } else if (editState == EDIT_MINUTES) {
    minutes = (minutes + dir + 60) % 60;
  } else if (editState == EDIT_SECONDS) {
    seconds = (seconds + dir + 60) % 60;
  }
  if (s_callbacks.drawSetTime) s_callbacks.drawSetTime();
  if (s_callbacks.updateSevenSeg) s_callbacks.updateSevenSeg();
}

void ui_handleEvent(EncoderEvent e) {
  if (e == ENC_NONE) return;

  // ========================================================================
  // 1. OBRÓT ENKODERA (lewo/prawo)
  // ========================================================================
  if (e == ENC_LEFT || e == ENC_RIGHT) {
    int dir = (e == ENC_RIGHT) ? 1 : -1;

    if (appState == STATE_MENU) {
      menuIndex = constrain(menuIndex + dir, 0, menuCount - 1);
      if (s_callbacks.drawMenu) s_callbacks.drawMenu();
    }
    // --- Przewijanie MENU STATYSTYK ---
    else if (appState == STATE_STATS) {
      statsMenuIndex = constrain(statsMenuIndex + dir, 0, statsMenuCount - 1);
      if (s_callbacks.drawStats) s_callbacks.drawStats();
    }
    // ----------------------------------
    else if (appState == STATE_SET_TIME) {
      adjustTime_internal(dir);
    }
    else if (appState == STATE_ALARM) {
      if (editState == EDIT_HOURS) {
        alarmHour = (alarmHour + dir + 24) % 24;
      } else {
        alarmMinute = (alarmMinute + dir + 60) % 60;
      }
      if (s_callbacks.drawAlarm) s_callbacks.drawAlarm();
    }
    return;
  }

  // ========================================================================
  // 2. KRÓTKIE KLIKNIĘCIE (enter/select)
  // ========================================================================
  if (e == ENC_CLICK) {
    
    // --- HOME -> MENU ---
    if (appState == STATE_HOME) {
      appState = STATE_MENU;
      if (s_callbacks.drawMenu) s_callbacks.drawMenu();
      return;
    }

    // --- GŁÓWNE MENU (Wybór opcji) ---
    if (appState == STATE_MENU) {
       if (menuIndex == 0) {
         appState = STATE_SET_TIME;
         editState = EDIT_HOURS;
         if (s_callbacks.drawSetTime) s_callbacks.drawSetTime();
       }
       else if (menuIndex == 1) {
         appState = STATE_STOPER;
         stoperRunning = false;
         stoperElapsed = 0;
         if (s_callbacks.drawStoper) s_callbacks.drawStoper();
       }
       else if (menuIndex == 2) {
         appState = STATE_ALARM;
         editState = EDIT_HOURS;
         if (s_callbacks.drawAlarm) s_callbacks.drawAlarm();
       }
       else if (menuIndex == 3) {
         syncTimeFromWiFi();
         if (s_callbacks.updateSevenSeg) s_callbacks.updateSevenSeg();
         if (s_callbacks.drawMenu) s_callbacks.drawMenu();
       }
       else if (menuIndex == 4) {
           // Wejście w Menu Statystyk
           appState = STATE_STATS;
           statsMenuIndex = 0; // Reset na pierwszą pozycję
           if (s_callbacks.drawStats) s_callbacks.drawStats();
       }
       else if (menuIndex == 5) {
         appState = STATE_DEBUG_STM32;
         if (s_callbacks.updateSevenSeg) s_callbacks.updateSevenSeg();
         if (s_callbacks.drawDebugSTM32) s_callbacks.drawDebugSTM32();
       }
       else if (menuIndex == 6) {   // TEMPERATURA
       appState = STATE_TEMPERATURE;
       drawTemperature();
       showTemperature7Seg();
       }
       else if (menuIndex == 7) {   // WILGOTNOSC
       appState = STATE_HUMIDITY;
       drawHumidity();
       showHumidity7Seg();
       }

       else { // Wyjście
         appState = STATE_HOME;
         if (s_callbacks.drawHome) s_callbacks.drawHome();
       }
       return;
    }

    // --- LOGIKA MENU STATYSTYK ---
if (appState == STATE_STATS) {

    if (statsMenuIndex == 0) {
        // Kliki
        appState = STATE_STATS_CLICKS;
        if (s_callbacks.drawStats) s_callbacks.drawStats();
    }
    else if (statsMenuIndex == 1) {
        // Kroki
        appState = STATE_STATS_STEPS;
        if (s_callbacks.drawStats) s_callbacks.drawStats();
    }
    else if (statsMenuIndex == 2) {
        // Temp min/max
        appState = STATE_STATS_TEMP;
        if (s_callbacks.drawStats) s_callbacks.drawStats();
    }
    else if (statsMenuIndex == 3) {
        // Wilg min/max
        appState = STATE_STATS_HUM;
        if (s_callbacks.drawStats) s_callbacks.drawStats();
    }
    else if (statsMenuIndex == 4) {
        // Wyjscie -> MENU GŁÓWNE
        appState = STATE_MENU;
        if (s_callbacks.drawMenu) s_callbacks.drawMenu();
    }

    return;
}

    // --- LOGIKA POZOSTAŁYCH STANÓW ---
    
    if (appState == STATE_STOPER) {
      if (!stoperRunning) {
        stoperRunning = true;
        stoperStart = millis();
      } else {
        stoperRunning = false;
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
      editState = (EditState)(editState + 1);
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
      editState = (EditState)(editState + 1);
      if (editState > EDIT_MINUTES) {
        alarmEnabled = true;
        appState = STATE_HOME;
        if (s_callbacks.updateSevenSeg) s_callbacks.updateSevenSeg();
        if (s_callbacks.drawHome) s_callbacks.drawHome();
      } else {
        if (s_callbacks.drawAlarm) s_callbacks.drawAlarm();
      }
      return;
    }
    
    return;
  }

  // ========================================================================
  // 3. DŁUGIE KLIKNIĘCIE (back/escape)
  // ========================================================================
  if (e == ENC_LONG) {
    // =====================================================
// STATYSTYKI ENV -> powrót do MENU STATYSTYK
// =====================================================
if (appState == STATE_STATS_TEMP || appState == STATE_STATS_HUM) {
    appState = STATE_STATS;

    // ustaw kursor na odpowiedniej pozycji
    statsMenuIndex = (appState == STATE_STATS_TEMP) ? 2 : 3;

    if (s_callbacks.drawStats) s_callbacks.drawStats();
    return;
}

// =====================================================
// DHT (Temperatura / Wilgotność) -> powrót do MENU
// =====================================================
if (appState == STATE_TEMPERATURE || appState == STATE_HUMIDITY) {

    // przywróć czas na 7-seg
    if (timeSaved) {
        hours = savedHours;
        minutes = savedMinutes;
        seconds = savedSeconds;
        timeSaved = false;
    }

    appState = STATE_MENU;
    if (s_callbacks.drawMenu) s_callbacks.drawMenu();
    return;
}


    appState = STATE_MENU;
    if (s_callbacks.drawMenu) s_callbacks.drawMenu();
    return;
    }


  
    // Z głębokich statystyk -> do MENU STATYSTYK
    if (appState == STATE_STATS_CLICKS || appState == STATE_STATS_STEPS) {
        appState = STATE_STATS;
        if (s_callbacks.drawStats) s_callbacks.drawStats();
        return;
    }

    // Z MENU STATYSTYK -> do MENU GŁÓWNEGO
    if (appState == STATE_STATS) {
        appState = STATE_MENU;
        if (s_callbacks.drawMenu) s_callbacks.drawMenu();
        return;
    }

    // Inne wyjścia
    if (appState == STATE_STOPER || appState == STATE_DEBUG_STM32) {
      appState = STATE_MENU;
      if (s_callbacks.drawMenu) s_callbacks.drawMenu();
    }
    return;
  }

