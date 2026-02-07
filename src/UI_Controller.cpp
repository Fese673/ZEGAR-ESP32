#include "UI_Controller.h"
#include <Arduino.h>
#include "AppState.h"
#include "ModeManager.h"
#include "RadioModeSwitch.h"
#include "PMS_Czujnik.h"
#include "UI_Draw.h"

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
extern int settingsBuzzerMenuIndex;
extern int settingsBuzzerMenuCount;
extern bool pms5003Enabled;
extern bool buzzerEnabled;

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

      case STATE_STATS_RESOURCES_MENU:
        resourcesMenuIndex = constrain(resourcesMenuIndex + dir, 0, resourcesMenuCount - 1);
        if (s_callbacks.drawStats) s_callbacks.drawStats();
        break;

      case STATE_PMS5003:
        pms5003MenuIndex = constrain(pms5003MenuIndex + dir, 0, pms5003MenuCount - 1);
        pmsScreenDirty = true;
        if (s_callbacks.drawStats) s_callbacks.drawStats();
        break;

      case STATE_PMS5003_CF1:
        pms5003CF1MenuIndex = constrain(pms5003CF1MenuIndex + dir, 0, pms5003CF1MenuCount - 1);
        pmsScreenDirty = true;
        if (s_callbacks.drawStats) s_callbacks.drawStats();
        break;

      case STATE_PMS5003_ATM:
        pms5003ATMMenuIndex = constrain(pms5003ATMMenuIndex + dir, 0, pms5003ATMMenuCount - 1);
        pmsScreenDirty = true;
        if (s_callbacks.drawStats) s_callbacks.drawStats();
        break;

      case STATE_PMS5003_PARTICLES:
        pms5003ParticlesMenuIndex = constrain(pms5003ParticlesMenuIndex + dir, 0, pms5003ParticlesMenuCount - 1);
        pmsScreenDirty = true;
        if (s_callbacks.drawStats) s_callbacks.drawStats();
        break;

      case STATE_SETTINGS:
        settingsMenuIndex = constrain(settingsMenuIndex + dir, 0, settingsMenuCount - 1);
        if (s_callbacks.drawStats) s_callbacks.drawStats();
        break;

      case STATE_SETTINGS_PMS5003:
        settingsPmsMenuIndex = constrain(settingsPmsMenuIndex + dir, 0, settingsPmsMenuCount - 1);
        if (s_callbacks.drawStats) s_callbacks.drawStats();
        break;

      case STATE_SETTINGS_BUZZER:
        settingsBuzzerMenuIndex = constrain(settingsBuzzerMenuIndex + dir, 0, settingsBuzzerMenuCount - 1);
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

        case 6:  // PMS5003
          appState       = STATE_PMS5003;
          pms5003MenuIndex = 0;
          // Przy wejściu do menu PMS – wymuś odczyt i pozwól na natychmiastowe rysowanie
          PMS5003Sensor::requestImmediateRead();
          pmsScreenDirty = true;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          return;

        case 7:  // Temperatura
          appState = STATE_TEMPERATURE;
          drawTemperature();
          showTemperature7Seg();
          return;

        case 8:  // Wilgotność
          appState = STATE_HUMIDITY;
          drawHumidity();
          showHumidity7Seg();
          return;

        case 9:  // Ustawienia
          appState        = STATE_SETTINGS;
          settingsMenuIndex = 0;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          return;

        case 10:  // Wyjście
          appState = STATE_HOME;
          if (s_callbacks.updateSevenSeg) s_callbacks.updateSevenSeg();
          if (s_callbacks.drawHome) s_callbacks.drawHome();
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
          appState = STATE_STATS_RESOURCES_MENU;
          resourcesMenuIndex = 0;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
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

    // --- LOGIKA MENU ZASOBÓW ---
    if (appState == STATE_STATS_RESOURCES_MENU) {
      switch (resourcesMenuIndex) {
        case 0:  // RAM Free
          appState = STATE_STATS_RESOURCES_RAM;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          break;
        case 1:  // CPU
          appState = STATE_STATS_RESOURCES_CPU;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          break;
        case 2:  // Flash Free
          appState = STATE_STATS_RESOURCES_FLASH;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
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
          pmsScreenDirty = true;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          break;
        case 1:
          appState = STATE_PMS5003_ATM;
          pms5003ATMMenuIndex = 0;
          PMS5003Sensor::requestImmediateRead();
          pmsScreenDirty = true;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          break;
        case 2:  // L.Czastek
          appState = STATE_PMS5003_PARTICLES;
          pms5003ParticlesMenuIndex = 0;
          PMS5003Sensor::requestImmediateRead();
          pmsScreenDirty = true;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          break;
        case 3:  // Telemetria
          appState = STATE_PMS5003_TELEMETRY;
          PMS5003Sensor::requestImmediateRead();
          pmsScreenDirty = true;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          break;
        case 4:  // Wyjście
          appState = STATE_MENU;
          if (s_callbacks.drawMenu) s_callbacks.drawMenu();
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
          pmsScreenDirty = true;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          break;
        case 1:  // PM2.5 - wejdź w szczegóły
          appState = STATE_PMS5003_CF1_PM25;
          pmsScreenDirty = true;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          break;
        case 2:  // PM10 - wejdź w szczegóły
          appState = STATE_PMS5003_CF1_PM10;
          pmsScreenDirty = true;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
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
          pmsScreenDirty = true;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          break;
        case 1:  // PM2.5 - wejdź w szczegóły
          appState = STATE_PMS5003_ATM_PM25;
          pmsScreenDirty = true;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          break;
        case 2:  // PM10 - wejdź w szczegóły
          appState = STATE_PMS5003_ATM_PM10;
          pmsScreenDirty = true;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
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
          pmsScreenDirty = true;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          break;
        case 1:  // 0.5um - wejdź w szczegóły
          appState = STATE_PMS5003_PARTICLES_0_5;
          pmsScreenDirty = true;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          break;
        case 2:  // 1.0um - wejdź w szczegóły
          appState = STATE_PMS5003_PARTICLES_1_0;
          pmsScreenDirty = true;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          break;
        case 3:  // 2.5um - wejdź w szczegóły
          appState = STATE_PMS5003_PARTICLES_2_5;
          pmsScreenDirty = true;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          break;
        case 4:  // 5.0um - wejdź w szczegóły
          appState = STATE_PMS5003_PARTICLES_5_0;
          pmsScreenDirty = true;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          break;
        case 5:  // 10um - wejdź w szczegóły
          appState = STATE_PMS5003_PARTICLES_10_0;
          pmsScreenDirty = true;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
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
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          break;
        case 1:  // Buzzer
          appState = STATE_SETTINGS_BUZZER;
          settingsBuzzerMenuIndex = buzzerEnabled ? 0 : 1;
          if (s_callbacks.drawStats) s_callbacks.drawStats();
          break;
        case 2:  // Wyjście
          appState = STATE_MENU;
          if (s_callbacks.drawMenu) s_callbacks.drawMenu();
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
      if (s_callbacks.drawStats) s_callbacks.drawStats();
      return;
    }

    // --- LOGIKA MENU USTAWIEŃ BUZERA (włącz/wyłącz) ---
    if (appState == STATE_SETTINGS_BUZZER) {
      buzzerEnabled = (settingsBuzzerMenuIndex == 0);
      appState = STATE_SETTINGS;
      if (s_callbacks.drawStats) s_callbacks.drawStats();
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
      case STATE_STATS_RESOURCES_MENU:
        returnToStatsMenu(4);
        return;
      
      // --- Zasoby: ekrany szczegółowe -> powrót do menu zasobów ---
      case STATE_STATS_RESOURCES_RAM:
      case STATE_STATS_RESOURCES_CPU:
      case STATE_STATS_RESOURCES_FLASH:
        appState = STATE_STATS_RESOURCES_MENU;
        if (s_callbacks.drawStats) s_callbacks.drawStats();
        return;

      // --- PMS5003: ekrany szczegółowe -> powrót do menu PMS5003 ---
      case STATE_PMS5003_CF1_PM1:
      case STATE_PMS5003_CF1_PM25:
      case STATE_PMS5003_CF1_PM10:
        appState = STATE_PMS5003_CF1;
        pmsScreenDirty = true;
        if (s_callbacks.drawStats) s_callbacks.drawStats();
        return;

      case STATE_PMS5003_CF1:
        // Menu CF1 -> powrót do menu PMS5003
        appState = STATE_PMS5003;
        pmsScreenDirty = true;
        if (s_callbacks.drawStats) s_callbacks.drawStats();
        return;

      // --- PMS5003 ATM: ekrany szczegółowe -> powrót do menu ATM ---
      case STATE_PMS5003_ATM_PM1:
      case STATE_PMS5003_ATM_PM25:
      case STATE_PMS5003_ATM_PM10:
        appState = STATE_PMS5003_ATM;
        pmsScreenDirty = true;
        if (s_callbacks.drawStats) s_callbacks.drawStats();
        return;

      case STATE_PMS5003_ATM:
        // Menu ATM -> powrót do menu PMS5003
        appState = STATE_PMS5003;
        pmsScreenDirty = true;
        if (s_callbacks.drawStats) s_callbacks.drawStats();
        return;

      // --- PMS5003 PARTICLES: ekrany szczegółowe -> powrót do menu PARTICLES ---
      case STATE_PMS5003_PARTICLES_0_3:
      case STATE_PMS5003_PARTICLES_0_5:
      case STATE_PMS5003_PARTICLES_1_0:
      case STATE_PMS5003_PARTICLES_2_5:
      case STATE_PMS5003_PARTICLES_5_0:
      case STATE_PMS5003_PARTICLES_10_0:
        appState = STATE_PMS5003_PARTICLES;
        pmsScreenDirty = true;
        if (s_callbacks.drawStats) s_callbacks.drawStats();
        return;

      case STATE_PMS5003_PARTICLES:
        // Menu PARTICLES -> powrót do menu PMS5003
        appState = STATE_PMS5003;
        pmsScreenDirty = true;
        if (s_callbacks.drawStats) s_callbacks.drawStats();
        return;

      case STATE_PMS5003_TELEMETRY:
        // Telemetria -> powrót do menu PMS5003
        appState = STATE_PMS5003;
        pmsScreenDirty = true;
        if (s_callbacks.drawStats) s_callbacks.drawStats();
        return;

      case STATE_PMS5003:
        // Menu PMS5003 -> menu główne
        appState = STATE_MENU;
        if (s_callbacks.drawMenu) s_callbacks.drawMenu();
        return;

      // --- Ustawienia (Settings) -> powrót do menu głównego ---
      case STATE_SETTINGS_PMS5003:
      case STATE_SETTINGS_BUZZER:
        appState = STATE_SETTINGS;
        if (s_callbacks.drawStats) s_callbacks.drawStats();
        return;

      case STATE_SETTINGS:
        // Menu Ustawień -> menu główne
        appState = STATE_MENU;
        if (s_callbacks.drawMenu) s_callbacks.drawMenu();
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