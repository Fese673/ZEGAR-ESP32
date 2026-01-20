#include "UI_Controller.h"
#include <Arduino.h>
#include "AppState.h"

// Zmienne globalne z main.cpp (stan aplikacji)
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

static UI_Callbacks s_callbacks;

void ui_begin(const UI_Callbacks &callbacks) {
  s_callbacks = callbacks;
  if (s_callbacks.drawHome) s_callbacks.drawHome();
}

// Pomocnicza: zmiana czasu (godzina/minuta/sekunda)
// dir: +1 do góry, -1 do dołu
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

  // OBRÓT ENKODERA (lewo/prawo)
  if (e == ENC_LEFT || e == ENC_RIGHT) {
    int dir = (e == ENC_RIGHT) ? 1 : -1;

    if (appState == STATE_MENU) {
      // Przewijanie menu górą/dołem
      menuIndex = constrain(menuIndex + dir, 0, menuCount - 1);
      if (s_callbacks.drawMenu) s_callbacks.drawMenu();
    }
    else if (appState == STATE_SET_TIME) {
      // Edycja czasu (hours/minutes/seconds)
      adjustTime_internal(dir);
    }
    else if (appState == STATE_ALARM) {
      // Edycja alarmu (godzina/minuta)
      if (editState == EDIT_HOURS) {
        alarmHour = (alarmHour + dir + 24) % 24;
      } else {
        alarmMinute = (alarmMinute + dir + 60) % 60;
      }
      if (s_callbacks.drawAlarm) s_callbacks.drawAlarm();
    }
    return;
  }

  // KRÓTKIE KLIKNIĘCIE (enter/select)
  if (e == ENC_CLICK) {
    if (appState == STATE_HOME) {
      // Wejście do menu
      appState = STATE_MENU;
      if (s_callbacks.drawMenu) s_callbacks.drawMenu();
      return;
    }

    if (appState == STATE_MENU) {
      // Wybór opcji z menu
      if (menuIndex == 0) {
        appState = STATE_SET_TIME;
        editState = EDIT_HOURS;
        if (s_callbacks.drawSetTime) s_callbacks.drawSetTime();
      }
      else if (menuIndex == 1) {
        // Stoper: start z zerowym czasem
        appState = STATE_STOPER;
        stoperRunning = false;
        stoperElapsed = 0;
        if (s_callbacks.drawStoper) s_callbacks.drawStoper();
      }
      else if (menuIndex == 2) {
        // Alarm: edycja
        appState = STATE_ALARM;
        editState = EDIT_HOURS;
        if (s_callbacks.drawAlarm) s_callbacks.drawAlarm();
      }
      else if (menuIndex == 3) {
        // Sync: synchronizacja czasu z WiFi
        syncTimeFromWiFi();
        if (s_callbacks.updateSevenSeg) s_callbacks.updateSevenSeg();
        if (s_callbacks.drawMenu) s_callbacks.drawMenu();
      }
      else if (menuIndex == 4) {
        // Debug STM32
        appState = STATE_DEBUG_STM32;
        if (s_callbacks.updateSevenSeg) s_callbacks.updateSevenSeg();
        if (s_callbacks.drawDebugSTM32) s_callbacks.drawDebugSTM32();
      }
      else {
        appState = STATE_HOME;
        if (s_callbacks.drawHome) s_callbacks.drawHome();
      }
      return;
    }

    if (appState == STATE_STOPER) {
      // Play/Pause stopera
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
      // Wyjście z debug
      appState = STATE_HOME;
      if (s_callbacks.updateSevenSeg) s_callbacks.updateSevenSeg();
      if (s_callbacks.drawHome) s_callbacks.drawHome();
      return;
    }

    if (appState == STATE_SET_TIME) {
      // Następny krok edycji (hours → minutes → seconds → done)
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
      // Następny krok edycji alarmu (hours → minutes → enable i powrót)
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

  // DŁUGIE KLIKNIĘCIE (back/escape)
  if (e == ENC_LONG) {
    if (appState == STATE_STOPER || appState == STATE_DEBUG_STM32) {
      // Powrót do menu z stopera/debug
      appState = STATE_MENU;
      if (s_callbacks.updateSevenSeg) s_callbacks.updateSevenSeg();
      if (s_callbacks.drawMenu) s_callbacks.drawMenu();
    }
    // Można dodać więcej akcji dla długiego przycisku w innych stanach
    return;
  }
}

void ui_tick() {
  // Periodyczne zadania UI (animacje, migania, itp)
  // Pole do rozwoju - na razie pusty, logika główna w main.cpp
}