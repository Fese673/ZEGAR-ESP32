#include "RamTelemetry.h"

#if TEST_RAM

#include <cstdio>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>

#include "AudioBT.h"
#include "MQTTSync.h"
#include "WiFiSync.h"

namespace RamTelemetry {
namespace {
TaskWatermark captureTaskWatermark(TaskHandle_t handle) {
  TaskWatermark watermark;
  if (handle == nullptr) {
    return watermark;
  }

  watermark.available = true;
  watermark.freeStackBytes = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(handle)) * sizeof(StackType_t);
  return watermark;
}

Snapshot g_baseline{};
Snapshot g_lastCheckpoint{};
Snapshot g_lastHeartbeat{};
bool g_hasState = false;
bool g_hasHeartbeatState = false;
uint32_t g_checkpointSequence = 0;
uint32_t g_heartbeatSequence = 0;
unsigned long g_lastHeartbeatMs = 0;

const char* normalizeTag(const char* tag) {
  return (tag != nullptr && tag[0] != '\0') ? tag : "RAM";
}

void formatTaskField(char* out, size_t outSize, const char* label, const TaskWatermark& watermark) {
  if (watermark.available) {
    snprintf(out,
             outSize,
             "%s=%luB",
             label,
             static_cast<unsigned long>(watermark.freeStackBytes));
    return;
  }

  snprintf(out, outSize, "%s=n/a", label);
}

void printSnapshotLine(Stream& out,
                       const char* kind,
                       uint32_t sequence,
                       const char* tag,
                       const Snapshot& now,
                       const Snapshot& previous,
                       const Snapshot& baseline) {
  const int32_t deltaFreePrev = (int32_t)now.freeHeap - (int32_t)previous.freeHeap;
  const int32_t deltaLargestPrev = (int32_t)now.largestFreeBlock - (int32_t)previous.largestFreeBlock;
  const int32_t deltaDmaPrev = (int32_t)now.dmaFree - (int32_t)previous.dmaFree;
  const int32_t deltaFreeBase = (int32_t)now.freeHeap - (int32_t)baseline.freeHeap;
  const int32_t deltaLargestBase = (int32_t)now.largestFreeBlock - (int32_t)baseline.largestFreeBlock;
  const int32_t deltaDmaBase = (int32_t)now.dmaFree - (int32_t)baseline.dmaFree;

  char mqttTaskField[24];
  char wifiInitTaskField[24];
  char btI2STaskField[24];
  formatTaskField(mqttTaskField, sizeof(mqttTaskField), "mqtt", now.taskWatermarks.mqttTask);
  formatTaskField(wifiInitTaskField, sizeof(wifiInitTaskField), "wifiInit", now.taskWatermarks.wifiInitTask);
  formatTaskField(btI2STaskField, sizeof(btI2STaskField), "btI2S", now.taskWatermarks.btI2STask);

  char line[512];
  snprintf(line,
           sizeof(line),
           "[RAM][%s] #%lu %s free=%luB (prev %+ld, base %+ld) largest=%luB (prev %+ld, base %+ld) ratio=%u/1000 frag=%u/1000 dma=%luB (prev %+ld, base %+ld) min=%luB total=%luB stk[%s %s %s]",
           kind,
           static_cast<unsigned long>(sequence),
           normalizeTag(tag),
           static_cast<unsigned long>(now.freeHeap),
           static_cast<long>(deltaFreePrev),
           static_cast<long>(deltaFreeBase),
           static_cast<unsigned long>(now.largestFreeBlock),
           static_cast<long>(deltaLargestPrev),
           static_cast<long>(deltaLargestBase),
           static_cast<unsigned int>(now.largestFreeBlockRatioPermille),
           static_cast<unsigned int>(now.fragmentationPermille),
           static_cast<unsigned long>(now.dmaFree),
           static_cast<long>(deltaDmaPrev),
           static_cast<long>(deltaDmaBase),
           static_cast<unsigned long>(now.minFreeHeap),
           static_cast<unsigned long>(now.totalHeap),
           mqttTaskField,
           wifiInitTaskField,
           btI2STaskField);
  out.println(line);
}
}  // namespace

void begin() {
  g_baseline = Snapshot{};
  g_lastCheckpoint = Snapshot{};
  g_lastHeartbeat = Snapshot{};
  g_hasState = false;
  g_hasHeartbeatState = false;
  g_checkpointSequence = 0;
  g_heartbeatSequence = 0;
  g_lastHeartbeatMs = 0;
}

void reset() {
  begin();
}

Snapshot snapshot() {
  Snapshot values;
  values.freeHeap = ESP.getFreeHeap();
  values.totalHeap = ESP.getHeapSize();
  values.largestFreeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  values.dmaFree = heap_caps_get_free_size(MALLOC_CAP_DMA);
  values.minFreeHeap = ESP.getMinFreeHeap();

  if (values.freeHeap > 0U) {
    const uint32_t ratioPermille = (values.largestFreeBlock * 1000UL) / values.freeHeap;
    values.largestFreeBlockRatioPermille = static_cast<uint16_t>(ratioPermille > 1000UL ? 1000UL : ratioPermille);
    values.fragmentationPermille = static_cast<uint16_t>(1000UL - values.largestFreeBlockRatioPermille);
  }

  values.taskWatermarks.mqttTask = captureTaskWatermark(MQTTSync::getTaskHandle());
  values.taskWatermarks.wifiInitTask = captureTaskWatermark(WiFiSync::getInitTaskHandle());
  values.taskWatermarks.btI2STask = captureTaskWatermark(audioBT_getI2STaskHandle());
  return values;
}

bool checkpoint(const char* tag, bool dumpHeapInfo) {
  const Snapshot now = snapshot();

  if (!g_hasState) {
    g_baseline = now;
    g_lastCheckpoint = now;
    g_hasState = true;
    g_checkpointSequence = 1;
  } else {
    ++g_checkpointSequence;
  }

  printSnapshotLine(Serial, "CHK", g_checkpointSequence, tag, now, g_lastCheckpoint, g_baseline);

  if (dumpHeapInfo) {
    heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);
  }

  g_lastCheckpoint = now;
  return true;
}

bool service(Stream& out, unsigned long nowMs, unsigned long intervalMs) {
  if (intervalMs == 0) {
    return false;
  }

  if (!g_hasHeartbeatState) {
    const Snapshot now = snapshot();
    if (!g_hasState) {
      g_baseline = now;
      g_lastCheckpoint = now;
      g_hasState = true;
    }
    g_lastHeartbeat = now;
    g_hasHeartbeatState = true;
    g_lastHeartbeatMs = nowMs;
    return false;
  }

  if ((nowMs - g_lastHeartbeatMs) < intervalMs) {
    return false;
  }

  const Snapshot now = snapshot();

  ++g_heartbeatSequence;

  printSnapshotLine(out, "HB", g_heartbeatSequence, "HEARTBEAT", now, g_lastHeartbeat, g_baseline);
  g_lastHeartbeat = now;
  g_lastHeartbeatMs = nowMs;
  return true;
}

}  // namespace RamTelemetry

#endif
