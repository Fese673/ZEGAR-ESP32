#include "RtcSyncService.h"

#include <Arduino.h>
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
static bool s_clockSeeded = false;

static const char* kTzPoland = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr time_t kMinValidEpoch = 1609459200;
constexpr uint8_t kRtcRestoreAttempts = 3;
constexpr unsigned long kRtcRestoreRetryDelayMs = 2UL;

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

bool isClockSeeded() {
  return s_clockSeeded;
}

void markClockSeeded() {
  s_clockSeeded = true;
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

  for (uint8_t attempt = 1; attempt <= kRtcRestoreAttempts; ++attempt) {
    const RTCService::Status beginStatus = RTCService::begin(rtcCfg);
    if (beginStatus == RTCService::Status::DeviceNotFound) {
      LOG_W(TAG, "Begin status=%s action=skip attempt=%u", RTCService::statusToString(beginStatus), (unsigned)attempt);
      return;
    }

    if (beginStatus == RTCService::Status::OscillatorStopped) {
      LOG_W(TAG, "Begin status=%s action=try_epoch attempt=%u", RTCService::statusToString(beginStatus), (unsigned)attempt);
    } else if (beginStatus != RTCService::Status::Ok) {
      LOG_W(TAG, "Begin status=%s action=read_fallback attempt=%u", RTCService::statusToString(beginStatus), (unsigned)attempt);
    } else {
      LOG_I(TAG, "Begin status=%s attempt=%u", RTCService::statusToString(beginStatus), (unsigned)attempt);
    }

    time_t epoch = 0;
    const RTCService::Status readStatus = RTCService::getEpoch(&epoch);
    const RTCService::Diagnostics& diag = RTCService::getDiagnostics();
    LOG_I(TAG,
          "Get epoch status=%s epoch=%ld attempt=%u i2c_timeout=%lu i2c_nack=%lu i2c_error=%lu",
          RTCService::statusToString(readStatus),
          (long)epoch,
          (unsigned)attempt,
          (unsigned long)diag.i2c.timeout,
          (unsigned long)diag.i2c.nack,
          (unsigned long)diag.i2c.error);

    if (readStatus == RTCService::Status::Ok) {
      if (epoch < kMinValidEpoch) {
        LOG_W(TAG, "Epoch too old or invalid ignored=true epoch=%ld attempt=%u", (long)epoch, (unsigned)attempt);
        return;
      }

      timeval tv;
      tv.tv_sec = epoch;
      tv.tv_usec = 0;
      settimeofday(&tv, nullptr);

      lastTick = millis();
      syncLocalClockFromSystemTime(hours, minutes, seconds);
      s_clockSeeded = true;
      LOG_I(TAG, "Time restored from DS3231 local_time=%02d:%02d:%02d attempt=%u", hours, minutes, seconds, (unsigned)attempt);
      return;
    }

    const bool retryableStatus =
        readStatus == RTCService::Status::BusBusyTimeout ||
        readStatus == RTCService::Status::ReadFailed ||
        readStatus == RTCService::Status::InternalError ||
        beginStatus == RTCService::Status::InternalError ||
        beginStatus == RTCService::Status::BusBusyTimeout;
    if (attempt < kRtcRestoreAttempts && retryableStatus) {
      vTaskDelay(pdMS_TO_TICKS(kRtcRestoreRetryDelayMs));
      continue;
    }

    return;
  }
}

void noteNtpSync(unsigned long ntpSyncMillis) {
  if (ntpSyncMillis == 0 || ntpSyncMillis == lastSeenNtpSyncMillis) {
    return;
  }

  lastSeenNtpSyncMillis = ntpSyncMillis;
  s_clockSeeded = true;
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
