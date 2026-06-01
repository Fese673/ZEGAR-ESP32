#pragma once
#include <stdint.h>
#include <Arduino.h>

enum class StopwatchState : uint8_t {
  IDLE     = 0,
  RUNNING  = 1,
  STOPPED  = 2,
};

namespace StopwatchService {

// State machine
void start();
void stop();
void reset();

// Read-only
StopwatchState getState();
uint32_t getElapsedMs();
bool isRunning();

}  // namespace StopwatchService
