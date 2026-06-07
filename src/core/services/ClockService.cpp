#include "ClockService.h"

#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#endif

namespace Clock {
namespace {

int s_hours = 12;
int s_minutes = 0;
int s_seconds = 0;
unsigned long s_lastTick = 0;

#ifdef ARDUINO_ARCH_ESP32
static portMUX_TYPE s_clockMux = portMUX_INITIALIZER_UNLOCKED;
#define CLOCK_LOCK()   portENTER_CRITICAL(&s_clockMux)
#define CLOCK_UNLOCK() portEXIT_CRITICAL(&s_clockMux)
#else
#define CLOCK_LOCK()   ((void)0)
#define CLOCK_UNLOCK() ((void)0)
#endif

}  // namespace

void begin() {
  CLOCK_LOCK();
  s_hours = 12;
  s_minutes = 0;
  s_seconds = 0;
  s_lastTick = 0;
  CLOCK_UNLOCK();
}

int hours() {
  CLOCK_LOCK();
  const int v = s_hours;
  CLOCK_UNLOCK();
  return v;
}

int minutes() {
  CLOCK_LOCK();
  const int v = s_minutes;
  CLOCK_UNLOCK();
  return v;
}

int seconds() {
  CLOCK_LOCK();
  const int v = s_seconds;
  CLOCK_UNLOCK();
  return v;
}

void hms(int& h, int& m, int& s) {
  CLOCK_LOCK();
  h = s_hours;
  m = s_minutes;
  s = s_seconds;
  CLOCK_UNLOCK();
}

void set(int h, int m, int s) {
  CLOCK_LOCK();
  s_hours = (h % 24 + 24) % 24;
  s_minutes = (m % 60 + 60) % 60;
  s_seconds = (s % 60 + 60) % 60;
  CLOCK_UNLOCK();
}

void setLastTick(unsigned long nowMs) {
  CLOCK_LOCK();
  s_lastTick = nowMs;
  CLOCK_UNLOCK();
}

unsigned long lastTick() {
  CLOCK_LOCK();
  const unsigned long v = s_lastTick;
  CLOCK_UNLOCK();
  return v;
}

void tickSecond() {
  CLOCK_LOCK();
  if (++s_seconds >= 60) {
    s_seconds = 0;
    if (++s_minutes >= 60) {
      s_minutes = 0;
      if (++s_hours >= 24) {
        s_hours = 0;
      }
    }
  }
  CLOCK_UNLOCK();
}

void adjust(Target t, int dir) {
  CLOCK_LOCK();
  switch (t) {
    case HOURS:
      s_hours = (s_hours + dir + 24) % 24;
      break;
    case MINUTES:
      s_minutes = (s_minutes + dir + 60) % 60;
      break;
    case SECONDS:
      s_seconds = (s_seconds + dir + 60) % 60;
      break;
  }
  CLOCK_UNLOCK();
}

int formatHms(char* buf, size_t size) {
  CLOCK_LOCK();
  const int result = snprintf(buf, size, "%02d:%02d:%02d", s_hours, s_minutes, s_seconds);
  CLOCK_UNLOCK();
  return result;
}

void applyToSystemTime() {
  time_t now = time(nullptr);
  if (now < 1609459200) return;
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  CLOCK_LOCK();
  timeinfo.tm_hour = s_hours;
  timeinfo.tm_min = s_minutes;
  timeinfo.tm_sec = s_seconds;
  CLOCK_UNLOCK();
  timeval tv;
  tv.tv_sec = mktime(&timeinfo);
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}

}
