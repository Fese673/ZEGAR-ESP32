#include "UI_Controller.h"
#include <Arduino.h>
#include "AppState.h"
#include "ModeManager.h"
#include "RadioModeSwitch.h"
#include "PMS_Czujnik.h"
#include "ENS160AHT21Screen.h"
#include "BMP280Sensor.h"
#include "AlarmMelodies.h"
#include "AlarmMelodyPrefs.h"
#include "AlarmTypes.h"
#include <Preferences.h>
#include "HomeRuntime.h"
#include "WiFiSync.h"

int pms5003MenuIndex = 0;
const char* pms5003MenuItems[] = {
  "Tryb Fabryczny",
  "Tryb Atmosferyczny",
  "L.Czastek #/100cm3",
  "Telemetria",
};
constexpr int kPms5003MenuCount = 4;
int pms5003MenuCount = kPms5003MenuCount;

int pms5003CF1MenuIndex = 0;
const char* pms5003CF1MenuItems[] = {
  "PM1.0",
  "PM2.5",
  "PM10",
};
constexpr int kPms5003Cf1MenuCount = 3;
int pms5003CF1MenuCount = kPms5003Cf1MenuCount;

int pms5003ATMMenuIndex = 0;
const char* pms5003ATMMenuItems[] = {
  "PM1.0",
  "PM2.5",
  "PM10",
};
constexpr int kPms5003AtmMenuCount = 3;
int pms5003ATMMenuCount = kPms5003AtmMenuCount;

int pms5003ParticlesMenuIndex = 0;
const char* pms5003ParticlesMenuItems[] = {
  "0.3um",
  "0.5um",
  "1.0um",
  "2.5um",
  "5.0um",
  "10.0um",
};
constexpr int kPms5003ParticlesMenuCount = 6;
int pms5003ParticlesMenuCount = kPms5003ParticlesMenuCount;

int settingsPmsMenuIndex = 0;
constexpr int kSettingsPmsMenuCount = 2;
int settingsPmsMenuCount = kSettingsPmsMenuCount;

bool pmsScreenDirty = true;

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
extern int alarmEditCursor;

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

// --- Zasoby ---
extern int resourcesMenuIndex;
extern int resourcesMenuCount;

// --- PMS5003 ---
extern int ens160MenuIndex;
extern int ens160MenuCount;
extern int bmp280MenuIndex;
extern int bmp280MenuCount;

// --- Minutnik (Timer) externs
extern int timerSetMinutes;
extern int timerSetSeconds;
extern bool timerRunning;
extern int timerSetHours;
extern unsigned long timerStartMillis;
extern unsigned long timerDurationMs;
extern int timerUiCursor;
extern int timerPresetIndex;

// --- Multi-Alarm shared state ---
extern const int MAX_ALARMS;
extern AlarmEntry alarms[];
extern int alarmsCount;
extern int alarmsMenuIndex;
extern int selectedAlarmIndex;

// --- Ustawienia (Settings) ---
extern int settingsMenuIndex;
extern int settingsMenuCount;
extern int settingsMqttMenuIndex;
extern int settingsMqttMenuCount;
extern int settingsBuzzerMenuIndex;
extern int settingsBuzzerMenuCount;
extern int settingsAlarmMelodyIndex;
extern int s_prevSettingsAlarmMelodyIndex;
extern int settingsEpicIntroIndex;
extern int settingsEpicIntroMenuCount;
extern bool buzzerEnabled;
extern bool mqttEnabled;
extern bool showEpicIntro;
extern int settingsRotationSec;
extern int s_prevSettingsRotationSec;
extern int settingsSyncMinutes;
extern int s_prevSettingsSyncMin;
extern int settingsUiScreenIndex;
extern int s_prevSettingsUiScreenIndex;
extern int settingsUiScreenCount;
extern Preferences s_prefs;

// ============================================================================
// FUNKCJE EXTERN (z main.cpp)
// ============================================================================
extern void startAlarmMelodyDemo(uint8_t melodyIndex);
extern void stopAlarmMelodyDemo();

static bool getPms5003Enabled() {
  return PMS5003Sensor::isEnabled();
}

static void setPms5003Enabled(bool enabled) {
  PMS5003Sensor::setEnabled(enabled);
  pmsScreenDirty = true;
}

static bool getBuzzerEnabled() {
  return buzzerEnabled;
}

static void setBuzzerEnabled(bool enabled) {
  buzzerEnabled = enabled;
}

static bool getMqttEnabled() {
  return mqttEnabled;
}

static void setMqttEnabled(bool enabled) {
  mqttEnabled = enabled;
}

static bool getShowEpicIntro() {
  return showEpicIntro;
}

static void setShowEpicIntro(bool enabled) {
  showEpicIntro = enabled;
}

// ============================================================================
// UI CONTROLLER - IMPLEMENTACJA
// ============================================================================

static UI_Callbacks s_callbacks;
static AppState s_alarmReturnState = STATE_MENU;

static inline void callDraw(DrawFn fn) {
  if (fn) {
    fn();
  }
}

static inline void drawHomeSafe() { callDraw(s_callbacks.drawHome); }
static inline void drawMenuSafe() { callDraw(s_callbacks.drawMenu); }
static inline void drawSetTimeSafe() { callDraw(s_callbacks.drawSetTime); }
static inline void drawAlarmSafe() { callDraw(s_callbacks.drawAlarm); }
static inline void drawTimerSafe() { callDraw(s_callbacks.drawTimer); }
static inline void drawStoperSafe() { callDraw(s_callbacks.drawStoper); }
static inline void drawDebugSTM32Safe() { callDraw(s_callbacks.drawDebugSTM32); }
static inline void drawStatsSafe() { callDraw(s_callbacks.drawStats); }
static inline void updateSevenSegSafe() { callDraw(s_callbacks.updateSevenSeg); }
static inline void setHomeUiProfileSafe(uint8_t profileIndex) {
  if (s_callbacks.setHomeUiProfile) {
    s_callbacks.setHomeUiProfile(profileIndex);
  }
}

static inline void markPmsDirtyAndDrawStats() {
  pmsScreenDirty = true;
  drawStatsSafe();
}

static inline void markBmp280DirtyAndDrawStats() {
  BMP280Screen::markScreenDirty();
  drawStatsSafe();
}

static void persistAlarmAt(int idx) {
  char keyH[12];
  char keyM[12];
  char keyE[12];
  snprintf(keyH, sizeof(keyH), "a%dh", idx);
  snprintf(keyM, sizeof(keyM), "a%dm", idx);
  snprintf(keyE, sizeof(keyE), "a%de", idx);
  s_prefs.putUShort(keyH, (uint16_t)alarms[idx].hour);
  s_prefs.putUShort(keyM, (uint16_t)alarms[idx].minute);
  s_prefs.putBool(keyE, alarms[idx].enabled);
}

static void persistAllAlarms() {
  s_prefs.putUShort("alarmCount", (uint16_t)alarmsCount);
  for (int k = 0; k < alarmsCount; ++k) {
    persistAlarmAt(k);
  }
}

static void removeAlarmAt(int index) {
  if (alarmsCount <= 0) {
    return;
  }
  if (index < 0 || index >= alarmsCount) {
    return;
  }
  for (int j = index; j < alarmsCount - 1; ++j) {
    alarms[j] = alarms[j + 1];
  }
  if (alarmsCount > 0) {
    alarmsCount--;
  }
  persistAllAlarms();
}

struct ToggleSettingBinding {
  AppState state;
  int* menuIndex;
  int* menuCount;
  void (*setValue)(bool);
  const char* prefKey;
};

static int clampMenuIndexSafe(int index, int count) {
  if (count <= 0) {
    return 0;
  }
  return constrain(index, 0, count - 1);
}

static ToggleSettingBinding* findToggleSettingBinding(AppState state) {
  static ToggleSettingBinding kBindings[] = {
      {STATE_SETTINGS_PMS5003, &settingsPmsMenuIndex, &settingsPmsMenuCount, &setPms5003Enabled, nullptr},
      {STATE_SETTINGS_BUZZER, &settingsBuzzerMenuIndex, &settingsBuzzerMenuCount, &setBuzzerEnabled, nullptr},
      {STATE_SETTINGS_MQTT, &settingsMqttMenuIndex, &settingsMqttMenuCount, &setMqttEnabled, "mqttEnabled"},
      {STATE_SETTINGS_BOOT_INTRO, &settingsEpicIntroIndex, &settingsEpicIntroMenuCount, &setShowEpicIntro, "epicIntro"},
  };

  for (size_t i = 0; i < (sizeof(kBindings) / sizeof(kBindings[0])); ++i) {
    if (kBindings[i].state == state) {
      return &kBindings[i];
    }
  }

  return nullptr;
}

static void returnToSettingsMenu() {
  appState = STATE_SETTINGS;
  drawStatsSafe();
}

static bool ensureSelectedAlarmIndexValid() {
  if (selectedAlarmIndex >= 0 && selectedAlarmIndex < alarmsCount) {
    return true;
  }

  if (alarmsCount <= 0) {
    selectedAlarmIndex = 0;
    alarmsMenuIndex = 0;
  } else {
    selectedAlarmIndex = constrain(selectedAlarmIndex, 0, alarmsCount - 1);
    alarmsMenuIndex = constrain(alarmsMenuIndex, 0, alarmsCount);
  }

  appState = STATE_ALARMS_LIST;
  drawStatsSafe();
  return false;
}

static bool handleSettingsRotate(int dir) {
  if (appState == STATE_SETTINGS_ROTATION) {
    settingsRotationSec = constrain(settingsRotationSec + dir, 1, 10);
    HomeRuntime::setOverlayIntervalSeconds((uint8_t)settingsRotationSec);
    drawStatsSafe();
    return true;
  }

  if (appState == STATE_SETTINGS_UI_SCREEN) {
    settingsUiScreenIndex = clampMenuIndexSafe(settingsUiScreenIndex + dir, settingsUiScreenCount);
    if (settingsUiScreenCount > 0) {
      setHomeUiProfileSafe((uint8_t)settingsUiScreenIndex);
    }
    drawStatsSafe();
    return true;
  }

  if (appState == STATE_SETTINGS_SYNC) {
    settingsSyncMinutes = constrain(settingsSyncMinutes + dir * 10, 10, 360);
    WiFiSync::setPeriodicSyncIntervalMinutes((uint16_t)settingsSyncMinutes);
    drawStatsSafe();
    return true;
  }

  if (appState == STATE_SETTINGS_ALARM_MELODY) {
    settingsAlarmMelodyIndex = clampMenuIndexSafe(settingsAlarmMelodyIndex + dir, AlarmMelodies::kCount);
    drawStatsSafe();
    return true;
  }

  ToggleSettingBinding* toggle = findToggleSettingBinding(appState);
  if (toggle != nullptr) {
    const int count = (toggle->menuCount != nullptr) ? *toggle->menuCount : 0;
    *toggle->menuIndex = clampMenuIndexSafe(*toggle->menuIndex + dir, count);
    drawStatsSafe();
    return true;
  }

  return false;
}

static bool handleSettingsConfirmClick() {
  if (appState == STATE_SETTINGS_ALARM_MELODY) {
    settingsAlarmMelodyIndex = clampMenuIndexSafe(settingsAlarmMelodyIndex, AlarmMelodies::kCount);
    AlarmMelodyPrefs::saveSelection(s_prefs, settingsAlarmMelodyIndex);
    drawStatsSafe();
    if (AlarmMelodies::kCount > 0) {
      startAlarmMelodyDemo((uint8_t)settingsAlarmMelodyIndex);
    }
    return true;
  }

  if (appState == STATE_SETTINGS_ROTATION) {
    s_prefs.putUShort("homeOverlaySec", (uint16_t)settingsRotationSec);
    returnToSettingsMenu();
    return true;
  }

  if (appState == STATE_SETTINGS_SYNC) {
    s_prefs.putUShort("ntpSyncMin", (uint16_t)settingsSyncMinutes);
    WiFiSync::setPeriodicSyncIntervalMinutes((uint16_t)settingsSyncMinutes);
    returnToSettingsMenu();
    return true;
  }

  if (appState == STATE_SETTINGS_UI_SCREEN) {
    settingsUiScreenIndex = clampMenuIndexSafe(settingsUiScreenIndex, settingsUiScreenCount);
    s_prefs.putUShort("uiScreenMode", (uint16_t)settingsUiScreenIndex);
    returnToSettingsMenu();
    return true;
  }

  ToggleSettingBinding* toggle = findToggleSettingBinding(appState);
  if (toggle != nullptr) {
    const bool value = (*toggle->menuIndex == 0);
    if (toggle->setValue != nullptr) {
      toggle->setValue(value);
    }
    if (toggle->prefKey != nullptr) {
      s_prefs.putBool(toggle->prefKey, value);
    }
    returnToSettingsMenu();
    return true;
  }

  return false;
}

static bool handleSettingsLongCancel() {
  if (appState == STATE_SETTINGS_ROTATION) {
    settingsRotationSec = s_prevSettingsRotationSec;
    HomeRuntime::setOverlayIntervalSeconds((uint8_t)settingsRotationSec);
    returnToSettingsMenu();
    return true;
  }

  if (appState == STATE_SETTINGS_SYNC) {
    settingsSyncMinutes = s_prevSettingsSyncMin;
    WiFiSync::setPeriodicSyncIntervalMinutes((uint16_t)settingsSyncMinutes);
    returnToSettingsMenu();
    return true;
  }

  if (appState == STATE_SETTINGS_UI_SCREEN) {
    settingsUiScreenIndex = clampMenuIndexSafe(s_prevSettingsUiScreenIndex, settingsUiScreenCount);
    if (settingsUiScreenCount > 0) {
      setHomeUiProfileSafe((uint8_t)settingsUiScreenIndex);
    }
    returnToSettingsMenu();
    return true;
  }

  if (appState == STATE_SETTINGS_ALARM_MELODY) {
    settingsAlarmMelodyIndex = clampMenuIndexSafe(s_prevSettingsAlarmMelodyIndex, AlarmMelodies::kCount);
    stopAlarmMelodyDemo();
    returnToSettingsMenu();
    return true;
  }

  if (findToggleSettingBinding(appState) != nullptr) {
    returnToSettingsMenu();
    return true;
  }

  return false;
}

static void rotateMenuIndexByDir(int& index, int count, int dir) {
  index = clampMenuIndexSafe(index + dir, count);
}

static void requestPmsReadAndDraw() {
  PMS5003Sensor::requestImmediateRead();
  markPmsDirtyAndDrawStats();
}

static bool handleMainMenuClick() {
  if (appState != STATE_MENU) {
    return false;
  }

  switch (menuIndex) {
    case 0:  // Ustaw czas
      appState  = STATE_SET_TIME;
      editState = EDIT_HOURS;
      drawSetTimeSafe();
      return true;

    case 1:  // Minutnik
      appState  = STATE_TIMER;
      editState = EDIT_DONE;
      timerUiCursor = 0;
      timerPresetIndex = 1;
      drawTimerSafe();
      return true;

    case 2:  // Stoper
      appState      = STATE_STOPER;
      stoperRunning = false;
      stoperElapsed = 0;
      drawStoperSafe();
      return true;

    case 3:  // Budzik (lista budzików)
      s_alarmReturnState = STATE_MENU;
      appState = STATE_ALARMS_LIST;
      if (alarmsMenuIndex < 0) alarmsMenuIndex = 0;
      if (alarmsMenuIndex > alarmsCount) alarmsMenuIndex = alarmsCount;
      drawStatsSafe();
      return true;

    case 4:  // Statystyki
      appState       = STATE_STATS;
      statsMenuIndex = 0;
      drawStatsSafe();
      return true;

    case 5:  // Debug STM32
      appState = STATE_DEBUG_STM32;
      updateSevenSegSafe();
      drawDebugSTM32Safe();
      return true;

    case 6:  // PMS5003
      appState       = STATE_PMS5003;
      pms5003MenuIndex = 0;
      requestPmsReadAndDraw();
      return true;

    case 7:  // AHT21 + ENS160
      appState = STATE_ENS160_AHT21;
      ens160MenuIndex = 0;
      ENS160AHT21Screen::markScreenDirty();
      drawStatsSafe();
      return true;

    case 8:  // BMP280
      appState = STATE_BMP280;
      bmp280MenuIndex = 0;
      BMP280Screen::markScreenDirty();
      drawStatsSafe();
      return true;

    case 9:  // Ustawienia
      appState        = STATE_SETTINGS;
      settingsMenuIndex = 0;
      drawStatsSafe();
      return true;

    case 10:  // Wyjście
      appState = STATE_HOME;
      updateSevenSegSafe();
      drawHomeSafe();
      return true;

    case 11:  // Radio Mode (WiFi/Bluetooth)
      if (RadioModeSwitch::getCurrentState() == RADIO_STATE_TRANSITIONING) {
        return true;
      }

      if (RadioModeSwitch::getCurrentState() == RADIO_STATE_WIFI) {
        radioMode = BT_ONLY;
        RadioModeSwitch::requestModeSwitch_BT();
      } else {
        radioMode = WIFI_ONLY;
        RadioModeSwitch::requestModeSwitch_WiFi();
      }
      return true;

    default:
      return true;
  }
}

static bool handleStatsMenuClick() {
  if (appState != STATE_STATS) {
    return false;
  }

  switch (statsMenuIndex) {
    case 0:
      appState = STATE_STATS_CLICKS;
      drawStatsSafe();
      break;
    case 1:
      appState = STATE_STATS_STEPS;
      drawStatsSafe();
      break;
    case 2:
      appState = STATE_STATS_TEMP;
      drawStatsSafe();
      break;
    case 3:
      appState = STATE_STATS_HUM;
      drawStatsSafe();
      break;
    case 4:
      appState = STATE_STATS_RESOURCES_MENU;
      resourcesMenuIndex = 0;
      drawStatsSafe();
      break;
    case 5:
      appState = STATE_MENU;
      drawMenuSafe();
      break;
    default:
      break;
  }

  return true;
}

static bool handleResourcesMenuClick() {
  if (appState != STATE_STATS_RESOURCES_MENU) {
    return false;
  }

  switch (resourcesMenuIndex) {
    case 0:
      appState = STATE_STATS_RESOURCES_RAM;
      drawStatsSafe();
      break;
    case 1:
      appState = STATE_STATS_RESOURCES_CPU;
      drawStatsSafe();
      break;
    case 2:
      appState = STATE_STATS_RESOURCES_FLASH;
      drawStatsSafe();
      break;
    default:
      break;
  }

  return true;
}

static bool handlePmsMenuClick() {
  if (appState != STATE_PMS5003) {
    return false;
  }

  switch (pms5003MenuIndex) {
    case 0:
      appState = STATE_PMS5003_CF1;
      pms5003CF1MenuIndex = 0;
      requestPmsReadAndDraw();
      break;
    case 1:
      appState = STATE_PMS5003_ATM;
      pms5003ATMMenuIndex = 0;
      requestPmsReadAndDraw();
      break;
    case 2:
      appState = STATE_PMS5003_PARTICLES;
      pms5003ParticlesMenuIndex = 0;
      requestPmsReadAndDraw();
      break;
    case 3:
      appState = STATE_PMS5003_TELEMETRY;
      requestPmsReadAndDraw();
      break;
    case 5:
      appState = STATE_MENU;
      drawMenuSafe();
      break;
    default:
      break;
  }

  return true;
}

static bool handlePmsCf1MenuClick() {
  if (appState != STATE_PMS5003_CF1) {
    return false;
  }

  bool changed = false;

  switch (pms5003CF1MenuIndex) {
    case 0:
      appState = STATE_PMS5003_CF1_PM1;
      changed = true;
      break;
    case 1:
      appState = STATE_PMS5003_CF1_PM25;
      changed = true;
      break;
    case 2:
      appState = STATE_PMS5003_CF1_PM10;
      changed = true;
      break;
    default:
      break;
  }

  if (changed) {
    markPmsDirtyAndDrawStats();
  }
  return true;
}

static bool handlePmsAtmMenuClick() {
  if (appState != STATE_PMS5003_ATM) {
    return false;
  }

  bool changed = false;

  switch (pms5003ATMMenuIndex) {
    case 0:
      appState = STATE_PMS5003_ATM_PM1;
      changed = true;
      break;
    case 1:
      appState = STATE_PMS5003_ATM_PM25;
      changed = true;
      break;
    case 2:
      appState = STATE_PMS5003_ATM_PM10;
      changed = true;
      break;
    default:
      break;
  }

  if (changed) {
    markPmsDirtyAndDrawStats();
  }
  return true;
}

static bool handlePmsParticlesMenuClick() {
  if (appState != STATE_PMS5003_PARTICLES) {
    return false;
  }

  bool changed = false;

  switch (pms5003ParticlesMenuIndex) {
    case 0:
      appState = STATE_PMS5003_PARTICLES_0_3;
      changed = true;
      break;
    case 1:
      appState = STATE_PMS5003_PARTICLES_0_5;
      changed = true;
      break;
    case 2:
      appState = STATE_PMS5003_PARTICLES_1_0;
      changed = true;
      break;
    case 3:
      appState = STATE_PMS5003_PARTICLES_2_5;
      changed = true;
      break;
    case 4:
      appState = STATE_PMS5003_PARTICLES_5_0;
      changed = true;
      break;
    case 5:
      appState = STATE_PMS5003_PARTICLES_10_0;
      changed = true;
      break;
    default:
      break;
  }

  if (changed) {
    markPmsDirtyAndDrawStats();
  }
  return true;
}

static bool handleEnsMenuClick() {
  if (appState != STATE_ENS160_AHT21) {
    return false;
  }

  bool changed = false;

  switch (ens160MenuIndex) {
    case 0:
      appState = STATE_ENS160_AHT21_GAS_AQI;
      changed = true;
      break;
    case 1:
      appState = STATE_ENS160_AHT21_GAS_TVOC;
      changed = true;
      break;
    case 2:
      appState = STATE_ENS160_AHT21_GAS_ECO2;
      changed = true;
      break;
    case 3:
      appState = STATE_ENS160_AHT21_CLIMATE_TEMP;
      changed = true;
      break;
    case 4:
      appState = STATE_ENS160_AHT21_CLIMATE_HUM;
      changed = true;
      break;
    case 5:
      appState = STATE_ENS160_AHT21_STATUS;
      changed = true;
      break;
    default:
      break;
  }

  if (changed) {
    ENS160AHT21Screen::markScreenDirty();
    drawStatsSafe();
  }
  return true;
}

static bool handleBmp280MenuClick() {
  if (appState != STATE_BMP280) {
    return false;
  }

  bool changed = false;

  switch (bmp280MenuIndex) {
    case 0:
      appState = STATE_BMP280_TEMP;
      changed = true;
      break;
    case 1:
      appState = STATE_BMP280_PRESSURE;
      changed = true;
      break;
    case 2:
      appState = STATE_BMP280_STATUS;
      changed = true;
      break;
    case 3:
      appState = STATE_BMP280_ALTITUDE;
      changed = true;
      break;
    default:
      break;
  }

  if (changed) {
    markBmp280DirtyAndDrawStats();
  }
  return true;
}

static bool handleSettingsMenuClick() {
  if (appState != STATE_SETTINGS) {
    return false;
  }

  switch (settingsMenuIndex) {
    case 0:
      appState = STATE_SETTINGS_PMS5003;
      settingsPmsMenuIndex = getPms5003Enabled() ? 0 : 1;
      drawStatsSafe();
      break;
    case 1:
      appState = STATE_SETTINGS_BUZZER;
      settingsBuzzerMenuIndex = getBuzzerEnabled() ? 0 : 1;
      drawStatsSafe();
      break;
    case 2:
      appState = STATE_SETTINGS_MQTT;
      settingsMqttMenuIndex = getMqttEnabled() ? 0 : 1;
      drawStatsSafe();
      break;
    case 3:
      settingsAlarmMelodyIndex = clampMenuIndexSafe(AlarmMelodyPrefs::loadIndex(s_prefs), AlarmMelodies::kCount);
      s_prevSettingsAlarmMelodyIndex = settingsAlarmMelodyIndex;
      appState = STATE_SETTINGS_ALARM_MELODY;
      drawStatsSafe();
      break;
    case 4:
      appState = STATE_SETTINGS_SYNC;
      s_prevSettingsSyncMin = settingsSyncMinutes;
      drawStatsSafe();
      break;
    case 5:
      appState = STATE_SETTINGS_ROTATION;
      s_prevSettingsRotationSec = settingsRotationSec;
      drawStatsSafe();
      break;
    case 6:
      settingsUiScreenIndex = clampMenuIndexSafe(settingsUiScreenIndex, settingsUiScreenCount);
      appState = STATE_SETTINGS_UI_SCREEN;
      s_prevSettingsUiScreenIndex = settingsUiScreenIndex;
      drawStatsSafe();
      break;
    case 7:
      settingsEpicIntroIndex = getShowEpicIntro() ? 0 : 1;
      appState = STATE_SETTINGS_BOOT_INTRO;
      drawStatsSafe();
      break;
    case 8:
      appState = STATE_MENU;
      drawMenuSafe();
      break;
    default:
      break;
  }

  return true;
}

void ui_begin(const UI_Callbacks& callbacks) {
  s_callbacks = callbacks;
  drawHomeSafe();
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

  drawSetTimeSafe();
  updateSevenSegSafe();
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
        rotateMenuIndexByDir(menuIndex, menuCount, dir);
        drawMenuSafe();
        break;

      case STATE_STATS:
        rotateMenuIndexByDir(statsMenuIndex, statsMenuCount, dir);
        drawStatsSafe();
        break;

      case STATE_STATS_RESOURCES_MENU:
        rotateMenuIndexByDir(resourcesMenuIndex, resourcesMenuCount, dir);
        drawStatsSafe();
        break;

      case STATE_PMS5003:
        rotateMenuIndexByDir(pms5003MenuIndex, pms5003MenuCount, dir);
        markPmsDirtyAndDrawStats();
        break;

      case STATE_PMS5003_CF1:
        rotateMenuIndexByDir(pms5003CF1MenuIndex, pms5003CF1MenuCount, dir);
        markPmsDirtyAndDrawStats();
        break;

      case STATE_PMS5003_ATM:
        rotateMenuIndexByDir(pms5003ATMMenuIndex, pms5003ATMMenuCount, dir);
        markPmsDirtyAndDrawStats();
        break;

      case STATE_PMS5003_PARTICLES:
        rotateMenuIndexByDir(pms5003ParticlesMenuIndex, pms5003ParticlesMenuCount, dir);
        markPmsDirtyAndDrawStats();
        break;

      case STATE_ENS160_AHT21:
        rotateMenuIndexByDir(ens160MenuIndex, ens160MenuCount, dir);
        ENS160AHT21Screen::markScreenDirty();
        drawStatsSafe();
        break;

      case STATE_BMP280:
        rotateMenuIndexByDir(bmp280MenuIndex, bmp280MenuCount, dir);
        BMP280Screen::markScreenDirty();
        drawStatsSafe();
        break;

      case STATE_SETTINGS:
        rotateMenuIndexByDir(settingsMenuIndex, settingsMenuCount, dir);
        drawStatsSafe();
        break;

      case STATE_SETTINGS_ROTATION:
      case STATE_SETTINGS_UI_SCREEN:
      case STATE_SETTINGS_BOOT_INTRO:
      case STATE_SETTINGS_SYNC:
      case STATE_SETTINGS_PMS5003:
      case STATE_SETTINGS_MQTT:
      case STATE_SETTINGS_BUZZER:
      case STATE_SETTINGS_ALARM_MELODY:
        handleSettingsRotate(dir);
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
        drawAlarmSafe();
        break;

      case STATE_ALARMS_LIST:
        // Move selection up/down; last entry is [+] add new
        alarmsMenuIndex = constrain(alarmsMenuIndex + dir, 0, max(alarmsCount, 0));
        drawStatsSafe();
        break;

      case STATE_ALARM_EDIT:
        if (!ensureSelectedAlarmIndexValid()) {
          return;
        }
        // If actively editing time fields, apply changes; otherwise move the cursor
        if (editState == EDIT_HOURS) {
          alarms[selectedAlarmIndex].hour = (alarms[selectedAlarmIndex].hour + dir + 24) % 24;
        } else if (editState == EDIT_MINUTES) {
          alarms[selectedAlarmIndex].minute = (alarms[selectedAlarmIndex].minute + dir + 60) % 60;
        } else {
          // move cursor between CZAS(0), STATUS(1), USUN(2)
          alarmEditCursor = constrain(alarmEditCursor + dir, 0, 2);
        }
        drawStatsSafe();
        break;

      case STATE_ALARM_DELETE:
        // Reuse alarmsMenuIndex: 0 -> NO, 1 -> YES
        alarmsMenuIndex = constrain(alarmsMenuIndex + dir, 0, 1);
        drawStatsSafe();
        break;

      case STATE_TIMER:
        if (timerRunning) {
          // While running, wheel does not alter set values.
        } else if (editState == EDIT_HOURS) {
          timerSetHours = constrain(timerSetHours + dir, 0, 99);
        } else if (editState == EDIT_MINUTES) {
          timerSetMinutes = (timerSetMinutes + dir + 60) % 60;
        } else if (editState == EDIT_SECONDS) {
          timerSetSeconds = (timerSetSeconds + dir + 60) % 60;
        } else {
          // Navigation mode: move between rows and choose preset.
          if (timerUiCursor == 0) {
            if (dir > 0) {
              timerUiCursor = 1;
            }
          } else {
            if (dir < 0 && timerPresetIndex == 0) {
              timerUiCursor = 0;
            } else {
              timerPresetIndex = constrain(timerPresetIndex + dir, 0, 2);
            }
          }
        }
        drawTimerSafe();
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
      drawMenuSafe();
      return;
    }

    if (handleMainMenuClick()) return;
    if (handleStatsMenuClick()) return;
    if (handleResourcesMenuClick()) return;
    if (handlePmsMenuClick()) return;
    if (handlePmsCf1MenuClick()) return;
    if (handlePmsAtmMenuClick()) return;
    if (handlePmsParticlesMenuClick()) return;
    if (handleEnsMenuClick()) return;
    if (handleBmp280MenuClick()) return;
    if (handleSettingsMenuClick()) return;

    if (handleSettingsConfirmClick()) {
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
      drawStoperSafe();
      return;
    }

    if (appState == STATE_DEBUG_STM32) {
      appState = STATE_HOME;
      updateSevenSegSafe();
      drawHomeSafe();
      return;
    }

    if (appState == STATE_SET_TIME) {
      editState = static_cast<EditState>(editState + 1);
      if (editState == EDIT_DONE) {
        lastTick = millis();
        appState = STATE_HOME;
        updateSevenSegSafe();
        drawHomeSafe();
      } else {
        drawSetTimeSafe();
      }
      return;
    }

    if (appState == STATE_ALARM) {
      editState = static_cast<EditState>(editState + 1);
      if (editState > EDIT_MINUTES) {
        alarmEnabled = true;
        appState     = STATE_HOME;
        updateSevenSegSafe();
        drawHomeSafe();
      } else {
        drawAlarmSafe();
      }
      return;
    }

    if (appState == STATE_TIMER) {
      if (timerRunning) {
        // Click while running: stop countdown.
        timerRunning = false;
        editState = EDIT_DONE;
        drawTimerSafe();
        return;
      }

      if (editState == EDIT_DONE) {
        if (timerUiCursor == 0) {
          // Enter manual HH:MM:SS edit.
          editState = EDIT_HOURS;
        } else {
          // Apply selected quick preset.
          static const int kPresetMinutes[3] = {2, 15, 45};
          timerSetHours = 0;
          timerSetMinutes = kPresetMinutes[timerPresetIndex];
          timerSetSeconds = 0;
          editState = EDIT_DONE;
        }
        drawTimerSafe();
        return;
      }

      // Manual edit progression: HOURS -> MINUTES -> SECONDS -> START
      editState = static_cast<EditState>(editState + 1);
      if (editState > EDIT_SECONDS) {
        timerDurationMs = (unsigned long)timerSetHours * 3600000UL + (unsigned long)timerSetMinutes * 60000UL + (unsigned long)timerSetSeconds * 1000UL;
        if (timerDurationMs > 0) {
          timerStartMillis = millis();
          timerRunning = true;
        }
        editState = EDIT_DONE;
      }
      drawTimerSafe();
      return;
    }

    // --- ALARM LIST / EDIT / DELETE click handling ---
    if (appState == STATE_ALARMS_LIST) {
      // If selected is existing alarm -> open edit; if it's the add slot -> add new alarm
      if (alarmsMenuIndex < alarmsCount) {
        selectedAlarmIndex = alarmsMenuIndex;
        appState = STATE_ALARM_EDIT;
        editState = EDIT_DONE; // not actively editing time yet
        alarmEditCursor = 0; // start with CZAS selected
        drawStatsSafe();
      } else {
        // add new alarm (if room)
        if (alarmsCount < MAX_ALARMS) {
          alarms[alarmsCount].hour = 7;
          alarms[alarmsCount].minute = 0;
          alarms[alarmsCount].enabled = true;
          alarms[alarmsCount].lastTriggerDay = 0;
          alarmsCount++;
          // persist
          int i = alarmsCount - 1;
          persistAllAlarms();
          // edit newly added (start in cursor mode)
          selectedAlarmIndex = i;
          appState = STATE_ALARM_EDIT;
          editState = EDIT_DONE;
          alarmEditCursor = 0;
          drawStatsSafe();
        }
      }
      return;
    }

    if (appState == STATE_ALARM_EDIT) {
      if (!ensureSelectedAlarmIndexValid()) {
        return;
      }

      if (editState == EDIT_DONE) {
        // interpret click based on cursor selection
        if (alarmEditCursor == 0) {
          // enter time edit (hours)
          editState = EDIT_HOURS;
          drawStatsSafe();
        } else if (alarmEditCursor == 1) {
          // toggle enabled and persist
          alarms[selectedAlarmIndex].enabled = !alarms[selectedAlarmIndex].enabled;
          persistAlarmAt(selectedAlarmIndex);
          drawStatsSafe();
        } else {
          // delete selected alarm immediately (no confirmation)
          removeAlarmAt(selectedAlarmIndex);
          appState = STATE_ALARMS_LIST;
          drawStatsSafe();
        }
      } else if (editState == EDIT_HOURS) {
        // advance to minutes
        editState = EDIT_MINUTES;
        drawStatsSafe();
      } else if (editState == EDIT_MINUTES) {
        // finish edit: persist alarm and return to list
        persistAlarmAt(selectedAlarmIndex);
        editState = EDIT_DONE;
        appState = STATE_ALARMS_LIST;
        drawStatsSafe();
      }
      return;
    }

    if (appState == STATE_ALARM_DELETE) {
      // click confirms deletion if selection is 'TAK' (we encoded selection in alarmsMenuIndex)
      // Reuse alarmsMenuIndex: 0 -> NO, 1 -> YES
      if (alarmsMenuIndex == 1) {
        // delete selectedAlarmIndex
        removeAlarmAt(selectedAlarmIndex);
      }
      appState = STATE_ALARMS_LIST;
      drawStatsSafe();
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
      drawStatsSafe();
    };

    if (handleSettingsLongCancel()) {
      return;
    }

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
      case STATE_STATS_RESOURCES_MENU:
        returnToStatsMenu(4);
        return;
      
      // --- Zasoby: ekrany szczegółowe -> powrót do menu zasobów ---
      case STATE_STATS_RESOURCES_RAM:
      case STATE_STATS_RESOURCES_CPU:
      case STATE_STATS_RESOURCES_FLASH:
        appState = STATE_STATS_RESOURCES_MENU;
        drawStatsSafe();
        return;

      // --- PMS5003: ekrany szczegółowe -> powrót do menu PMS5003 ---
      case STATE_PMS5003_CF1_PM1:
      case STATE_PMS5003_CF1_PM25:
      case STATE_PMS5003_CF1_PM10:
        appState = STATE_PMS5003_CF1;
        markPmsDirtyAndDrawStats();
        return;

      case STATE_PMS5003_CF1:
        // Menu CF1 -> powrót do menu PMS5003
        appState = STATE_PMS5003;
        markPmsDirtyAndDrawStats();
        return;

      // --- PMS5003 ATM: ekrany szczegółowe -> powrót do menu ATM ---
      case STATE_PMS5003_ATM_PM1:
      case STATE_PMS5003_ATM_PM25:
      case STATE_PMS5003_ATM_PM10:
        appState = STATE_PMS5003_ATM;
        markPmsDirtyAndDrawStats();
        return;

      case STATE_PMS5003_ATM:
        // Menu ATM -> powrót do menu PMS5003
        appState = STATE_PMS5003;
        markPmsDirtyAndDrawStats();
        return;

      // --- PMS5003 PARTICLES: ekrany szczegółowe -> powrót do menu PARTICLES ---
      case STATE_PMS5003_PARTICLES_0_3:
      case STATE_PMS5003_PARTICLES_0_5:
      case STATE_PMS5003_PARTICLES_1_0:
      case STATE_PMS5003_PARTICLES_2_5:
      case STATE_PMS5003_PARTICLES_5_0:
      case STATE_PMS5003_PARTICLES_10_0:
        appState = STATE_PMS5003_PARTICLES;
        markPmsDirtyAndDrawStats();
        return;

      case STATE_PMS5003_PARTICLES:
        // Menu PARTICLES -> powrót do menu PMS5003
        appState = STATE_PMS5003;
        markPmsDirtyAndDrawStats();
        return;

      case STATE_PMS5003_TELEMETRY:
        // Telemetria -> powrót do menu PMS5003
        appState = STATE_PMS5003;
        markPmsDirtyAndDrawStats();
        return;

      case STATE_PMS5003:
        // Menu PMS5003 -> menu główne
        appState = STATE_MENU;
        drawMenuSafe();
        return;

      case STATE_ENS160_AHT21_SUMMARY:
      case STATE_ENS160_AHT21_STATUS:
      case STATE_ENS160_AHT21_GAS:
      case STATE_ENS160_AHT21_CLIMATE:
      case STATE_ENS160_AHT21_GAS_AQI:
      case STATE_ENS160_AHT21_GAS_TVOC:
      case STATE_ENS160_AHT21_GAS_ECO2:
      case STATE_ENS160_AHT21_CLIMATE_TEMP:
      case STATE_ENS160_AHT21_CLIMATE_HUM:
        appState = STATE_ENS160_AHT21;
        ENS160AHT21Screen::markScreenDirty();
        drawStatsSafe();
        return;

      case STATE_ENS160_AHT21:
        appState = STATE_MENU;
        drawMenuSafe();
        return;

      case STATE_BMP280_TEMP:
      case STATE_BMP280_PRESSURE:
      case STATE_BMP280_STATUS:
      case STATE_BMP280_ALTITUDE:
        appState = STATE_BMP280;
        BMP280Screen::markScreenDirty();
        drawStatsSafe();
        return;

      case STATE_BMP280:
        appState = STATE_MENU;
        drawMenuSafe();
        return;

      case STATE_TIMER:
        // Long press in TIMER: stop timer (if running) and return to main menu
        timerRunning = false;
        appState = STATE_MENU;
        drawMenuSafe();
        return;

      case STATE_ALARMS_LIST:
        // Lista budzików -> powrót do miejsca wejścia (menu główne lub ustawienia)
        appState = s_alarmReturnState;
        if (s_alarmReturnState == STATE_SETTINGS) {
          drawStatsSafe();
        } else {
          drawMenuSafe();
        }
        return;

      case STATE_ALARM_EDIT:
        // Edycja budzika -> powrót do listy (bez dodatkowego zapisu)
        appState = STATE_ALARMS_LIST;
        drawStatsSafe();
        return;

      case STATE_ALARM_DELETE:
        // Potwierdzenie usunięcia -> powrót do edycji
        appState = STATE_ALARM_EDIT;
        drawStatsSafe();
        return;

      case STATE_SETTINGS:
        // Menu Ustawień -> menu główne
        appState = STATE_MENU;
        drawMenuSafe();
        return;

      case STATE_STATS:
        // Menu statystyk -> menu główne
        appState = STATE_MENU;
        drawMenuSafe();
        return;

      case STATE_MENU:
        // Long press in main menu -> go back to home
        appState = STATE_HOME;
        updateSevenSegSafe();
        drawHomeSafe();
        return;

      case STATE_STOPER:
      case STATE_DEBUG_STM32:
        // Inne wyjścia -> MENU
        appState = STATE_MENU;
        drawMenuSafe();
        return;

      default:
        // Fallback (cokolwiek innego) -> MENU
        appState = STATE_MENU;
        drawMenuSafe();
        return;
    }
  }
} // koniec: ui_handleEvent(...)