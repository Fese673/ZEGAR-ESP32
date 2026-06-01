#include "AlarmRuntime.h"
#include <cstdio>

namespace AlarmRuntime {

State& mutableState() {
  static State s_state;
  return s_state;
}

const State& state() {
  return mutableState();
}

void reset() {
  mutableState() = State{};
}

bool isAnyAlarmArmed() {
  const State &st = state();
  if (st.alarmRinging) return true;
  for (int i = 0; i < st.alarmsCount; ++i) {
    if (st.alarms[i].enabled) return true;
  }
  return false;
}

void saveAlarm(Preferences &prefs, int idx) {
  if (idx < 0 || idx >= kMaxAlarms) return;
  const AlarmEntry &a = mutableState().alarms[idx];
  char buf[16];
  snprintf(buf, sizeof(buf), "almH%d", idx); prefs.putUChar(buf, a.hour);
  snprintf(buf, sizeof(buf), "almM%d", idx); prefs.putUChar(buf, a.minute);
  snprintf(buf, sizeof(buf), "almE%d", idx); prefs.putBool(buf, a.enabled);
  snprintf(buf, sizeof(buf), "almD%d", idx); prefs.putUChar(buf, a.dayMask);
  snprintf(buf, sizeof(buf), "almF%d", idx); prefs.putUChar(buf, a.flags);
  snprintf(buf, sizeof(buf), "almT%d", idx); prefs.putUShort(buf, a.lastTriggerDay);
}

void saveAllAlarms(Preferences &prefs) {
  State &st = mutableState();
  prefs.putUShort("alarmCount", (uint16_t)st.alarmsCount);
  for (int i = 0; i < st.alarmsCount; ++i) {
    saveAlarm(prefs, i);
  }
}

void loadAllAlarms(Preferences &prefs) {
  State &st = mutableState();

  // Nowy klucz alarmCount
  st.alarmsCount = (int)prefs.getUShort("alarmCount", 0);

  // Fallback: stary klucz (pre-rozszerzenia) — migracja
  if (st.alarmsCount == 0 && prefs.isKey("a0h")) {
    // Próbuj odczytać stare klucze
    int count = 0;
    for (int i = 0; i < kMaxAlarms; ++i) {
      char keyH[12], keyM[12], keyE[12];
      snprintf(keyH, sizeof(keyH), "a%dh", i);
      snprintf(keyM, sizeof(keyM), "a%dm", i);
      snprintf(keyE, sizeof(keyE), "a%de", i);
      if (!prefs.isKey(keyH)) break;

      AlarmEntry &a = st.alarms[i];
      a.hour = (uint8_t)prefs.getUShort(keyH, 7);
      a.minute = (uint8_t)prefs.getUShort(keyM, 0);
      a.enabled = prefs.getBool(keyE, true);
      a.dayMask = 0x7F;   // domyślnie wszystkie dni
      a.flags = 0;
      a.lastTriggerDay = UINT16_MAX;
      count++;
    }
    if (count > 0) {
      st.alarmsCount = count;
      // Zapisz od razu w nowym formacie
      prefs.putUShort("alarmCount", (uint16_t)count);
      for (int i = 0; i < count; ++i) saveAlarm(prefs, i);
      return;
    }
  }

  // Ograniczenie do max
  if (st.alarmsCount > kMaxAlarms) st.alarmsCount = kMaxAlarms;

  for (int i = 0; i < st.alarmsCount; ++i) {
    AlarmEntry &a = st.alarms[i];
    char buf[16];
    snprintf(buf, sizeof(buf), "almH%d", i); a.hour = prefs.getUChar(buf, 7);
    snprintf(buf, sizeof(buf), "almM%d", i); a.minute = prefs.getUChar(buf, 0);
    snprintf(buf, sizeof(buf), "almE%d", i); a.enabled = prefs.getBool(buf, true);
    snprintf(buf, sizeof(buf), "almD%d", i); a.dayMask = prefs.getUChar(buf, 0x7F);
    snprintf(buf, sizeof(buf), "almF%d", i); a.flags = prefs.getUChar(buf, 0);
    snprintf(buf, sizeof(buf), "almT%d", i); a.lastTriggerDay = prefs.getUShort(buf, UINT16_MAX);
  }
}

}  // namespace AlarmRuntime