#include "AppBoot.h"

#include <Arduino.h>
#include <Esp.h>
#include <Wire.h>

#include "AlarmMelodyPrefs.h"
#include "AlarmRuntime.h"
#include "AppLog.h"
#include "AppRuntime.h"
#include "AppSettings.h"
#include "BMP280Sensor.h"
#include "BoardPins.h"
#include "BootIntroService.h"
#include "ENS160AHT21Screen.h"
#include "ENS160AHT21Sensor.h"
#include "comms/esp_to_gution/Esptogution.h"
#include "HomeRuntime.h"
#include "LCDIcons.h"
#include "LCDMirror.h"
#include "LoopBaselineTelemetry.h"
#include "MQTTSync.h"
#include "ModeManager.h"
#include "NetworkOrchestrator.h"
#include "PMS_Czujnik.h"
#include "RadioModeSwitch.h"
#include "RamTelemetry.h"
#include "STM32_Data.h"
#include "touch_buzzer_test.h"
#include "RtcSyncService.h"
#include "SecretsConfig.h"
#include "StatsManager.h"
#include "UIState.h"
#include "UI_Controller.h"
#include "UI_Draw.h"
#include "WiFiSync.h"
#include "i2c/SharedBus.h"
#include "Encoder.h"
#include "meteoSync.h"

namespace AppBoot {
namespace {

static constexpr char TAG_MAIN[] = "MAIN";
static constexpr char TAG_I2C[] = "I2C";
static constexpr char TAG_RAM[] = "RAM";
static constexpr char TAG_NET[] = "NET";

constexpr unsigned long SETUP_DELAY_MS = 100UL;
constexpr long UART_BAUD = 921600;
constexpr uint32_t GUITION_BAUD = 115200UL;
constexpr uint8_t ENC_CLK = BoardPins::kEncoderClk;
constexpr uint8_t ENC_DT = BoardPins::kEncoderDt;
constexpr uint8_t ENC_SW = BoardPins::kEncoderSw;
uint32_t s_heapBaseline = 0;

String s_wifiSsid = PROJECT_WIFI_SSID;
String s_wifiPass = PROJECT_WIFI_PASS;
String s_ntpServer = PROJECT_NTP_SERVER;
MQTTSync::Config s_mqttConfig;

void lcdBacklightSafe() {
  if (I2cShared::lock(LCD_I2C_LOCK_TIMEOUT_MS)) {
    lcd.backlight();
    I2cShared::unlock();
  }
}

void lcdNoBacklightSafe() {
  if (I2cShared::lock(LCD_I2C_LOCK_TIMEOUT_MS)) {
    lcd.noBacklight();
    I2cShared::unlock();
  }
}

void lcdClearSafe() {
  if (I2cShared::lock(LCD_I2C_LOCK_TIMEOUT_MS)) {
    lcd.clear();
    I2cShared::unlock();
#if BOOT_LCD_CLEAR_TELEMETRY
    LOG_I(TAG_MAIN, "Boot LCD clear: success");
#endif
    return;
  }
#if BOOT_LCD_CLEAR_TELEMETRY
  LOG_W(TAG_MAIN, "Boot LCD clear: i2c lock failed");
#endif
}

void drawHomeThrottled() {
  HomeRuntime::markHomeDirty();
  HomeRuntime::serviceRedraw(appState);
}

bool looksLikePlaceholder(const String& value) {
  return value.length() == 0 || value.startsWith("REPLACE_");
}

void loadNetworkConfigFromPreferences() {
  s_wifiSsid = s_prefs.isKey("wifiSsid") ? s_prefs.getString("wifiSsid", PROJECT_WIFI_SSID) : PROJECT_WIFI_SSID;
  s_wifiPass = s_prefs.isKey("wifiPass") ? s_prefs.getString("wifiPass", PROJECT_WIFI_PASS) : PROJECT_WIFI_PASS;
  s_ntpServer = s_prefs.isKey("ntpServer") ? s_prefs.getString("ntpServer", PROJECT_NTP_SERVER) : PROJECT_NTP_SERVER;

  s_mqttConfig.brokerAddress = s_prefs.isKey("mqttHost") ? s_prefs.getString("mqttHost", PROJECT_MQTT_BROKER) : PROJECT_MQTT_BROKER;
  s_mqttConfig.brokerPort = static_cast<uint16_t>(s_prefs.getUShort("mqttPort", PROJECT_MQTT_PORT));
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

bool restoreRtcHandoffTime(RuntimeContext& ctx, unsigned long nowMs, bool refreshHomeUi) {
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
    LOG_I(TAG_MAIN,
          "Restore time from RTC handoff hours=%02u minutes=%02u seconds=%02u",
          static_cast<unsigned>(ctx.hours),
          static_cast<unsigned>(ctx.minutes),
          static_cast<unsigned>(ctx.seconds));
    updateSevenSeg();
    if (refreshHomeUi) {
      drawHomeThrottled();
    }
    RadioModeSwitch::clearRTCTime();
    return true;
  }

  LOG_W(TAG_MAIN,
        "Invalid RTC handoff time hours=%02u minutes=%02u seconds=%02u action=ignore",
        static_cast<unsigned>(rtcHours),
        static_cast<unsigned>(rtcMinutes),
        static_cast<unsigned>(rtcSeconds));
  RadioModeSwitch::clearRTCTime();
  return false;
}

void initCoreHardware() {
  Serial.begin(UART_BAUD);
  const unsigned long setupWaitUntilMs = millis() + SETUP_DELAY_MS;
  while (static_cast<long>(millis() - setupWaitUntilMs) < 0) {
    yield();
  }

  RtcSyncService::applyTimezone();
  s_heapBaseline = ESP.getFreeHeap();
  LOG_I(TAG_RAM, "Baseline heap free_b=%u", s_heapBaseline);
  ModeManager::logDiag("boot");
  RamTelemetry::begin();

  ModeManager::begin(&appState);
  RAM_CHECKPOINT("BOOT");

  // 7-seg GPIO must be ready before any early time restore.
  initSevenSeg();
}

void initPersistenceAndConfig(RuntimeContext& ctx) {
  AppSettings::State& appSettings = AppSettings::mutableState();
  UIState::State& uiState = UIState::mutableState();

  s_prefs.begin("zegar", false);
  AlarmRuntime::reset();
  loadNetworkConfigFromPreferences();
  appSettings.touchTestEnabled = s_prefs.isKey("touchTest") ? s_prefs.getBool("touchTest", true) : true;
  appSettings.backgroundMusicEnabled = s_prefs.getBool("menuMusic", true);
  uiState.settingsBackgroundMusicMenu.index = appSettings.backgroundMusicEnabled ? 0 : 1;
  appSettings.buzzerEnabled = s_prefs.getBool("buzzerEnabled", true);
  appSettings.showEpicIntro = s_prefs.getBool("epicIntro", true);
  uiState.settingsBootIntroMenu.index = appSettings.showEpicIntro ? 0 : 1;
  
  bool pmsEnabled = s_prefs.getBool("pmsEnabled", true);
  PMS5003Sensor::setEnabled(pmsEnabled);

  RadioModeSwitch::begin();
  restoreRtcHandoffTime(ctx, millis(), false);
}

void initUiAndInput() {
  AppSettings::State& appSettings = AppSettings::mutableState();
  UIState::State& uiState = UIState::mutableState();
  const bool showEpicIntro = AppSettings::state().showEpicIntro;

#if UART_LCD_MIRROR
  lcdMirror.begin();
#endif
   lcdFrame.begin();

   lcd.setExecTimes(37, 1520);
   lcd.init();
   LCDIcons::resetPaletteCache();

   const bool i2cClockApplied = I2cShared::initMaster(&Wire,
                                                      BoardPins::kI2cSda,
                                                      BoardPins::kI2cScl,
                                                      BoardPins::kI2cClockHz,
                                                      true);
   LOG_I(TAG_I2C,
         "Clock readback requested_hz=%lu actual_hz=%lu status=%s",
         static_cast<unsigned long>(BoardPins::kI2cClockHz),
         static_cast<unsigned long>(Wire.getClock()),
         i2cClockApplied ? "applied" : "fallback_mismatch");
   RAM_CHECKPOINT("I2C_READY");

   RtcSyncService::tryRestoreSystemTimeFromDs3231(hours, minutes, seconds, lastTick);

   lcdBacklightSafe();
   lcdClearSafe();
   lcdFrame.syncToCurrentFrame();
   LCDIcons::loadPalette(lcd, LCDIcons::Palette::Home);

   // Buzzer pin must be configured as OUTPUT BEFORE any tone() calls
   pinMode(BUZZER_PIN, OUTPUT);
   digitalWrite(BUZZER_PIN, LOW);

   BootIntroService::Callbacks introCallbacks;
   introCallbacks.backlightOn = lcdBacklightSafe;
   introCallbacks.backlightOff = lcdNoBacklightSafe;
   BootIntroService::begin(introCallbacks, BUZZER_PIN);

  const bool skipIntroAfterModeHandoff = RadioModeSwitch::wasBootHandoffDetected();
  if (showEpicIntro && !skipIntroAfterModeHandoff) {
    BootIntroService::start();
  } else if (showEpicIntro && skipIntroAfterModeHandoff) {
    LOG_I(TAG_MAIN, "Boot intro skipped reason=radio_mode_handoff_restart");
  }

   encoder_begin(ENC_CLK, ENC_DT, ENC_SW);
   TouchBuzzerTest::begin(BoardPins::kTouchTestPad, BUZZER_PIN);
  TouchBuzzerTest::setEnabled(AppSettings::state().touchTestEnabled);
  appSettings.touchTestEnabled = TouchBuzzerTest::isEnabled();
  uiState.settingsTouchMenu.index = appSettings.touchTestEnabled ? 0 : 1;
  statsManager.begin();
}

void initSensors(RuntimeContext& ctx) {
  UIState::State& uiState = UIState::mutableState();

  STM32data_begin(BoardPins::kStm32UartRx, BoardPins::kStm32UartTx);
  EsptoGuition::begin(Serial2, GUITION_BAUD, BoardPins::kGuitionUartRx, BoardPins::kGuitionUartTx);

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
  HomeRuntime::begin(homeDrawCallbacks,
                     static_cast<uint8_t>(ctx.settingsUiScreenIndex),
                     static_cast<uint8_t>(ctx.settingsRotationSec));
  RAM_CHECKPOINT("UI_READY");
}

void initComms(RuntimeContext& ctx) {
  AlarmRuntime::State& alarmRuntime = AlarmRuntime::mutableState();

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

  ctx.settingsRotationSec = s_prefs.getUShort("homeOverlaySec", static_cast<uint16_t>(ctx.settingsRotationSec));
  if (ctx.settingsRotationSec < 1) {
    ctx.settingsRotationSec = 1;
  }
  if (ctx.settingsRotationSec > 10) {
    ctx.settingsRotationSec = 10;
  }
  HomeRuntime::setOverlayIntervalSeconds(static_cast<uint8_t>(ctx.settingsRotationSec));
  ctx.prevSettingsRotationSec = ctx.settingsRotationSec;

  ctx.settingsAlarmMelodyIndex = AlarmMelodyPrefs::loadIndex(s_prefs);
  ctx.prevSettingsAlarmMelodyIndex = ctx.settingsAlarmMelodyIndex;

  ctx.settingsUiScreenIndex = static_cast<int>(s_prefs.getUShort("uiScreenMode", static_cast<uint16_t>(ctx.settingsUiScreenIndex)));
  if (ctx.settingsUiScreenIndex < 0) {
    ctx.settingsUiScreenIndex = 0;
  }
  if (ctx.settingsUiScreenIndex > 2) {
    ctx.settingsUiScreenIndex = 2;
  }
  setHomeUiProfile(static_cast<uint8_t>(ctx.settingsUiScreenIndex));
  ctx.prevSettingsUiScreenIndex = ctx.settingsUiScreenIndex;

  ctx.mqttEnabled = s_prefs.getBool("mqttEnabled", true);
  ctx.settingsMqttMenuIndex = ctx.mqttEnabled ? 0 : 1;

  ctx.alarmsCount = s_prefs.getUShort("alarmCount", 0);
  if (ctx.alarmsCount > AlarmRuntime::kMaxAlarms) {
    ctx.alarmsCount = AlarmRuntime::kMaxAlarms;
  }
  for (int i = 0; i < ctx.alarmsCount; ++i) {
    char keyH[12];
    char keyM[12];
    char keyE[12];
    snprintf(keyH, sizeof(keyH), "a%dh", i);
    snprintf(keyM, sizeof(keyM), "a%dm", i);
    snprintf(keyE, sizeof(keyE), "a%de", i);
    alarmRuntime.alarms[i].hour = static_cast<uint8_t>(s_prefs.getUShort(keyH, 7));
    alarmRuntime.alarms[i].minute = static_cast<uint8_t>(s_prefs.getUShort(keyM, 0));
    alarmRuntime.alarms[i].enabled = s_prefs.getBool(keyE, true);
    alarmRuntime.alarms[i].lastTriggerDay = 0;
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

void finalizeStartup() {
  LoopBaselineTelemetry::resetWindow(millis());
  if (NetworkOrchestrator::getCurrentRadioState() == RADIO_STATE_BT) {
    radioMode = BT_ONLY;
  } else {
    radioMode = WIFI_ONLY;
  }

  ModeManager::logDiag("after-setup-radio-ready");
  RAM_CHECKPOINT("SETUP_DONE");
}

}  // namespace

void runSetup() {
  RuntimeContext ctx = makeRuntimeContext();
  initCoreHardware();
  initPersistenceAndConfig(ctx);
  initUiAndInput();
  initSensors(ctx);
  initComms(ctx);
  finalizeStartup();
  meteoSync::begin();
}

}  // namespace AppBoot
