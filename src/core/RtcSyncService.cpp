#include "RtcSyncService.h"

#include <Wire.h>
#include <sys/time.h>
#include <time.h>

#include "AppLog.h"
#include "BoardPins.h"
#include "RTCService.h"

namespace RtcSyncService {
namespace {

constexpr char TAG[] = "RTC";

static bool rtcWritePending = false;
static unsigned long lastRtcWriteAttemptMillis = 0;
static unsigned long rtcWriteNotBeforeMillis = 0;
static uint8_t rtcWriteFailureCount = 0;
static time_t rtcPendingEpoch = 0;
static unsigned long lastSeenNtpSyncMillis = 0;

static const char* kTzPoland = "CET-1CEST,M3.5.0,M10.5.0/3";

static unsigned long rtcComputeBackoffMs(uint8_t failures) {
  if (failures == 0) return 0;
  if (failures == 1) return 1000;
  if (failures == 2) return 2000;
  if (failures == 3) return 5000;
  if (failures == 4) return 10000;
  if (failures == 5) return 20000;
  return 60000;
}

static void scheduleRtcWriteFromSystemTime() {
  if (!isSystemTimeValid()) return;

  rtcPendingEpoch = time(nullptr);
  rtcWritePending = true;
  rtcWriteFailureCount = 0;
  rtcWriteNotBeforeMillis = millis() + 2000UL;
}

}  // namespace

void applyTimezone() {
  if (setenv("TZ", kTzPoland, 1) == 0) {
    tzset();
  }
}

bool isSystemTimeValid() {
  return time(nullptr) >= 1609459200;
}

void syncLocalClockFromSystemTime(int& hours, int& minutes, int& seconds) {
  const time_t now = time(nullptr);
  tm localTime;
  localtime_r(&now, &localTime);
  hours = localTime.tm_hour;
  minutes = localTime.tm_min;
  seconds = localTime.tm_sec;
}

void tryRestoreSystemTimeFromDs3231(int& hours, int& minutes, int& seconds, unsigned long& lastTick) {
  RTCService::Config rtcCfg;
  rtcCfg.wire = &Wire;
  rtcCfg.sdaPin = BoardPins::kI2cSda;
  rtcCfg.sclPin = BoardPins::kI2cScl;
  rtcCfg.i2cClockHz = BoardPins::kI2cClockHz;
  rtcCfg.i2cTimeoutMs = 10;
  rtcCfg.i2cRetries = 2;
  rtcCfg.initI2cMaster = false;
  rtcCfg.enableI2cDiagnostics = true;

  const RTCService::Status beginStatus = RTCService::begin(rtcCfg);
  LOG_I(TAG, "Begin status=%s", RTCService::statusToString(beginStatus));
  if (beginStatus != RTCService::Status::Ok) {
    return;
  }

  time_t epoch = 0;
  const RTCService::Status readStatus = RTCService::getEpoch(&epoch);
  LOG_I(TAG, "Get epoch status=%s epoch=%ld", RTCService::statusToString(readStatus), (long)epoch);
  if (readStatus != RTCService::Status::Ok) {
    return;
  }

  if (epoch < 1609459200) {
    LOG_W(TAG, "Epoch too old or invalid ignored=true epoch=%ld", (long)epoch);
    return;
  }

  timeval tv;
  tv.tv_sec = epoch;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);

  lastTick = millis();
  syncLocalClockFromSystemTime(hours, minutes, seconds);
  LOG_I(TAG, "Time restored from DS3231 local_time=%02d:%02d:%02d", hours, minutes, seconds);
}

void noteNtpSync(unsigned long ntpSyncMillis) {
  if (ntpSyncMillis == 0 || ntpSyncMillis == lastSeenNtpSyncMillis) {
    return;
  }

  lastSeenNtpSyncMillis = ntpSyncMillis;
  scheduleRtcWriteFromSystemTime();
}

void processPendingWrite() {
  if (!rtcWritePending) return;
  if (!RTCService::isReady()) return;
  if (!isSystemTimeValid()) return;

  const unsigned long nowMs = millis();

  if (nowMs < rtcWriteNotBeforeMillis) return;
  if (lastRtcWriteAttemptMillis != 0 && (nowMs - lastRtcWriteAttemptMillis) < 1000UL) return;

  const time_t epochToWrite = (rtcPendingEpoch != 0) ? rtcPendingEpoch : time(nullptr);
  const RTCService::Status writeStatus = RTCService::setEpoch(epochToWrite, true);
  LOG_I(TAG, "Set epoch status=%s epoch=%ld", RTCService::statusToString(writeStatus), (long)epochToWrite);
  lastRtcWriteAttemptMillis = nowMs;

  if (writeStatus == RTCService::Status::Ok) {
    rtcWritePending = false;
    rtcPendingEpoch = 0;
    rtcWriteFailureCount = 0;
    rtcWriteNotBeforeMillis = 0;
    return;
  }

  rtcWriteFailureCount++;
  rtcWriteNotBeforeMillis = nowMs + rtcComputeBackoffMs(rtcWriteFailureCount);
}

}  // namespace RtcSyncService
