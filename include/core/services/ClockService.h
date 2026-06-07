#pragma once

#include <stddef.h>
#include <stdint.h>
namespace Clock {

enum Target : uint8_t {
  HOURS = 0,
  MINUTES,
  SECONDS,
};

void begin();

int hours();
int minutes();
int seconds();
unsigned long lastTick();

// Atomic: read all three in one shot (snapshot-safe for display)
void hms(int& h, int& m, int& s);

// Atomic: set all three at once
void set(int h, int m, int s);
void setLastTick(unsigned long nowMs);

// Atomic: advance by one second (handles hh:mm:ss overflow)
void tickSecond();

// Atomic: adjust a single field by dir (+1 or -1) with modulo wrap
void adjust(Target t, int dir);

// Convenience: sprintf(buf, "%02d:%02d:%02d", h, m, s)
int formatHms(char* buf, size_t size);

// Push current ClockService time to ESP32 system time (settimeofday).
// DS3231 RTC przechowuje UTC – write jest schedulowany przez RtcSyncService.
void applyToSystemTime();

}
