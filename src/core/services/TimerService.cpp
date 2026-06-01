#include "TimerService.h"
#include "AppRuntime.h"
#include "AlarmMelodies.h"
#include "AppSettings.h"
#include "AlarmRuntime.h"
#include "TimeSyncProtocol.h"
#include "comms/esp_to_gution/Esptogution.h"

// Backward compat z LCD i UI — nadal ustawiamy te globale
extern bool timerRunning;
extern unsigned long timerStartMillis;
extern unsigned long timerDurationMs;

namespace TimerService {
namespace {

TimerState s_state = TimerState::IDLE;
uint32_t s_durationSec = 0;
unsigned long s_remainingMs = 0;
unsigned long s_startMs = 0;
unsigned long s_ringStartMs = 0;

} // namespace

void start(uint32_t durationSec) {
  if (durationSec == 0 || durationSec > 86400UL) durationSec = 86400UL;
  s_durationSec = durationSec;
  s_remainingMs = durationSec * 1000UL;
  s_startMs = millis();
  s_state = TimerState::RUNNING;
  timerRunning = true;
  timerStartMillis = s_startMs;
  timerDurationMs = s_remainingMs;
  TimeSync::sendTimerState();
}

void stop() {
  s_state = TimerState::IDLE;
  s_remainingMs = 0;
  s_durationSec = 0;
  timerRunning = false;
  timerStartMillis = 0;
  timerDurationMs = 0;
  TimeSync::sendTimerState();
}

void pause() {
  if (s_state != TimerState::RUNNING) return;
  unsigned long elapsed = millis() - s_startMs;
  if (elapsed < s_remainingMs) {
    s_remainingMs -= elapsed;
  } else {
    s_remainingMs = 0;
  }
  s_state = TimerState::PAUSED;
  timerRunning = false;
  timerDurationMs = s_remainingMs; // zachowaj pozostały czas dla LCD
  TimeSync::sendTimerState();
}

void resume() {
  if (s_state != TimerState::PAUSED) return;
  s_state = TimerState::RUNNING;
  s_startMs = millis();
  timerRunning = true;
  timerStartMillis = s_startMs;
  timerDurationMs = s_remainingMs; // przywróć pozostały czas dla LCD
  TimeSync::sendTimerState();
}

void togglePause() {
  if (s_state == TimerState::RUNNING) pause();
  else if (s_state == TimerState::PAUSED) resume();
}

TimerState getState() { return s_state; }

uint32_t getRemainingSec() {
  if (s_state == TimerState::IDLE) return 0;
  uint32_t ms;
  if (s_state == TimerState::RUNNING) {
    unsigned long elapsed = millis() - s_startMs;
    ms = (elapsed < s_remainingMs) ? (s_remainingMs - elapsed) : 0;
  } else {
    ms = s_remainingMs;
  }
  return ms / 1000UL;
}

uint32_t getDurationSec() { return s_durationSec; }

bool isRinging() { return s_state == TimerState::RINGING; }

void dismissRing() {
  if (s_state != TimerState::RINGING) return;
  AlarmMelodies::stop(BUZZER_PIN);
  s_state = TimerState::IDLE;
  s_remainingMs = 0;
  s_durationSec = 0;
  s_ringStartMs = 0;
  TimeSync::sendTimerState();
}

void tick(uint8_t buzzerPin) {
  if (s_state != TimerState::RUNNING) return;
  unsigned long elapsed = millis() - s_startMs;
  if (elapsed >= s_remainingMs) {
    s_state = TimerState::RINGING;
    s_ringStartMs = millis();
    s_remainingMs = 0;
    timerRunning = false;
    timerDurationMs = 0;

    // Włącz buzzer — NIE używamy alarmRinging
    if (AppSettings::state().buzzerEnabled) {
      AlarmMelodies::start(
        (uint8_t)AppSettings::state().alarmMelodyIndex,
        buzzerPin
      );
    }
    TimeSync::sendTimerState();
  }
}

void servicePlayback(uint8_t buzzerPin, unsigned long autoStopMs) {
  if (s_state != TimerState::RINGING) return;
  // Alarm główny ma wyższy priorytet — nie mieszaj dźwięków
  if (AlarmRuntime::state().alarmRinging) return;
  AlarmMelodies::service(buzzerPin, millis());
  if (millis() - s_ringStartMs >= autoStopMs) {
    AlarmMelodies::stop(buzzerPin);
    s_state = TimerState::IDLE;
    s_remainingMs = 0;
    s_durationSec = 0;
    s_ringStartMs = 0;
    // Natychmiastowy push do Gution — odśwież stan timera i dzwonka
    TimeSync::sendTimerState();
    EsptoGuition::sendStatusBell(EsptoGuition::nextSequence());
  }
}

void reset() {
  s_state = TimerState::IDLE;
  s_remainingMs = 0;
  s_durationSec = 0;
  s_startMs = 0;
  s_ringStartMs = 0;
  timerRunning = false;
  timerDurationMs = 0;
  TimeSync::sendTimerState();
}

} // namespace TimerService
