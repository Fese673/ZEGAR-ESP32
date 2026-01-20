#pragma once

enum AppState {
  STATE_HOME,
  STATE_MENU,
  STATE_SET_TIME,
  STATE_ALARM,
  STATE_STOPER,
  STATE_DEBUG_STM32
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
