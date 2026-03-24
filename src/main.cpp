#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <time.h>
#include <Esp.h>

#include "STM32_Data.h"
#include "LCDMirror.h"
#include "UI_Controller.h"
#include "Encoder.h"
#include "AppState.h"
#include "UI_Draw.h"
#include "WiFiSync.h"
#include "RTCService.h"

#include <sys/time.h>
#include "MQTTSync.h"
#include "StatsManager.h" 
#include "AudioBT.h" 
#include "ModeManager.h"
#include "RadioModeSwitch.h"
#include "PMS_Czujnik.h"
#include "ENS160AHT21Screen.h"
#include "ENS160AHT21Sensor.h"
#include "BMP280Sensor.h"
#include "LCDIcons.h"
#include "AlarmMelodies.h"
#include <cstring>
#include <DHT.h>
#include "TemperatureConfig.h"
#include "I2C_bus_shared.h"
#include <Preferences.h>

// ============================================================================
// STAŁE CZASOWE (zamiast magic numbers)
// ============================================================================
constexpr unsigned long CLOCK_TICK_MS       = 1000; // tykanie zegara co 1s
constexpr unsigned long ALARM_DURATION_MS   = 60000; // jak długo gra alarm
constexpr unsigned long STM32_UPDATE_MS     = 500;  // odświeżanie danych STM32
constexpr unsigned long STM32_TIMEOUT_MS    = 3000; // timeout połączenia STM32
constexpr unsigned long STOPER_DRAW_MS      = 100;  // odświeżanie stopera
constexpr unsigned long WIFI_RETRY_DELAY_MS = 500;  // próba połączenia WiFi
constexpr int           WIFI_MAX_RETRIES    = 20;   // max prób połączenia
constexpr unsigned long MSG_DISPLAY_MS      = 1500; // wyświetlanie komunikatów

// ============================================================================
// HOME LCD refresh policy
// ============================================================================
// Wymóg: ekran HOME (LCD + UART mirror) ma odświeżać się maks. 1x/s.
// Zamiast wołać drawHome() bezpośrednio z wielu miejsc, używamy bramki:
// - requestHomeRedraw(): zaznacza, że HOME wymaga odświeżenia
// - serviceHomeRedraw(): wykonuje drawHome() nie częściej niż co 1000ms
static bool          s_homeRedrawDirty      = true;
static unsigned long s_lastHomeRedrawMs    = 0;
static constexpr unsigned long HOME_REDRAW_MIN_INTERVAL_MS = 1000;

enum class HomeUiProfile : uint8_t {
  Minimal  = 0,
  Balanced = 1,
  Extreme  = 2,
};

enum class HomeOverlayPage : uint8_t {
  Time     = 0,
  Indoor   = 1,
  Outdoor  = 2,
  Extreme  = 3,
  Systems  = 4,
};

static HomeUiProfile   s_homeUiProfile       = HomeUiProfile::Minimal;
static HomeOverlayPage s_homeOverlay         = HomeOverlayPage::Time;
static uint8_t         s_homeOverlayIndex    = 0;
static unsigned long   s_homeOverlaySinceMs  = 0;
static unsigned long   s_homeIndoorAnimSinceMs = 0;
// Runtime-configurable overlay switch interval (ms). Persisted via Preferences as seconds.
unsigned long s_homeOverlaySwitchMs = 7000; // default 7s
int settingsRotationSec = 7;               // 1..10 seconds (user-facing)
int s_prevSettingsRotationSec = 7;         // used to restore on cancel
int s_prevSettingsUiScreenIndex = 0;        // used to restore UI screen selection on cancel

extern int settingsUiScreenIndex;

constexpr uint8_t BUZZER_PIN = 19;

enum class IntroPhase : uint8_t {
  Idle,
  Noise,
  AuthHold,
  FrameBuild,
  Reveal,
  FlashOn,
  FlashOff,
  SuccessReady,
  SuccessRiff,
  Done,
};

static void introPrintPaddedLine(uint8_t row, const char* text);
static void introPrintCentered(uint8_t row, const char* text);
static void introPrintBorderLine(uint8_t row, char leftCorner, char rightCorner);
static void introRenderNoiseFrame();
static void renderAuthIntroFrame();
static void renderTickingFrameBase();
static void renderTickingFrameText(uint8_t revealCount);

struct BootIntroState {
  IntroPhase phase = IntroPhase::Idle;
  unsigned long phaseStartedMs = 0;
  unsigned long lastNoiseMs = 0;
  unsigned long lastRevealMs = 0;
  unsigned long flashStartedMs = 0;
  unsigned long riffStartedMs = 0;
  uint8_t revealIndex = 0;
  uint8_t flashCount = 0;
  uint8_t riffIndex = 0;
  bool backlightOn = true;
};

static BootIntroState s_intro;

static void introPrintPaddedLine(uint8_t row, const char* text) {
  char line[21];
  const size_t length = strnlen(text, 20);
  memset(line, ' ', 20);
  memcpy(line, text, length);
  line[20] = '\0';
  LCD_SET(0, row);
  LCD_PRINT(line);
}

static void introPrintCentered(uint8_t row, const char* text) {
  constexpr size_t width = 20;
  const size_t length = strnlen(text, width);
  char line[21];
  memset(line, ' ', width);
  const size_t start = (width - length) / 2;
  memcpy(line + start, text, length);
  line[width] = '\0';
  LCD_SET(0, row);
  LCD_PRINT(line);
}

static void introPrintBorderLine(uint8_t row, char leftCorner, char rightCorner) {
  LCD_SET(0, row);
  LCD_PRINT(leftCorner);
  LCD_PRINT(F("=================="));
  LCD_PRINT(rightCorner);
}

static void introRenderNoiseFrame() {
  LCD_CLEAR();
  for (uint8_t row = 0; row < 4; ++row) {
    char line[21];
    for (uint8_t col = 0; col < 20; ++col) {
      static const char noiseChars[] = {'.', ':', '*', '#', '@', ' ', '/'};
      line[col] = noiseChars[random(0, (int)(sizeof(noiseChars) / sizeof(noiseChars[0])))];
    }
    line[20] = '\0';
    LCD_SET(0, row);
    LCD_PRINT(line);
  }
  LCD_DUMP();
}

static void renderAuthIntroFrame() {
  LCD_CLEAR();
  introPrintPaddedLine(0, "[ QUANTUM CORE OS ]");
  introPrintPaddedLine(1, "  AUTH: D.  MELCER  ");
  introPrintPaddedLine(2, "  AUTH: R. WOZNIAK  ");
  introPrintPaddedLine(3, "VERIFYING CREDENTIAL");
  LCD_DUMP();
}

static void renderTickingFrameBase() {
  LCD_CLEAR();
  introPrintBorderLine(0, '.', '.');
  LCD_SET(0, 1);
  LCD_PRINT(F("|                  |"));
  LCD_SET(0, 2);
  LCD_PRINT(F("|                  |"));
  introPrintBorderLine(3, '\'', '\'');
  LCD_DUMP();
}

static void renderTickingFrameText(uint8_t revealCount) {
  static const char kTicking[] = "TICKING";
  static const char kBomb[] = "BOMB";
  char row1[19];
  char row2[19];
  memset(row1, ' ', 18);
  memset(row2, ' ', 18);
  row1[18] = '\0';
  row2[18] = '\0';

  const uint8_t tickingVisible = revealCount < 7 ? revealCount : 7;
  const uint8_t bombVisible = revealCount > 7 ? (uint8_t)((revealCount - 7) < 4 ? (revealCount - 7) : 4) : 0;
  const uint8_t tickingStart = (18 - 7) / 2;
  const uint8_t bombStart = (18 - 4) / 2;

  memcpy(row1 + tickingStart, kTicking, tickingVisible);
  memcpy(row2 + bombStart, kBomb, bombVisible);

  introPrintBorderLine(0, '.', '.');
  LCD_SET(0, 1);
  LCD_PRINT(F("|"));
  LCD_PRINT(row1);
  LCD_PRINT(F("|"));
  LCD_SET(0, 2);
  LCD_PRINT(F("|"));
  LCD_PRINT(row2);
  LCD_PRINT(F("|"));
  introPrintBorderLine(3, '\'', '\'');
  LCD_DUMP();
}

static void introBegin() {
  randomSeed((uint32_t)micros());
  s_intro = BootIntroState{};
  s_intro.phase = IntroPhase::Noise;
  s_intro.phaseStartedMs = millis();
  s_intro.backlightOn = true;
}

static bool serviceEpicBootSequence() {
  if (s_intro.phase == IntroPhase::Idle || s_intro.phase == IntroPhase::Done) {
    return false;
  }

  const unsigned long nowMs = millis();
  switch (s_intro.phase) {
    case IntroPhase::Noise:
      if (s_intro.lastNoiseMs == 0 || (nowMs - s_intro.lastNoiseMs) >= 90UL) {
        introRenderNoiseFrame();
        s_intro.lastNoiseMs = nowMs;
      }
      if ((nowMs - s_intro.phaseStartedMs) >= 1800UL) {
        renderAuthIntroFrame();
        tone(BUZZER_PIN, 1760, 70);
        s_intro.phase = IntroPhase::AuthHold;
        s_intro.phaseStartedMs = nowMs;
      }
      break;

    case IntroPhase::AuthHold:
      if ((nowMs - s_intro.phaseStartedMs) >= 3000UL) {
        s_intro.phase = IntroPhase::FrameBuild;
        s_intro.phaseStartedMs = nowMs;
      }
      break;

    case IntroPhase::FrameBuild:
      renderTickingFrameBase();
      tone(BUZZER_PIN, 140, 120);
      s_intro.revealIndex = 0;
      s_intro.phase = IntroPhase::Reveal;
      s_intro.phaseStartedMs = nowMs;
      s_intro.lastRevealMs = nowMs;
      break;

    case IntroPhase::Reveal:
      if ((nowMs - s_intro.lastRevealMs) >= 200UL) {
        ++s_intro.revealIndex;
        renderTickingFrameText(s_intro.revealIndex);
        tone(BUZZER_PIN, (uint16_t)(145 + (s_intro.revealIndex * 14U)), 160);
        s_intro.lastRevealMs = nowMs;
      }
      if (s_intro.revealIndex >= 11) {
        s_intro.phase = IntroPhase::FlashOn;
        s_intro.flashStartedMs = nowMs;
        s_intro.flashCount = 0;
        s_intro.backlightOn = true;
        renderTickingFrameText(11);
        lcd.backlight();
      }
      break;

    case IntroPhase::FlashOn:
      if (!s_intro.backlightOn) {
        lcd.backlight();
        s_intro.backlightOn = true;
      }
      if ((nowMs - s_intro.flashStartedMs) >= 110UL) {
        s_intro.phase = IntroPhase::FlashOff;
        s_intro.flashStartedMs = nowMs;
      }
      break;

    case IntroPhase::FlashOff:
      if (s_intro.backlightOn) {
        lcd.noBacklight();
        s_intro.backlightOn = false;
      }
      if ((nowMs - s_intro.flashStartedMs) >= 90UL) {
        ++s_intro.flashCount;
        if (s_intro.flashCount >= 3) {
          lcd.backlight();
          s_intro.backlightOn = true;
          introPrintBorderLine(0, '.', '.');
          introPrintCentered(1, "SYSTEM READY");
          introPrintPaddedLine(2, "                  ");
          introPrintBorderLine(3, '\'', '\'');
          LCD_DUMP();
          tone(BUZZER_PIN, 523, 90);
          s_intro.phase = IntroPhase::SuccessReady;
          s_intro.phaseStartedMs = nowMs;
        } else {
          s_intro.phase = IntroPhase::FlashOn;
          s_intro.flashStartedMs = nowMs;
        }
      }
      break;

    case IntroPhase::SuccessReady:
      if ((nowMs - s_intro.phaseStartedMs) >= 300UL) {
        s_intro.phase = IntroPhase::SuccessRiff;
        s_intro.riffStartedMs = nowMs;
        s_intro.riffIndex = 0;
      }
      break;

    case IntroPhase::SuccessRiff: {
      static const uint16_t kNotes[] = {523, 659, 784, 1047, 1319};
      static const uint16_t kDurationsMs[] = {120, 100, 95, 85, 180};

      if (s_intro.riffIndex == 0) {
        tone(BUZZER_PIN, kNotes[0], kDurationsMs[0]);
        s_intro.riffStartedMs = nowMs;
        ++s_intro.riffIndex;
      } else if (s_intro.riffIndex < 5 && (nowMs - s_intro.riffStartedMs) >= kDurationsMs[s_intro.riffIndex - 1]) {
        tone(BUZZER_PIN, kNotes[s_intro.riffIndex], kDurationsMs[s_intro.riffIndex]);
        s_intro.riffStartedMs = nowMs;
        ++s_intro.riffIndex;
      } else if (s_intro.riffIndex >= 5 && (nowMs - s_intro.riffStartedMs) >= 220UL) {
        noTone(BUZZER_PIN);
        s_intro.phase = IntroPhase::Done;
      }
      break;
    }

    case IntroPhase::Done:
    case IntroPhase::Idle:
    default:
      return false;
  }

  return s_intro.phase != IntroPhase::Done;
}

Preferences s_prefs;
bool showEpicIntro = true;
int settingsEpicIntroIndex = 0;
int settingsEpicIntroMenuCount = 2;
const char* settingsEpicIntroItems[] = {"ON", "OFF"};

constexpr unsigned long SETUP_DELAY_MS      = 100;  // min delay for serial init
constexpr long UART_BAUD = 115200;
HardwareSerial& uart = Serial2;
#define DHT_PIN 4
#define DHT_TYPE DHT11
static uint32_t heapBaseline = 0;
static bool s_alarmMelodyDemoActive = false;
static unsigned long s_alarmMelodyDemoEndMs = 0;
int settingsUiScreenIndex = 0;
int settingsAlarmMelodyIndex = 0;
int s_prevSettingsAlarmMelodyIndex = 0;
constexpr uint8_t ENC_CLK = 25;
constexpr uint8_t ENC_DT  = 26;
constexpr uint8_t ENC_SW  = 27;
const char* const WIFI_SSID  = "Orange_Swiatlowod_98E2";
const char* const WIFI_PASS   = "x1Z6P(~8pry<St.";
const char* const NTP_SERVER  = "pool.ntp.org";
constexpr int BMP280_MENU_COUNT = 4;
int bmp280MenuCount = BMP280_MENU_COUNT;

void updateDHT();

// --- Menu Główne ---
int menuIndex = 0;
const char* menuItems[] = {
  "Ustaw czas",
  "Minutnik",
  "Stoper",
  "Budzik",
  "Statystyki",
  "Debug STM32",
  "PMS5003",
  "AHT21 + ENS160",
  "BMP280",
  "Temperatura",
  "Wilgotnosc",
  "Ustawienia",
  "Wyjscie",
  "Radio: Toggle"
};
constexpr int MENU_COUNT = 14;
int menuCount = MENU_COUNT;

// --- Menu Statystyk ---
int statsMenuIndex = 0;
const char* statsMenuItems[] = {
  "Kliki",
  "Kroki",
  "Temp min/max",
  "Wilg min/max",
  "Ram Free",
  "CPU",
  "Flash Free"
};
constexpr int STATS_MENU_COUNT = 7;
int statsMenuCount = STATS_MENU_COUNT;

// --- Menu Zasobów Systemu ---
int resourcesMenuIndex = 0;
const char* resourcesMenuItems[] = {
  "RAM Free",
  "CPU",
  "Flash Free"
};
constexpr int RESOURCES_MENU_COUNT = 3;
int resourcesMenuCount = RESOURCES_MENU_COUNT;

// --- Menu PMS5003 ---
int pms5003MenuIndex = 0;
const char* pms5003MenuItems[] = {
  "Tryb Fabryczny",
  "Tryb Atmosferyczny",
  "L.Czastek #/100cm3",
  "Telemetria",
};
constexpr int PMS5003_MENU_COUNT = 4;
int pms5003MenuCount = PMS5003_MENU_COUNT;

// --- Menu ENS160 + AHT21 ---
int ens160MenuIndex = 0;
const char* ens160MenuItems[] = {
  "AQI",
  "TVOC",
  "eCO2",
};
constexpr int ENS160_MENU_COUNT = 3;
int ens160MenuCount = ENS160_MENU_COUNT;

// --- Menu BMP280 ---
int bmp280MenuIndex = 0;
const char* bmp280MenuItems[] = {
  "Temperature",
  "Pressure",
  "Status",
  "Altitude"
};

// --- Menu PMS5003 CF=1 ---
int pms5003CF1MenuIndex = 0;
const char* pms5003CF1MenuItems[] = {
  "PM1.0",
  "PM2.5",
  "PM10"
};
constexpr int PMS5003_CF1_MENU_COUNT = 3;
int pms5003CF1MenuCount = PMS5003_CF1_MENU_COUNT;

// --- Menu PMS5003 ATM ---
int pms5003ATMMenuIndex = 0;
const char* pms5003ATMMenuItems[] = {
  "PM1.0",
  "PM2.5",
  "PM10"
};
constexpr int PMS5003_ATM_MENU_COUNT = 3;
int pms5003ATMMenuCount = PMS5003_ATM_MENU_COUNT;

// --- Menu PMS5003 PARTICLE COUNT ---
int pms5003ParticlesMenuIndex = 0;
const char* pms5003ParticlesMenuItems[] = {
  "0.3um",
  "0.5um",
  "1.0um",
  "2.5um",
  "5.0um",
  "10.0um"
};
constexpr int PMS5003_PARTICLES_MENU_COUNT = 6;
int pms5003ParticlesMenuCount = PMS5003_PARTICLES_MENU_COUNT;

// --- Menu Ustawień ---
int settingsMenuIndex = 0;
const char* settingsMenuItems[] = {
  "PMS5003",
  "Buzzer",
  "MQTT",
  "Alarmy",
  "Synchronizacja",
  "Rotacja Ekranu",
  "UI ekran",
  "Boot Intro",
  "Wyjscie"
};
constexpr int SETTINGS_MENU_COUNT = 9;
int settingsMenuCount = SETTINGS_MENU_COUNT;

// --- Menu: Synchronizacja ---
int settingsSyncMinutes = 60;
int s_prevSettingsSyncMin = 60;

// --- Menu Ustawienia UI EKRAN ---
const char* settingsUiScreenItems[] = {
  "Minimal",
  "Balanced",
  "Extreme"
};
constexpr int SETTINGS_UI_SCREEN_COUNT = 3;
int settingsUiScreenCount = SETTINGS_UI_SCREEN_COUNT;

// --- Menu Ustawienia PMS5003 ---
int settingsPmsMenuIndex = 0;
const char* settingsPmsMenuItems[] = {
  "Wlaczony",
  "Wylaczony"
};
constexpr int SETTINGS_PMS_MENU_COUNT = 2;
int settingsPmsMenuCount = SETTINGS_PMS_MENU_COUNT;

// --- Menu Ustawienia Buzera ---
int settingsBuzzerMenuIndex = 0;
const char* settingsBuzzerMenuItems[] = {
  "Wlaczony",
  "Wylaczony"
};
constexpr int SETTINGS_BUZZER_MENU_COUNT = 2;
int settingsBuzzerMenuCount = SETTINGS_BUZZER_MENU_COUNT;

// --- Menu Ustawienia MQTT ---
int settingsMqttMenuIndex = 0;
const char* settingsMqttMenuItems[] = {
  "Wlaczony",
  "Wylaczony"
};
constexpr int SETTINGS_MQTT_MENU_COUNT = 2;
int settingsMqttMenuCount = SETTINGS_MQTT_MENU_COUNT;

// --- PMS5003 Telemetry ---
uint16_t pms5003_errorCount_current = 0;
uint16_t pms5003_errorCount_total = 0;
uint16_t pms5003_bytesReceived = 0;
uint32_t pms5003_lastFrameTime = 0;
uint32_t pms5003_latency_ms = 0;

// --- PMS5003 Current Values ---
uint16_t pms5003_PM1_0_CF1 = 0;
uint16_t pms5003_PM2_5_CF1 = 0;
uint16_t pms5003_PM10_CF1 = 0;

uint16_t pms5003_PM1_0_ATM = 0;

static void requestHomeRedraw();

static int loadAlarmMelodyIndexFromPrefs() {
  String savedMelodyId = s_prefs.getString("alarmMelodyId", "");
  if (savedMelodyId.length() > 0) {
    int loadedIndex = AlarmMelodies::indexOfId(savedMelodyId.c_str());
    if (loadedIndex >= 0 && loadedIndex < AlarmMelodies::kCount) {
      return loadedIndex;
    }
  }

  int loadedIndex = (int)s_prefs.getUShort("alarmMelody", 0);
  if (loadedIndex < 0) loadedIndex = 0;
  if (loadedIndex >= AlarmMelodies::kCount) loadedIndex = AlarmMelodies::kCount - 1;
  return loadedIndex;
}

static uint8_t homeOverlayCountForProfile(HomeUiProfile profile) {
  switch (profile) {
    case HomeUiProfile::Minimal:
      return 2;
    case HomeUiProfile::Balanced:
      return 3;
    case HomeUiProfile::Extreme:
      return 5;
  }
  return 2;
}

static HomeOverlayPage homeOverlayPageFor(HomeUiProfile profile, uint8_t index) {
  switch (profile) {
    case HomeUiProfile::Minimal:
      return (index % 2 == 0) ? HomeOverlayPage::Time : HomeOverlayPage::Outdoor;
    case HomeUiProfile::Balanced:
      switch (index % 3) {
        case 0: return HomeOverlayPage::Time;
        case 1: return HomeOverlayPage::Indoor;
        default: return HomeOverlayPage::Outdoor;
      }
    case HomeUiProfile::Extreme:
      switch (index % 5) {
        case 0: return HomeOverlayPage::Time;
        case 1: return HomeOverlayPage::Indoor;
        case 2: return HomeOverlayPage::Outdoor;
        case 3: return HomeOverlayPage::Extreme;
        default: return HomeOverlayPage::Systems;
      }
  }
  return HomeOverlayPage::Time;
}

static void syncHomeOverlayToProfile(bool resetTimer) {
  const uint8_t overlayCount = homeOverlayCountForProfile(s_homeUiProfile);
  if (overlayCount == 0) {
    s_homeOverlayIndex = 0;
    s_homeOverlay = HomeOverlayPage::Time;
    s_homeOverlaySinceMs = 0;
    s_homeIndoorAnimSinceMs = 0;
    requestHomeRedraw();
    return;
  }

  if (s_homeOverlayIndex >= overlayCount) {
    s_homeOverlayIndex = 0;
  }

  s_homeOverlay = homeOverlayPageFor(s_homeUiProfile, s_homeOverlayIndex);
  if (resetTimer) {
    s_homeOverlaySinceMs = millis();
    s_homeIndoorAnimSinceMs = s_homeOverlay == HomeOverlayPage::Indoor ? s_homeOverlaySinceMs : 0;
  }
  requestHomeRedraw();
}

static void requestHomeRedraw() {
  s_homeRedrawDirty = true;
}

static void serviceHomeRedraw() {
  if (appState != STATE_HOME) return;
  if (!s_homeRedrawDirty) return;

  const unsigned long nowMs = millis();
  if (s_lastHomeRedrawMs != 0 && (nowMs - s_lastHomeRedrawMs) < HOME_REDRAW_MIN_INTERVAL_MS) {
    return;
  }

  s_lastHomeRedrawMs = nowMs;
  s_homeRedrawDirty  = false;

  switch (s_homeOverlay) {
    case HomeOverlayPage::Indoor:
      drawIndoorWeatherScreen();
      break;
    case HomeOverlayPage::Outdoor:
      drawAirScreen();
      break;
    case HomeOverlayPage::Extreme:
      drawExtremeEnvironmentScreen();
      break;
    case HomeOverlayPage::Systems:
      drawSystemResources();
      break;
    case HomeOverlayPage::Time:
    default:
      drawHome();
      break;
  }
}

static void drawHomeThrottled() {
  requestHomeRedraw();
  serviceHomeRedraw();
}

static void serviceHomeOverlayRotation() {
  if (appState != STATE_HOME) return;

  const unsigned long nowMs = millis();
  const uint8_t overlayCount = homeOverlayCountForProfile(s_homeUiProfile);
  if (overlayCount == 0) {
    return;
  }

  if (s_homeOverlaySinceMs == 0) {
    s_homeOverlayIndex = 0;
    s_homeOverlay = homeOverlayPageFor(s_homeUiProfile, s_homeOverlayIndex);
    s_homeOverlaySinceMs = nowMs;
    requestHomeRedraw();
    return;
  }

  if (nowMs - s_homeOverlaySinceMs >= s_homeOverlaySwitchMs) {
    s_homeOverlaySinceMs = nowMs;
    s_homeOverlayIndex = (uint8_t)((s_homeOverlayIndex + 1) % overlayCount);
    s_homeOverlay = homeOverlayPageFor(s_homeUiProfile, s_homeOverlayIndex);
    s_homeIndoorAnimSinceMs = s_homeOverlay == HomeOverlayPage::Indoor ? nowMs : 0;
    requestHomeRedraw();
    return;
  }
}

static void handleHomeEntryIfStateChanged() {
  static AppState lastState = STATE_HOME;
  if (appState == lastState) return;

  if (appState == STATE_HOME) {
    s_homeOverlayIndex = 0;
    syncHomeOverlayToProfile(true);
  }

  lastState = appState;
}

void setHomeUiProfile(uint8_t profileIndex) {
  if (profileIndex > static_cast<uint8_t>(HomeUiProfile::Extreme)) {
    profileIndex = 0;
  }

  s_homeUiProfile = static_cast<HomeUiProfile>(profileIndex);
  settingsUiScreenIndex = (int)profileIndex;
  syncHomeOverlayToProfile(true);
}

uint16_t pms5003_PM2_5_ATM = 0;
uint16_t pms5003_PM10_ATM = 0;

// --- Dane PMS5003 HISTORYCZNE (min/max) - TRYB CF=1 ---
uint16_t pms5003_PM1_0_CF1_MIN = 9999;  uint16_t pms5003_PM1_0_CF1_MAX = 0;
uint16_t pms5003_PM2_5_CF1_MIN = 9999;  uint16_t pms5003_PM2_5_CF1_MAX = 0;
uint16_t pms5003_PM10_CF1_MIN = 9999;   uint16_t pms5003_PM10_CF1_MAX = 0;

// --- Dane PMS5003 HISTORYCZNE (min/max) - TRYB ATMOSFERYCZNY ---
uint16_t pms5003_PM1_0_ATM_MIN = 9999;  uint16_t pms5003_PM1_0_ATM_MAX = 0;
uint16_t pms5003_PM2_5_ATM_MIN = 9999;  uint16_t pms5003_PM2_5_ATM_MAX = 0;
uint16_t pms5003_PM10_ATM_MIN = 9999;   uint16_t pms5003_PM10_ATM_MAX = 0;

// --- Dane PMS5003 LICZBA CZĄSTEK (#/100cm³) - BIEŻĄCE ---
uint16_t pms5003_particleCount_0_3 = 0;
uint16_t pms5003_particleCount_0_5 = 0;
uint16_t pms5003_particleCount_1_0 = 0;
uint16_t pms5003_particleCount_2_5 = 0;
uint16_t pms5003_particleCount_5_0 = 0;
uint16_t pms5003_particleCount_10_0 = 0;

// --- Dane PMS5003 LICZBA CZĄSTEK (min/max) ---
uint16_t pms5003_particleCount_0_3_MIN = 9999;  uint16_t pms5003_particleCount_0_3_MAX = 0;
uint16_t pms5003_particleCount_0_5_MIN = 9999;  uint16_t pms5003_particleCount_0_5_MAX = 0;
uint16_t pms5003_particleCount_1_0_MIN = 9999;  uint16_t pms5003_particleCount_1_0_MAX = 0;
uint16_t pms5003_particleCount_2_5_MIN = 9999;  uint16_t pms5003_particleCount_2_5_MAX = 0;
uint16_t pms5003_particleCount_5_0_MIN = 9999;  uint16_t pms5003_particleCount_5_0_MAX = 0;
uint16_t pms5003_particleCount_10_0_MIN = 9999; uint16_t pms5003_particleCount_10_0_MAX = 0;

// --- CPU Load Tracking ---
uint8_t cpuLoadPercent = 0;
uint8_t cpuCore0Percent = 0;
uint8_t cpuCore1Percent = 0;
static unsigned long lastCpuReadTime = 0;

// --- System Resources Tracking ---
uint32_t ramFreeBytes = 0;
uint32_t flashFreeBytes = 0;

// --- Ustawienia (Configuration settings) ---
bool pms5003Enabled = true;   // Czy czujnik PMS5003 jest włączony
bool buzzerEnabled = true;    // Czy buzzer jest włączony

// --- Budzik ---
int  alarmHour       = 7;
int  alarmMinute     = 0;
bool alarmEnabled    = false;
bool alarmRinging    = false;
unsigned long alarmStartTime  = 0;

// --- Multi-alarm storage ---
const int MAX_ALARMS = 8;
AlarmEntry alarms[MAX_ALARMS];
int alarmsCount = 0; // number of configured alarms
int alarmsMenuIndex = 0; // selection in list view
int selectedAlarmIndex = 0; // index for editing/deleting
int alarmEditCursor = 0; // 0=CZAS,1=STATUS,2=USUN

// --- Minutnik (Timer) ---
int  timerSetMinutes  = 0;    // ustawiane przez użytkownika
int  timerSetSeconds  = 0;
bool timerRunning     = false;
unsigned long timerStartMillis = 0;
unsigned long timerDurationMs  = 0;

int  timerSetHours    = 0;
int  timerUiCursor    = 0; // 0=CZAS, 1=PRESETY
int  timerPresetIndex = 1; // default highlight: 15m

// --- STM32 DANE (UART) ---
unsigned long lastSTM32Update       = 0;
unsigned long lastSTM32DataReceived = 0;
int  displayedBPM    = 0;
int  displayedSPO2   = 0;
bool stm32Connected  = false;

// --- Stoper ---
bool stoperRunning         = false;
unsigned long stoperStart  = 0;
unsigned long stoperElapsed = 0;
unsigned long lastStoperDraw = 0;

// --- Czas (HH:MM:SS) ---
int hours   = 12;
int minutes = 0;
int seconds = 0;
unsigned long lastTick = 0;
bool bootDiagReprinted = false;

// --- Flaga do przywrócenia czasu z RTC (po soft reset) ---
static bool timeRestored = false;

// DS3231 persistence: write system time to RTC after successful NTP sync
static bool rtcWritePending = false;
static unsigned long lastRtcWriteAttemptMillis = 0;
static unsigned long rtcWriteNotBeforeMillis = 0;
static uint8_t rtcWriteFailureCount = 0;
static time_t rtcPendingEpoch = 0;
static unsigned long lastSeenNtpSyncMillis = 0;

static const char* TZ_POLAND = "CET-1CEST,M3.5.0,M10.5.0/3";

static bool isSystemTimeValid() {
  // 2021-01-01 00:00:00 UTC
  return time(nullptr) >= 1609459200;
}

static unsigned long rtcComputeBackoffMs(uint8_t failures) {
  // 0->0ms, 1->1s, 2->2s, 3->5s, 4->10s, 5->20s, >=6->60s
  if (failures == 0) return 0;
  if (failures == 1) return 1000;
  if (failures == 2) return 2000;
  if (failures == 3) return 5000;
  if (failures == 4) return 10000;
  if (failures == 5) return 20000;
  return 60000;
}

static void scheduleRtcWriteFromSystemTime() {
  if (!isSystemTimeValid()) return;
  rtcPendingEpoch = time(nullptr);
  rtcWritePending = true;
  rtcWriteFailureCount = 0;

  // Give the loop a moment after SNTP update (and reduce chance of I2C collisions)
  rtcWriteNotBeforeMillis = millis() + 2000UL;
}

static void syncLocalClockFromSystemTime() {
  time_t now = time(nullptr);
  struct tm ti;
  localtime_r(&now, &ti);
  hours = ti.tm_hour;
  minutes = ti.tm_min;
  seconds = ti.tm_sec;
}

static void tryRestoreSystemTimeFromDs3231() {
  RTCService::Config rtcCfg;
  rtcCfg.wire = &Wire;
  rtcCfg.sdaPin = 21;
  rtcCfg.sclPin = 22;
  rtcCfg.i2cClockHz = 400000;
  rtcCfg.i2cTimeoutMs = 10;
  rtcCfg.i2cRetries = 2;
  rtcCfg.initI2cMaster = false; // Wire.begin() already done in setup()
  rtcCfg.enableI2cDiagnostics = true;

  const RTCService::Status st = RTCService::begin(rtcCfg);
  Serial.printf("[RTC] begin: %s\n", RTCService::statusToString(st));
  if (st != RTCService::Status::Ok) {
    return;
  }

  time_t epoch = 0;
  const RTCService::Status rd = RTCService::getEpoch(&epoch);
  Serial.printf("[RTC] getEpoch: %s epoch=%ld\n", RTCService::statusToString(rd), (long)epoch);
  if (rd != RTCService::Status::Ok) {
    return;
  }

  // Additional sanity: ignore clearly invalid timestamps.
  if (epoch < 1609459200) {
    Serial.println("[RTC] epoch too old/invalid; ignoring");
    return;
  }

  timeval tv;
  tv.tv_sec = epoch;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);

  lastTick = millis();
  syncLocalClockFromSystemTime();
  Serial.printf("[RTC] system time restored from DS3231 (local %02d:%02d:%02d)\n", hours, minutes, seconds);
}

static void handleRtcWriteIfPending() {
  if (!rtcWritePending) return;
  if (!RTCService::isReady()) return;
  if (!isSystemTimeValid()) return;

  const unsigned long nowMs = millis();

  if (nowMs < rtcWriteNotBeforeMillis) return;
  // Avoid hammering the device on repeated failures.
  if (lastRtcWriteAttemptMillis != 0 && (nowMs - lastRtcWriteAttemptMillis) < 1000UL) return;

  const time_t epochToWrite = (rtcPendingEpoch != 0) ? rtcPendingEpoch : time(nullptr);
  const RTCService::Status st = RTCService::setEpoch(epochToWrite, true);
  Serial.printf("[RTC] setEpoch: %s epoch=%ld\n", RTCService::statusToString(st), (long)epochToWrite);
  lastRtcWriteAttemptMillis = nowMs;

  if (st == RTCService::Status::Ok) {
    rtcWritePending = false;
    rtcPendingEpoch = 0;
    rtcWriteFailureCount = 0;
    rtcWriteNotBeforeMillis = 0;
    return;
  }

  rtcWriteFailureCount++;
  rtcWriteNotBeforeMillis = nowMs + rtcComputeBackoffMs(rtcWriteFailureCount);
}

// --- MQTT Mode Control ---
static bool mqtt_initialized = false;
static RadioModeSwitchState last_radio_mode = RADIO_STATE_WIFI;
bool mqttEnabled = true;

// --- DHT Sensor ---
DHT dht(DHT_PIN, DHT_TYPE);
bool dhtScreenDirty = true;
int  savedHours     = 0;
int  savedMinutes   = 0;
int  savedSeconds   = 0;
bool timeSaved      = false;

// Flaga do wymuszenia rysowania ekranów PMS5003 przy wejściu do podmenu
bool pmsScreenDirty = true;

// Odczyty DHT
float dhtTemperature = 0.0f; // compensated (used by app)
float dhtTemperatureRaw = 0.0f; // raw sensor reading for diagnostics
float dhtHumidity    = 0.0f;

// Stabilizacja odczytów
bool dhtReady = false;
unsigned long dhtLastRead = 0;
constexpr unsigned long DHT_READ_INTERVAL_MS = 2000;


// ============================================================================
// IMPLEMENTACJA FUNKCJI - LOGIKA ZEGARA
// ============================================================================

void tickClock() {
  if (appState == STATE_SET_TIME) return;

  const unsigned long nowMs = millis();

  if (isSystemTimeValid()) {
    if (nowMs - lastTick >= CLOCK_TICK_MS) {
      // align tick
      lastTick = nowMs - ((nowMs - lastTick) % CLOCK_TICK_MS);
      syncLocalClockFromSystemTime();
      if (appState != STATE_STOPER) updateSevenSeg();
      if (appState == STATE_HOME) requestHomeRedraw();
    }
  } else {
    // catch up missed ticks
    if (nowMs - lastTick >= CLOCK_TICK_MS) {
      int loops = 0;
      unsigned long now = nowMs;
      while (now - lastTick >= CLOCK_TICK_MS && loops < 60) {
        lastTick += CLOCK_TICK_MS;
        seconds++;
        if (seconds >= 60) {
          seconds = 0;
          minutes++;
          if (minutes >= 60) {
            minutes = 0;
            hours = (hours + 1) % 24;
          }
        }
        loops++;
      }
      if (appState != STATE_STOPER) updateSevenSeg();
      if (appState == STATE_HOME) requestHomeRedraw();
    }
  }

  // Check alarms (multi)
  if (!alarmRinging && seconds == 0 && alarmsCount > 0) {
    time_t now_t = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now_t, &timeinfo);
    int today = timeinfo.tm_yday;
    for (int i = 0; i < alarmsCount; ++i) {
      if (!alarms[i].enabled) continue;
      if (alarms[i].hour == hours && alarms[i].minute == minutes && alarms[i].lastTriggerDay != (uint16_t)today) {
        alarmRinging = true;
        alarmStartTime = millis();
        alarms[i].lastTriggerDay = (uint16_t)today;
        AlarmMelodies::start((uint8_t)settingsAlarmMelodyIndex, BUZZER_PIN);
        break;
      }
    }
  }

  // Timer expiry
  if (timerRunning) {
    unsigned long elapsed = millis() - timerStartMillis;
    if (elapsed >= timerDurationMs) {
      timerRunning = false;
      alarmRinging = true;
      alarmStartTime = millis();
      AlarmMelodies::start((uint8_t)settingsAlarmMelodyIndex, BUZZER_PIN);
      editState = EDIT_HOURS;
      timerStartMillis = 0;
      timerDurationMs = 0;
    }
  }
}


// ============================================================================
// IMPLEMENTACJA FUNKCJI - ALARM / BUZZER
// ============================================================================

void playAlarmMelody() {
  AlarmMelodies::service(BUZZER_PIN, millis());
}

void startAlarmMelodyDemo(uint8_t melodyIndex) {
  AlarmMelodies::start(melodyIndex, BUZZER_PIN);
  s_alarmMelodyDemoActive = true;
  s_alarmMelodyDemoEndMs = millis() + 10000UL;
}

void stopAlarmMelodyDemo() {
  if (!s_alarmMelodyDemoActive) {
    return;
  }

  AlarmMelodies::stop(BUZZER_PIN);
  s_alarmMelodyDemoActive = false;
  s_alarmMelodyDemoEndMs = 0;
}

// ============================================================================
// MONITOROWANIE ZASOBÓW SYSTEMU
// ============================================================================

void updateSystemResources() {
  unsigned long currentTime = millis();
  
  // Aktualizuj co ~1 sekundę
  if (currentTime - lastCpuReadTime < 1000) {
    return;
  }
  
  lastCpuReadTime = currentTime;
  
  // --- CPU Load ---
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t totalHeap = ESP.getHeapSize();
  
  uint32_t usedHeap = totalHeap - freeHeap;
  cpuLoadPercent = (usedHeap * 100) / totalHeap;
  
  if (cpuLoadPercent > 80) {
    cpuCore0Percent = cpuLoadPercent - 5;
    cpuCore1Percent = cpuLoadPercent;
  } else if (cpuLoadPercent > 60) {
    cpuCore0Percent = cpuLoadPercent + 5;
    cpuCore1Percent = cpuLoadPercent - 5;
  } else {
    cpuCore0Percent = cpuLoadPercent + 10;
    cpuCore1Percent = cpuLoadPercent;
  }
  
  if (cpuCore0Percent > 100) cpuCore0Percent = 100;
  if (cpuCore1Percent > 100) cpuCore1Percent = 100;
  
  // --- RAM Free ---
  ramFreeBytes = ESP.getFreeHeap();
  
  // --- Flash Free ---
  flashFreeBytes = ESP.getFreeSketchSpace();
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  Serial.begin(UART_BAUD);
  delay(SETUP_DELAY_MS);

  // Set TZ early so localtime_r() is correct even before WiFiSync begins.
  if (setenv("TZ", TZ_POLAND, 1) == 0) {
    tzset();
  }

  heapBaseline = ESP.getFreeHeap();
  Serial.printf("[diag] baseline_heap=%u\n", heapBaseline);
  ModeManager::logDiag("boot");

  // --- DHT Sensor Init ---
  dht.begin();

  // ===== Menedżer trybów (Wi-Fi/BT) - inicjalizuj WCZEŚNIE =====
  ModeManager::begin(&appState);

  s_prefs.begin("zegar", false);
  showEpicIntro = s_prefs.getBool("epicIntro", true);
  settingsEpicIntroIndex = showEpicIntro ? 0 : 1;

  // ===== RadioModeSwitch - inicjalizacja przełączania trybu =====
  // BĘDZIE NA KONIEC setup() po LCD i UI init!
  RadioModeSwitch::begin();

  // ===== Przywrócenie czasu z RTC WCZEŚNIE (jeśli zapisane) =====
  // Jeśli RadioModeSwitch zapisał czas przed restartem, zastosuj go natychmiast
  uint8_t early_rtc_hours = RadioModeSwitch::getRTCHours();
  uint8_t early_rtc_minutes = RadioModeSwitch::getRTCMinutes();
  uint8_t early_rtc_seconds = RadioModeSwitch::getRTCSeconds();
  if (early_rtc_hours > 0 || early_rtc_minutes > 0 || early_rtc_seconds > 0) {
    if (early_rtc_hours < 24 && early_rtc_minutes < 60 && early_rtc_seconds < 60) {
      hours = early_rtc_hours;
      minutes = early_rtc_minutes;
      seconds = early_rtc_seconds;
      lastTick = millis();
      Serial.printf("[main] Early restore time from RTC: %02d:%02d:%02d\n", hours, minutes, seconds);
      updateSevenSeg();
    } else {
      Serial.printf("[main] Early RTC time invalid: %02d:%02d:%02d - ignoring\n", early_rtc_hours, early_rtc_minutes, early_rtc_seconds);
      RadioModeSwitch::clearRTCTime();
    }
  }

#if UART_LCD_MIRROR
  lcdMirror.begin();
#endif
  lcdFrame.begin();

  // I2C initialization with explicit pins: SDA=21, SCL=22 (GPIO22 now free from I2S after fix)
  const bool i2cClockApplied = I2cShared::initMaster(&Wire, 21, 22, 400000);
  Serial.printf("[main] I2C clock readback: %lu Hz (%s)\n",
                (unsigned long)Wire.getClock(),
                i2cClockApplied ? "applied" : "fallback/mismatch");

  // ===== DS3231: restore system time early (before heavy UI/I2C traffic) =====
  tryRestoreSystemTimeFromDs3231();

  // Tighten hd44780 timings to near-datasheet values.
  lcd.setExecTimes(37, 1520);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcdFrame.syncToCurrentFrame();
  LCDIcons::loadPalette(lcd, LCDIcons::Palette::Home);

  if (showEpicIntro) {
    introBegin();
  }

  // --- Encoder init ---
  encoder_begin(ENC_CLK, ENC_DT, ENC_SW);

  // --- Buzzer setup ---
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  // --- 7-Segment setup ---
  initSevenSeg();

  // --- Inicjalizacja statystyk ---
  statsManager.begin();

  // --- STM32 setup ---
  STM32data_begin(uart, UART_BAUD, 16, 17);

  // --- PMS5003 Czujnik pyłu ---
  PMS5003Sensor::begin();
  ENS160AHT21Screen::resetRuntimeData();
  ENS160AHT21Sensor::begin();
  BMP280Sensor::begin();
  bmp280MenuCount = BMP280Sensor::menuItemCount();

  // ===== UI CONTROLLER =====
  UI_Callbacks callbacks;
  callbacks.drawHome            = drawHomeThrottled;
  callbacks.drawMenu            = drawMenu;
  callbacks.drawSetTime         = drawSetTime;
  callbacks.drawAlarm           = drawAlarm;
  callbacks.drawTimer           = drawTimer;
  callbacks.drawStoper          = drawStoper;
  callbacks.drawDebugSTM32      = drawDebugSTM32;
  callbacks.updateSevenSeg      = updateSevenSeg;
  callbacks.updateSevenSegStoper = updateSevenSegStoper;
  callbacks.drawStats           = drawStats;           // Callbacki do UI statystyk
  callbacks.drawSystemResources = drawSystemResources; // Zasoby systemu (RAM/FLASH)

  // Ensure first screen (boot/home) performs a full redraw once
  lcdFrame.forceFullRedrawOnce();
  ui_begin(callbacks);
  // ui_begin() calls drawHome callback already; keep HOME refresh centrally throttled.

  // Turn the backlight on only after the first valid frame is staged.
  lcd.backlight();

#if CORE_DEBUG_LEVEL > 0
  lcdFrame.reportTiming("startup");
#endif

  // Load persisted rotation interval (seconds) from NVS/Preferences if present
  settingsRotationSec = s_prefs.getUShort("homeOverlaySec", (uint16_t)settingsRotationSec);
  if (settingsRotationSec < 1) settingsRotationSec = 1;
  if (settingsRotationSec > 10) settingsRotationSec = 10;
  s_homeOverlaySwitchMs = (unsigned long)settingsRotationSec * 1000UL;
  s_prevSettingsRotationSec = settingsRotationSec;
  settingsAlarmMelodyIndex = loadAlarmMelodyIndexFromPrefs();
  s_prevSettingsAlarmMelodyIndex = settingsAlarmMelodyIndex;
  settingsUiScreenIndex = (int)s_prefs.getUShort("uiScreenMode", (uint16_t)settingsUiScreenIndex);
  if (settingsUiScreenIndex < 0) settingsUiScreenIndex = 0;
  if (settingsUiScreenIndex > 2) settingsUiScreenIndex = 2;
  setHomeUiProfile((uint8_t)settingsUiScreenIndex);
  s_prevSettingsUiScreenIndex = settingsUiScreenIndex;
  mqttEnabled = s_prefs.getBool("mqttEnabled", true);
  settingsMqttMenuIndex = mqttEnabled ? 0 : 1;
  // Load persisted alarms
  alarmsCount = s_prefs.getUShort("alarmCount", 0);
  if (alarmsCount < 0) alarmsCount = 0;
  if (alarmsCount > MAX_ALARMS) alarmsCount = MAX_ALARMS;
  for (int i = 0; i < alarmsCount; ++i) {
    char keyH[12]; char keyM[12]; char keyE[12];
    snprintf(keyH, sizeof(keyH), "a%dh", i);
    snprintf(keyM, sizeof(keyM), "a%dm", i);
    snprintf(keyE, sizeof(keyE), "a%de", i);
    alarms[i].hour = (uint8_t)s_prefs.getUShort(keyH, 7);
    alarms[i].minute = (uint8_t)s_prefs.getUShort(keyM, 0);
    alarms[i].enabled = s_prefs.getBool(keyE, true);
    alarms[i].lastTriggerDay = 0;
  }

  // ===== WiFi / NTP Sync =====
  WiFiSync::setTimeRefs(hours, minutes, seconds, lastTick);        // referencje do zmiennych czasu
  WiFiSync::setOnDone([]() {
    const unsigned long ntpSyncMs = WiFiSync::getLastNtpSyncTime();
    if (ntpSyncMs != 0 && ntpSyncMs != lastSeenNtpSyncMillis) {
      lastSeenNtpSyncMillis = ntpSyncMs;
      scheduleRtcWriteFromSystemTime();
    }
    drawHomeThrottled();
  });
  WiFiSync::begin(WIFI_SSID, WIFI_PASS, NTP_SERVER);

  // ===== MQTT Sync (Core 1) - initialized only in WiFi mode =====
  // MQTTSync will be initialized later in RadioModeSwitch::update() when WiFi mode is confirmed
  Serial.println("[main] MQTT will start when WiFi mode is active");

  // ===== RadioModeSwitch inicjalizuje się, ale faktyczna inicjalizacja WiFi/BT =====
  // będzie opóźniona w loop() aby zagwarantować że LCD jest w pełni gotowe
  
  // Synchronizuj radioMode ze stanem RadioModeSwitch na starcie
  if (RadioModeSwitch::getCurrentState() == RADIO_STATE_BT) {
    radioMode = BT_ONLY;
  } else {
    radioMode = WIFI_ONLY;
  }

  ModeManager::logDiag("after-setup-radio-ready");
}

// ============================================================================
// LOOP
// ============================================================================

void loop() {
  if (serviceEpicBootSequence()) {
    return;
  }

  if (!bootDiagReprinted && millis() >= 5000UL) {
    bootDiagReprinted = true;
    Serial.printf("[boot] serial alive, i2c=%lu Hz, heap=%u\n",
                  (unsigned long)Wire.getClock(),
                  ESP.getFreeHeap());
  }

  handleHomeEntryIfStateChanged();

  // --- Encoder handling ---
  while (true) {
    const EncoderEvent evt = encoder_update();
    if (evt == ENC_NONE) {
      break;
    }

    // === REGISTER STATS ===
    if (evt == ENC_CLICK || evt == ENC_LONG) {
      statsManager.registerClick();
    } else if (evt == ENC_LEFT) {
      statsManager.registerStepLeft();
    } else if (evt == ENC_RIGHT) {
      statsManager.registerStepRight();
    }

    // Pass event to UI (unless RadioModeSwitch is initializing)
    if (!RadioModeSwitch::isInitializing()) {
      ui_handleEvent(evt);
    }
  }

  // --- Stats update (save to NVS if needed) ---
  statsManager.update();

  // Rotacja zawartości HOME (co 3s) — non-blocking.
  serviceHomeOverlayRotation();

  // HOME LCD refresh (1Hz max, LCD + UART mirror)
  // tickClock() / callbacks mark HOME dirty; this performs the actual draw.
  serviceHomeRedraw();

  // --- System resources update ---
  updateSystemResources();

  // --- PMS5003 update ---
  PMS5003Sensor::update();

  // --- ENS160 + AHT21 update ---
  ENS160AHT21Sensor::update();

  // --- BMP280 update ---
  BMP280Sensor::update();

  // --- Stats screen refresh (live data update) ---
  static unsigned long lastStatsRedraw = 0;
  if ((appState == STATE_STATS_RESOURCES_CPU || appState == STATE_STATS_RESOURCES_RAM ||
       appState == STATE_STATS_RESOURCES_FLASH || appState == STATE_STATS_RESOURCES ||
       // PMS menus and all detail sub-states
       appState == STATE_PMS5003_CF1 || appState == STATE_PMS5003_CF1_PM1 || appState == STATE_PMS5003_CF1_PM25 || appState == STATE_PMS5003_CF1_PM10 ||
       appState == STATE_PMS5003_ATM || appState == STATE_PMS5003_ATM_PM1 || appState == STATE_PMS5003_ATM_PM25 || appState == STATE_PMS5003_ATM_PM10 ||
       appState == STATE_PMS5003_PARTICLES || appState == STATE_PMS5003_PARTICLES_0_3 || appState == STATE_PMS5003_PARTICLES_0_5 || appState == STATE_PMS5003_PARTICLES_1_0 || appState == STATE_PMS5003_PARTICLES_2_5 || appState == STATE_PMS5003_PARTICLES_5_0 || appState == STATE_PMS5003_PARTICLES_10_0 ||
      appState == STATE_PMS5003_TELEMETRY ||
      appState == STATE_ENS160_AHT21 || appState == STATE_ENS160_AHT21_SUMMARY || appState == STATE_ENS160_AHT21_GAS ||
      appState == STATE_ENS160_AHT21_GAS_AQI || appState == STATE_ENS160_AHT21_GAS_TVOC || appState == STATE_ENS160_AHT21_GAS_ECO2 ||
      appState == STATE_ENS160_AHT21_CLIMATE || appState == STATE_ENS160_AHT21_CLIMATE_TEMP || appState == STATE_ENS160_AHT21_CLIMATE_HUM ||
      appState == STATE_ENS160_AHT21_STATUS ||
      appState == STATE_BMP280 || appState == STATE_BMP280_TEMP || appState == STATE_BMP280_PRESSURE ||
      appState == STATE_BMP280_STATUS || appState == STATE_BMP280_ALTITUDE) &&
      millis() - lastStatsRedraw >= 1000) {
    lastStatsRedraw = millis();
    drawStats();
  }

  // --- Timer screen refresh (1s) ---
  static unsigned long lastTimerRedraw = 0;
  if (appState == STATE_TIMER && millis() - lastTimerRedraw >= 1000) {
    lastTimerRedraw = millis();
    if (timerRunning || editState == EDIT_DONE) {
      drawTimer();
    }
  }

  // --- Status diagnostics every 2s (disabled in BT mode to not affect audio) ---
  static unsigned long last_status_diag = 0;
  if (!ModeManager::isBtOn() && (millis() - last_status_diag >= 2000)) {
    last_status_diag = millis();
    static uint32_t last_heap = 0;
    uint32_t current_heap = ESP.getFreeHeap();
    int heap_delta = (int)current_heap - (int)last_heap;
    last_heap = current_heap;
    
    Serial.printf("[STATUS] WiFi:%s MQTT:%s BT:%s Heap:%u (%+d) Mode:%s\n",
      ModeManager::isWifiOn() ? "ON" : "OFF",
      mqtt_initialized ? "ON" : "OFF",
      ModeManager::isBtOn() ? "ON" : "OFF",
      current_heap,
      heap_delta,
      (RadioModeSwitch::getCurrentState() == RADIO_STATE_BT) ? "BT" : "WiFi");
  }

#if CORE_DEBUG_LEVEL > 0
  // Report LCD timing in 30-second windows so each print reflects the last
  // full measurement interval, not the startup path.
  static bool lcdTimingPrimed = false;
  static unsigned long lastLcdTimingReport = 0;
  constexpr unsigned long LCD_TIMING_WINDOW_MS = 30000UL;
  if (!lcdTimingPrimed && millis() >= LCD_TIMING_WINDOW_MS) {
    lcdFrame.resetStats();
    lcdTimingPrimed = true;
    lastLcdTimingReport = millis();
  }
  if (lcdTimingPrimed && (millis() - lastLcdTimingReport >= LCD_TIMING_WINDOW_MS)) {
    lastLcdTimingReport = millis();
    lcdFrame.reportTiming("runtime");
    lcdFrame.resetStats();
  }
#endif

  // --- Clock tick ---
  tickClock();

  // --- WiFi sync update ---
  WiFiSync::update();

  // --- Persist current system time to DS3231 when requested ---
  handleRtcWriteIfPending();

  // --- RadioModeSwitch update (delayed WiFi/BT init after startup) ---
  RadioModeSwitch::update();

  // --- MQTT Control (start/stop based on WiFi mode + user setting) ---
  RadioModeSwitchState current_radio_mode = RadioModeSwitch::getCurrentState();

  // Hard switch OFF: stop task and never publish.
  if (!mqttEnabled) {
    if (mqtt_initialized) {
      Serial.println("[main] MQTT disabled in settings -> stopping MQTT task");
      MQTTSync::stopCore1Task();
      mqtt_initialized = false;
    }
  }
  else if (current_radio_mode == RADIO_STATE_WIFI && !mqtt_initialized) {
    // Start MQTT only when WiFi is ACTUALLY connected (not just in WiFi mode)
    // Use periodic timer (every 2s) to avoid lock contention with WiFi.status() calls
    static unsigned long lastWiFiCheck = 0;
    if (millis() - lastWiFiCheck >= 2000) {
      lastWiFiCheck = millis();
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[main] WiFi connected! Activating MQTT");
        MQTTSync::begin(WIFI_SSID, WIFI_PASS);
        MQTTSync::startCore1Task();
        mqtt_initialized = true;
        last_radio_mode = RADIO_STATE_WIFI;
      }
    }
  } 
  else if (current_radio_mode == RADIO_STATE_BT && mqtt_initialized) {
    // Stop MQTT when switching to BT mode
    Serial.println("[main] Deactivating MQTT for BT mode");
    MQTTSync::stopCore1Task();
    mqtt_initialized = false;
    last_radio_mode = RADIO_STATE_BT;
  }

  // --- MQTT Update (publish sensor data only if enabled+initialized) ---
  if (mqttEnabled && mqtt_initialized) {
    static unsigned long lastMQTTPublish = 0;
    if (millis() - lastMQTTPublish >= 5000) {
      lastMQTTPublish = millis();
      MQTTSync::publishSensorData(dhtTemperature, (int)dhtHumidity, 1013);
    }
  }

  // --- Synchronizuj radioMode ze stanem RadioModeSwitch ---
  // Ważne: to zapewnia, że menu zawsze pokazuje prawidłowy stan
  if (RadioModeSwitch::getCurrentState() == RADIO_STATE_BT) {
    radioMode = BT_ONLY;
  } else {
    radioMode = WIFI_ONLY;
  }

  // --- Przywrócenie czasu z RTC (jeśli był soft reset z przełączeniem trybu) ---
  if (!timeRestored && !RadioModeSwitch::isInitializing()) {
    uint8_t rtc_hours = RadioModeSwitch::getRTCHours();
    uint8_t rtc_minutes = RadioModeSwitch::getRTCMinutes();
    uint8_t rtc_seconds = RadioModeSwitch::getRTCSeconds();
    
    // Sprawdź czy czas był zapisany (non-zero wartości)
    if (rtc_hours > 0 || rtc_minutes > 0 || rtc_seconds > 0) {
      // Waliduj zakresy: hours 0-23, minutes 0-59, seconds 0-59
      if (rtc_hours < 24 && rtc_minutes < 60 && rtc_seconds < 60) {
        hours = rtc_hours;
        minutes = rtc_minutes;
        seconds = rtc_seconds;
        lastTick = millis();  // Zresetuj tick timer

        Serial.printf("[main] Przywrócono czas z RTC: %02d:%02d:%02d\n", hours, minutes, seconds);
        updateSevenSeg();
        drawHomeThrottled();

        // Wyczyść RTC czas (one-time restoration)
        RadioModeSwitch::clearRTCTime();
      } else {
        // Nieprawidłowy zapis w RTC - zignoruj i wyczyść
        Serial.printf("[main] Ignoruję nieprawidłowy czas z RTC: %02d:%02d:%02d\n", rtc_hours, rtc_minutes, rtc_seconds);
        RadioModeSwitch::clearRTCTime();
      }
    }
    
    timeRestored = true;
  }

  // --- STM32 Debug State ---
  if (appState == STATE_DEBUG_STM32 &&
      millis() - lastSTM32Update >= STM32_UPDATE_MS) {
    lastSTM32Update = millis();
    STM32data_update();

    if (stmDataUpdated) {
      stmDataUpdated        = false;
      displayedBPM          = bpmNumber;
      displayedSPO2         = spo2Number;
      stm32Connected        = true;
      lastSTM32DataReceived = millis();
    } else if (millis() - lastSTM32DataReceived > STM32_TIMEOUT_MS) {
      stm32Connected = false;
      displayedBPM   = 0;
      displayedSPO2  = 0;
    }

    drawDebugSTM32();
  }

  // --- Alarm Ringing ---
  if (alarmRinging) {
    playAlarmMelody();
    if (millis() - alarmStartTime >= ALARM_DURATION_MS) {
      AlarmMelodies::stop(BUZZER_PIN);
      alarmRinging = false;
      alarmEnabled = false;
    }
  } else if (s_alarmMelodyDemoActive) {
    playAlarmMelody();
    if ((long)(millis() - s_alarmMelodyDemoEndMs) >= 0) {
      stopAlarmMelodyDemo();
    }
  }

  // --- Stopwatch Drawing ---
  if (appState == STATE_STOPER &&
      millis() - lastStoperDraw >= STOPER_DRAW_MS) {
    lastStoperDraw = millis();
    drawStoper();
  }

  // --- Diagnostyka BT (co 60s) ---
  static unsigned long lastBtCheck = 0;
  if (millis() - lastBtCheck > 60000) {
    lastBtCheck = millis();
    Serial.print("BT Connected: ");
    Serial.println(audioBT_isConnected() ? "TAK" : "NIE");
  }

  // --- DHT Sensor Update ---
  updateDHT();

  // --- Ekran temperatury/wilgotności ---
  if (appState == STATE_TEMPERATURE) {
    drawTemperature();
    showTemperature7Seg();
  }

  if (appState == STATE_HUMIDITY) {
    drawHumidity();
    showHumidity7Seg();
  }
}

// ============================================================================
// FUNKCJE DHT (TEMPERATURA/WILGOTNOŚĆ)
// ============================================================================

void updateDHT() {
  // NAPRAWA: DHT czytanie blokuje główny loop - wyłącz w trybie BT aby uniknąć zniekształceń audio
  if (ModeManager::isBtOn()) {
    return;  // DHT wyłączony w trybie BT - priorytet dla czystego audio
  }

  if (millis() - dhtLastRead < DHT_READ_INTERVAL_MS) return;
  dhtLastRead = millis();

  const float t = dht.readTemperature();  // ~2-3ms blokada
  const float h = dht.readHumidity();     // ~2-3ms blokada

  if (!isnan(t) && !isnan(h)) {
    dhtTemperatureRaw = t;
    dhtTemperature = dhtTemperatureRaw + TempConfig::TEMPERATURE_OFFSET_C;
    dhtHumidity    = h;
    statsManager.updateTemperature(dhtTemperature);
    statsManager.updateHumidity(h);

    if (!dhtReady) {
      dhtScreenDirty = true;
    }
    dhtReady = true;
  }
}
