#pragma once

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif

namespace AppLog {

constexpr size_t kMessageBufferSize = 640;

#ifdef ARDUINO_ARCH_ESP32
inline SemaphoreHandle_t logMutex() {
  static SemaphoreHandle_t s_mutex = xSemaphoreCreateMutex();
  return s_mutex;
}

inline bool tryLock() {
  if (xPortInIsrContext()) {
    return false;
  }

  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
    return false;
  }

  SemaphoreHandle_t mutex = logMutex();
  if (mutex == nullptr) {
    return false;
  }

  return xSemaphoreTake(mutex, pdMS_TO_TICKS(10)) == pdTRUE;
}

inline void unlock() {
  if (xPortInIsrContext()) {
    return;
  }

  SemaphoreHandle_t mutex = logMutex();
  if (mutex != nullptr) {
    xSemaphoreGive(mutex);
  }
}
#endif

inline void vlogTo(Print& stream, const char* tag, char level, const char* fmt, va_list args) {
  char message[kMessageBufferSize];
  const int written = vsnprintf(message, sizeof(message), fmt, args);
  if (written < 0) {
    return;
  }

  message[sizeof(message) - 1] = '\0';

#ifdef ARDUINO_ARCH_ESP32
  const bool locked = tryLock();
#else
  const bool locked = false;
#endif

  stream.print('[');
  stream.print((tag != nullptr && tag[0] != '\0') ? tag : "LOG");
  stream.print("][");
  stream.print(level);
  stream.print("] ");
  stream.print(message);
  stream.print("\r\n");

#ifdef ARDUINO_ARCH_ESP32
  if (locked) {
    unlock();
  }
#endif
}

inline void logTo(Print& stream, const char* tag, char level, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vlogTo(stream, tag, level, fmt, args);
  va_end(args);
}

}  // namespace AppLog

#define LOG_TO(stream, tag, level, fmt, ...) \
  do { \
    AppLog::logTo((stream), tag, level, fmt, ##__VA_ARGS__); \
  } while (0)

#define LOG(tag, level, fmt, ...) LOG_TO(Serial, tag, level, fmt, ##__VA_ARGS__)
#define LOG_D(tag, fmt, ...) LOG(tag, 'D', fmt, ##__VA_ARGS__)
#define LOG_I(tag, fmt, ...) LOG(tag, 'I', fmt, ##__VA_ARGS__)
#define LOG_W(tag, fmt, ...) LOG(tag, 'W', fmt, ##__VA_ARGS__)
#define LOG_E(tag, fmt, ...) LOG(tag, 'E', fmt, ##__VA_ARGS__)

#define LOG_D_TO(stream, tag, fmt, ...) LOG_TO(stream, tag, 'D', fmt, ##__VA_ARGS__)
#define LOG_I_TO(stream, tag, fmt, ...) LOG_TO(stream, tag, 'I', fmt, ##__VA_ARGS__)
#define LOG_W_TO(stream, tag, fmt, ...) LOG_TO(stream, tag, 'W', fmt, ##__VA_ARGS__)
#define LOG_E_TO(stream, tag, fmt, ...) LOG_TO(stream, tag, 'E', fmt, ##__VA_ARGS__)