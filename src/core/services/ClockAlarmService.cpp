#include "ClockAlarmService.h"

#include <time.h>

#include "AlarmMelodies.h"
#include "AlarmRuntime.h"
#include "AppSettings.h"
#include "AppState.h"
#include "HomeRuntime.h"
#include "RtcSyncService.h"

// Global clock/timer runtime state from main.cpp
extern int hours;
extern int minutes;
extern int seconds;
extern unsigned long lastTick;

// Shared alarm runtime state.
namespace {

AlarmRuntime::State& alarmRuntime = AlarmRuntime::mutableState();
AlarmEntry (&alarms)[AlarmRuntime::kMaxAlarms] = alarmRuntime.alarms;
int& alarmsCount = alarmRuntime.alarmsCount;
bool& alarmEnabled = alarmRuntime.alarmEnabled;
bool& alarmRinging = alarmRuntime.alarmRinging;
unsigned long& alarmStartTime = alarmRuntime.alarmStartTime;
const int& settingsAlarmMelodyIndex = AppSettings::state().alarmMelodyIndex;
const bool& buzzerEnabled = AppSettings::state().buzzerEnabled;

}  // namespace

extern bool timerRunning;
extern unsigned long timerStartMillis;
extern unsigned long timerDurationMs;

extern EditState editState;

extern void updateSevenSeg();

namespace ClockAlarmService {
namespace {

bool s_alarmMelodyDemoActive = false;
unsigned long s_alarmMelodyDemoEndMs = 0;

}  // namespace

void tickClock(unsigned long clockTickMs, uint8_t buzzerPin) {
  if (appState == STATE_SET_TIME) {
    return;
  }

  const unsigned long nowMs = millis();
  const bool timeSeeded = RtcSyncService::isClockSeeded();
  const bool timeValid = RtcSyncService::isSystemTimeValid();

  if (timeSeeded && timeValid) {
    if (nowMs - lastTick >= clockTickMs) {
      lastTick = nowMs - ((nowMs - lastTick) % clockTickMs);
      RtcSyncService::syncLocalClockFromSystemTime(hours, minutes, seconds);
      if (appState != STATE_STOPER) {
        updateSevenSeg();
      }
      if (appState == STATE_HOME) {
        HomeRuntime::markHomeDirty();
      }
    }
  } else if (timeSeeded) {
    if (nowMs - lastTick >= clockTickMs) {
      int loops = 0;
      while (nowMs - lastTick >= clockTickMs && loops < 60) {
        lastTick += clockTickMs;
        seconds++;
        if (seconds >= 60) {
          seconds = 0;
          minutes++;
          if (minutes >= 60) {
            minutes = 0;
            hours = (hours + 1) % 24;
          }
        }
        loops++;
      }

      if (appState != STATE_STOPER) {
        updateSevenSeg();
      }
      if (appState == STATE_HOME) {
        HomeRuntime::markHomeDirty();
      }
    }
  } else if (timerRunning && appState != STATE_STOPER) {
    updateSevenSeg();
  }

  if (timeValid && !alarmRinging && seconds == 0 && alarmsCount > 0) {
    const time_t nowTime = time(nullptr);
    tm timeInfo;
    localtime_r(&nowTime, &timeInfo);
    const int today = timeInfo.tm_yday;

    for (int i = 0; i < alarmsCount; ++i) {
      if (!alarms[i].enabled) continue;

      if (alarms[i].hour == hours &&
          alarms[i].minute == minutes &&
          alarms[i].lastTriggerDay != (uint16_t)today) {
        alarmStartTime = millis();
        alarms[i].lastTriggerDay = (uint16_t)today;
        if (buzzerEnabled) {
          alarmRinging = true;
          AlarmMelodies::start((uint8_t)settingsAlarmMelodyIndex, buzzerPin);
        }
        break;
      }
    }
  }

  if (timerRunning) {
    const unsigned long elapsed = millis() - timerStartMillis;
    if (elapsed >= timerDurationMs) {
      timerRunning = false;
      alarmStartTime = millis();
      if (buzzerEnabled) {
        alarmRinging = true;
        AlarmMelodies::start((uint8_t)settingsAlarmMelodyIndex, buzzerPin);
      }
      editState = EDIT_HOURS;
      timerStartMillis = 0;
      timerDurationMs = 0;
    }
  }
}

void startAlarmMelodyDemo(uint8_t melodyIndex, uint8_t buzzerPin) {
  AlarmMelodies::start(melodyIndex, buzzerPin);
  s_alarmMelodyDemoActive = true;
  s_alarmMelodyDemoEndMs = millis() + 10000UL;
}

void stopAlarmMelodyDemo(uint8_t buzzerPin) {
  if (!s_alarmMelodyDemoActive) {
    return;
  }

  AlarmMelodies::stop(buzzerPin);
  s_alarmMelodyDemoActive = false;
  s_alarmMelodyDemoEndMs = 0;
}

void serviceAlarmPlayback(uint8_t buzzerPin, unsigned long alarmDurationMs) {
  if (alarmRinging) {
    AlarmMelodies::service(buzzerPin, millis());
    if (millis() - alarmStartTime >= alarmDurationMs) {
      AlarmMelodies::stop(buzzerPin);
      alarmRinging = false;
      alarmEnabled = false;
    }
    return;
  }

  if (!s_alarmMelodyDemoActive) {
    return;
  }

  AlarmMelodies::service(buzzerPin, millis());
  if ((long)(millis() - s_alarmMelodyDemoEndMs) >= 0) {
    stopAlarmMelodyDemo(buzzerPin);
  }
}

}  // namespace ClockAlarmService
