#pragma once

#include <Arduino.h>

namespace ClockAlarmService {

void tickClock(unsigned long clockTickMs, uint8_t buzzerPin);

void startAlarmMelodyDemo(uint8_t melodyIndex, uint8_t buzzerPin);
void stopAlarmMelodyDemo(uint8_t buzzerPin);
void serviceAlarmPlayback(uint8_t buzzerPin, unsigned long alarmDurationMs);

}  // namespace ClockAlarmService
