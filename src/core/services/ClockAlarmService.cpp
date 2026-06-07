#include "ClockAlarmService.h"

#include <time.h>

#include "AlarmMelodies.h"
#include "AlarmRuntime.h"
#include "AppRuntime.h"
#include "AppSettings.h"
#include "AppState.h"
#include "ClockService.h"
#include "HomeRuntime.h"
#include "RtcSyncService.h"
namespace {

AlarmRuntime::State& alarmRuntime = AlarmRuntime::mutableState();
AlarmEntry (&alarms)[AlarmRuntime::kMaxAlarms] = alarmRuntime.alarms;
int& alarmsCount = alarmRuntime.alarmsCount;
bool& alarmRinging = alarmRuntime.alarmRinging;
unsigned long& alarmStartTime = alarmRuntime.alarmStartTime;
const int& settingsAlarmMelodyIndex = AppSettings::state().alarmMelodyIndex;
const bool& buzzerEnabled = AppSettings::state().buzzerEnabled;

}  // namespace

extern bool timerRunning;

extern void updateSevenSeg();

namespace ClockAlarmService {
namespace {

enum class BackgroundMelodyMode : uint8_t {
  None,
  Demo,
  Menu,
};

BackgroundMelodyMode s_backgroundMelodyMode = BackgroundMelodyMode::None;
unsigned long s_backgroundMelodyEndMs = 0;

static int cantinaBandIndex() {
  static const int index = AlarmMelodies::indexOfId("cantinaband");
  return index;
}

static void stopBackgroundMelody(uint8_t buzzerPin) {
  if (s_backgroundMelodyMode == BackgroundMelodyMode::None) {
    return;
  }

  AlarmMelodies::stop(buzzerPin);
  s_backgroundMelodyMode = BackgroundMelodyMode::None;
  s_backgroundMelodyEndMs = 0;
}

static void startBackgroundMelody(uint8_t melodyIndex,
                                  uint8_t buzzerPin,
                                  BackgroundMelodyMode mode,
                                  unsigned long autoStopMs) {
  stopBackgroundMelody(buzzerPin);

  AlarmMelodies::start(melodyIndex, buzzerPin);
  s_backgroundMelodyMode = mode;
  s_backgroundMelodyEndMs = autoStopMs;
}

}  // namespace

void tickClock(unsigned long clockTickMs, uint8_t buzzerPin) {
  const unsigned long nowMs = millis();
  const bool timeSeeded = RtcSyncService::isClockSeeded();
  const bool timeValid = RtcSyncService::isSystemTimeValid();

  if (timeSeeded && timeValid) {
    if (nowMs - Clock::lastTick() >= clockTickMs) {
      Clock::setLastTick(nowMs - ((nowMs - Clock::lastTick()) % clockTickMs));
      int h, m, s;
      RtcSyncService::syncLocalClockFromSystemTime(h, m, s);
      Clock::set(h, m, s);
      if (appState != STATE_STOPER) {
        updateSevenSeg();
      }
      if (appState == STATE_HOME) {
        HomeRuntime::markHomeDirty();
      }
    }
  } else if (timeSeeded) {
    if (nowMs - Clock::lastTick() >= clockTickMs) {
      while (nowMs - Clock::lastTick() >= clockTickMs) {
        Clock::setLastTick(Clock::lastTick() + clockTickMs);
        Clock::tickSecond();
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

  if (timeValid && !alarmRinging && alarmsCount > 0) {
    static int s_lastAlarmMinute = -1;
    const int currentMinute = Clock::minutes();
    if (currentMinute != s_lastAlarmMinute) {
      s_lastAlarmMinute = currentMinute;
      const time_t nowTime = time(nullptr);
      struct tm timeInfo;
      localtime_r(&nowTime, &timeInfo);
      const int today = timeInfo.tm_yday;

      for (int i = 0; i < alarmsCount; ++i) {
        if (!alarms[i].enabled) continue;

        // Sprawdź dayMask — czy dziś ma dzwonić?
        // tm_wday: 0=Nd, 1=Pn..6=So → maska bit0=Pn..bit6=Nd
        const int dayBit = (timeInfo.tm_wday + 6) % 7;
        if ((alarms[i].dayMask & (1 << dayBit)) == 0) continue;

        if (alarms[i].hour == Clock::hours() &&
            alarms[i].minute == currentMinute &&
            alarms[i].lastTriggerDay != (uint16_t)today) {
          // SingleShot — wyłącz po odpaleniu (DOPIERO tutaj, po potwierdzeniu czasu)
          if (alarms[i].flags & 0x01) {
            alarms[i].enabled = false;
          }
          alarmStartTime = millis();
          alarms[i].lastTriggerDay = (uint16_t)today;
          alarmRuntime.ringingAlarmIndex = i; // zapamiętaj KTÓRY dzwoni
          g_nvsAlarmsDirty = true;
          if (buzzerEnabled) {
            alarmRinging = true;
            AlarmMelodies::start((uint8_t)settingsAlarmMelodyIndex, buzzerPin);
          }
          break;
        }
      }
    }
  }
}

void startAlarmMelodyDemo(uint8_t melodyIndex, uint8_t buzzerPin) {
  startBackgroundMelody(melodyIndex,
                         buzzerPin,
                         BackgroundMelodyMode::Demo,
                         millis() + 10000UL);
}

void stopAlarmMelodyDemo(uint8_t buzzerPin) {
  if (s_backgroundMelodyMode != BackgroundMelodyMode::Demo) {
    return;
  }

  stopBackgroundMelody(buzzerPin);
}

void startMenuMusic(uint8_t buzzerPin) {
  const int index = cantinaBandIndex();
  if (index < 0) {
    return;
  }

  startBackgroundMelody(static_cast<uint8_t>(index),
                        buzzerPin,
                        BackgroundMelodyMode::Menu,
                        0);
}

void stopMenuMusic(uint8_t buzzerPin) {
  if (s_backgroundMelodyMode != BackgroundMelodyMode::Menu) {
    return;
  }

  stopBackgroundMelody(buzzerPin);
}

void serviceAlarmPlayback(uint8_t buzzerPin, unsigned long alarmDurationMs) {
  if (alarmRinging) {
    AlarmMelodies::service(buzzerPin, millis());
    if (millis() - alarmStartTime >= alarmDurationMs) {
      AlarmMelodies::stop(buzzerPin);
      alarmRinging = false;
      // Wyłącz konkretny alarm który dzwonił (nie legacy alarmEnabled)
      int idx = alarmRuntime.ringingAlarmIndex;
      if (idx >= 0 && idx < alarmsCount) {
        alarms[idx].enabled = false;
        g_nvsAlarmsDirty = true;
      }
      alarmRuntime.ringingAlarmIndex = -1;
      updateSevenSeg();
      return;
    }

    updateSevenSeg();
    return;
  }

  if (s_backgroundMelodyMode == BackgroundMelodyMode::None) {
    return;
  }

  AlarmMelodies::service(buzzerPin, millis());
  if (s_backgroundMelodyMode == BackgroundMelodyMode::Demo &&
      (long)(millis() - s_backgroundMelodyEndMs) >= 0) {
    stopBackgroundMelody(buzzerPin);
  }
}

}  // namespace ClockAlarmService
