#include "AppState.h"
AppState appState = STATE_HOME;
EditState editState = EDIT_HOURS;
std::atomic<RadioMode> radioMode{WIFI_ONLY};
