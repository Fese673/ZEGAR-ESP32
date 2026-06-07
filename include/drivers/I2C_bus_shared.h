#pragma once

#include <Arduino.h>
#include <Wire.h>
#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

struct I2cSharedStats {
    uint32_t timeout = 0;
    uint32_t nack = 0;
    uint32_t error = 0;
};

namespace I2cShared {

bool initMaster(TwoWire *wire, int sdaPin, int sclPin, uint32_t clockHz, bool wireAlreadyStarted = false);
bool initMaster(int sdaPin, int sclPin, uint32_t clockHz, bool wireAlreadyStarted = false);

void setDiagnosticsEnabled(bool enabled);
bool diagnosticsEnabled();
I2cSharedStats getStats();
void resetStats();

bool lock(uint32_t timeoutMs);
void unlock();

#ifdef ARDUINO_ARCH_ESP32
// Zwraca uchwyt shared I2C worker taska albo nullptr, gdy worker nie działa.
TaskHandle_t getWorkerTaskHandle();
#endif

bool probe(TwoWire *wire, uint8_t address7bit, uint32_t timeoutMs, uint8_t retries = 2);
bool probeAddress(uint8_t address7bit, uint32_t timeoutMs, uint8_t retries = 2);
bool write(TwoWire *wire,
           uint8_t address7bit,
           const uint8_t *data,
           size_t len,
           bool sendStop,
           uint32_t timeoutMs,
           uint8_t retries = 2);
bool writeRead(TwoWire *wire,
               uint8_t address7bit,
               const uint8_t *writeData,
               size_t writeLen,
               uint8_t *readData,
               size_t readLen,
               uint32_t timeoutMs,
               uint8_t retries = 2);

}  // namespace I2cShared
