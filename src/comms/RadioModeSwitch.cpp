#include "RadioModeSwitch.h"

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>

#include "AppLog.h"
#include "ClockService.h"
#include "NetworkOrchestrator.h"
#include "StatsManager.h"

namespace RadioModeSwitch {
void clearRTCTime();
}

namespace {

constexpr char TAG[] = "RADIO";

constexpr uint32_t kRtcFlagWifi = 0x1234UL;
constexpr uint32_t kRtcFlagBt = 0x5678UL;
constexpr uint32_t kRtcFlagNone = 0x0000UL;
constexpr unsigned long kRestartDelayMs = 100UL;
constexpr unsigned long kStartupDelayMs = 200UL;

struct RtcState {
  uint32_t mode_flag;
  uint8_t hours;
  uint8_t minutes;
  uint8_t seconds;
};

RTC_NOINIT_ATTR static RtcState rtc_state;

RadioModeSwitchState s_current_state = RADIO_STATE_WIFI;
RadioModeSwitchNextMode s_next_mode = RADIO_NEXT_NONE;
bool s_initialized = false;
bool s_start_mode_ready = false;
bool s_boot_handoff_detected = false;
unsigned long s_start_time_ms = 0;
bool s_restart_pending = false;
unsigned long s_restart_deadline_ms = 0;

const char* stateToText(RadioModeSwitchState state) {
  switch (state) {
    case RADIO_STATE_WIFI: return "WiFi";
    case RADIO_STATE_BT: return "Bluetooth";
    case RADIO_STATE_TRANSITIONING: return "Transitioning";
    default: return "Unknown";
  }
}

const char* nextModeToText(RadioModeSwitchNextMode mode) {
  switch (mode) {
    case RADIO_NEXT_WIFI: return "WiFi";
    case RADIO_NEXT_BT: return "Bluetooth";
    case RADIO_NEXT_NONE: return "Neutral";
    default: return "Unknown";
  }
}

RadioModeSwitchNextMode modeFromRtcFlag(uint32_t rtcFlag) {
  if (rtcFlag == kRtcFlagBt) {
    return RADIO_NEXT_BT;
  }

  return RADIO_NEXT_WIFI;
}

bool isValidRtcModeFlag(uint32_t rtcFlag) {
  return rtcFlag == kRtcFlagWifi || rtcFlag == kRtcFlagBt;
}

void storeRtcSnapshot(uint32_t modeFlag) {
  rtc_state.hours = (uint8_t)Clock::hours();
  rtc_state.minutes = (uint8_t)Clock::minutes();
  rtc_state.seconds = (uint8_t)Clock::seconds();
  rtc_state.mode_flag = modeFlag;
}

void scheduleRestart() {
  s_current_state = RADIO_STATE_TRANSITIONING;
  s_restart_pending = true;
  s_restart_deadline_ms = millis() + kRestartDelayMs;
}

void loadBootModeFromRtc() {
  const uint32_t rtcFlag = rtc_state.mode_flag;

    LOG_I(TAG,
      "Rtc flag=0x%04lX time=%02u:%02u:%02u",
      (unsigned long)rtcFlag,
      (unsigned int)rtc_state.hours,
      (unsigned int)rtc_state.minutes,
      (unsigned int)rtc_state.seconds);

  s_boot_handoff_detected = isValidRtcModeFlag(rtcFlag);
  if (s_boot_handoff_detected) {
    s_next_mode = modeFromRtcFlag(rtcFlag);
    s_current_state = (s_next_mode == RADIO_NEXT_BT) ? RADIO_STATE_BT : RADIO_STATE_WIFI;
  } else {
    s_next_mode = RADIO_NEXT_NONE;
    s_current_state = RADIO_STATE_WIFI;
    RadioModeSwitch::clearRTCTime();
  }

  rtc_state.mode_flag = kRtcFlagNone;
}

void requestModeSwitch(uint32_t modeFlag,
                       RadioModeSwitchNextMode nextMode,
                       const char* logLabel) {
  storeRtcSnapshot(modeFlag);

    LOG_I(TAG,
      "Mode switch scheduled target=%s time=%02u:%02u:%02u restart=true",
      logLabel,
      (unsigned int)rtc_state.hours,
      (unsigned int)rtc_state.minutes,
      (unsigned int)rtc_state.seconds);

  statsManager.saveStats();
  NetworkOrchestrator::quiesceForModeSwitch(nextMode);
  Serial.flush();

  scheduleRestart();
}

}  // namespace

namespace RadioModeSwitch {

void begin() {
  if (s_initialized) {
    return;
  }

  loadBootModeFromRtc();

  s_initialized = true;
  s_start_mode_ready = false;
  s_start_time_ms = millis();
  s_restart_pending = false;
  s_restart_deadline_ms = 0;

  LOG_I(TAG, "Init complete");
  printDiagnostics();
}

RadioModeSwitchState getCurrentState() {
  return s_current_state;
}

RadioModeSwitchNextMode getNextMode() {
  return s_next_mode;
}

bool isInitializing() {
  return s_initialized && !s_start_mode_ready;
}

void requestModeSwitch_WiFi() {
  requestModeSwitch(kRtcFlagWifi, RADIO_NEXT_WIFI, "WiFi");
}

void requestModeSwitch_BT() {
  requestModeSwitch(kRtcFlagBt, RADIO_NEXT_BT, "BT");
}

void cancelModeSwitch() {
  rtc_state.mode_flag = kRtcFlagNone;
  s_next_mode = RADIO_NEXT_NONE;
  s_restart_pending = false;
  s_restart_deadline_ms = 0;

  LOG_I(TAG, "Switch canceled");
}

void update() {
  const unsigned long nowMs = millis();

  if (s_restart_pending) {
    if ((long)(nowMs - s_restart_deadline_ms) >= 0) {
      esp_restart();
    }
    return;
  }

  if (!s_start_mode_ready && s_initialized && (nowMs - s_start_time_ms >= kStartupDelayMs)) {
    s_start_mode_ready = true;
    if (s_next_mode == RADIO_NEXT_BT) {
      s_current_state = RADIO_STATE_BT;
      LOG_I(TAG, "Startup armed target=Bluetooth");
    } else {
      s_current_state = RADIO_STATE_WIFI;
      LOG_I(TAG, "Startup armed target=WiFi");
    }
  }
}

void initializeStartMode() {
  if (!s_initialized) {
    begin();
  }

  if (s_next_mode == RADIO_NEXT_BT) {
    LOG_I(TAG, "Startup mode=Bluetooth");
  } else {
    LOG_I(TAG, "Startup mode=WiFi default=true");
    rtc_state.mode_flag = kRtcFlagNone;
    s_next_mode = RADIO_NEXT_NONE;
  }
}

bool isDefaultStartupWiFi() {
  return s_next_mode != RADIO_NEXT_BT;
}

bool wasBootHandoffDetected() {
  return s_boot_handoff_detected;
}

void forceMode(RadioModeSwitchState state, RadioModeSwitchNextMode nextMode) {
  s_current_state = state;
  s_next_mode = nextMode;
  s_start_mode_ready = true;
  s_restart_pending = false;
  s_restart_deadline_ms = 0;

  if (state == RADIO_STATE_WIFI) {
    rtc_state.mode_flag = kRtcFlagNone;
  }

  LOG_I(TAG, "Force mode state=%s", stateToText(state));
}

void printDiagnostics() {
  LOG_I(TAG, "Diagnostics begin");
  LOG_I(TAG, "Rtc flag=0x%04lX", (unsigned long)rtc_state.mode_flag);
  LOG_I(TAG,
        "Rtc time=%02u:%02u:%02u",
        (unsigned int)rtc_state.hours,
        (unsigned int)rtc_state.minutes,
        (unsigned int)rtc_state.seconds);
  LOG_I(TAG, "Current state=%s", stateToText(s_current_state));
  LOG_I(TAG, "Next mode=%s", nextModeToText(s_next_mode));
  LOG_I(TAG, "Boot handoff=%s", s_boot_handoff_detected ? "yes" : "no");
  LOG_I(TAG, "Initialized=%s", s_initialized ? "yes" : "no");
  LOG_I(TAG, "Diagnostics end");
}

uint8_t getRTCHours() {
  return rtc_state.hours;
}

uint8_t getRTCMinutes() {
  return rtc_state.minutes;
}

uint8_t getRTCSeconds() {
  return rtc_state.seconds;
}

void clearRTCTime() {
  rtc_state.hours = 0;
  rtc_state.minutes = 0;
  rtc_state.seconds = 0;
}

}  // namespace RadioModeSwitch
