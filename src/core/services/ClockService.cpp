#include "ClockService.h"
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

namespace Clock {
namespace {

int s_hours = 12;
int s_minutes = 0;
int s_seconds = 0;
unsigned long s_lastTick = 0;

}  // namespace

void begin() {
  s_hours = 12;
  s_minutes = 0;
  s_seconds = 0;
  s_lastTick = 0;
}

int hours() { return s_hours; }
int minutes() { return s_minutes; }
int seconds() { return s_seconds; }
unsigned long lastTick() { return s_lastTick; }

void hms(int& h, int& m, int& s) {
  h = s_hours;
  m = s_minutes;
  s = s_seconds;
}

void set(int h, int m, int s) {
  s_hours = (h % 24 + 24) % 24;
  s_minutes = (m % 60 + 60) % 60;
  s_seconds = (s % 60 + 60) % 60;
}

void setLastTick(unsigned long nowMs) {
  s_lastTick = nowMs;
}

void tickSecond() {
  if (++s_seconds >= 60) {
    s_seconds = 0;
    if (++s_minutes >= 60) {
      s_minutes = 0;
      if (++s_hours >= 24) {
        s_hours = 0;
      }
    }
  }
}

void adjust(Target t, int dir) {
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
}

int formatHms(char* buf, size_t size) {
  return snprintf(buf, size, "%02d:%02d:%02d", s_hours, s_minutes, s_seconds);
}

void applyToSystemTime() {
  time_t now = time(nullptr);
  if (now < 1609459200) return;
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  timeinfo.tm_hour = s_hours;
  timeinfo.tm_min = s_minutes;
  timeinfo.tm_sec = s_seconds;
  timeval tv;
  tv.tv_sec = mktime(&timeinfo);
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}

}
