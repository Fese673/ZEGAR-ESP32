#pragma once

#include <Arduino.h>
#include <stdint.h>
#ifndef TEST_RAM
#define TEST_RAM 0
#endif

#ifndef TEST_RAM_HEAP_INFO
#define TEST_RAM_HEAP_INFO 0
#endif

#ifndef RAM_TELEMETRY_PRINT_INTERVAL_MS
#define RAM_TELEMETRY_PRINT_INTERVAL_MS 0UL
#endif

namespace RamTelemetry {

struct TaskWatermark {
  uint32_t freeStackBytes = 0;
  bool available = false;
};

struct TaskWatermarks {
  TaskWatermark mqttTask;
  TaskWatermark wifiInitTask;
  TaskWatermark btAppTask;
  TaskWatermark btI2STask;
  TaskWatermark encoderTask;
  TaskWatermark i2cWorkerTask;
};

struct Snapshot {
  uint32_t freeHeap = 0;
  uint32_t totalHeap = 0;
  uint32_t largestFreeBlock = 0;
  uint32_t dmaFree = 0;
  uint32_t minFreeHeap = 0;
  uint16_t largestFreeBlockRatioPermille = 0;
  uint16_t fragmentationPermille = 0;
  TaskWatermarks taskWatermarks{};
};

#if TEST_RAM
void begin();
void reset();
Snapshot snapshot();
bool checkpoint(const char* tag, bool dumpHeapInfo = (TEST_RAM_HEAP_INFO != 0));
bool service(Stream& out,
             unsigned long nowMs,
             unsigned long intervalMs = RAM_TELEMETRY_PRINT_INTERVAL_MS);
#else
inline void begin() {}
inline void reset() {}
inline Snapshot snapshot() { return Snapshot{}; }
inline bool checkpoint(const char*, bool = false) { return false; }
inline bool service(Stream&, unsigned long, unsigned long = RAM_TELEMETRY_PRINT_INTERVAL_MS) {
  return false;
}
#endif

}  // namespace RamTelemetry

#if TEST_RAM
#  define RAM_CHECKPOINT(tag) ::RamTelemetry::checkpoint(tag)
#  define RAM_CHECKPOINT_HEAP(tag) ::RamTelemetry::checkpoint(tag, true)
#else
#  define RAM_CHECKPOINT(tag) do { } while (0)
#  define RAM_CHECKPOINT_HEAP(tag) do { } while (0)
#endif
