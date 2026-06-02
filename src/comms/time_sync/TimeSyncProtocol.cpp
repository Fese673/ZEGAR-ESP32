#include "TimeSyncProtocol.h"
#include "comms/esp_to_gution/EsptoGuitionTransport.h"
#include "comms/esp_to_gution/Esptogution.h"
#include "AlarmRuntime.h"
#include "AlarmMelodies.h"
#include "TimerService.h"
#include "StopwatchService.h"
#include "AppSettings.h"
#include "AppRuntime.h"
#include "Board_Pins.h"
#include <time.h>

#include "AppState.h"
extern volatile bool g_nvsAlarmsDirty;

namespace TimeSync {

// ─── Send: AlarmList ────────────────────────────────────────────────
void sendAlarmList() {
  const auto& rt = AlarmRuntime::state();
  // max 1 + 8*7 = 57 bajtów
  uint8_t buf[1 + AlarmRuntime::kMaxAlarms * 7];
  uint8_t* p = buf;
  *p++ = (uint8_t)rt.alarmsCount;

  for (int i = 0; i < rt.alarmsCount && i < AlarmRuntime::kMaxAlarms; ++i) {
    const AlarmEntry& a = rt.alarms[i];
    *p++ = a.hour;
    *p++ = a.minute;
    *p++ = a.enabled ? 1 : 0;
    *p++ = a.dayMask;
    *p++ = a.flags;
    *p++ = 0; // padding
    *p++ = 0; // padding
  }

  uint16_t len = (uint16_t)(p - buf);
  EsptoGuition::sendRawFrame(kTypeAlarmList, EsptoGuition::nextSequence(), buf, len);
}

// ─── Send: TimerState ───────────────────────────────────────────────
void sendTimerState() {
  uint8_t buf[9];
  buf[0] = (uint8_t)TimerService::getState();
  uint32_t rem = TimerService::getRemainingSec();
  uint32_t dur = TimerService::getDurationSec();
  buf[1] = (uint8_t)(rem & 0xFF);
  buf[2] = (uint8_t)((rem >> 8) & 0xFF);
  buf[3] = (uint8_t)((rem >> 16) & 0xFF);
  buf[4] = (uint8_t)((rem >> 24) & 0xFF);
  buf[5] = (uint8_t)(dur & 0xFF);
  buf[6] = (uint8_t)((dur >> 8) & 0xFF);
  buf[7] = (uint8_t)((dur >> 16) & 0xFF);
  buf[8] = (uint8_t)((dur >> 24) & 0xFF);
  EsptoGuition::sendRawFrame(kTypeTimerState, EsptoGuition::nextSequence(), buf, 9);
}

// ─── Send: EditLock ─────────────────────────────────────────────────
void sendEditLock(bool locked) {
  uint8_t buf[1] = { locked ? (uint8_t)1 : (uint8_t)0 };
  EsptoGuition::sendRawFrame(kTypeEditLock, EsptoGuition::nextSequence(), buf, 1);
}

static bool alarmCoreFieldsMatch(const AlarmEntry& lhs, const AlarmEntry& rhs) {
  return lhs.hour == rhs.hour &&
         lhs.minute == rhs.minute &&
         lhs.enabled == rhs.enabled &&
         lhs.dayMask == rhs.dayMask &&
         lhs.flags == rhs.flags;
}

void handleAlarmListSync(const uint8_t* payload, uint16_t len) {
  if (payload == nullptr || len < 1) return;
  if (g_alarmEditActive) return;

  uint8_t count = payload[0];
  if (count > AlarmRuntime::kMaxAlarms) {
    count = AlarmRuntime::kMaxAlarms;
  }

  const uint16_t requiredLen = static_cast<uint16_t>(1 + (count * 7));
  if (len < requiredLen) return;

  auto& st = AlarmRuntime::mutableState();
  AlarmEntry previous[AlarmRuntime::kMaxAlarms] = {};
  const int previousCount = st.alarmsCount;
  for (int i = 0; i < previousCount; ++i) {
    previous[i] = st.alarms[i];
  }

  bool matched[AlarmRuntime::kMaxAlarms] = {};
  for (int i = 0; i < count; ++i) {
    const uint8_t* p = payload + 1 + (i * 7);
    AlarmEntry next = {};
    next.hour = p[0];
    next.minute = p[1];
    next.enabled = p[2] != 0;
    next.dayMask = p[3];
    next.flags = p[4];
    next.lastTriggerDay = UINT16_MAX;

    for (int j = 0; j < previousCount; ++j) {
      if (matched[j]) continue;
      if (alarmCoreFieldsMatch(previous[j], next)) {
        next.lastTriggerDay = previous[j].lastTriggerDay;
        matched[j] = true;
        break;
      }
    }

    st.alarms[i] = next;
  }

  for (int i = count; i < AlarmRuntime::kMaxAlarms; ++i) {
    st.alarms[i] = AlarmEntry{};
  }
  st.alarmsCount = count;
  if (!st.alarmRinging) {
    st.ringingAlarmIndex = -1;
  }

  g_nvsAlarmsDirty = true;
  sendAlarmList();
}

// ─── Handle: SetAlarm (Gution -> Zegar) ─────────────────────────────
void handleSetAlarm(const uint8_t* payload, uint16_t len) {
  // Format: [index(1)] [hour(1)] [min(1)] [enabled(1)] [dayMask(1)] [flags(1)]
  if (len < 6) return;

  // Odrzuć jeśli użytkownik edytuje alarm enkoderem
  if (g_alarmEditActive) return;

  int idx = payload[0];
  if (idx < 0 || idx >= AlarmRuntime::kMaxAlarms) return;

  auto& st = AlarmRuntime::mutableState();
  AlarmEntry& a = st.alarms[idx];
  a.hour = payload[1];
  a.minute = payload[2];
  a.enabled = payload[3] != 0;
  a.dayMask = payload[4];
  a.flags = payload[5];
  a.lastTriggerDay = UINT16_MAX;

  // Jeśli to nowy alarm (index >= count) zwiększ licznik
  if (idx >= st.alarmsCount) {
    st.alarmsCount = idx + 1;
    if (st.alarmsCount > AlarmRuntime::kMaxAlarms)
      st.alarmsCount = AlarmRuntime::kMaxAlarms;
  }

  // Deferred NVS write — unikamy blokady wątku UART
  g_nvsAlarmsDirty = true;

  // Odpowiedz zaktualizowaną listą
  sendAlarmList();
}

// ─── Handle: TimerCmd (Gution -> Zegar) ─────────────────────────────
void handleTimerCmd(const uint8_t* payload, uint16_t len) {
  if (len < 1) return;
  uint8_t cmd = payload[0];

  switch (cmd) {
    case 0: { // START
      if (len >= 5) {
        uint32_t dur = (uint32_t)payload[1]
                     | ((uint32_t)payload[2] << 8)
                     | ((uint32_t)payload[3] << 16)
                     | ((uint32_t)payload[4] << 24);
        if (dur > 0 && dur <= 86400) {
          TimerService::start(dur);
        }
      }
      break;
    }
    case 1: // STOP
      TimerService::stop();
      break;
    case 2: // PAUSE
      TimerService::pause();
      break;
    case 3: // RESUME
      TimerService::resume();
      break;
    default:
      return;
  }
  // Natychmiastowy push stanu do Gution — nie czekaj na broadcast 1s
  sendTimerState();
  EsptoGuition::sendStatusBell(EsptoGuition::nextSequence());
}

// ─── Send: StopwatchState ────────────────────────────────────────
void sendStopwatchState() {
  uint8_t buf[5];
  uint32_t elapsedMs = StopwatchService::getElapsedMs();
  buf[0] = (uint8_t)StopwatchService::getState();
  buf[1] = (uint8_t)(elapsedMs & 0xFF);
  buf[2] = (uint8_t)((elapsedMs >> 8) & 0xFF);
  buf[3] = (uint8_t)((elapsedMs >> 16) & 0xFF);
  buf[4] = (uint8_t)((elapsedMs >> 24) & 0xFF);
  EsptoGuition::sendRawFrame(kTypeStopwatchState, EsptoGuition::nextSequence(), buf, 5);
}

// ─── Handle: StopwatchCmd (Gution -> Zegar) ─────────────────────
void handleStopwatchCmd(const uint8_t* payload, uint16_t len) {
  if (len < 1) return;
  uint8_t cmd = payload[0];

  switch (cmd) {
    case 0: // START
      StopwatchService::start();
      break;
    case 1: // STOP
      StopwatchService::stop();
      break;
    case 2: // RESET
      StopwatchService::reset();
      break;
    default:
      return;
  }
  sendStopwatchState();
}

// ─── Handle: AlarmAction (Gution -> Zegar) ──────────────────────────
void handleAlarmAction(const uint8_t* payload, uint16_t len) {
  if (len < 1) return;
  uint8_t action = payload[0];

  if (TimerService::isRinging()) {
    // Dzwoni timer — obsłuż przez TimerService
    if (action == 0) {
      // Dismiss (całkowite wyłączenie)
      TimerService::dismissRing();
    } else {
      // Snooze — stop + restart za 5 minut
      TimerService::dismissRing();
      TimerService::start(300); // 5 minut
    }
    sendTimerState(); // natychmiastowy push do HMI
    return;
  }

  if (AlarmRuntime::state().alarmRinging) {
    auto& rt = AlarmRuntime::mutableState();
    // Indeks dzwoniącego alarmu — ustawiony przez ClockAlarmService przy odpaleniu
    int ringingIdx = rt.ringingAlarmIndex;
    rt.ringingAlarmIndex = -1;

    if (action == 0) {
      // Dismiss
      AlarmMelodies::stop(BUZZER_PIN);
      rt.alarmRinging = false;
      rt.alarmStartTime = 0;
      if (ringingIdx >= 0 && ringingIdx < rt.alarmsCount) {
        rt.alarms[ringingIdx].enabled = false;
        g_nvsAlarmsDirty = true;
      }
    } else {
      // Snooze — stop, wznow za 5 minut
      AlarmMelodies::stop(BUZZER_PIN);
      rt.alarmRinging = false;
      rt.alarmStartTime = 0;
      if (ringingIdx >= 0 && ringingIdx < rt.alarmsCount) {
        time_t now = time(nullptr);
        struct tm snooze_tm;
        now += 300; // +5 minut
        localtime_r(&now, &snooze_tm);
        rt.alarms[ringingIdx].hour = (uint8_t)snooze_tm.tm_hour;
        rt.alarms[ringingIdx].minute = (uint8_t)snooze_tm.tm_min;
        rt.alarms[ringingIdx].enabled = true;
        rt.alarms[ringingIdx].lastTriggerDay = (uint16_t)snooze_tm.tm_yday;
        g_nvsAlarmsDirty = true;
      }
    }
    sendTimerState();
  }
}

} // namespace TimeSync
