#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <time.h>
#include <Esp.h>
#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
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
#include "RuntimeTelemetry.h"
#include "RamTelemetry.h"
#include "HomeRuntime.h"
#include "AlarmRuntime.h"
#include "AppSettings.h"
#include "UIState.h"
#include <cstring>
#include "AppLog.h"
#include "SecretsConfig.h"
#include "BoardPins.h"
#include "i2c/SharedBus.h"
#include <Preferences.h>

// Log format standard:
//  - use LOG_I / LOG_W / LOG_E / LOG_D for application logs
//  - every line must start with [TAG][LEVEL]
//  - keep messages in English and prefer key=val fields

// ============================================================================
// STAŁE CZASOWE (zamiast magic numbers)
// ============================================================================
constexpr unsigned long CLOCK_TICK_MS       = 1000; // tykanie zegara co 1s
constexpr unsigned long ALARM_DURATION_MS   = 60000; // jak długo gra alarm
constexpr unsigned long STM32_UPDATE_MS     = 500;  // odświeżanie danych STM32
constexpr unsigned long STM32_TIMEOUT_MS    = 3000; // timeout połączenia STM32
constexpr unsigned long STOPER_DRAW_MS      = 100;  // odświeżanie stopera
constexpr unsigned long BASELINE_REPORT_WINDOW_MS = 10000; // okno telemetrii etapu 0

namespace {

AppSettings::State& appSettings = AppSettings::mutableState();
UIState::State& uiState = UIState::mutableState();
AlarmRuntime::State& alarmRuntime = AlarmRuntime::mutableState();

int& settingsRotationSec = appSettings.homeOverlaySeconds;
int& settingsUiScreenIndex = appSettings.homeUiProfile;
int& settingsAlarmMelodyIndex = appSettings.alarmMelodyIndex;
bool& mqttEnabled = appSettings.mqttEnabled;
bool& showEpicIntro = appSettings.showEpicIntro;

int& s_prevSettingsRotationSec = uiState.prevSettingsRotationSec;
int& s_prevSettingsUiScreenIndex = uiState.prevSettingsUiScreenIndex;
int& s_prevSettingsAlarmMelodyIndex = uiState.prevSettingsAlarmMelodyIndex;
int& s_prevSettingsSyncMin = uiState.prevSettingsSyncMin;

int& settingsMqttMenuIndex = uiState.settingsMqttMenu.index;
int& settingsEpicIntroIndex = uiState.settingsBootIntroMenu.index;

int& alarmHour = alarmRuntime.alarmHour;
int& alarmMinute = alarmRuntime.alarmMinute;
bool& alarmEnabled = alarmRuntime.alarmEnabled;
bool& alarmRinging = alarmRuntime.alarmRinging;
unsigned long& alarmStartTime = alarmRuntime.alarmStartTime;
AlarmEntry (&alarms)[AlarmRuntime::kMaxAlarms] = alarmRuntime.alarms;
int& alarmsCount = alarmRuntime.alarmsCount;

}  // namespace

constexpr uint8_t BUZZER_PIN = BoardPins::kBuzzer;

static void lcdBacklightSafe() {
  if (I2cShared::lock(LCD_I2C_LOCK_TIMEOUT_MS)) {
    lcd.backlight();
    I2cShared::unlock();
  }
}

static void lcdNoBacklightSafe() {
  if (I2cShared::lock(LCD_I2C_LOCK_TIMEOUT_MS)) {
    lcd.noBacklight();
    I2cShared::unlock();
  }
}

static void lcdClearSafe() {
  if (I2cShared::lock(LCD_I2C_LOCK_TIMEOUT_MS)) {
    lcd.clear();
    I2cShared::unlock();
  }
}

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

static constexpr char TAG_MAIN[] = "MAIN";
static constexpr char TAG_I2C[] = "I2C";
static constexpr char TAG_RAM[] = "RAM";
static constexpr char TAG_BASELINE[] = "BASELINE";
static constexpr char TAG_NET[] = "NET";
static constexpr char TAG_BT[] = "BT";

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
        lcdBacklightSafe();
      }
      break;

    case IntroPhase::FlashOn:
      if (!s_intro.backlightOn) {
        lcdBacklightSafe();
        s_intro.backlightOn = true;
      }
      if ((nowMs - s_intro.flashStartedMs) >= 110UL) {
        s_intro.phase = IntroPhase::FlashOff;
        s_intro.flashStartedMs = nowMs;
      }
      break;

    case IntroPhase::FlashOff:
      if (s_intro.backlightOn) {
        lcdNoBacklightSafe();
        s_intro.backlightOn = false;
      }
      if ((nowMs - s_intro.flashStartedMs) >= 90UL) {
        ++s_intro.flashCount;
        if (s_intro.flashCount >= 3) {
          lcdBacklightSafe();
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

constexpr unsigned long SETUP_DELAY_MS      = 100;  // cooperative startup wait for serial init
constexpr long UART_BAUD = 921600;
HardwareSerial& uart = Serial2;
static uint32_t heapBaseline = 0;
constexpr uint8_t ENC_CLK = BoardPins::kEncoderClk;
constexpr uint8_t ENC_DT  = BoardPins::kEncoderDt;
constexpr uint8_t ENC_SW  = BoardPins::kEncoderSw;

static String s_wifiSsid = PROJECT_WIFI_SSID;
static String s_wifiPass = PROJECT_WIFI_PASS;
static String s_ntpServer = PROJECT_NTP_SERVER;
static MQTTSync::Config s_mqttConfig;

// runtime menu counts are owned by UIState and initialized in ui_begin()

static void drawHomeThrottled() {
  HomeRuntime::markHomeDirty();
  HomeRuntime::serviceRedraw(appState);
}

void setHomeUiProfile(uint8_t profileIndex) {
  HomeRuntime::setProfile(profileIndex);
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
    LOG_W(TAG_MAIN, "WiFi credentials are placeholders config_keys=wifiSsid,wifiPass");
  }

  if (looksLikePlaceholder(s_mqttConfig.brokerAddress) ||
      looksLikePlaceholder(s_mqttConfig.username) ||
      looksLikePlaceholder(s_mqttConfig.password)) {
    LOG_W(TAG_MAIN, "MQTT credentials are placeholders config_keys=mqttHost,mqttUser,mqttPass");
  }
}

// --- Heap Usage Tracking (used by system resources view) ---
uint8_t heapUsagePercent = 0;
uint8_t heapUsageCore0Percent = 0;
uint8_t heapUsageCore1Percent = 0;
static unsigned long lastCpuReadTime = 0;

// --- System Resources Tracking ---
uint32_t ramFreeBytes = 0;
uint32_t ramTotalBytes = 0;
uint32_t ramLargestBlockBytes = 0;
uint32_t ramMinFreeBytes = 0;
uint32_t ramDmaFreeBytes = 0;
uint32_t flashFreeBytes = 0;

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
int  displayedBPM    = 0;
int  displayedSPO2   = 0;
bool stm32Connected  = false;

// --- Stoper ---
bool stoperRunning         = false;
unsigned long stoperStart  = 0;
unsigned long stoperElapsed = 0;

// --- Czas (HH:MM:SS) ---
int hours   = 12;
int minutes = 0;
int seconds = 0;
unsigned long lastTick = 0;

struct MainRuntimeState {
  bool bootDiagReprinted = false;
  unsigned long lastSTM32Update = 0;
  unsigned long lastSTM32DataReceived = 0;
  unsigned long lastStoperDraw = 0;
};

static MainRuntimeState s_runtimeState;

struct LoopBaselineTelemetry {
  unsigned long windowStartedMs = 0;
  unsigned long loopCount = 0;
  uint32_t lastLoopStartUs = 0;
  uint32_t lastLoopPeriodUs = 0;
  uint32_t minLoopPeriodUs = 0xFFFFFFFFUL;
  uint32_t maxLoopPeriodUs = 0;
  uint32_t maxLoopJitterUs = 0;
  uint32_t maxLoopBodyUs = 0;
  uint32_t maxUiRefreshUs = 0;
  uint32_t maxMqttPublishUs = 0;
  uint32_t maxNetworkUpdateUs = 0;
  uint32_t minFreeHeapBytes = 0xFFFFFFFFUL;
};

static LoopBaselineTelemetry s_loopBaseline;

static uint32_t deltaUs(uint32_t startUs, uint32_t endUs) {
  return (uint32_t)(endUs - startUs);
}

static uint32_t absDiffU32(uint32_t a, uint32_t b) {
  return (a >= b) ? (a - b) : (b - a);
}

static void recordMaxU32(uint32_t& target, uint32_t sample) {
  if (sample > target) {
    target = sample;
  }
}

static void baselineResetWindow(unsigned long nowMs) {
  s_loopBaseline.windowStartedMs = nowMs;
  s_loopBaseline.loopCount = 0;
  s_loopBaseline.minLoopPeriodUs = 0xFFFFFFFFUL;
  s_loopBaseline.maxLoopPeriodUs = 0;
  s_loopBaseline.maxLoopJitterUs = 0;
  s_loopBaseline.maxLoopBodyUs = 0;
  s_loopBaseline.maxUiRefreshUs = 0;
  s_loopBaseline.maxMqttPublishUs = 0;
  s_loopBaseline.maxNetworkUpdateUs = 0;
  s_loopBaseline.minFreeHeapBytes = 0xFFFFFFFFUL;
}

static void baselineOnLoopStart(unsigned long nowMs, uint32_t nowUs) {
  if (s_loopBaseline.windowStartedMs == 0) {
    baselineResetWindow(nowMs);
  }

  if (s_loopBaseline.lastLoopStartUs != 0) {
    const uint32_t periodUs = deltaUs(s_loopBaseline.lastLoopStartUs, nowUs);
    if (periodUs < s_loopBaseline.minLoopPeriodUs) {
      s_loopBaseline.minLoopPeriodUs = periodUs;
    }
    if (periodUs > s_loopBaseline.maxLoopPeriodUs) {
      s_loopBaseline.maxLoopPeriodUs = periodUs;
    }
    if (s_loopBaseline.lastLoopPeriodUs != 0) {
      const uint32_t jitterUs = absDiffU32(periodUs, s_loopBaseline.lastLoopPeriodUs);
      if (jitterUs > s_loopBaseline.maxLoopJitterUs) {
        s_loopBaseline.maxLoopJitterUs = jitterUs;
      }
    }
    s_loopBaseline.lastLoopPeriodUs = periodUs;
  }

  s_loopBaseline.lastLoopStartUs = nowUs;
  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < s_loopBaseline.minFreeHeapBytes) {
    s_loopBaseline.minFreeHeapBytes = freeHeap;
  }
}

static void baselineOnLoopEnd(unsigned long nowMs, uint32_t loopStartUs, uint32_t loopEndUs) {
  recordMaxU32(s_loopBaseline.maxLoopBodyUs, deltaUs(loopStartUs, loopEndUs));
  ++s_loopBaseline.loopCount;

  if ((nowMs - s_loopBaseline.windowStartedMs) < BASELINE_REPORT_WINDOW_MS) {
    return;
  }

  const uint32_t minPeriodUs = (s_loopBaseline.minLoopPeriodUs == 0xFFFFFFFFUL) ? 0UL : s_loopBaseline.minLoopPeriodUs;
  const uint32_t minHeapBytes = (s_loopBaseline.minFreeHeapBytes == 0xFFFFFFFFUL) ? ESP.getFreeHeap() : s_loopBaseline.minFreeHeapBytes;
    LOG_I(TAG_BASELINE,
      "Window complete window_ms=%lu loops=%lu loop_us_min=%lu loop_us_max=%lu loop_us_jitter=%lu body_max_us=%lu ui_max_us=%lu net_max_us=%lu mqtt_max_us=%lu heap_min_b=%lu",
      (unsigned long)(nowMs - s_loopBaseline.windowStartedMs),
      s_loopBaseline.loopCount,
      (unsigned long)minPeriodUs,
      (unsigned long)s_loopBaseline.maxLoopPeriodUs,
      (unsigned long)s_loopBaseline.maxLoopJitterUs,
      (unsigned long)s_loopBaseline.maxLoopBodyUs,
      (unsigned long)s_loopBaseline.maxUiRefreshUs,
      (unsigned long)s_loopBaseline.maxNetworkUpdateUs,
      (unsigned long)s_loopBaseline.maxMqttPublishUs,
      (unsigned long)minHeapBytes);

  baselineResetWindow(nowMs);
}

struct RuntimeContext {
  int& hours;
  int& minutes;
  int& seconds;
  unsigned long& lastTick;
  bool& mqttEnabled;
  int& settingsRotationSec;
  int& settingsUiScreenIndex;
  int& settingsMqttMenuIndex;
  int& settingsAlarmMelodyIndex;
  int& prevSettingsAlarmMelodyIndex;
  int& prevSettingsRotationSec;
  int& prevSettingsUiScreenIndex;
  int& alarmsCount;
  MainRuntimeState& state;
};

static RuntimeContext makeRuntimeContext() {
  return RuntimeContext{
      hours,
      minutes,
      seconds,
      lastTick,
      mqttEnabled,
      settingsRotationSec,
      settingsUiScreenIndex,
      settingsMqttMenuIndex,
      settingsAlarmMelodyIndex,
      s_prevSettingsAlarmMelodyIndex,
      s_prevSettingsRotationSec,
      s_prevSettingsUiScreenIndex,
      alarmsCount,
      s_runtimeState,
  };
}

static bool restoreRtcHandoffTime(RuntimeContext& ctx, unsigned long nowMs, bool refreshHomeUi);

static void initCoreHardware();
static void initPersistenceAndConfig(RuntimeContext& ctx);
static void initUiAndInput();
static void initSensors(RuntimeContext& ctx);
static void initComms(RuntimeContext& ctx);
static void finalizeStartup(RuntimeContext& ctx);

static void serviceBootDiagnostics(RuntimeContext& ctx, unsigned long nowMs);
static void serviceInputAndUiEvents(RuntimeContext& ctx, unsigned long nowMs);
static void serviceUiRefreshPreSensors(RuntimeContext& ctx, unsigned long nowMs);
static void serviceSensors(RuntimeContext& ctx, unsigned long nowMs);
static void serviceUiRefresh(RuntimeContext& ctx, unsigned long nowMs);
static void serviceDiagnostics(RuntimeContext& ctx, unsigned long nowMs);
static void serviceComms(RuntimeContext& ctx, unsigned long nowMs);
static void serviceStm32AndStopwatch(RuntimeContext& ctx, unsigned long nowMs);
static void serviceDiagnosticsTail(RuntimeContext& ctx, unsigned long nowMs);

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
  
  // --- Heap usage ratio (historically shown on CPU screen) ---
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t totalHeap = ESP.getHeapSize();

  uint32_t usedHeap = totalHeap - freeHeap;
  if (totalHeap == 0) {
    heapUsagePercent = 0;
  } else {
    heapUsagePercent = (uint8_t)((usedHeap * 100U) / totalHeap);
  }

  if (heapUsagePercent > 80) {
    heapUsageCore0Percent = heapUsagePercent - 5;
    heapUsageCore1Percent = heapUsagePercent;
  } else if (heapUsagePercent > 60) {
    heapUsageCore0Percent = heapUsagePercent + 5;
    heapUsageCore1Percent = heapUsagePercent - 5;
  } else {
    heapUsageCore0Percent = heapUsagePercent + 10;
    heapUsageCore1Percent = heapUsagePercent;
  }

  if (heapUsageCore0Percent > 100) heapUsageCore0Percent = 100;
  if (heapUsageCore1Percent > 100) heapUsageCore1Percent = 100;
  
  // --- RAM Free ---
  ramFreeBytes = freeHeap;
  ramTotalBytes = totalHeap;
  
#ifdef ARDUINO_ARCH_ESP32
  ramLargestBlockBytes = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  ramMinFreeBytes = ESP.getMinFreeHeap();
  ramDmaFreeBytes = heap_caps_get_free_size(MALLOC_CAP_DMA);
#else
  ramLargestBlockBytes = 0;
  ramMinFreeBytes = ramFreeBytes;
  ramDmaFreeBytes = 0;
#endif

  // --- Flash Free ---
  flashFreeBytes = ESP.getFreeSketchSpace();
}

// ============================================================================
// SETUP
// ============================================================================

static bool restoreRtcHandoffTime(RuntimeContext& ctx, unsigned long nowMs, bool refreshHomeUi) {
  if (!RadioModeSwitch::wasBootHandoffDetected()) {
    LOG_I(TAG_MAIN, "RTC handoff skipped reason=no_boot_handoff");
    RadioModeSwitch::clearRTCTime();
    return false;
  }

  const uint8_t rtcHours = RadioModeSwitch::getRTCHours();
  const uint8_t rtcMinutes = RadioModeSwitch::getRTCMinutes();
  const uint8_t rtcSeconds = RadioModeSwitch::getRTCSeconds();

  if (rtcHours == 0 && rtcMinutes == 0 && rtcSeconds == 0) {
    RadioModeSwitch::clearRTCTime();
    return false;
  }

  if (rtcHours < 24 && rtcMinutes < 60 && rtcSeconds < 60) {
    ctx.hours = rtcHours;
    ctx.minutes = rtcMinutes;
    ctx.seconds = rtcSeconds;
    ctx.lastTick = nowMs;
    RtcSyncService::markClockSeeded();
    LOG_I(TAG_MAIN, "Restore time from RTC handoff hours=%02u minutes=%02u seconds=%02u", (unsigned)ctx.hours, (unsigned)ctx.minutes, (unsigned)ctx.seconds);
    updateSevenSeg();
    if (refreshHomeUi) {
      drawHomeThrottled();
    }
    RadioModeSwitch::clearRTCTime();
    return true;
  }

  LOG_W(TAG_MAIN, "Invalid RTC handoff time hours=%02u minutes=%02u seconds=%02u action=ignore", (unsigned)rtcHours, (unsigned)rtcMinutes, (unsigned)rtcSeconds);
  RadioModeSwitch::clearRTCTime();
  return false;
}

static void initCoreHardware() {
  Serial.begin(UART_BAUD);
  const unsigned long setupWaitUntilMs = millis() + SETUP_DELAY_MS;
  while ((long)(millis() - setupWaitUntilMs) < 0) {
    yield();
  }

  RtcSyncService::applyTimezone();
  heapBaseline = ESP.getFreeHeap();
  LOG_I(TAG_RAM, "Baseline heap free_b=%u", heapBaseline);
  ModeManager::logDiag("boot");
  RamTelemetry::begin();

  ModeManager::begin(&appState);
  RAM_CHECKPOINT("BOOT");

  // Ensure 7-seg GPIO is configured before any early time restoration.
  initSevenSeg();
}

static void initPersistenceAndConfig(RuntimeContext& ctx) {
  s_prefs.begin("zegar", false);
  AlarmRuntime::reset();
  loadNetworkConfigFromPreferences();
  showEpicIntro = s_prefs.getBool("epicIntro", true);
  settingsEpicIntroIndex = showEpicIntro ? 0 : 1;

  RadioModeSwitch::begin();
  restoreRtcHandoffTime(ctx, millis(), false);
}

static void initUiAndInput() {
#if UART_LCD_MIRROR
  lcdMirror.begin();
#endif
  lcdFrame.begin();

  const bool i2cClockApplied = I2cShared::initMaster(&Wire,
                                                     BoardPins::kI2cSda,
                                                     BoardPins::kI2cScl,
                                                     BoardPins::kI2cClockHz);
    LOG_I(TAG_I2C,
      "Clock readback requested_hz=%lu actual_hz=%lu status=%s",
      (unsigned long)BoardPins::kI2cClockHz,
      (unsigned long)Wire.getClock(),
      i2cClockApplied ? "applied" : "fallback_mismatch");
  RAM_CHECKPOINT("I2C_READY");

  RtcSyncService::tryRestoreSystemTimeFromDs3231(hours, minutes, seconds, lastTick);

  lcd.setExecTimes(37, 1520);
  lcd.init();
  lcdBacklightSafe();
  lcdClearSafe();
  lcdFrame.syncToCurrentFrame();
  LCDIcons::loadPalette(lcd, LCDIcons::Palette::Home);

  const bool skipIntroAfterModeHandoff = RadioModeSwitch::wasBootHandoffDetected();
  if (showEpicIntro && !skipIntroAfterModeHandoff) {
    introBegin();
  } else if (showEpicIntro && skipIntroAfterModeHandoff) {
    LOG_I(TAG_MAIN, "Boot intro skipped reason=radio_mode_handoff_restart");
  }

  encoder_begin(ENC_CLK, ENC_DT, ENC_SW);
  pinMode(BUZZER_PIN, OUTPUT);
  statsManager.begin();
}

static void initSensors(RuntimeContext& ctx) {
  STM32data_begin(uart, UART_BAUD, BoardPins::kStm32UartRx, BoardPins::kStm32UartTx);

  PMS5003Sensor::begin();
  ENS160AHT21Screen::resetRuntimeData();
  ENS160AHT21Sensor::begin();
  BMP280Sensor::begin();
  uiState.bmp280Menu.count = BMP280Sensor::menuItemCount();
  RAM_CHECKPOINT("SENSORS_INIT");

  HomeRuntime::DrawCallbacks homeDrawCallbacks;
  homeDrawCallbacks.drawHome = drawHome;
  homeDrawCallbacks.drawIndoorWeather = drawIndoorWeatherScreen;
  homeDrawCallbacks.drawOutdoorAir = drawAirScreen;
  homeDrawCallbacks.drawExtremeEnvironment = drawExtremeEnvironmentScreen;
  homeDrawCallbacks.drawSystemResources = drawSystemResources;
  homeDrawCallbacks.drawExtremeAlgorithms = drawExtremeAlgorithmScreen;
  HomeRuntime::begin(homeDrawCallbacks, (uint8_t)ctx.settingsUiScreenIndex, (uint8_t)ctx.settingsRotationSec);
  RAM_CHECKPOINT("UI_READY");
}

static void initComms(RuntimeContext& ctx) {
  UI_Callbacks callbacks;
  callbacks.drawHome = drawHomeThrottled;
  callbacks.drawMenu = drawMenu;
  callbacks.drawSetTime = drawSetTime;
  callbacks.drawAlarm = drawAlarm;
  callbacks.drawTimer = drawTimer;
  callbacks.drawStoper = drawStoper;
  callbacks.drawDebugSTM32 = drawDebugSTM32;
  callbacks.updateSevenSeg = updateSevenSeg;
  callbacks.updateSevenSegStoper = updateSevenSegStoper;
  callbacks.drawStats = drawStats;
  callbacks.drawSystemResources = drawSystemResources;
  callbacks.setHomeUiProfile = setHomeUiProfile;

  lcdFrame.forceFullRedrawOnce();
  ui_begin(callbacks);
  lcdBacklightSafe();

#if CORE_DEBUG_LEVEL > 0
  lcdFrame.reportTiming("startup");
#endif

  ctx.settingsRotationSec = s_prefs.getUShort("homeOverlaySec", (uint16_t)ctx.settingsRotationSec);
  if (ctx.settingsRotationSec < 1) ctx.settingsRotationSec = 1;
  if (ctx.settingsRotationSec > 10) ctx.settingsRotationSec = 10;
  HomeRuntime::setOverlayIntervalSeconds((uint8_t)ctx.settingsRotationSec);
  ctx.prevSettingsRotationSec = ctx.settingsRotationSec;

  ctx.settingsAlarmMelodyIndex = AlarmMelodyPrefs::loadIndex(s_prefs);
  ctx.prevSettingsAlarmMelodyIndex = ctx.settingsAlarmMelodyIndex;

  ctx.settingsUiScreenIndex = (int)s_prefs.getUShort("uiScreenMode", (uint16_t)ctx.settingsUiScreenIndex);
  if (ctx.settingsUiScreenIndex < 0) ctx.settingsUiScreenIndex = 0;
  if (ctx.settingsUiScreenIndex > 2) ctx.settingsUiScreenIndex = 2;
  setHomeUiProfile((uint8_t)ctx.settingsUiScreenIndex);
  ctx.prevSettingsUiScreenIndex = ctx.settingsUiScreenIndex;

  ctx.mqttEnabled = s_prefs.getBool("mqttEnabled", true);
  ctx.settingsMqttMenuIndex = ctx.mqttEnabled ? 0 : 1;

  ctx.alarmsCount = s_prefs.getUShort("alarmCount", 0);
  if (ctx.alarmsCount < 0) ctx.alarmsCount = 0;
  if (ctx.alarmsCount > AlarmRuntime::kMaxAlarms) ctx.alarmsCount = AlarmRuntime::kMaxAlarms;
  for (int i = 0; i < ctx.alarmsCount; ++i) {
    char keyH[12];
    char keyM[12];
    char keyE[12];
    snprintf(keyH, sizeof(keyH), "a%dh", i);
    snprintf(keyM, sizeof(keyM), "a%dm", i);
    snprintf(keyE, sizeof(keyE), "a%de", i);
    alarms[i].hour = (uint8_t)s_prefs.getUShort(keyH, 7);
    alarms[i].minute = (uint8_t)s_prefs.getUShort(keyM, 0);
    alarms[i].enabled = s_prefs.getBool(keyE, true);
    alarms[i].lastTriggerDay = 0;
  }

  WiFiSync::setTimeRefs(ctx.hours, ctx.minutes, ctx.seconds, ctx.lastTick);
  WiFiSync::setOnDone([]() {
    const unsigned long ntpSyncMs = WiFiSync::getLastNtpSyncTime();
    RtcSyncService::noteNtpSync(ntpSyncMs);
    drawHomeThrottled();
  });
  WiFiSync::begin(s_wifiSsid.c_str(), s_wifiPass.c_str(), s_ntpServer.c_str());

  MQTTSync::configure(s_mqttConfig);

  NetworkOrchestrator::Config netCfg;
  netCfg.wifiSsid = s_wifiSsid.c_str();
  netCfg.wifiPass = s_wifiPass.c_str();
  netCfg.wifiStatusCheckMs = 2000UL;
  NetworkOrchestrator::begin(netCfg);
  NetworkOrchestrator::setMqttEnabled(ctx.mqttEnabled);
  LOG_I(TAG_NET, "Network orchestrator armed services=wifi,radio,mqtt");
  RAM_CHECKPOINT("NETWORK_INIT");
}

static void finalizeStartup(RuntimeContext& ctx) {
  (void)ctx;
  if (NetworkOrchestrator::getCurrentRadioState() == RADIO_STATE_BT) {
    radioMode = BT_ONLY;
  } else {
    radioMode = WIFI_ONLY;
  }

  ModeManager::logDiag("after-setup-radio-ready");
  RAM_CHECKPOINT("SETUP_DONE");
}

void setup() {
  RuntimeContext ctx = makeRuntimeContext();
  initCoreHardware();
  initPersistenceAndConfig(ctx);
  initUiAndInput();
  initSensors(ctx);
  initComms(ctx);
  finalizeStartup(ctx);
}

// ============================================================================
// LOOP SERVICES (ETAP 1)
// ============================================================================

static void serviceBootDiagnostics(RuntimeContext& ctx, unsigned long nowMs) {
  if (!ctx.state.bootDiagReprinted && nowMs >= 5000UL) {
    ctx.state.bootDiagReprinted = true;
    LOG_I(TAG_MAIN,
          "Boot serial alive i2c_hz=%lu heap_b=%u",
          (unsigned long)Wire.getClock(),
          ESP.getFreeHeap());
  }
}

static void serviceInputAndUiEvents(RuntimeContext& ctx, unsigned long nowMs) {
  (void)ctx;
  (void)nowMs;
  HomeRuntime::handleHomeEntryIfStateChanged(appState);

  while (true) {
    const EncoderEvent evt = encoder_update();
    if (evt == ENC_NONE) {
      break;
    }

    if (evt == ENC_CLICK || evt == ENC_LONG) {
      statsManager.registerClick();
    } else if (evt == ENC_LEFT) {
      statsManager.registerStepLeft();
    } else if (evt == ENC_RIGHT) {
      statsManager.registerStepRight();
    }

    if (!RadioModeSwitch::isInitializing()) {
      ui_handleEvent(evt);
    }
  }

  statsManager.update();
}

static void serviceUiRefreshPreSensors(RuntimeContext& ctx, unsigned long nowMs) {
  (void)ctx;
  (void)nowMs;
  const uint32_t refreshStartUs = micros();
  HomeRuntime::serviceOverlayRotation(appState);
  HomeRuntime::serviceRedraw(appState);
  recordMaxU32(s_loopBaseline.maxUiRefreshUs, deltaUs(refreshStartUs, micros()));
}

static void serviceSensors(RuntimeContext& ctx, unsigned long nowMs) {
  (void)ctx;
  (void)nowMs;
  updateSystemResources();
  PMS5003Sensor::update();
  ENS160AHT21Sensor::update();
  BMP280Sensor::update();
}

static void serviceUiRefresh(RuntimeContext& ctx, unsigned long nowMs) {
  (void)ctx;
  const uint32_t refreshStartUs = micros();

  static unsigned long lastStatsRedraw = 0;
  if ((appState == STATE_STATS_RESOURCES_CPU || appState == STATE_STATS_RESOURCES_RAM ||
       appState == STATE_STATS_RESOURCES_FLASH || appState == STATE_STATS_RESOURCES ||
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
      (nowMs - lastStatsRedraw >= 1000UL)) {
    lastStatsRedraw = nowMs;
    drawStats();
  }

  static unsigned long lastTimerRedraw = 0;
  if (appState == STATE_TIMER && (nowMs - lastTimerRedraw >= 1000UL)) {
    lastTimerRedraw = nowMs;
    if (timerRunning || editState == EDIT_DONE) {
      drawTimer();
    }
  }

  recordMaxU32(s_loopBaseline.maxUiRefreshUs, deltaUs(refreshStartUs, micros()));
}

static void serviceDiagnostics(RuntimeContext& ctx, unsigned long nowMs) {
  (void)ctx;
  static unsigned long last_status_diag = 0;
  if (!ModeManager::isBtOn() && (nowMs - last_status_diag >= 2000UL)) {
    last_status_diag = nowMs;
    static uint32_t last_heap = 0;
    const uint32_t current_heap = ESP.getFreeHeap();
    const int heap_delta = (int)current_heap - (int)last_heap;
    last_heap = current_heap;

    LOG_I(TAG_MAIN,
          "Status wifi=%s mqtt=%s bt=%s heap_b=%u delta_b=%+d mode=%s",
          ModeManager::isWifiOn() ? "ON" : "OFF",
          NetworkOrchestrator::isMqttInitialized() ? "ON" : "OFF",
          ModeManager::isBtOn() ? "ON" : "OFF",
          current_heap,
          heap_delta,
          (NetworkOrchestrator::getCurrentRadioState() == RADIO_STATE_BT) ? "BT" : "WiFi");
  }

#if ENABLE_RUNTIME_TELEMETRY
  RuntimeTelemetry::service(Serial, nowMs);
#endif

  RamTelemetry::service(Serial, nowMs);

#if CORE_DEBUG_LEVEL > 0
  static bool lcdTimingPrimed = false;
  static unsigned long lastLcdTimingReport = 0;
  constexpr unsigned long LCD_TIMING_WINDOW_MS = 30000UL;
  if (!lcdTimingPrimed && nowMs >= LCD_TIMING_WINDOW_MS) {
    lcdFrame.resetStats();
    lcdTimingPrimed = true;
    lastLcdTimingReport = nowMs;
  }
  if (lcdTimingPrimed && (nowMs - lastLcdTimingReport >= LCD_TIMING_WINDOW_MS)) {
    lastLcdTimingReport = nowMs;
    lcdFrame.reportTiming("runtime");
    lcdFrame.resetStats();
  }
#endif
}

static void serviceComms(RuntimeContext& ctx, unsigned long nowMs) {
  ClockAlarmService::tickClock(CLOCK_TICK_MS, BUZZER_PIN);
  ClockAlarmService::serviceAlarmPlayback(BUZZER_PIN, ALARM_DURATION_MS);

  NetworkOrchestrator::setMqttEnabled(ctx.mqttEnabled);
  const uint32_t netStartUs = micros();
  NetworkOrchestrator::update();
  recordMaxU32(s_loopBaseline.maxNetworkUpdateUs, deltaUs(netStartUs, micros()));

  RtcSyncService::processPendingWrite();

  if (ctx.mqttEnabled && NetworkOrchestrator::isMqttInitialized()) {
    static unsigned long lastMQTTPublish = 0;
    if (nowMs - lastMQTTPublish >= 5000UL) {
      lastMQTTPublish = nowMs;
      const uint32_t mqttStartUs = micros();
      TelemetryComposer::Sample sample;
      TelemetryComposer::buildMqttTelemetrySample(sample);
      MQTTSync::publishSensorData(sample.temperatureC,
                                  sample.humidityPct,
                                  sample.pressureHpa,
                                  sample.aqi,
                                  sample.tvoc,
                                  sample.eco2);
      recordMaxU32(s_loopBaseline.maxMqttPublishUs, deltaUs(mqttStartUs, micros()));
    }
  }

  if (NetworkOrchestrator::getCurrentRadioState() == RADIO_STATE_BT) {
    radioMode = BT_ONLY;
  } else {
    radioMode = WIFI_ONLY;
  }
}

static void serviceStm32AndStopwatch(RuntimeContext& ctx, unsigned long nowMs) {
  if (appState == STATE_DEBUG_STM32 && (nowMs - ctx.state.lastSTM32Update >= STM32_UPDATE_MS)) {
    ctx.state.lastSTM32Update = nowMs;
    STM32data_update();

    if (stmDataUpdated) {
      stmDataUpdated = false;
      displayedBPM = bpmNumber;
      displayedSPO2 = spo2Number;
      stm32Connected = true;
      ctx.state.lastSTM32DataReceived = nowMs;
    } else if (nowMs - ctx.state.lastSTM32DataReceived > STM32_TIMEOUT_MS) {
      stm32Connected = false;
      displayedBPM = 0;
      displayedSPO2 = 0;
    }

    drawDebugSTM32();
  }

  if (appState == STATE_STOPER && (nowMs - ctx.state.lastStoperDraw >= STOPER_DRAW_MS)) {
    ctx.state.lastStoperDraw = nowMs;
    drawStoper();
  }
}

static void serviceDiagnosticsTail(RuntimeContext& ctx, unsigned long nowMs) {
  (void)ctx;
  static unsigned long lastBtCheck = 0;
  if (nowMs - lastBtCheck > 60000UL) {
    lastBtCheck = nowMs;
    LOG_I(TAG_BT, "Connected=%s", audioBT_isConnected() ? "yes" : "no");
  }
}

// ============================================================================
// LOOP
// ============================================================================

void loop() {
  if (serviceEpicBootSequence()) {
    return;
  }

  static RuntimeContext ctx = makeRuntimeContext();
  const unsigned long nowMs = millis();
  const uint32_t loopStartUs = micros();
  baselineOnLoopStart(nowMs, loopStartUs);

  serviceBootDiagnostics(ctx, nowMs);
  serviceInputAndUiEvents(ctx, nowMs);
  serviceUiRefreshPreSensors(ctx, nowMs);
  serviceSensors(ctx, nowMs);
  serviceUiRefresh(ctx, nowMs);
  serviceDiagnostics(ctx, nowMs);
  serviceComms(ctx, nowMs);
  serviceStm32AndStopwatch(ctx, nowMs);
  serviceDiagnosticsTail(ctx, nowMs);

  baselineOnLoopEnd(nowMs, loopStartUs, micros());
}
