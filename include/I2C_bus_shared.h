#pragma once

#include <Arduino.h>
#include <Wire.h>

struct I2cSharedStats {
    uint32_t timeout = 0;
    uint32_t nack = 0;
    uint32_t error = 0;
};

namespace I2cShared {

bool initMaster(TwoWire *wire, int sdaPin, int sclPin, uint32_t clockHz);
bool initMaster(int sdaPin, int sclPin, uint32_t clockHz);

void setDiagnosticsEnabled(bool enabled);
bool diagnosticsEnabled();
I2cSharedStats getStats();
void resetStats();

bool lock(uint32_t timeoutMs);
void unlock();

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
