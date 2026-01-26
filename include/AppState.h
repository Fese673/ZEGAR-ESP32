#pragma once

enum AppState {
  STATE_HOME,
  STATE_MENU,
  STATE_SET_TIME,
  STATE_ALARM,
  STATE_STOPER,
  STATE_DEBUG_STM32,
  STATE_STATS_RESOURCES, // stan do wyświetlania zasobów systemu
  STATE_WIFI_SYNC,
  STATE_TEMPERATURE,
  STATE_HUMIDITY,

  // --- Statystyki ---
  STATE_STATS,          // Menu statystyk (lista z >)
  STATE_STATS_CLICKS,   // Widok samych kliknięć
  STATE_STATS_STEPS,    // Widok kroków L/R
  STATE_STATS_TEMP,     // Widok min/max temperatury
  STATE_STATS_HUM,      // Widok min/max wilgotności

  // --- Aliasy kompatybilności (stare nazwy bez podkreśleń) ---
  STATEHOME           = STATE_HOME,
  STATEMENU           = STATE_MENU,
  STATESETTIME        = STATE_SET_TIME,
  STATEALARM          = STATE_ALARM,
  STATESTOPER         = STATE_STOPER,
  STATEDEBUGSTM32     = STATE_DEBUG_STM32,
  STATESTATSRESOURCES = STATE_STATS_RESOURCES,
  STATEWIFISYNC       = STATE_WIFI_SYNC,
  STATETEMPERATURE    = STATE_TEMPERATURE,
  STATEHUMIDITY       = STATE_HUMIDITY,

  STATESTATS        = STATE_STATS,
  STATESTATSCLICKS  = STATE_STATS_CLICKS,
  STATESTATSSTEPS   = STATE_STATS_STEPS,
  STATESTATSTEMP    = STATE_STATS_TEMP,
  STATESTATSHUM     = STATE_STATS_HUM
};

enum EditState {
  EDIT_HOURS,
  EDIT_MINUTES,
  EDIT_SECONDS,
  EDIT_DONE,

  // Aliasy kompatybilności
  EDITHOURS   = EDIT_HOURS,
  EDITMINUTES = EDIT_MINUTES,
  EDITSECONDS = EDIT_SECONDS,
  EDITDONE    = EDIT_DONE
};

enum RadioMode {
  WIFI_ONLY = 0, // WiFi ON, BT OFF
  BT_ONLY   = 1, // WiFi OFF, BT ON

  // Aliasy kompatybilności
  WIFIONLY = WIFI_ONLY,
  BTONLY   = BT_ONLY
};

// GLOBALNY STAN APLIKACJI
extern AppState appState;
extern EditState editState;
extern RadioMode radioMode;
