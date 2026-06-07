#pragma once

#include <Arduino.h>

#include "BMP280Screen.h"
namespace BMP280Sensor {

void begin();
void update();
uint8_t menuItemCount();
bool altitudeViewEnabled();
void setPressureOffset(float hpa);
float getPressureOffset();

}  // namespace BMP280Sensor
