#pragma once

#include <Arduino.h>

#include "AppState.h"
namespace HomeRuntime {

struct DrawCallbacks {
  void (*drawHome)();
  void (*drawIndoorWeather)();
  void (*drawOutdoorAir)();
  void (*drawExtremeEnvironment)();
  void (*drawSystemResources)();
  void (*drawExtremeAlgorithms)();
};

void begin(const DrawCallbacks& callbacks, uint8_t initialProfileIndex, uint8_t overlaySwitchSeconds);

void setProfile(uint8_t profileIndex);
uint8_t getProfile();

void setOverlayIntervalSeconds(uint8_t seconds);
uint8_t getOverlayIntervalSeconds();

void markHomeDirty();
void serviceRedraw(AppState appState);
void serviceOverlayRotation(AppState appState);
void handleHomeEntryIfStateChanged(AppState appState);

}  // namespace HomeRuntime
