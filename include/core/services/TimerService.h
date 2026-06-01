#pragma once
#include <stdint.h>
#include <Arduino.h>

enum class TimerState : uint8_t {
  IDLE = 0,
  RUNNING = 1,
  PAUSED = 2,
  RINGING = 3,
};

namespace TimerService {

void start(uint32_t durationSec);
void stop();
void pause();
void resume();
void togglePause();

TimerState getState();
uint32_t getRemainingSec();
uint32_t getDurationSec();

// Wołane co 1s z EventBus (AppLoop)
void tick(uint8_t buzzerPin);

// Czy timer aktualnie dzwoni (dla StatusBell)
bool isRinging();

// Wyłącz dzwonek timera (dla TimeSyncProtocol / enkodera)
void dismissRing();

// Service playback — wołane z AppLoop
void servicePlayback(uint8_t buzzerPin, unsigned long autoStopMs);

// Dla testów/reset
void reset();

}  // namespace TimerService
