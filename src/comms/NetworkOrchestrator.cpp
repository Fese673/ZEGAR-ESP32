#include "NetworkOrchestrator.h"

#include <WiFi.h>

#include "MQTTSync.h"
#include "WiFiSync.h"

namespace NetworkOrchestrator {
namespace {

Config s_config;
bool s_initialized = false;
bool s_mqttEnabled = true;
bool s_mqttInitialized = false;
bool s_btIsolationEnforced = false;
unsigned long s_lastWifiCheckMs = 0;
RadioModeSwitchState s_lastRadioMode = RADIO_STATE_WIFI;

}  // namespace

void begin(const Config& config) {
  s_config = config;
  s_initialized = true;
  s_mqttInitialized = false;
  s_btIsolationEnforced = false;
  s_lastWifiCheckMs = 0;
  s_lastRadioMode = RadioModeSwitch::getCurrentState();
}

void setMqttEnabled(bool enabled) {
  s_mqttEnabled = enabled;
}

void update() {
  if (!s_initialized) {
    return;
  }

  // Keep communication state machines in one place to avoid split ownership.
  WiFiSync::update();
  RadioModeSwitch::update();

  const RadioModeSwitchState currentRadioMode = RadioModeSwitch::getCurrentState();

  if (currentRadioMode == RADIO_STATE_BT) {
    if (!s_btIsolationEnforced) {
      Serial.println("[NetworkOrchestrator] Enforcing BT isolation: stopping WiFi and MQTT");
      WiFiSync::stop();
      if (s_mqttInitialized) {
        MQTTSync::stopCore1Task();
        s_mqttInitialized = false;
      }
      s_btIsolationEnforced = true;
    }
    s_lastRadioMode = RADIO_STATE_BT;
    return;
  }

  s_btIsolationEnforced = false;

  if (!s_mqttEnabled) {
    if (s_mqttInitialized) {
      Serial.println("[NetworkOrchestrator] MQTT disabled in settings -> stopping task");
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
        Serial.println("[NetworkOrchestrator] WiFi connected -> starting MQTT task");
        MQTTSync::begin(s_config.wifiSsid, s_config.wifiPass);
        MQTTSync::startCore1Task();
        s_mqttInitialized = true;
        s_lastRadioMode = RADIO_STATE_WIFI;
      }
    }
    return;
  }

  s_lastRadioMode = currentRadioMode;
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
