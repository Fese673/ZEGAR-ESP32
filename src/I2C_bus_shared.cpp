#include "I2C_bus_shared.h"

#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif

namespace {

#ifdef ARDUINO_ARCH_ESP32
SemaphoreHandle_t gI2cMutex = nullptr;
#endif

volatile bool gDiagEnabled = false;
volatile uint32_t gTimeoutCount = 0;
volatile uint32_t gNackCount = 0;
volatile uint32_t gErrorCount = 0;

uint8_t clampRetries(uint8_t retries)
{
    if (retries < 1) return 1;
    if (retries > 3) return 3;
    return retries;
}

void classifyWireError(int err)
{
    if (!gDiagEnabled) return;

    if (err == 2 || err == 3) {
        gNackCount++;
    } else if (err == 5) {
        gTimeoutCount++;
    } else if (err != 0) {
        gErrorCount++;
    }
}

bool ensureMutex()
{
#ifdef ARDUINO_ARCH_ESP32
    if (gI2cMutex == nullptr) {
        gI2cMutex = xSemaphoreCreateRecursiveMutex();
    }
    return gI2cMutex != nullptr;
#else
    return true;
#endif
}

bool applyClockAndVerify(TwoWire *wire, uint32_t clockHz, uint32_t *actualClockHz)
{
    if (wire == nullptr) {
        return false;
    }

    wire->setClock(clockHz);

#ifdef ARDUINO_ARCH_ESP32
    const uint32_t actualHz = wire->getClock();
    if (actualClockHz != nullptr) {
        *actualClockHz = actualHz;
    }

    if (actualHz == clockHz) {
        return true;
    }

    return false;
#else
    if (actualClockHz != nullptr) {
        *actualClockHz = clockHz;
    }
    return true;
#endif
}

}  // namespace

namespace I2cShared {

bool initMaster(TwoWire *wire, int sdaPin, int sclPin, uint32_t clockHz)
{
    if (wire == nullptr) {
        return false;
    }

    bool applied = false;
    uint32_t actualClockHz = 0;

    wire->begin(sdaPin, sclPin);
    applied = applyClockAndVerify(wire, clockHz, &actualClockHz);

#ifdef ARDUINO_ARCH_ESP32
    if (!applied) {
        Serial.printf("[I2C] setClock(%lu) mismatch after begin(sda=%d, scl=%d): actual=%lu. Reinitializing bus and retrying.\n",
                      (unsigned long)clockHz,
                      sdaPin,
                      sclPin,
                      (unsigned long)actualClockHz);

        wire->end();
        delay(1);
        wire->begin(sdaPin, sclPin);
        applied = applyClockAndVerify(wire, clockHz, &actualClockHz);
    }

    Serial.printf("[I2C] bus ready: requested=%lu actual=%lu sda=%d scl=%d %s\n",
                  (unsigned long)clockHz,
                  (unsigned long)actualClockHz,
                  sdaPin,
                  sclPin,
                  applied ? "OK" : "MISMATCH");
#endif

    return applied;
}

bool initMaster(int sdaPin, int sclPin, uint32_t clockHz)
{
    return initMaster(&Wire, sdaPin, sclPin, clockHz);
}

void setDiagnosticsEnabled(bool enabled)
{
    gDiagEnabled = enabled;
}

bool diagnosticsEnabled()
{
    return gDiagEnabled;
}

I2cSharedStats getStats()
{
    I2cSharedStats out = {};
    out.timeout = gTimeoutCount;
    out.nack = gNackCount;
    out.error = gErrorCount;
    return out;
}

void resetStats()
{
    gTimeoutCount = 0;
    gNackCount = 0;
    gErrorCount = 0;
}

bool lock(uint32_t timeoutMs)
{
#ifdef ARDUINO_ARCH_ESP32
    if (!ensureMutex()) {
        return false;
    }

    TickType_t ticks = pdMS_TO_TICKS(timeoutMs);
    if (timeoutMs > 0 && ticks == 0) {
        ticks = 1;
    }

    if (xSemaphoreTakeRecursive(gI2cMutex, ticks) != pdTRUE) {
        if (gDiagEnabled) gTimeoutCount++;
        return false;
    }
#endif
    return true;
}

void unlock()
{
#ifdef ARDUINO_ARCH_ESP32
    if (gI2cMutex != nullptr) {
        xSemaphoreGiveRecursive(gI2cMutex);
    }
#endif
}

bool probe(TwoWire *wire, uint8_t address7bit, uint32_t timeoutMs, uint8_t retries)
{
    if (wire == nullptr) return false;
    if (!lock(timeoutMs)) return false;

    wire->setTimeOut((uint16_t)(timeoutMs == 0 ? 1 : timeoutMs));

    const uint8_t attempts = clampRetries(retries);
    bool ok = false;
    for (uint8_t i = 0; i < attempts; i++) {
        wire->beginTransmission(address7bit);
        const int err = wire->endTransmission(true);
        if (err == 0) {
            ok = true;
            break;
        }
        classifyWireError(err);
#ifdef ARDUINO_ARCH_ESP32
        if (i + 1 < attempts) {
            vTaskDelay(1);
        }
#endif
    }

    unlock();
    return ok;
}

bool probeAddress(uint8_t address7bit, uint32_t timeoutMs, uint8_t retries)
{
    return probe(&Wire, address7bit, timeoutMs, retries);
}

bool write(TwoWire *wire,
           uint8_t address7bit,
           const uint8_t *data,
           size_t len,
           bool sendStop,
           uint32_t timeoutMs,
           uint8_t retries)
{
    if (wire == nullptr || (len > 0 && data == nullptr)) return false;
    if (!lock(timeoutMs)) return false;

    wire->setTimeOut((uint16_t)(timeoutMs == 0 ? 1 : timeoutMs));

    const uint8_t attempts = clampRetries(retries);
    bool ok = false;
    for (uint8_t i = 0; i < attempts; i++) {
        wire->beginTransmission(address7bit);
        if (len > 0) {
            wire->write(data, len);
        }
        const int err = wire->endTransmission(sendStop);
        if (err == 0) {
            ok = true;
            break;
        }
        classifyWireError(err);
#ifdef ARDUINO_ARCH_ESP32
        if (i + 1 < attempts) {
            vTaskDelay(1);
        }
#endif
    }

    unlock();
    return ok;
}

bool writeRead(TwoWire *wire,
               uint8_t address7bit,
               const uint8_t *writeData,
               size_t writeLen,
               uint8_t *readData,
               size_t readLen,
               uint32_t timeoutMs,
               uint8_t retries)
{
    if (wire == nullptr) return false;
    if (writeLen > 0 && writeData == nullptr) return false;
    if (readLen > 0 && readData == nullptr) return false;
    if (!lock(timeoutMs)) return false;

    wire->setTimeOut((uint16_t)(timeoutMs == 0 ? 1 : timeoutMs));

    const uint8_t attempts = clampRetries(retries);
    bool ok = false;

    for (uint8_t i = 0; i < attempts; i++) {
        wire->beginTransmission(address7bit);
        if (writeLen > 0) {
            wire->write(writeData, writeLen);
        }

        int err = wire->endTransmission(readLen == 0);
        if (err != 0) {
            classifyWireError(err);
#ifdef ARDUINO_ARCH_ESP32
            if (i + 1 < attempts) {
                vTaskDelay(1);
            }
#endif
            continue;
        }

        if (readLen == 0) {
            ok = true;
            break;
        }

        const size_t received = wire->requestFrom((int)address7bit, (int)readLen);
        if (received != readLen) {
            if (gDiagEnabled) gErrorCount++;
            while (wire->available()) {
                (void)wire->read();
            }
#ifdef ARDUINO_ARCH_ESP32
            if (i + 1 < attempts) {
                vTaskDelay(1);
            }
#endif
            continue;
        }

        for (size_t idx = 0; idx < readLen; idx++) {
            if (!wire->available()) {
                if (gDiagEnabled) gErrorCount++;
                break;
            }
            readData[idx] = (uint8_t)wire->read();
        }

        ok = true;
        break;
    }

    unlock();
    return ok;
}

}  // namespace I2cShared
