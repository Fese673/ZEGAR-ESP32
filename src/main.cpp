#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <time.h>
#include <Esp.h>
#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#include "STM32_Data.h"
#include "LCDMirror.h"
#include "UI_Controller.h"
#include "Encoder.h"
#include "AppState.h"
#include "UI_Draw.h"
#include "WiFiSync.h"

#include <sys/time.h>
#include "MQTTSync.h"
#include "NetworkOrchestrator.h"
#include "StatsManager.h" 
#include "AudioBT.h" 
#include "ModeManager.h"
#include "RadioModeSwitch.h"
#include "PMS_Czujnik.h"
#include "ENS160AHT21Screen.h"
#include "ENS160AHT21Sensor.h"
#include "BMP280Screen.h"
#include "BMP280Sensor.h"
#include "LCDIcons.h"
#include "AlarmMelodyPrefs.h"
#include "ClockAlarmService.h"
#include "RtcSyncService.h"
#include "TelemetryComposer.h"
#include "HomeRuntime.h"
#include "AlarmTypes.h"
#include <cstring>
#include "SecretsConfig.h"
#include "BoardPins.h"
#include "i2c/SharedBus.h"
#include <Preferences.h>

// ============================================================================
// STAŁE CZASOWE (zamiast magic numbers)
// ============================================================================
constexpr unsigned long CLOCK_TICK_MS       = 1000; // tykanie zegara co 1s
constexpr unsigned long ALARM_DURATION_MS   = 60000; // jak długo gra alarm
constexpr unsigned long STM32_UPDATE_MS     = 500;  // odświeżanie danych STM32
constexpr unsigned long STM32_TIMEOUT_MS    = 3000; // timeout połączenia STM32
constexpr unsigned long STOPER_DRAW_MS      = 100;  // odświeżanie stopera

// Runtime-configurable overlay switch interval (ms). Persisted via Preferences as seconds.
int settingsRotationSec = 7;               // 1..10 seconds (user-facing)
int s_prevSettingsRotationSec = 7;         // used to restore on cancel
int s_prevSettingsUiScreenIndex = 0;        // used to restore UI screen selection on cancel

extern int settingsUiScreenIndex;

constexpr uint8_t BUZZER_PIN = BoardPins::kBuzzer;

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

static size_t boundedTextLength(const char* text, size_t maxLength) {
  if (text == nullptr) {
    return 0;
  }

  size_t length = 0;
  while (length < maxLength && text[length] != '\0') {
    ++length;
  }

  return length;
}

static void introPrintPaddedLine(uint8_t row, const char* text) {
  char line[21];
  const size_t length = boundedTextLength(text, 20);
  memset(line, ' ', 20);
  memcpy(line, text, length);
  line[20] = '\0';
  LCD_SET(0, row);
  LCD_PRINT(line);
}

static void introPrintCentered(uint8_t row, const char* text) {
  constexpr size_t width = 20;
  const size_t length = boundedTextLength(text, width);
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

constexpr unsigned long SETUP_DELAY_MS      = 100;  // cooperative startup wait for serial init
constexpr long UART_BAUD = 115200;
HardwareSerial& uart = Serial2;
static uint32_t heapBaseline = 0;
int settingsUiScreenIndex = 0;
int settingsAlarmMelodyIndex = 0;
int s_prevSettingsAlarmMelodyIndex = 0;
constexpr uint8_t ENC_CLK = BoardPins::kEncoderClk;
constexpr uint8_t ENC_DT  = BoardPins::kEncoderDt;
constexpr uint8_t ENC_SW  = BoardPins::kEncoderSw;

static String s_wifiSsid = PROJECT_WIFI_SSID;
static String s_wifiPass = PROJECT_WIFI_PASS;
static String s_ntpServer = PROJECT_NTP_SERVER;
static MQTTSync::Config s_mqttConfig;

constexpr int BMP280_MENU_COUNT = 4;
int bmp280MenuCount = BMP280_MENU_COUNT;

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
  "Ustawienia",
  "Wyjscie",
  "Tryb radia"
};
constexpr int MENU_COUNT = 12;
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

static void drawHomeThrottled() {
  HomeRuntime::markHomeDirty();
  HomeRuntime::serviceRedraw(appState);
}

void setHomeUiProfile(uint8_t profileIndex) {
  HomeRuntime::setProfile(profileIndex);
  settingsUiScreenIndex = (int)HomeRuntime::getProfile();
}

static bool looksLikePlaceholder(const String& value) {
  return value.length() == 0 || value.startsWith("REPLACE_");
}

static void loadNetworkConfigFromPreferences() {
  s_wifiSsid = s_prefs.isKey("wifiSsid") ? s_prefs.getString("wifiSsid", PROJECT_WIFI_SSID) : PROJECT_WIFI_SSID;
  s_wifiPass = s_prefs.isKey("wifiPass") ? s_prefs.getString("wifiPass", PROJECT_WIFI_PASS) : PROJECT_WIFI_PASS;
  s_ntpServer = s_prefs.isKey("ntpServer") ? s_prefs.getString("ntpServer", PROJECT_NTP_SERVER) : PROJECT_NTP_SERVER;

  s_mqttConfig.brokerAddress = s_prefs.isKey("mqttHost") ? s_prefs.getString("mqttHost", PROJECT_MQTT_BROKER) : PROJECT_MQTT_BROKER;
  s_mqttConfig.brokerPort = (uint16_t)s_prefs.getUShort("mqttPort", PROJECT_MQTT_PORT);
  s_mqttConfig.username = s_prefs.isKey("mqttUser") ? s_prefs.getString("mqttUser", PROJECT_MQTT_USERNAME) : PROJECT_MQTT_USERNAME;
  s_mqttConfig.password = s_prefs.isKey("mqttPass") ? s_prefs.getString("mqttPass", PROJECT_MQTT_PASSWORD) : PROJECT_MQTT_PASSWORD;
  s_mqttConfig.topic = s_prefs.isKey("mqttTopic") ? s_prefs.getString("mqttTopic", PROJECT_MQTT_TOPIC) : PROJECT_MQTT_TOPIC;
  s_mqttConfig.clientId = s_prefs.isKey("mqttClient") ? s_prefs.getString("mqttClient", PROJECT_MQTT_CLIENT_ID) : PROJECT_MQTT_CLIENT_ID;

  if (looksLikePlaceholder(s_wifiSsid) || looksLikePlaceholder(s_wifiPass)) {
    Serial.println("[config] WiFi credentials are placeholders; configure NVS keys wifiSsid/wifiPass.");
  }

  if (looksLikePlaceholder(s_mqttConfig.brokerAddress) ||
      looksLikePlaceholder(s_mqttConfig.username) ||
      looksLikePlaceholder(s_mqttConfig.password)) {
    Serial.println("[config] MQTT credentials are placeholders; configure NVS keys mqttHost/mqttUser/mqttPass.");
  }
}

void startAlarmMelodyDemo(uint8_t melodyIndex) {
  ClockAlarmService::startAlarmMelodyDemo(melodyIndex, BUZZER_PIN);
}

void stopAlarmMelodyDemo() {
  ClockAlarmService::stopAlarmMelodyDemo(BUZZER_PIN);
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

// --- MQTT Mode Control ---
bool mqttEnabled = true;

// Flaga do wymuszenia rysowania ekranów PMS5003 przy wejściu do podmenu
bool pmsScreenDirty = true;

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
#ifdef ARDUINO_ARCH_ESP32
  vTaskDelay(pdMS_TO_TICKS(SETUP_DELAY_MS));
#else
  const unsigned long setupWaitUntilMs = millis() + SETUP_DELAY_MS;
  while ((long)(millis() - setupWaitUntilMs) < 0) {
    yield();
  }
#endif

  RtcSyncService::applyTimezone();

  heapBaseline = ESP.getFreeHeap();
  Serial.printf("[diag] baseline_heap=%u\n", heapBaseline);
  ModeManager::logDiag("boot");

  // ===== Menedżer trybów (Wi-Fi/BT) - inicjalizuj WCZEŚNIE =====
  ModeManager::begin(&appState);

  // --- 7-Segment setup ---
  // Inicjalizuj przed wczesnym przywróceniem czasu, żeby nie pisać na GPIO przed pinMode().
  initSevenSeg();

  s_prefs.begin("zegar", false);
  loadNetworkConfigFromPreferences();
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

  // I2C initialization from centralized board pin mapping.
  const bool i2cClockApplied = I2cShared::initMaster(&Wire,
                                                     BoardPins::kI2cSda,
                                                     BoardPins::kI2cScl,
                                                     BoardPins::kI2cClockHz);
  Serial.printf("[main] I2C clock readback: %lu Hz (%s)\n",
                (unsigned long)Wire.getClock(),
                i2cClockApplied ? "applied" : "fallback/mismatch");

  // ===== DS3231: restore system time early (before heavy UI/I2C traffic) =====
  RtcSyncService::tryRestoreSystemTimeFromDs3231(hours, minutes, seconds, lastTick);

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

  // --- Inicjalizacja statystyk ---
  statsManager.begin();

  // --- STM32 setup ---
  STM32data_begin(uart, UART_BAUD, BoardPins::kStm32UartRx, BoardPins::kStm32UartTx);

  // --- PMS5003 Czujnik pyłu ---
  PMS5003Sensor::begin();
  ENS160AHT21Screen::resetRuntimeData();
  ENS160AHT21Sensor::begin();
  BMP280Sensor::begin();
  bmp280MenuCount = BMP280Sensor::menuItemCount();

  HomeRuntime::DrawCallbacks homeDrawCallbacks;
  homeDrawCallbacks.drawHome = drawHome;
  homeDrawCallbacks.drawIndoorWeather = drawIndoorWeatherScreen;
  homeDrawCallbacks.drawOutdoorAir = drawAirScreen;
  homeDrawCallbacks.drawExtremeEnvironment = drawExtremeEnvironmentScreen;
  homeDrawCallbacks.drawSystemResources = drawSystemResources;
  homeDrawCallbacks.drawExtremeAlgorithms = drawExtremeAlgorithmScreen;
  HomeRuntime::begin(homeDrawCallbacks, (uint8_t)settingsUiScreenIndex, (uint8_t)settingsRotationSec);

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
  callbacks.setHomeUiProfile    = setHomeUiProfile;

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
  HomeRuntime::setOverlayIntervalSeconds((uint8_t)settingsRotationSec);
  s_prevSettingsRotationSec = settingsRotationSec;
  settingsAlarmMelodyIndex = AlarmMelodyPrefs::loadIndex(s_prefs);
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
    RtcSyncService::noteNtpSync(ntpSyncMs);
    drawHomeThrottled();
  });
  WiFiSync::begin(s_wifiSsid.c_str(), s_wifiPass.c_str(), s_ntpServer.c_str());

  MQTTSync::configure(s_mqttConfig);

  // ===== Central orchestration for WiFi/Radio/MQTT =====
  NetworkOrchestrator::Config netCfg;
  netCfg.wifiSsid = s_wifiSsid.c_str();
  netCfg.wifiPass = s_wifiPass.c_str();
  netCfg.wifiStatusCheckMs = 2000UL;
  NetworkOrchestrator::begin(netCfg);
  NetworkOrchestrator::setMqttEnabled(mqttEnabled);
  Serial.println("[main] Network orchestrator armed (WiFi/Radio/MQTT)");

  // ===== RadioModeSwitch inicjalizuje się, ale faktyczna inicjalizacja WiFi/BT =====
  // będzie opóźniona w loop() aby zagwarantować że LCD jest w pełni gotowe
  
  // Synchronizuj radioMode ze stanem RadioModeSwitch na starcie
  if (NetworkOrchestrator::getCurrentRadioState() == RADIO_STATE_BT) {
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

  HomeRuntime::handleHomeEntryIfStateChanged(appState);

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
  HomeRuntime::serviceOverlayRotation(appState);

  // HOME LCD refresh (1Hz max, LCD + UART mirror)
  // tickClock() / callbacks mark HOME dirty; this performs the actual draw.
  HomeRuntime::serviceRedraw(appState);

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
      NetworkOrchestrator::isMqttInitialized() ? "ON" : "OFF",
      ModeManager::isBtOn() ? "ON" : "OFF",
      current_heap,
      heap_delta,
      (NetworkOrchestrator::getCurrentRadioState() == RADIO_STATE_BT) ? "BT" : "WiFi");
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
  ClockAlarmService::tickClock(CLOCK_TICK_MS, BUZZER_PIN);

  // --- Central comms orchestration (WiFi + RadioModeSwitch + MQTT) ---
  NetworkOrchestrator::setMqttEnabled(mqttEnabled);
  NetworkOrchestrator::update();

  // --- Persist current system time to DS3231 when requested ---
  RtcSyncService::processPendingWrite();

  // --- MQTT Update (publish sensor data only if enabled+initialized) ---
  if (mqttEnabled && NetworkOrchestrator::isMqttInitialized()) {
    static unsigned long lastMQTTPublish = 0;
    if (millis() - lastMQTTPublish >= 5000) {
      lastMQTTPublish = millis();
      TelemetryComposer::Sample sample;
      TelemetryComposer::buildMqttTelemetrySample(sample);
      MQTTSync::publishSensorData(sample.temperatureC,
                                  sample.humidityPct,
                                  sample.pressureHpa,
                                  sample.aqi,
                                  sample.tvoc,
                                  sample.eco2);
    }
  }

  // --- Synchronizuj radioMode ze stanem RadioModeSwitch ---
  // Ważne: to zapewnia, że menu zawsze pokazuje prawidłowy stan
  if (NetworkOrchestrator::getCurrentRadioState() == RADIO_STATE_BT) {
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
  ClockAlarmService::serviceAlarmPlayback(BUZZER_PIN, ALARM_DURATION_MS);

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

}
