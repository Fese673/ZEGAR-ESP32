#pragma once

#include <Arduino.h>
namespace TouchBuzzerTest {

void begin(uint8_t touchPad, uint8_t buzzerPin);
void setEnabled(bool enabled);
bool isEnabled();
void service();

}  // namespace TouchBuzzerTest