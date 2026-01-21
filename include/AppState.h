#pragma once

enum AppState {
  STATE_HOME,
  STATE_MENU,
  STATE_SET_TIME,
  STATE_ALARM,
  STATE_STOPER,
  STATE_DEBUG_STM32,
  STATE_WIFI_SYNC,
  
   // --- Statystyki ---
    STATE_STATS,          // Menu statystyk (lista z >)
    STATE_STATS_CLICKS,   // Nowy: Widok samych kliknięć
    STATE_STATS_STEPS     // (Dawniej DETAIL): Widok kroków L/R
};

enum EditState {
  EDIT_HOURS,
  EDIT_MINUTES,
  EDIT_SECONDS,
  EDIT_DONE
};

//  GLOBALNY STAN APLIKACJI
extern AppState appState;
extern EditState editState;
