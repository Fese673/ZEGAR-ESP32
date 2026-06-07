#pragma once

#include <Arduino.h>

#include "Encoder.h"
namespace TankGame {

void begin();
void stop();
void handleEvent(EncoderEvent event);
bool service(unsigned long nowMs);
void draw();

}  // namespace TankGame
