#pragma once

#include <Arduino.h>
namespace BootIntroService {

struct Callbacks {
  void (*backlightOn)();
  void (*backlightOff)();
};

void begin(const Callbacks& callbacks, uint8_t buzzerPin);
void start();
bool service();
bool isActive();

}  // namespace BootIntroService
