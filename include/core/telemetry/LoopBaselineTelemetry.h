#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace LoopBaselineTelemetry {

void resetWindow(unsigned long nowMs);
void onLoopStart(unsigned long nowMs, uint32_t nowUs);
void onLoopEnd(unsigned long nowMs, uint32_t loopStartUs, uint32_t loopEndUs);
void recordUiRefreshUs(uint32_t durationUs);
void recordNetworkUpdateUs(uint32_t durationUs);
void recordMqttPublishUs(uint32_t durationUs);

}  // namespace LoopBaselineTelemetry
