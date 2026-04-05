#include "I2C_bus_shared.h"

#include "AppLog.h"
#include "RuntimeTelemetry.h"

#include <atomic>

#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif

namespace {

constexpr char TAG[] = "I2C";

#ifdef ARDUINO_ARCH_ESP32
SemaphoreHandle_t gI2cMutex = nullptr;
QueueHandle_t gI2cRequestQueue = nullptr;
TaskHandle_t gI2cWorkerTask = nullptr;
#endif

bool gDiagEnabled = false;
uint32_t gTimeoutCount = 0;
uint32_t gNackCount = 0;
uint32_t gErrorCount = 0;

#ifdef ARDUINO_ARCH_ESP32
constexpr uint32_t kI2cQueueSubmitTimeoutMs = 5;
constexpr size_t kI2cRequestPoolSize = 8;
constexpr uint32_t kI2cWorkerStackSize = 3072;
constexpr UBaseType_t kI2cWorkerPriority = configMAX_PRIORITIES - 4;
constexpr BaseType_t kI2cWorkerCore = 1;

struct I2cRequest {
    enum class Op : uint8_t {
        Probe,
        Write,
        WriteRead,
    };

    std::atomic<uint8_t> refs{2};
    std::atomic<bool> completed{false};
    SemaphoreHandle_t done = nullptr;
    bool inUse = false;
    bool result = false;
    Op op = Op::Probe;
    TwoWire *wire = nullptr;
    uint8_t address7bit = 0;
    const uint8_t *writeData = nullptr;
    size_t writeLen = 0;
    uint8_t *readData = nullptr;
    size_t readLen = 0;
    bool sendStop = true;
    uint32_t timeoutMs = 1;
    uint8_t retries = 1;
};

I2cRequest gI2cRequestPool[kI2cRequestPoolSize];
portMUX_TYPE gI2cRequestPoolMux = portMUX_INITIALIZER_UNLOCKED;

I2cRequest *acquireRequest()
{
    taskENTER_CRITICAL(&gI2cRequestPoolMux);
    for (size_t index = 0; index < kI2cRequestPoolSize; ++index) {
        I2cRequest &slot = gI2cRequestPool[index];
        if (!slot.inUse) {
            slot.inUse = true;
            slot.refs.store(2, std::memory_order_relaxed);
            slot.completed.store(false, std::memory_order_relaxed);
            slot.result = false;
            slot.op = I2cRequest::Op::Probe;
            slot.wire = nullptr;
            slot.address7bit = 0;
            slot.writeData = nullptr;
            slot.writeLen = 0;
            slot.readData = nullptr;
            slot.readLen = 0;
            slot.sendStop = true;
            slot.timeoutMs = 1;
            slot.retries = 1;
            taskEXIT_CRITICAL(&gI2cRequestPoolMux);

            if (slot.done == nullptr) {
                slot.done = xSemaphoreCreateBinary();
                if (slot.done == nullptr) {
                    taskENTER_CRITICAL(&gI2cRequestPoolMux);
                    slot.inUse = false;
                    taskEXIT_CRITICAL(&gI2cRequestPoolMux);
                    return nullptr;
                }
            }

            (void)xSemaphoreTake(slot.done, 0);
            return &slot;
        }
    }
    taskEXIT_CRITICAL(&gI2cRequestPoolMux);
    return nullptr;
}

void releaseRequest(I2cRequest *request)
{
    if (request == nullptr) {
        return;
    }

    if (request->refs.fetch_sub(1, std::memory_order_acq_rel) != 1) {
        return;
    }

    taskENTER_CRITICAL(&gI2cRequestPoolMux);
    request->wire = nullptr;
    request->writeData = nullptr;
    request->writeLen = 0;
    request->readData = nullptr;
    request->readLen = 0;
    request->completed.store(false, std::memory_order_relaxed);
    request->inUse = false;
    taskEXIT_CRITICAL(&gI2cRequestPoolMux);
}

bool executeProbe(TwoWire *wire, uint8_t address7bit, uint32_t timeoutMs, uint8_t retries);
bool executeWrite(TwoWire *wire,
                  uint8_t address7bit,
                  const uint8_t *data,
                  size_t len,
                  bool sendStop,
                  uint32_t timeoutMs,
                  uint8_t retries);
bool executeWriteRead(TwoWire *wire,
                      uint8_t address7bit,
                      const uint8_t *writeData,
                      size_t writeLen,
                      uint8_t *readData,
                      size_t readLen,
                      uint32_t timeoutMs,
                      uint8_t retries);

void i2cWorkerTask(void *)
{
    for (;;) {
        I2cRequest *request = nullptr;
        if (xQueueReceive(gI2cRequestQueue, &request, portMAX_DELAY) != pdTRUE || request == nullptr) {
            continue;
        }

        switch (request->op) {
            case I2cRequest::Op::Probe:
                request->result = executeProbe(request->wire, request->address7bit, request->timeoutMs, request->retries);
                break;

            case I2cRequest::Op::Write:
                request->result = executeWrite(request->wire,
                                               request->address7bit,
                                               request->writeData,
                                               request->writeLen,
                                               request->sendStop,
                                               request->timeoutMs,
                                               request->retries);
                break;

            case I2cRequest::Op::WriteRead:
                request->result = executeWriteRead(request->wire,
                                                   request->address7bit,
                                                   request->writeData,
                                                   request->writeLen,
                                                   request->readData,
                                                   request->readLen,
                                                   request->timeoutMs,
                                                   request->retries);
                break;
        }

        request->completed.store(true, std::memory_order_release);
        if (request->done != nullptr) {
            (void)xSemaphoreGive(request->done);
        }
        releaseRequest(request);
    }
}

bool ensureWorker()
{
    if (gI2cRequestQueue == nullptr) {
        gI2cRequestQueue = xQueueCreate(8, sizeof(I2cRequest *));
        if (gI2cRequestQueue == nullptr) {
            return false;
        }
    }

    if (gI2cWorkerTask == nullptr) {
        if (xTaskCreatePinnedToCore(i2cWorkerTask,
                                    "i2cWorker",
                                    kI2cWorkerStackSize,
                                    nullptr,
                                    kI2cWorkerPriority,
                                    &gI2cWorkerTask,
                                    kI2cWorkerCore) != pdPASS) {
            gI2cWorkerTask = nullptr;
            return false;
        }
    }

    return true;
}

bool submitRequest(I2cRequest *request)
{
    if (request == nullptr) {
        return false;
    }

    if (!ensureWorker()) {
        return false;
    }

    if (xQueueSend(gI2cRequestQueue, &request, pdMS_TO_TICKS(kI2cQueueSubmitTimeoutMs)) != pdTRUE) {
        if (gDiagEnabled) {
            ++gTimeoutCount;
        }
        TELEMETRY_INC(i2c_queue_full);
        TELEMETRY_INC(i2c_timeouts);
        releaseRequest(request);
        releaseRequest(request);
        return false;
    }

    const uint32_t timeoutMs = request->timeoutMs == 0 ? 1 : request->timeoutMs;
    TickType_t waitTicks = pdMS_TO_TICKS(timeoutMs);
    if (timeoutMs > 0 && waitTicks == 0) {
        waitTicks = 1;
    }

    if (request->done == nullptr || xSemaphoreTake(request->done, waitTicks) != pdTRUE) {
        if (gDiagEnabled) {
            ++gTimeoutCount;
        }
        TELEMETRY_INC(i2c_timeouts);
        releaseRequest(request);
        return false;
    }

    const bool ok = request->completed.load(std::memory_order_acquire) && request->result;
    releaseRequest(request);
    return ok;
}
#endif

uint8_t clampRetries(uint8_t retries)
{
    if (retries < 1) return 1;
    if (retries > 3) return 3;
    return retries;
}

void classifyWireError(int err)
{
    if (err == 2 || err == 3) {
        TELEMETRY_INC(i2c_errors);
        if (gDiagEnabled) {
            ++gNackCount;
        }
    } else if (err == 5) {
        TELEMETRY_INC(i2c_timeouts);
        if (gDiagEnabled) {
            ++gTimeoutCount;
        }
    } else if (err != 0) {
        TELEMETRY_INC(i2c_errors);
        if (gDiagEnabled) {
            ++gErrorCount;
        }
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

bool executeProbe(TwoWire *wire, uint8_t address7bit, uint32_t timeoutMs, uint8_t retries)
{
    if (wire == nullptr) return false;

    if (!I2cShared::lock(timeoutMs)) {
        return false;
    }

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

    I2cShared::unlock();
    return ok;
}

bool executeWrite(TwoWire *wire,
                  uint8_t address7bit,
                  const uint8_t *data,
                  size_t len,
                  bool sendStop,
                  uint32_t timeoutMs,
                  uint8_t retries)
{
    if (wire == nullptr || (len > 0 && data == nullptr)) return false;

    if (!I2cShared::lock(timeoutMs)) {
        return false;
    }

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

    I2cShared::unlock();
    return ok;
}

bool executeWriteRead(TwoWire *wire,
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

    if (!I2cShared::lock(timeoutMs)) {
        return false;
    }

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
            if (gDiagEnabled) {
                ++gErrorCount;
            }
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
                if (gDiagEnabled) {
                    ++gErrorCount;
                }
                break;
            }
            readData[idx] = (uint8_t)wire->read();
        }

        ok = true;
        break;
    }

    I2cShared::unlock();
    return ok;
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
    if (!ensureWorker()) {
        LOG_W(TAG, "Worker task could not be started fallback_path=true");
    }
#endif

#ifdef ARDUINO_ARCH_ESP32
    if (!applied) {
        LOG_W(TAG,
              "Clock mismatch after begin requested_hz=%lu actual_hz=%lu sda=%d scl=%d retrying=true",
              (unsigned long)clockHz,
              (unsigned long)actualClockHz,
              sdaPin,
              sclPin);

        wire->end();
        TickType_t settleTicks = pdMS_TO_TICKS(1);
        if (settleTicks == 0) {
            settleTicks = 1;
        }
        vTaskDelay(settleTicks);
        wire->begin(sdaPin, sclPin);
        applied = applyClockAndVerify(wire, clockHz, &actualClockHz);
    }

        LOG_I(TAG,
            "Bus ready requested_hz=%lu actual_hz=%lu sda=%d scl=%d status=%s",
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
        if (gDiagEnabled) {
            ++gTimeoutCount;
        }
        TELEMETRY_INC(i2c_timeouts);
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

#ifdef ARDUINO_ARCH_ESP32
    if (gI2cWorkerTask != nullptr && gI2cRequestQueue != nullptr) {
        I2cRequest *request = acquireRequest();
        if (request != nullptr) {
            request->op = I2cRequest::Op::Probe;
            request->wire = wire;
            request->address7bit = address7bit;
            request->timeoutMs = (timeoutMs == 0) ? 1U : timeoutMs;
            request->retries = retries;
            return submitRequest(request);
        }
    }
#endif

    return executeProbe(wire, address7bit, timeoutMs, retries);
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

#ifdef ARDUINO_ARCH_ESP32
    if (gI2cWorkerTask != nullptr && gI2cRequestQueue != nullptr) {
        I2cRequest *request = acquireRequest();
        if (request != nullptr) {
            request->op = I2cRequest::Op::Write;
            request->wire = wire;
            request->address7bit = address7bit;
            request->writeData = data;
            request->writeLen = len;
            request->sendStop = sendStop;
            request->timeoutMs = (timeoutMs == 0) ? 1U : timeoutMs;
            request->retries = retries;
            return submitRequest(request);
        }
    }
#endif

    return executeWrite(wire, address7bit, data, len, sendStop, timeoutMs, retries);
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

#ifdef ARDUINO_ARCH_ESP32
    if (gI2cWorkerTask != nullptr && gI2cRequestQueue != nullptr) {
        I2cRequest *request = acquireRequest();
        if (request != nullptr) {
            request->op = I2cRequest::Op::WriteRead;
            request->wire = wire;
            request->address7bit = address7bit;
            request->writeData = writeData;
            request->writeLen = writeLen;
            request->readData = readData;
            request->readLen = readLen;
            request->timeoutMs = (timeoutMs == 0) ? 1U : timeoutMs;
            request->retries = retries;
            return submitRequest(request);
        }
    }
#endif

    return executeWriteRead(wire, address7bit, writeData, writeLen, readData, readLen, timeoutMs, retries);
}

}  // namespace I2cShared
