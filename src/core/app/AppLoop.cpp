#include "AppLoop.h"

#include <Arduino.h>
#include <Esp.h>
#include <Wire.h>

#include "AppLog.h"
#include "AppRuntime.h"
#include "AppSettings.h"
#include "AppState.h"
#include "AudioBT.h"
#include "BMP280Sensor.h"
#include "BootIntroService.h"
#include "ClockAlarmService.h"
#include "ENS160AHT21Sensor.h"
#include "Encoder.h"
#include "core/events/EventBus.h"
#include "comms/esp_to_gution/Esptogution.h"
#include "HomeRuntime.h"
#include "LCDMirror.h"
#include "LoopBaselineTelemetry.h"
#include "MQTTSync.h"
#include "ModeManager.h"
#include "NetworkOrchestrator.h"
#include "PMS_Czujnik.h"
#include "RadioModeSwitch.h"
#include "RamTelemetry.h"
#include "RtcSyncService.h"
#include "RuntimeTelemetry.h"
#include "STM32_Data.h"
#include "StatsManager.h"
#include "SystemResourcesService.h"
#include "TelemetryComposer.h"
#include "SafeCracker.h"
#include "TANK-GAMES/TankGame.h"
#include "touch_buzzer_test.h"
#include "UIState.h"
#include "UI_Controller.h"
#include "UI_Draw.h"
#include "meteoSync.h"

namespace AppLoop {
namespace {

static constexpr char TAG_MAIN[] = "MAIN";
static constexpr char TAG_BT[] = "BT";
static RuntimeContext s_ctx = makeRuntimeContext();

// ─── Encoder input ───────────────────────────────────────────────────

static void serviceBtLongPress() {
  if (BootIntroService::isActive()) {
    return;
  }

  if (appState != STATE_HOME || !ModeManager::isBtOn()) {
    return;
  }

  const unsigned long holdMs = encoder_button_hold_ms();
  static bool latched = false;

  if (holdMs == 0) {
    latched = false;
    return;
  }

  if (!latched && holdMs >= 4000UL) {
    latched = true;
    UIState::mutableState().btMusicMenu.index = 0;
    appState = STATE_BT_MUSIC_CONTROL;
    drawBtMusicControl();
  }
}

static void serviceEncoderInput() {
  HomeRuntime::handleHomeEntryIfStateChanged(appState);

  while (true) {
    const EncoderEvent evt = encoder_update();
    if (evt == ENC_NONE) {
      break;
    }

    if (BootIntroService::isActive()) {
      continue;
    }

    if (evt == ENC_CLICK || (evt == ENC_LONG && appState != STATE_TANK_GAME)) {
      statsManager.registerClick();
    } else if (evt == ENC_LEFT) {
      statsManager.registerStepLeft();
    } else if (evt == ENC_RIGHT) {
      statsManager.registerStepRight();
    }

    const bool suppressLong = (evt == ENC_LONG && appState == STATE_HOME && ModeManager::isBtOn());
    if (!RadioModeSwitch::isInitializing() && !suppressLong) {
      ui_handleEvent(evt);
    }

    HomeRuntime::handleHomeEntryIfStateChanged(appState);
  }

  serviceBtLongPress();
}

// ─── Handler: EV_UI_OVERLAY (10ms) ────────────────────────────────────

static void onUiOverlay(const Event&) {
  if (BootIntroService::isActive()) return;

  const uint32_t startUs = micros();

  HomeRuntime::serviceOverlayRotation(appState);
  HomeRuntime::serviceRedraw(appState);
  TouchBuzzerTest::service();
  statsManager.update();

  LoopBaselineTelemetry::recordUiRefreshUs(static_cast<uint32_t>(micros() - startUs));
}

// ─── Handler: EV_SENSOR_READ (200ms) ──────────────────────────────────

static void onSensorRead(const Event&) {
  SystemResourcesService::update();
  PMS5003Sensor::update();
  ENS160AHT21Sensor::update();
  BMP280Sensor::update();

  NetworkOrchestrator::setMqttEnabled(s_ctx.mqttEnabled);
  const uint32_t netStartUs = micros();
  NetworkOrchestrator::update();
  LoopBaselineTelemetry::recordNetworkUpdateUs(static_cast<uint32_t>(micros() - netStartUs));

  if (NetworkOrchestrator::getCurrentRadioState() == RADIO_STATE_BT) {
    radioMode = BT_ONLY;
  } else {
    radioMode = WIFI_ONLY;
  }
}

// ─── Handler: EV_CLOCK_TICK (1000ms) ──────────────────────────────────

static void onClockTick(const Event&) {
  ClockAlarmService::tickClock(CLOCK_TICK_MS, BUZZER_PIN);
  RtcSyncService::processPendingWrite();
  meteoSync::update();
}

// ─── Handler: EV_UI_REFRESH (1000ms) ──────────────────────────────────

static void onUiRefresh(const Event&) {
  if (BootIntroService::isActive()) return;

  const uint32_t startUs = micros();

  switch (appState) {
    case STATE_TIMER:
      if (timerRunning || editState == EDIT_DONE) drawTimer();
      break;

    case STATE_DEBUG_STM32:
      if (millis() - s_ctx.state.lastSTM32Update >= STM32_UPDATE_MS) {
        s_ctx.state.lastSTM32Update = millis();
        STM32data_update();
        if (stmDataUpdated) {
          stmDataUpdated = false;
          displayedBPM = bpmNumber;
          displayedSPO2 = spo2Number;
          stm32Connected = true;
          s_ctx.state.lastSTM32DataReceived = millis();
        } else if (millis() - s_ctx.state.lastSTM32DataReceived > STM32_TIMEOUT_MS) {
          stm32Connected = false;
          displayedBPM = 0;
          displayedSPO2 = 0;
        }
        drawDebugSTM32();
      }
      break;

    case STATE_STOPER:
      if (millis() - s_ctx.state.lastStoperDraw >= STOPER_DRAW_MS) {
        s_ctx.state.lastStoperDraw = millis();
        drawStoper();
      }
      break;

    case STATE_BT_MUSIC_CONTROL:
      drawBtMusicControl();
      break;

    default:
      if (appState >= STATE_STATS_RESOURCES_MENU || appState == STATE_PMS5003 ||
          appState == STATE_ENS160_AHT21 || appState == STATE_BMP280) {
        drawStats();
      }
      break;
  }

  static unsigned long lastGamesRedraw = 0;
  const unsigned long now = millis();
  if (appState == STATE_GAMES_MENU && now - lastGamesRedraw >= 120UL) {
    lastGamesRedraw = now;
    drawMenu();
  }

  static bool menuMusicPlaying = false;
  const bool bgMusic = AppSettings::state().backgroundMusicEnabled;
  const bool shouldPlay = (appState == STATE_GAMES_MENU && bgMusic);
  if (shouldPlay != menuMusicPlaying) {
    if (shouldPlay) ClockAlarmService::startMenuMusic(BUZZER_PIN);
    else ClockAlarmService::stopMenuMusic(BUZZER_PIN);
    menuMusicPlaying = shouldPlay;
  }

  if (appState == STATE_SAFE_CRACKER && SafeCracker::service(now)) {
    SafeCracker::stop();
    appState = STATE_GAMES_MENU;
    drawMenu();
  }

  if (appState == STATE_TANK_GAME && TankGame::service(now)) {
    TankGame::stop();
    appState = STATE_GAMES_MENU;
    drawMenu();
  }

  LoopBaselineTelemetry::recordUiRefreshUs(static_cast<uint32_t>(micros() - startUs));
}

// ─── Handler: EV_DIAGNOSTICS (2000ms) ─────────────────────────────────

static void onDiagnostics(const Event&) {
  const unsigned long now = millis();

  if (!ModeManager::isBtOn()) {
    static uint32_t lastHeap = 0;
    const uint32_t currentHeap = ESP.getFreeHeap();
    const int delta = static_cast<int>(currentHeap) - static_cast<int>(lastHeap);
    lastHeap = currentHeap;

    LOG_I(TAG_MAIN,
          "Status wifi=%s mqtt=%s bt=%s heap_b=%u delta_b=%+d mode=%s",
          ModeManager::isWifiOn() ? "ON" : "OFF",
          NetworkOrchestrator::isMqttInitialized() ? "ON" : "OFF",
          ModeManager::isBtOn() ? "ON" : "OFF",
          currentHeap, delta,
          (NetworkOrchestrator::getCurrentRadioState() == RADIO_STATE_BT) ? "BT" : "WiFi");
  }

#if ENABLE_RUNTIME_TELEMETRY
  RuntimeTelemetry::service(Serial, now);
#endif

  RamTelemetry::service(Serial, now);

#if CORE_DEBUG_LEVEL > 0
  static bool primed = false;
  static unsigned long lastLcdReport = 0;
  if (!primed && now >= 30000UL) { lcdFrame.resetStats(); primed = true; lastLcdReport = now; }
  if (primed && now - lastLcdReport >= 30000UL) {
    lastLcdReport = now;
    lcdFrame.reportTiming("runtime");
    lcdFrame.resetStats();
  }
#endif
}

// ─── Handler: EV_MQTT_PUBLISH (5000ms) ────────────────────────────────

static void onMqttPublish(const Event&) {
  if (!s_ctx.mqttEnabled || !NetworkOrchestrator::isMqttInitialized()) return;

  const uint32_t startUs = micros();
  TelemetryComposer::Sample sample;
  TelemetryComposer::buildMqttTelemetrySample(sample);
  MQTTSync::publishSensorData(sample.temperatureC,
                              sample.humidityPct,
                              sample.pressureHpa,
                              sample.aqi,
                              sample.tvoc,
                              sample.eco2);
  LoopBaselineTelemetry::recordMqttPublishUs(static_cast<uint32_t>(micros() - startUs));
}

// ─── Handler: EV_BOOT_LOGGING (5000ms, oneshot) ──────────────────────

static void onBootLogging(const Event&) {
  if (s_ctx.state.bootDiagReprinted) return;
  s_ctx.state.bootDiagReprinted = true;
  LOG_I(TAG_MAIN,
        "Boot serial alive i2c_hz=%lu heap_b=%u",
        static_cast<unsigned long>(Wire.getClock()),
        ESP.getFreeHeap());
}

// ─── Handler: EV_BT_CONN_CHECK (60000ms) ──────────────────────────────

static void onBtConnCheck(const Event&) {
  LOG_I(TAG_BT, "Connected=%s", audioBT_isConnected() ? "yes" : "no");
}

// ─── AppState change helper ───────────────────────────────────────────

static void onAppStateChanged(const Event& e) {
  requestUiFullRedraw();
}

}  // namespace

// ─── Public API ───────────────────────────────────────────────────────

void runLoop() {
  const uint32_t nowMs = millis();
  const uint32_t loopStartUs = micros();
  LoopBaselineTelemetry::onLoopStart(nowMs, loopStartUs);

  serviceEncoderInput();
  BootIntroService::service();
  EventBus::process();
  EsptoGuition::update();
  ClockAlarmService::serviceAlarmPlayback(BUZZER_PIN, ALARM_DURATION_MS);

  LoopBaselineTelemetry::onLoopEnd(nowMs, loopStartUs, micros());
}

void initEventHandlers() {
  // Elevate the current loopTask priority to 2 so it preempts background tasks (like Meteo).
  vTaskPrioritySet(NULL, 2);

  EventBus::init();
  EventBus::subscribe(EV_UI_OVERLAY,     onUiOverlay);
  EventBus::subscribe(EV_SENSOR_READ,    onSensorRead);
  EventBus::subscribe(EV_CLOCK_TICK,     onClockTick);
  EventBus::subscribe(EV_UI_REFRESH,     onUiRefresh);
  EventBus::subscribe(EV_DIAGNOSTICS,    onDiagnostics);
  EventBus::subscribe(EV_MQTT_PUBLISH,   onMqttPublish);
  EventBus::subscribe(EV_BOOT_LOGGING,   onBootLogging);
  EventBus::subscribe(EV_BT_CONN_CHECK,  onBtConnCheck);
  EventBus::subscribe(EV_APP_STATE_CHANGED, onAppStateChanged);

  EventBus::addTimer(EV_UI_OVERLAY,     10);
  EventBus::addTimer(EV_SENSOR_READ,    200);
  EventBus::addTimer(EV_CLOCK_TICK,     1000);
  EventBus::addTimer(EV_UI_REFRESH,     100);
  EventBus::addTimer(EV_DIAGNOSTICS,    2000, false, 500);
  EventBus::addTimer(EV_MQTT_PUBLISH,   5000);
  EventBus::addTimer(EV_BOOT_LOGGING,   5000, true);
  EventBus::addTimer(EV_BT_CONN_CHECK,  60000);
}

}  // namespace AppLoop
