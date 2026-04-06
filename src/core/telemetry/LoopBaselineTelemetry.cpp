#include "LoopBaselineTelemetry.h"

#include <Esp.h>

#include "AppLog.h"

namespace LoopBaselineTelemetry {
namespace {

constexpr unsigned long REPORT_WINDOW_MS = 10000UL;
constexpr char TAG_BASELINE[] = "BASELINE";

struct State {
  unsigned long windowStartedMs = 0;
  unsigned long loopCount = 0;
  uint32_t lastLoopStartUs = 0;
  uint32_t lastLoopPeriodUs = 0;
  uint32_t minLoopPeriodUs = 0xFFFFFFFFUL;
  uint32_t maxLoopPeriodUs = 0;
  uint32_t maxLoopJitterUs = 0;
  uint32_t maxLoopBodyUs = 0;
  uint32_t maxUiRefreshUs = 0;
  uint32_t maxMqttPublishUs = 0;
  uint32_t maxNetworkUpdateUs = 0;
  uint32_t minFreeHeapBytes = 0xFFFFFFFFUL;
};

State s_state;

uint32_t deltaUs(uint32_t startUs, uint32_t endUs) {
  return static_cast<uint32_t>(endUs - startUs);
}

uint32_t absDiffU32(uint32_t a, uint32_t b) {
  return (a >= b) ? (a - b) : (b - a);
}

void recordMaxU32(uint32_t& target, uint32_t sample) {
  if (sample > target) {
    target = sample;
  }
}

}  // namespace

void resetWindow(unsigned long nowMs) {
  s_state.windowStartedMs = nowMs;
  s_state.loopCount = 0;
  s_state.minLoopPeriodUs = 0xFFFFFFFFUL;
  s_state.maxLoopPeriodUs = 0;
  s_state.maxLoopJitterUs = 0;
  s_state.maxLoopBodyUs = 0;
  s_state.maxUiRefreshUs = 0;
  s_state.maxMqttPublishUs = 0;
  s_state.maxNetworkUpdateUs = 0;
  s_state.minFreeHeapBytes = 0xFFFFFFFFUL;
}

void onLoopStart(unsigned long nowMs, uint32_t nowUs) {
  if (s_state.windowStartedMs == 0) {
    resetWindow(nowMs);
  }

  if (s_state.lastLoopStartUs != 0) {
    const uint32_t periodUs = deltaUs(s_state.lastLoopStartUs, nowUs);
    if (periodUs < s_state.minLoopPeriodUs) {
      s_state.minLoopPeriodUs = periodUs;
    }
    if (periodUs > s_state.maxLoopPeriodUs) {
      s_state.maxLoopPeriodUs = periodUs;
    }
    if (s_state.lastLoopPeriodUs != 0) {
      const uint32_t jitterUs = absDiffU32(periodUs, s_state.lastLoopPeriodUs);
      if (jitterUs > s_state.maxLoopJitterUs) {
        s_state.maxLoopJitterUs = jitterUs;
      }
    }
    s_state.lastLoopPeriodUs = periodUs;
  }

  s_state.lastLoopStartUs = nowUs;
  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < s_state.minFreeHeapBytes) {
    s_state.minFreeHeapBytes = freeHeap;
  }
}

void onLoopEnd(unsigned long nowMs, uint32_t loopStartUs, uint32_t loopEndUs) {
  recordMaxU32(s_state.maxLoopBodyUs, deltaUs(loopStartUs, loopEndUs));
  ++s_state.loopCount;

  if ((nowMs - s_state.windowStartedMs) < REPORT_WINDOW_MS) {
    return;
  }

  const uint32_t minPeriodUs = (s_state.minLoopPeriodUs == 0xFFFFFFFFUL) ? 0UL : s_state.minLoopPeriodUs;
  const uint32_t minHeapBytes = (s_state.minFreeHeapBytes == 0xFFFFFFFFUL) ? ESP.getFreeHeap() : s_state.minFreeHeapBytes;
  LOG_I(TAG_BASELINE,
        "Window complete window_ms=%lu loops=%lu loop_us_min=%lu loop_us_max=%lu loop_us_jitter=%lu body_max_us=%lu ui_max_us=%lu net_max_us=%lu mqtt_max_us=%lu heap_min_b=%lu",
        static_cast<unsigned long>(nowMs - s_state.windowStartedMs),
        s_state.loopCount,
        static_cast<unsigned long>(minPeriodUs),
        static_cast<unsigned long>(s_state.maxLoopPeriodUs),
        static_cast<unsigned long>(s_state.maxLoopJitterUs),
        static_cast<unsigned long>(s_state.maxLoopBodyUs),
        static_cast<unsigned long>(s_state.maxUiRefreshUs),
        static_cast<unsigned long>(s_state.maxNetworkUpdateUs),
        static_cast<unsigned long>(s_state.maxMqttPublishUs),
        static_cast<unsigned long>(minHeapBytes));

  resetWindow(nowMs);
}

void recordUiRefreshUs(uint32_t durationUs) {
  recordMaxU32(s_state.maxUiRefreshUs, durationUs);
}

void recordNetworkUpdateUs(uint32_t durationUs) {
  recordMaxU32(s_state.maxNetworkUpdateUs, durationUs);
}

void recordMqttPublishUs(uint32_t durationUs) {
  recordMaxU32(s_state.maxMqttPublishUs, durationUs);
}

}  // namespace LoopBaselineTelemetry
