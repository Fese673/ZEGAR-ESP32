#pragma once

#include <Arduino.h>
#include <stdint.h>
#ifndef ENABLE_RUNTIME_TELEMETRY
#  define ENABLE_RUNTIME_TELEMETRY 0
#endif

#ifndef RUNTIME_TELEMETRY_PRINT_INTERVAL_MS
#  define RUNTIME_TELEMETRY_PRINT_INTERVAL_MS 5000UL
#endif

#if ENABLE_RUNTIME_TELEMETRY
#  include <atomic>
#endif

namespace RuntimeTelemetry {

#if ENABLE_RUNTIME_TELEMETRY
struct Counters {
  std::atomic<uint32_t> i2c_timeouts{0};
  std::atomic<uint32_t> i2c_queue_full{0};
  std::atomic<uint32_t> i2c_errors{0};
  std::atomic<uint32_t> audio_underruns{0};
  std::atomic<uint32_t> audio_overflows{0};
  std::atomic<uint32_t> audio_drops{0};
  std::atomic<uint32_t> encoder_drops{0};
};

struct Snapshot {
  uint32_t i2c_timeouts = 0;
  uint32_t i2c_queue_full = 0;
  uint32_t i2c_errors = 0;
  uint32_t audio_underruns = 0;
  uint32_t audio_overflows = 0;
  uint32_t audio_drops = 0;
  uint32_t encoder_drops = 0;
};

Counters& counters();
Snapshot snapshot();
void reset();
void print(Stream& out);
void print(Stream& out, const Snapshot& values);
bool service(Stream& out,
             unsigned long nowMs,
             unsigned long intervalMs = RUNTIME_TELEMETRY_PRINT_INTERVAL_MS);
#else
struct Snapshot {
  uint32_t i2c_timeouts = 0;
  uint32_t i2c_queue_full = 0;
  uint32_t i2c_errors = 0;
  uint32_t audio_underruns = 0;
  uint32_t audio_overflows = 0;
  uint32_t audio_drops = 0;
  uint32_t encoder_drops = 0;
};

inline void reset() {}
inline Snapshot snapshot() { return Snapshot{}; }
inline void print(Stream&) {}
inline void print(Stream&, const Snapshot&) {}
inline bool service(Stream&, unsigned long, unsigned long = RUNTIME_TELEMETRY_PRINT_INTERVAL_MS) {
  return false;
}
#endif

}  // namespace RuntimeTelemetry

#if ENABLE_RUNTIME_TELEMETRY
#  define TELEMETRY_INC(field) \
    (::RuntimeTelemetry::counters().field.fetch_add(1U, std::memory_order_relaxed))
#  define TELEMETRY_ADD(field, value) \
    (::RuntimeTelemetry::counters().field.fetch_add(static_cast<uint32_t>(value), \
                                                    std::memory_order_relaxed))
#else
#  define TELEMETRY_INC(field) do { } while (0)
#  define TELEMETRY_ADD(field, value) do { } while (0)
#endif