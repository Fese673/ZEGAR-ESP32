#include <Arduino.h>

#include "AppBoot.h"
#include "AppLoop.h"
void setup() {
  AppBoot::runSetup();
}

void loop() {
  AppLoop::runLoop();
}
