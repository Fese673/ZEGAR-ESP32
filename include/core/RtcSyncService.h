#pragma once

#include <Arduino.h>

namespace RtcSyncService {

void applyTimezone();
bool isSystemTimeValid();

void syncLocalClockFromSystemTime(int& hours, int& minutes, int& seconds);
void tryRestoreSystemTimeFromDs3231(int& hours, int& minutes, int& seconds, unsigned long& lastTick);

void noteNtpSync(unsigned long ntpSyncMillis);
void processPendingWrite();

}  // namespace RtcSyncService
