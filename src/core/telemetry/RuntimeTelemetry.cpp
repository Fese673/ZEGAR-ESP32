#include "RuntimeTelemetry.h"

#include "AppLog.h"

#if ENABLE_RUNTIME_TELEMETRY

#include <cstdio>

namespace RuntimeTelemetry {
namespace {
constexpr char TAG[] = "TEL";

Counters gCounters;
unsigned long gLastPrintMs = 0;
bool gHasPrinted = false;
}  // namespace

Counters& counters() {
  return gCounters;
}

Snapshot snapshot() {
  Snapshot values;
  values.i2c_timeouts = gCounters.i2c_timeouts.load(std::memory_order_relaxed);
  values.i2c_queue_full = gCounters.i2c_queue_full.load(std::memory_order_relaxed);
  values.i2c_errors = gCounters.i2c_errors.load(std::memory_order_relaxed);
  values.audio_underruns = gCounters.audio_underruns.load(std::memory_order_relaxed);
  values.audio_overflows = gCounters.audio_overflows.load(std::memory_order_relaxed);
  values.audio_drops = gCounters.audio_drops.load(std::memory_order_relaxed);
  values.encoder_drops = gCounters.encoder_drops.load(std::memory_order_relaxed);
  return values;
}

void reset() {
  gCounters.i2c_timeouts.store(0, std::memory_order_relaxed);
  gCounters.i2c_queue_full.store(0, std::memory_order_relaxed);
  gCounters.i2c_errors.store(0, std::memory_order_relaxed);
  gCounters.audio_underruns.store(0, std::memory_order_relaxed);
  gCounters.audio_overflows.store(0, std::memory_order_relaxed);
  gCounters.audio_drops.store(0, std::memory_order_relaxed);
  gCounters.encoder_drops.store(0, std::memory_order_relaxed);
  gLastPrintMs = 0;
  gHasPrinted = false;
}

void print(Stream& out) {
  print(out, snapshot());
}

void print(Stream& out, const Snapshot& values) {
  LOG_TO(out,
         TAG,
         'I',
         "I2C timeouts=%lu queue_full=%lu errors=%lu audio_underruns=%lu audio_overflows=%lu audio_drops=%lu encoder_drops=%lu",
         static_cast<unsigned long>(values.i2c_timeouts),
         static_cast<unsigned long>(values.i2c_queue_full),
         static_cast<unsigned long>(values.i2c_errors),
         static_cast<unsigned long>(values.audio_underruns),
         static_cast<unsigned long>(values.audio_overflows),
         static_cast<unsigned long>(values.audio_drops),
         static_cast<unsigned long>(values.encoder_drops));
}

bool service(Stream& out, unsigned long nowMs, unsigned long intervalMs) {
  if (gHasPrinted && (nowMs - gLastPrintMs) < intervalMs) {
    return false;
  }

  gHasPrinted = true;
  gLastPrintMs = nowMs;
  print(out);
  return true;
}

}  // namespace RuntimeTelemetry

#endif