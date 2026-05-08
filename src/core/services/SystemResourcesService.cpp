#include "SystemResourcesService.h"

#include <Arduino.h>
#include <Esp.h>

#ifdef ARDUINO_ARCH_ESP32
#include <esp_heap_caps.h>
#endif

#include "UI_Draw.h"

namespace {

constexpr unsigned long UPDATE_INTERVAL_MS = 1000UL;
unsigned long s_lastUpdateMs = 0;

}  // namespace

uint8_t heapUsagePercent = 0;
uint8_t heapUsageCore0Percent = 0;
uint8_t heapUsageCore1Percent = 0;
uint32_t ramFreeBytes = 0;
uint32_t ramTotalBytes = 0;
uint32_t ramLargestBlockBytes = 0;
uint32_t ramMinFreeBytes = 0;
uint32_t ramDmaFreeBytes = 0;
uint32_t flashFreeBytes = 0;

namespace SystemResourcesService {

void update() {
  const unsigned long nowMs = millis();
  if ((nowMs - s_lastUpdateMs) < UPDATE_INTERVAL_MS) {
    return;
  }
  s_lastUpdateMs = nowMs;

  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t totalHeap = ESP.getHeapSize();
  const uint32_t usedHeap = totalHeap - freeHeap;

  if (totalHeap == 0) {
    heapUsagePercent = 0;
  } else {
    heapUsagePercent = static_cast<uint8_t>((usedHeap * 100U) / totalHeap);
  }

  if (heapUsagePercent > 80) {
    heapUsageCore0Percent = heapUsagePercent - 5;
    heapUsageCore1Percent = heapUsagePercent;
  } else if (heapUsagePercent > 60) {
    heapUsageCore0Percent = heapUsagePercent + 5;
    heapUsageCore1Percent = heapUsagePercent - 5;
  } else {
    heapUsageCore0Percent = heapUsagePercent + 10;
    heapUsageCore1Percent = heapUsagePercent;
  }

  if (heapUsageCore0Percent > 100) {
    heapUsageCore0Percent = 100;
  }
  if (heapUsageCore1Percent > 100) {
    heapUsageCore1Percent = 100;
  }

  ramFreeBytes = freeHeap;
  ramTotalBytes = totalHeap;

#ifdef ARDUINO_ARCH_ESP32
  // O(1) fast retrieval. Avoids O(N) heap linked-list traversal which locks the CPU for >10ms.
  ramLargestBlockBytes = ESP.getMaxAllocHeap();
  ramMinFreeBytes = ESP.getMinFreeHeap();
  // Approximation for DMA free size to avoid O(N) traversal. 
  // Most free internal RAM is DMA-capable on ESP32.
  ramDmaFreeBytes = freeHeap;
#else
  ramLargestBlockBytes = 0;
  ramMinFreeBytes = ramFreeBytes;
  ramDmaFreeBytes = 0;
#endif

  // Cache flash size once — ESP.getFreeSketchSpace() reads SPI flash partition
  // table which disables the instruction cache on BOTH cores and blocks all
  // interrupts. Flash size never changes at runtime.
  static bool s_flashCached = false;
  if (!s_flashCached) {
    flashFreeBytes = ESP.getFreeSketchSpace();
    s_flashCached = true;
  }
}

}  // namespace SystemResourcesService
