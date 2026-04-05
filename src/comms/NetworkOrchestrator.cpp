#include "NetworkOrchestrator.h"

#include <WiFi.h>

#include "AppLog.h"
#include "MQTTSync.h"
#include "ModeManager.h"
#include "WiFiSync.h"

namespace NetworkOrchestrator {
namespace {

constexpr char TAG[] = "NET";

Config s_config;
bool s_initialized = false;
bool s_mqttEnabled = true;
bool s_mqttInitialized = false;
unsigned long s_lastWifiCheckMs = 0;
RadioModeSwitchState s_lastRadioMode = RADIO_STATE_WIFI;
unsigned long s_nextBtActivationMs = 0;

constexpr unsigned long kBtActivationRetryMs = 3000UL;

}  // namespace

void begin(const Config& config) {
  s_config = config;
  s_initialized = true;
  s_mqttInitialized = false;
  s_lastWifiCheckMs = 0;
  s_lastRadioMode = RadioModeSwitch::getCurrentState();
  s_nextBtActivationMs = 0;
}

void setMqttEnabled(bool enabled) {
  s_mqttEnabled = enabled;
}

void quiesceForModeSwitch(RadioModeSwitchNextMode nextMode) {
  if (nextMode == RADIO_NEXT_BT) {
    LOG_I(TAG, "Quiescing runtime target=BT");
  } else if (nextMode == RADIO_NEXT_WIFI) {
    LOG_I(TAG, "Quiescing runtime target=WiFi");
  } else {
    LOG_I(TAG, "Quiescing runtime target=restart");
  }

  if (s_mqttInitialized) {
    MQTTSync::stopCore1Task();
    s_mqttInitialized = false;
  }

  ModeManager::wifiOff();
  ModeManager::btOff();

  s_lastWifiCheckMs = 0;
  s_nextBtActivationMs = 0;
}

static void startWifiStack() {
  if (ModeManager::isBtOn()) {
    LOG_I(TAG, "WiFi requested stopping_bt_first=true");
    ModeManager::btOff();
  }

  if (!ModeManager::isWifiOn()) {
    LOG_I(TAG, "WiFi requested starting_wifi_stack=true");
    ModeManager::wifiOn();
  }
}

static void startBtStack(unsigned long nowMs) {
  if (!ModeManager::isBtOn()) {
    if (s_nextBtActivationMs != 0 && nowMs < s_nextBtActivationMs) {
      return;
    }

    LOG_I(TAG, "BT requested starting_bt_stack=true");
    ModeManager::btOn();

    if (!ModeManager::isBtOn()) {
      s_nextBtActivationMs = nowMs + kBtActivationRetryMs;
      LOG_W(TAG, "BT init failed retry_ms=%lu", kBtActivationRetryMs);
    } else {
      s_nextBtActivationMs = 0;
    }
  }
}

void update() {
  if (!s_initialized) {
    return;
  }

  // Keep communication state machines in one place to avoid split ownership.
  WiFiSync::update();
  RadioModeSwitch::update();

  if (RadioModeSwitch::isInitializing()) {
    return;
  }

  const RadioModeSwitchState currentRadioMode = RadioModeSwitch::getCurrentState();

  if (currentRadioMode == RADIO_STATE_TRANSITIONING) {
    return;
  }

  if (currentRadioMode != s_lastRadioMode) {
    LOG_I(TAG, "Radio target changed target=%s", (currentRadioMode == RADIO_STATE_BT) ? "BT" : "WiFi");
    s_lastRadioMode = currentRadioMode;
    s_nextBtActivationMs = 0;
  }

  if (currentRadioMode == RADIO_STATE_BT) {
    const bool btCleanupNeeded = ModeManager::isWifiOn() || s_mqttInitialized || WiFi.getMode() != WIFI_MODE_NULL;
    if (btCleanupNeeded) {
      quiesceForModeSwitch(RADIO_NEXT_BT);
    }
    startBtStack(millis());
    return;
  }

  // MQTT is a steady-state companion to WiFi; only an actual BT handoff requires quiesce here.
  if (ModeManager::isBtOn()) {
    quiesceForModeSwitch(RADIO_NEXT_WIFI);
  }

  startWifiStack();

  if (!s_mqttEnabled) {
    if (s_mqttInitialized) {
      LOG_I(TAG, "MQTT disabled in settings stopping_service=true");
      MQTTSync::stopCore1Task();
      s_mqttInitialized = false;
    }
    return;
  }

  if (currentRadioMode == RADIO_STATE_WIFI && !s_mqttInitialized) {
    const unsigned long nowMs = millis();
    if (s_lastWifiCheckMs == 0 || nowMs - s_lastWifiCheckMs >= s_config.wifiStatusCheckMs) {
      s_lastWifiCheckMs = nowMs;
      if (WiFi.status() == WL_CONNECTED) {
        LOG_I(TAG, "WiFi connected starting_mqtt_service=true");
        MQTTSync::begin(s_config.wifiSsid, s_config.wifiPass);
        MQTTSync::startCore1Task();
        s_mqttInitialized = true;
      }
    }
  }

  if (s_mqttInitialized) {
    MQTTSync::update();
  }
}

bool isMqttInitialized() {
  return s_mqttInitialized;
}

RadioModeSwitchState getCurrentRadioState() {
  if (!s_initialized) {
    return RadioModeSwitch::getCurrentState();
  }
  return s_lastRadioMode;
}

}  // namespace NetworkOrchestrator
