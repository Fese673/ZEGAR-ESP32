#pragma once

#include <Arduino.h>

#include "RadioModeSwitch.h"

namespace NetworkOrchestrator {

struct Config {
  const char* wifiSsid = nullptr;
  const char* wifiPass = nullptr;
  unsigned long wifiStatusCheckMs = 2000UL;
};

void begin(const Config& config);
void setMqttEnabled(bool enabled);
void quiesceForModeSwitch(RadioModeSwitchNextMode nextMode);
void update();

bool isMqttInitialized();
RadioModeSwitchState getCurrentRadioState();

}  // namespace NetworkOrchestrator
