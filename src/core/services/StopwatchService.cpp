#include "StopwatchService.h"

namespace StopwatchService {
namespace {

bool s_running = false;
unsigned long s_startMs = 0;
unsigned long s_elapsedMs = 0;

} // namespace

void start() {
  if (s_running) return;
  s_startMs = millis();
  s_running = true;
}

void stop() {
  if (!s_running) return;
  s_elapsedMs += millis() - s_startMs;
  s_running = false;
}

void reset() {
  s_running = false;
  s_elapsedMs = 0;
  s_startMs = 0;
}

StopwatchState getState() {
  if (s_running) return StopwatchState::RUNNING;
  if (s_elapsedMs > 0) return StopwatchState::STOPPED;
  return StopwatchState::IDLE;
}

uint32_t getElapsedMs() {
  if (s_running) {
    return s_elapsedMs + (millis() - s_startMs);
  }
  return s_elapsedMs;
}

bool isRunning() {
  return s_running;
}

}  // namespace StopwatchService
