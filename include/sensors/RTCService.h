#pragma once

#include <Arduino.h>
#include <time.h>
#include <Wire.h>

#include "ErriezDS3231.h"
#include "I2C_bus_shared.h"

namespace RTCService {

enum class Status : uint8_t {
    Ok = 0,
    NotInitialized,
    InvalidArgument,
    InvalidDateTime,
    BusBusyTimeout,
    DeviceNotFound,
    ReadFailed,
    WriteFailed,
    OscillatorStopped,
    InternalError,
};

struct DateTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

struct Config {
    TwoWire *wire = &Wire;
    int sdaPin = 21;
    int sclPin = 22;
    uint32_t i2cClockHz = 400000;
    uint32_t i2cTimeoutMs = 10;
    uint8_t i2cRetries = 2;
    bool initI2cMaster = true;
    bool enableI2cDiagnostics = true;
};

struct Diagnostics {
    bool initialized = false;
    bool rtcDetected = false;
    bool oscillatorRunning = false;

    Status lastStatus = Status::NotInitialized;
    uint32_t lastOperationDurationMs = 0;

    uint32_t readsOk = 0;
    uint32_t readsFailed = 0;
    uint32_t writesOk = 0;
    uint32_t writesFailed = 0;
    uint32_t validationErrors = 0;

    I2cSharedStats i2c = {};
};

Status begin(const Config &config = Config());
bool isReady();

Status getDateTime(DateTime *outDateTime);
Status setDateTime(const DateTime &dateTime);

Status getTm(struct tm *outTm);
Status setTm(const struct tm &timeInfo);

// UTC epoch helpers (recommended for storing UTC in DS3231)
Status getEpoch(time_t *outEpoch);
Status setEpoch(time_t epoch, bool verify = true);

bool isValidDateTime(const DateTime &dateTime);
const Diagnostics &getDiagnostics();
void resetDiagnostics();

const char *statusToString(Status status);

}  // namespace RTCService
