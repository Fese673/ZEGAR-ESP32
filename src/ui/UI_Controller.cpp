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
extern int pms5003MenuIndex;
extern int pms5003MenuCount;
extern int pms5003CF1MenuIndex;
extern int pms5003CF1MenuCount;
extern int pms5003ATMMenuIndex;
extern int pms5003ATMMenuCount;
extern int pms5003ParticlesMenuIndex;
extern int pms5003ParticlesMenuCount;
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

// --- Dane PMS5003 ---
extern uint16_t pms5003_PM1_0_CF1;
extern uint16_t pms5003_PM2_5_CF1;
extern uint16_t pms5003_PM10_CF1;
extern uint16_t pms5003_PM1_0_ATM;
extern uint16_t pms5003_PM2_5_ATM;
extern uint16_t pms5003_PM10_ATM;
extern uint16_t pms5003_PM1_0_CF1_MIN;
extern uint16_t pms5003_PM1_0_CF1_MAX;
extern uint16_t pms5003_PM2_5_CF1_MIN;
extern uint16_t pms5003_PM2_5_CF1_MAX;
extern uint16_t pms5003_PM10_CF1_MIN;
extern uint16_t pms5003_PM10_CF1_MAX;
extern uint16_t pms5003_PM1_0_ATM_MIN;
extern uint16_t pms5003_PM1_0_ATM_MAX;
extern uint16_t pms5003_PM2_5_ATM_MIN;
extern uint16_t pms5003_PM2_5_ATM_MAX;
extern uint16_t pms5003_PM10_ATM_MIN;
extern uint16_t pms5003_PM10_ATM_MAX;
extern uint16_t pms5003_particleCount_0_3;
extern uint16_t pms5003_particleCount_0_5;
extern uint16_t pms5003_particleCount_1_0;
extern uint16_t pms5003_particleCount_2_5;
extern uint16_t pms5003_particleCount_5_0;
extern uint16_t pms5003_particleCount_10_0;
extern uint16_t pms5003_particleCount_0_3_MIN;
extern uint16_t pms5003_particleCount_0_3_MAX;
extern uint16_t pms5003_particleCount_0_5_MIN;
extern uint16_t pms5003_particleCount_0_5_MAX;
extern uint16_t pms5003_particleCount_1_0_MIN;
extern uint16_t pms5003_particleCount_1_0_MAX;
extern uint16_t pms5003_particleCount_2_5_MIN;
extern uint16_t pms5003_particleCount_2_5_MAX;
extern uint16_t pms5003_particleCount_5_0_MIN;
extern uint16_t pms5003_particleCount_5_0_MAX;
extern uint16_t pms5003_particleCount_10_0_MIN;
extern uint16_t pms5003_particleCount_10_0_MAX;
extern uint16_t pms5003_errorCount_current;
extern uint16_t pms5003_errorCount_total;
extern uint16_t pms5003_bytesReceived;
extern uint32_t pms5003_lastFrameTime;
extern uint32_t pms5003_latency_ms;
extern bool pmsScreenDirty;

// --- Ustawienia (Settings) ---
extern int settingsMenuIndex;
extern int settingsMenuCount;
extern int settingsPmsMenuIndex;
extern int settingsPmsMenuCount;
extern int settingsMqttMenuIndex;
extern int settingsMqttMenuCount;
extern int settingsBuzzerMenuIndex;
extern int settingsBuzzerMenuCount;
extern int settingsAlarmMelodyIndex;
extern int s_prevSettingsAlarmMelodyIndex;
extern int settingsEpicIntroIndex;
extern int settingsEpicIntroMenuCount;
extern bool pms5003Enabled;
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
        menuIndex = constrain(menuIndex + dir, 0, menuCount - 1);
        drawMenuSafe();
        break;

      case STATE_STATS:
        statsMenuIndex = constrain(statsMenuIndex + dir, 0, statsMenuCount - 1);
        drawStatsSafe();
        break;

      case STATE_STATS_RESOURCES_MENU:
        resourcesMenuIndex = constrain(resourcesMenuIndex + dir, 0, resourcesMenuCount - 1);
        drawStatsSafe();
        break;

      case STATE_PMS5003:
        pms5003MenuIndex = constrain(pms5003MenuIndex + dir, 0, pms5003MenuCount - 1);
        markPmsDirtyAndDrawStats();
        break;

      case STATE_PMS5003_CF1:
        pms5003CF1MenuIndex = constrain(pms5003CF1MenuIndex + dir, 0, pms5003CF1MenuCount - 1);
        markPmsDirtyAndDrawStats();
        break;

      case STATE_PMS5003_ATM:
        pms5003ATMMenuIndex = constrain(pms5003ATMMenuIndex + dir, 0, pms5003ATMMenuCount - 1);
        markPmsDirtyAndDrawStats();
        break;

      case STATE_PMS5003_PARTICLES:
        pms5003ParticlesMenuIndex = constrain(pms5003ParticlesMenuIndex + dir, 0, pms5003ParticlesMenuCount - 1);
        markPmsDirtyAndDrawStats();
        break;

      case STATE_ENS160_AHT21:
        ens160MenuIndex = constrain(ens160MenuIndex + dir, 0, ens160MenuCount - 1);
        ENS160AHT21Screen::markScreenDirty();
        drawStatsSafe();
        break;

      case STATE_BMP280:
        bmp280MenuIndex = constrain(bmp280MenuIndex + dir, 0, bmp280MenuCount - 1);
        BMP280Screen::markScreenDirty();
        drawStatsSafe();
        break;

      case STATE_SETTINGS:
        settingsMenuIndex = constrain(settingsMenuIndex + dir, 0, settingsMenuCount - 1);
        drawStatsSafe();
        break;

      case STATE_SETTINGS_ROTATION:
        // adjust rotation seconds (1..10)
        settingsRotationSec = constrain(settingsRotationSec + dir, 1, 10);
        HomeRuntime::setOverlayIntervalSeconds((uint8_t)settingsRotationSec);
        drawStatsSafe();
        break;

      case STATE_SETTINGS_UI_SCREEN:
        settingsUiScreenIndex = constrain(settingsUiScreenIndex + dir, 0, settingsUiScreenCount - 1);
        setHomeUiProfileSafe((uint8_t)settingsUiScreenIndex);
        drawStatsSafe();
        break;

      case STATE_SETTINGS_BOOT_INTRO:
        settingsEpicIntroIndex = constrain(settingsEpicIntroIndex + dir, 0, settingsEpicIntroMenuCount - 1);
        drawStatsSafe();
        break;

      case STATE_SETTINGS_SYNC:
        // adjust sync interval in 10-minute steps (10..360)
        settingsSyncMinutes = constrain(settingsSyncMinutes + dir * 10, 10, 360);
        // apply immediately to WiFiSync runtime
        WiFiSync::setPeriodicSyncIntervalMinutes((uint16_t)settingsSyncMinutes);
        drawStatsSafe();
        break;

      case STATE_SETTINGS_PMS5003:
        settingsPmsMenuIndex = constrain(settingsPmsMenuIndex + dir, 0, settingsPmsMenuCount - 1);
        drawStatsSafe();
        break;

      case STATE_SETTINGS_MQTT:
        settingsMqttMenuIndex = constrain(settingsMqttMenuIndex + dir, 0, settingsMqttMenuCount - 1);
        drawStatsSafe();
        break;

      case STATE_SETTINGS_BUZZER:
        settingsBuzzerMenuIndex = constrain(settingsBuzzerMenuIndex + dir, 0, settingsBuzzerMenuCount - 1);
        drawStatsSafe();
        break;

      case STATE_SETTINGS_ALARM_MELODY:
        settingsAlarmMelodyIndex = constrain(settingsAlarmMelodyIndex + dir, 0, AlarmMelodies::kCount - 1);
        drawStatsSafe();
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
        alarmsMenuIndex = constrain(alarmsMenuIndex + dir, 0, (alarmsCount > 0 ? alarmsCount : 0));
        drawStatsSafe();
        break;

      case STATE_ALARM_EDIT:
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

    // --- GŁÓWNE MENU (Wybór opcji) ---
    if (appState == STATE_MENU) {
      switch (menuIndex) {
        case 0:  // Ustaw czas
          appState  = STATE_SET_TIME;
          editState = EDIT_HOURS;
          drawSetTimeSafe();
          return;

        case 1:  // Minutnik
          appState  = STATE_TIMER;
          editState = EDIT_DONE;
          timerUiCursor = 0;
          timerPresetIndex = 1;
          drawTimerSafe();
          return;

        case 2:  // Stoper
          appState      = STATE_STOPER;
          stoperRunning = false;
          stoperElapsed = 0;
          drawStoperSafe();
          return;

        case 3:  // Budzik (lista budzików)
          s_alarmReturnState = STATE_MENU;
          appState = STATE_ALARMS_LIST;
          // ensure selection in range
          if (alarmsMenuIndex < 0) alarmsMenuIndex = 0;
          if (alarmsMenuIndex > alarmsCount) alarmsMenuIndex = alarmsCount;
          drawStatsSafe();
          return;

        case 4:  // Statystyki
          appState       = STATE_STATS;
          statsMenuIndex = 0;
          drawStatsSafe();
          return;

        case 5:  // Debug STM32
          appState = STATE_DEBUG_STM32;
          updateSevenSegSafe();
          drawDebugSTM32Safe();
          return;

        case 6:  // PMS5003
          appState       = STATE_PMS5003;
          pms5003MenuIndex = 0;
          // Przy wejściu do menu PMS – wymuś odczyt i pozwól na natychmiastowe rysowanie
          PMS5003Sensor::requestImmediateRead();
          markPmsDirtyAndDrawStats();
          return;

        case 7:  // AHT21 + ENS160
          appState = STATE_ENS160_AHT21;
          ens160MenuIndex = 0;
          ENS160AHT21Screen::markScreenDirty();
          drawStatsSafe();
          return;

        case 8:  // BMP280
          appState = STATE_BMP280;
          bmp280MenuIndex = 0;
          BMP280Screen::markScreenDirty();
          drawStatsSafe();
          return;

        case 9:  // Ustawienia
          appState        = STATE_SETTINGS;
          settingsMenuIndex = 0;
          drawStatsSafe();
          return;

        case 10:  // Wyjście
          appState = STATE_HOME;
          updateSevenSegSafe();
          drawHomeSafe();
          return;

        case 11:  // Radio Toggle (WiFi ↔ Bluetooth)
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
        case 4:  // Zasoby
          appState = STATE_STATS_RESOURCES_MENU;
          resourcesMenuIndex = 0;
          drawStatsSafe();
          break;
        case 5:  // Wyjście
          appState = STATE_MENU;
          drawMenuSafe();
          break;
        default:
          break;
      }
      return;
    }

    // --- LOGIKA MENU ZASOBÓW ---
    if (appState == STATE_STATS_RESOURCES_MENU) {
      switch (resourcesMenuIndex) {
        case 0:  // RAM Free
          appState = STATE_STATS_RESOURCES_RAM;
          drawStatsSafe();
          break;
        case 1:  // CPU
          appState = STATE_STATS_RESOURCES_CPU;
          drawStatsSafe();
          break;
        case 2:  // Flash Free
          appState = STATE_STATS_RESOURCES_FLASH;
          drawStatsSafe();
          break;
        default:
          break;
      }
      return;
    }

    // --- LOGIKA MENU PMS5003 ---
    if (appState == STATE_PMS5003) {
      switch (pms5003MenuIndex) {
        case 0:
          appState = STATE_PMS5003_CF1;
          pms5003CF1MenuIndex = 0;
          PMS5003Sensor::requestImmediateRead();
          markPmsDirtyAndDrawStats();
          break;
        case 1:
          appState = STATE_PMS5003_ATM;
          pms5003ATMMenuIndex = 0;
          PMS5003Sensor::requestImmediateRead();
          markPmsDirtyAndDrawStats();
          break;
        case 2:  // L.Czastek
          appState = STATE_PMS5003_PARTICLES;
          pms5003ParticlesMenuIndex = 0;
          PMS5003Sensor::requestImmediateRead();
          markPmsDirtyAndDrawStats();
          break;
        case 3:  // Telemetria
          appState = STATE_PMS5003_TELEMETRY;
          PMS5003Sensor::requestImmediateRead();
          markPmsDirtyAndDrawStats();
          break;
        case 5:  // Wyjście
          appState = STATE_MENU;
          drawMenuSafe();
          break;
        default:
          break;
      }
      return;
    }

    // --- LOGIKA MENU PMS5003 CF=1 (Widok danych z wyborem szczegółów) ---
    if (appState == STATE_PMS5003_CF1) {
      switch (pms5003CF1MenuIndex) {
        case 0:  // PM1.0 - wejdź w szczegóły
          appState = STATE_PMS5003_CF1_PM1;
          markPmsDirtyAndDrawStats();
          break;
        case 1:  // PM2.5 - wejdź w szczegóły
          appState = STATE_PMS5003_CF1_PM25;
          markPmsDirtyAndDrawStats();
          break;
        case 2:  // PM10 - wejdź w szczegóły
          appState = STATE_PMS5003_CF1_PM10;
          markPmsDirtyAndDrawStats();
          break;
        default:
          break;
      }
      return;
    }

    // --- LOGIKA MENU PMS5003 ATM (Widok danych z wyborem szczegółów) ---
    if (appState == STATE_PMS5003_ATM) {
      switch (pms5003ATMMenuIndex) {
        case 0:  // PM1.0 - wejdź w szczegóły
          appState = STATE_PMS5003_ATM_PM1;
          markPmsDirtyAndDrawStats();
          break;
        case 1:  // PM2.5 - wejdź w szczegóły
          appState = STATE_PMS5003_ATM_PM25;
          markPmsDirtyAndDrawStats();
          break;
        case 2:  // PM10 - wejdź w szczegóły
          appState = STATE_PMS5003_ATM_PM10;
          markPmsDirtyAndDrawStats();
          break;
        default:
          break;
      }
      return;
    }

    // --- LOGIKA MENU PMS5003 PARTICLES (Widok danych z wyborem szczegółów) ---
    if (appState == STATE_PMS5003_PARTICLES) {
      switch (pms5003ParticlesMenuIndex) {
        case 0:  // 0.3um - wejdź w szczegóły
          appState = STATE_PMS5003_PARTICLES_0_3;
          markPmsDirtyAndDrawStats();
          break;
        case 1:  // 0.5um - wejdź w szczegóły
          appState = STATE_PMS5003_PARTICLES_0_5;
          markPmsDirtyAndDrawStats();
          break;
        case 2:  // 1.0um - wejdź w szczegóły
          appState = STATE_PMS5003_PARTICLES_1_0;
          markPmsDirtyAndDrawStats();
          break;
        case 3:  // 2.5um - wejdź w szczegóły
          appState = STATE_PMS5003_PARTICLES_2_5;
          markPmsDirtyAndDrawStats();
          break;
        case 4:  // 5.0um - wejdź w szczegóły
          appState = STATE_PMS5003_PARTICLES_5_0;
          markPmsDirtyAndDrawStats();
          break;
        case 5:  // 10um - wejdź w szczegóły
          appState = STATE_PMS5003_PARTICLES_10_0;
          markPmsDirtyAndDrawStats();
          break;
        default:
          break;
      }
      return;
    }

    // --- LOGIKA MENU ENS160 + AHT21 ---
    if (appState == STATE_ENS160_AHT21) {
      switch (ens160MenuIndex) {
        case 0:
          appState = STATE_ENS160_AHT21_GAS_AQI;
          ENS160AHT21Screen::markScreenDirty();
          drawStatsSafe();
          break;
        case 1:
          appState = STATE_ENS160_AHT21_GAS_TVOC;
          ENS160AHT21Screen::markScreenDirty();
          drawStatsSafe();
          break;
        case 2:
          appState = STATE_ENS160_AHT21_GAS_ECO2;
          ENS160AHT21Screen::markScreenDirty();
          drawStatsSafe();
          break;
        case 3:
          appState = STATE_ENS160_AHT21_CLIMATE_TEMP;
          ENS160AHT21Screen::markScreenDirty();
          drawStatsSafe();
          break;
        case 4:
          appState = STATE_ENS160_AHT21_CLIMATE_HUM;
          ENS160AHT21Screen::markScreenDirty();
          drawStatsSafe();
          break;
        case 5:
          appState = STATE_ENS160_AHT21_STATUS;
          ENS160AHT21Screen::markScreenDirty();
          drawStatsSafe();
          break;
        default:
          break;
      }
      return;
    }

    // --- LOGIKA MENU BMP280 ---
    if (appState == STATE_BMP280) {
      switch (bmp280MenuIndex) {
        case 0:
          appState = STATE_BMP280_TEMP;
          markBmp280DirtyAndDrawStats();
          break;
        case 1:
          appState = STATE_BMP280_PRESSURE;
          markBmp280DirtyAndDrawStats();
          break;
        case 2:
          appState = STATE_BMP280_STATUS;
          markBmp280DirtyAndDrawStats();
          break;
        case 3:
          appState = STATE_BMP280_ALTITUDE;
          markBmp280DirtyAndDrawStats();
          break;
        default:
          break;
      }
      return;
    }

    // --- LOGIKA MENU USTAWIEŃ (Settings) ---
    if (appState == STATE_SETTINGS) {
      switch (settingsMenuIndex) {
        case 0:  // PMS5003
          appState = STATE_SETTINGS_PMS5003;
          settingsPmsMenuIndex = pms5003Enabled ? 0 : 1;
          drawStatsSafe();
          break;
        case 1:  // Buzzer
          appState = STATE_SETTINGS_BUZZER;
          settingsBuzzerMenuIndex = buzzerEnabled ? 0 : 1;
          drawStatsSafe();
          break;
        case 2:  // MQTT
          appState = STATE_SETTINGS_MQTT;
          settingsMqttMenuIndex = mqttEnabled ? 0 : 1;
          drawStatsSafe();
          break;
        case 3:  // ALARMY
          settingsAlarmMelodyIndex = AlarmMelodyPrefs::loadIndex(s_prefs);
          s_prevSettingsAlarmMelodyIndex = settingsAlarmMelodyIndex;
          appState = STATE_SETTINGS_ALARM_MELODY;
          drawStatsSafe();
          break;
        case 4:  // Synchronizacja
          appState = STATE_SETTINGS_SYNC;
          // store previous value so long-press can cancel
          s_prevSettingsSyncMin = settingsSyncMinutes;
          drawStatsSafe();
          break;
        case 5:  // Rotacja Ekranu
          appState = STATE_SETTINGS_ROTATION;
          // store previous value so long-press can cancel
          s_prevSettingsRotationSec = settingsRotationSec;
          drawStatsSafe();
          break;
        case 6:  // UI Ekran
          appState = STATE_SETTINGS_UI_SCREEN;
          s_prevSettingsUiScreenIndex = settingsUiScreenIndex;
          drawStatsSafe();
          break;
        case 7:  // Boot Intro
          settingsEpicIntroIndex = showEpicIntro ? 0 : 1;
          appState = STATE_SETTINGS_BOOT_INTRO;
          drawStatsSafe();
          break;
        case 8:  // Wyjście
          appState = STATE_MENU;
          drawMenuSafe();
          break;
        default:
          break;
      }
      return;
    }

    // --- LOGIKA MENU USTAWIEŃ PMS5003 (włącz/wyłącz) ---
    if (appState == STATE_SETTINGS_PMS5003) {
      pms5003Enabled = (settingsPmsMenuIndex == 0);
      appState = STATE_SETTINGS;
      drawStatsSafe();
      return;
    }

    // --- LOGIKA MENU USTAWIEŃ BUZERA (włącz/wyłącz) ---
    if (appState == STATE_SETTINGS_BUZZER) {
      buzzerEnabled = (settingsBuzzerMenuIndex == 0);
      appState = STATE_SETTINGS;
      drawStatsSafe();
      return;
    }

    // --- LOGIKA MENU USTAWIEŃ MELODII ALARMU ---
    if (appState == STATE_SETTINGS_ALARM_MELODY) {
      AlarmMelodyPrefs::saveSelection(s_prefs, settingsAlarmMelodyIndex);
      drawStatsSafe();
      startAlarmMelodyDemo((uint8_t)settingsAlarmMelodyIndex);
      return;
    }

    // --- LOGIKA MENU USTAWIEŃ MQTT (włącz/wyłącz) ---
    if (appState == STATE_SETTINGS_MQTT) {
      mqttEnabled = (settingsMqttMenuIndex == 0);
      s_prefs.putBool("mqttEnabled", mqttEnabled);
      appState = STATE_SETTINGS;
      drawStatsSafe();
      return;
    }

    // --- LOGIKA: Rotacja Ekranu (zapisz na klik) ---
    if (appState == STATE_SETTINGS_ROTATION) {
      // persist new value and return to settings menu
      s_prefs.putUShort("homeOverlaySec", (uint16_t)settingsRotationSec);
      appState = STATE_SETTINGS;
      drawStatsSafe();
      return;
    }

    // --- LOGIKA: Synchronizacja NTP (zapisz na klik) ---
    if (appState == STATE_SETTINGS_SYNC) {
      // persist new value and apply
      s_prefs.putUShort("ntpSyncMin", (uint16_t)settingsSyncMinutes);
      WiFiSync::setPeriodicSyncIntervalMinutes((uint16_t)settingsSyncMinutes);
      appState = STATE_SETTINGS;
      drawStatsSafe();
      return;
    }

    // --- LOGIKA: UI EKRAN (wizualny wybór profilu, bez zapisu) ---
    if (appState == STATE_SETTINGS_UI_SCREEN) {
      s_prefs.putUShort("uiScreenMode", (uint16_t)settingsUiScreenIndex);
      appState = STATE_SETTINGS;
      drawStatsSafe();
      return;
    }

    // --- LOGIKA MENU USTAWIEŃ BOOT INTRO (włącz/wyłącz) ---
    if (appState == STATE_SETTINGS_BOOT_INTRO) {
      showEpicIntro = (settingsEpicIntroIndex == 0);
      s_prefs.putBool("epicIntro", showEpicIntro);
      appState = STATE_SETTINGS;
      drawStatsSafe();
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

      // --- Ustawienia (Settings) -> powrót do menu głównego ---
      case STATE_SETTINGS_PMS5003:
      case STATE_SETTINGS_BUZZER:
      case STATE_SETTINGS_MQTT:
      case STATE_SETTINGS_BOOT_INTRO:
        appState = STATE_SETTINGS;
        drawStatsSafe();
        return;
      case STATE_SETTINGS_ROTATION:
        // cancel: restore previous value and go back
        settingsRotationSec = s_prevSettingsRotationSec;
        HomeRuntime::setOverlayIntervalSeconds((uint8_t)settingsRotationSec);
        appState = STATE_SETTINGS;
        drawStatsSafe();
        return;
      case STATE_SETTINGS_SYNC:
        // cancel: restore previous value and go back
        settingsSyncMinutes = s_prevSettingsSyncMin;
        WiFiSync::setPeriodicSyncIntervalMinutes((uint16_t)settingsSyncMinutes);
        appState = STATE_SETTINGS;
        drawStatsSafe();
        return;

      case STATE_SETTINGS_UI_SCREEN:
        settingsUiScreenIndex = s_prevSettingsUiScreenIndex;
        setHomeUiProfileSafe((uint8_t)settingsUiScreenIndex);
        appState = STATE_SETTINGS;
        drawStatsSafe();
        return;

      case STATE_SETTINGS_ALARM_MELODY:
        settingsAlarmMelodyIndex = s_prevSettingsAlarmMelodyIndex;
        stopAlarmMelodyDemo();
        appState = STATE_SETTINGS;
        drawStatsSafe();
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