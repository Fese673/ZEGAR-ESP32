#include "AppLoop.h"

#include <Arduino.h>
#include <Esp.h>
#include <Wire.h>

#include "AppLog.h"
#include "AppRuntime.h"
#include "AppState.h"
#include "AudioBT.h"
#include "BMP280Sensor.h"
#include "BootIntroService.h"
#include "ClockAlarmService.h"
#include "ENS160AHT21Sensor.h"
#include "Encoder.h"
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
#include "UI_Controller.h"
#include "UI_Draw.h"

namespace AppLoop {
namespace {

static constexpr char TAG_MAIN[] = "MAIN";
static constexpr char TAG_BT[] = "BT";

void serviceBootDiagnostics(RuntimeContext& ctx, unsigned long nowMs) {
  if (!ctx.state.bootDiagReprinted && nowMs >= 5000UL) {
    ctx.state.bootDiagReprinted = true;
    LOG_I(TAG_MAIN,
          "Boot serial alive i2c_hz=%lu heap_b=%u",
          static_cast<unsigned long>(Wire.getClock()),
          ESP.getFreeHeap());
  }
}

void serviceInputAndUiEvents() {
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

void serviceUiRefreshPreSensors() {
  const uint32_t refreshStartUs = micros();
  HomeRuntime::serviceOverlayRotation(appState);
  HomeRuntime::serviceRedraw(appState);
  LoopBaselineTelemetry::recordUiRefreshUs(static_cast<uint32_t>(micros() - refreshStartUs));
}

void serviceSensors() {
  SystemResourcesService::update();
  PMS5003Sensor::update();
  ENS160AHT21Sensor::update();
  BMP280Sensor::update();
}

void serviceUiRefresh(unsigned long nowMs) {
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

  if (appState == STATE_SAFE_CRACKER && SafeCracker::service(nowMs)) {
    SafeCracker::stop();
    appState = STATE_GAMES_MENU;
    drawMenu();
  }

  LoopBaselineTelemetry::recordUiRefreshUs(static_cast<uint32_t>(micros() - refreshStartUs));
}

void serviceDiagnostics(unsigned long nowMs) {
  static unsigned long lastStatusDiag = 0;
  if (!ModeManager::isBtOn() && (nowMs - lastStatusDiag >= 2000UL)) {
    lastStatusDiag = nowMs;
    static uint32_t lastHeap = 0;
    const uint32_t currentHeap = ESP.getFreeHeap();
    const int heapDelta = static_cast<int>(currentHeap) - static_cast<int>(lastHeap);
    lastHeap = currentHeap;

    LOG_I(TAG_MAIN,
          "Status wifi=%s mqtt=%s bt=%s heap_b=%u delta_b=%+d mode=%s",
          ModeManager::isWifiOn() ? "ON" : "OFF",
          NetworkOrchestrator::isMqttInitialized() ? "ON" : "OFF",
          ModeManager::isBtOn() ? "ON" : "OFF",
          currentHeap,
          heapDelta,
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

void serviceComms(RuntimeContext& ctx, unsigned long nowMs) {
  ClockAlarmService::tickClock(CLOCK_TICK_MS, BUZZER_PIN);
  ClockAlarmService::serviceAlarmPlayback(BUZZER_PIN, ALARM_DURATION_MS);

  NetworkOrchestrator::setMqttEnabled(ctx.mqttEnabled);
  const uint32_t netStartUs = micros();
  NetworkOrchestrator::update();
  LoopBaselineTelemetry::recordNetworkUpdateUs(static_cast<uint32_t>(micros() - netStartUs));

  RtcSyncService::processPendingWrite();

  if (ctx.mqttEnabled && NetworkOrchestrator::isMqttInitialized()) {
    static unsigned long lastMqttPublish = 0;
    if (nowMs - lastMqttPublish >= 5000UL) {
      lastMqttPublish = nowMs;
      const uint32_t mqttStartUs = micros();
      TelemetryComposer::Sample sample;
      TelemetryComposer::buildMqttTelemetrySample(sample);
      MQTTSync::publishSensorData(sample.temperatureC,
                                  sample.humidityPct,
                                  sample.pressureHpa,
                                  sample.aqi,
                                  sample.tvoc,
                                  sample.eco2);
      LoopBaselineTelemetry::recordMqttPublishUs(static_cast<uint32_t>(micros() - mqttStartUs));
    }
  }

  if (NetworkOrchestrator::getCurrentRadioState() == RADIO_STATE_BT) {
    radioMode = BT_ONLY;
  } else {
    radioMode = WIFI_ONLY;
  }
}

void serviceStm32AndStopwatch(RuntimeContext& ctx, unsigned long nowMs) {
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

void serviceDiagnosticsTail(unsigned long nowMs) {
  static unsigned long lastBtCheck = 0;
  if (nowMs - lastBtCheck > 60000UL) {
    lastBtCheck = nowMs;
    LOG_I(TAG_BT, "Connected=%s", audioBT_isConnected() ? "yes" : "no");
  }
}

}  // namespace

void runLoop() {
  if (BootIntroService::service()) {
    return;
  }

  static RuntimeContext ctx = makeRuntimeContext();
  const unsigned long nowMs = millis();
  const uint32_t loopStartUs = micros();
  LoopBaselineTelemetry::onLoopStart(nowMs, loopStartUs);

  serviceBootDiagnostics(ctx, nowMs);
  serviceInputAndUiEvents();
  serviceUiRefreshPreSensors();
  serviceSensors();
  serviceUiRefresh(nowMs);
  serviceDiagnostics(nowMs);
  serviceComms(ctx, nowMs);
  serviceStm32AndStopwatch(ctx, nowMs);
  serviceDiagnosticsTail(nowMs);

  LoopBaselineTelemetry::onLoopEnd(nowMs, loopStartUs, micros());
}

}  // namespace AppLoop
