#include "RTCService.h"

#include <cstring>

namespace RTCService {
namespace {

ErriezDS3231 gRtc;
Config gConfig = {};
Diagnostics gDiag = {};

bool isLeapYear(uint16_t year)
{
    if ((year % 4U) != 0U) {
        return false;
    }
    if ((year % 100U) != 0U) {
        return true;
    }
    return (year % 400U) == 0U;
}

uint8_t daysInMonth(uint16_t year, uint8_t month)
{
    static const uint8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1U || month > 12U) {
        return 0;
    }

    if (month == 2U && isLeapYear(year)) {
        return 29;
    }

    return kDays[month - 1U];
}

Status classifyReadWriteFailure(bool isWrite)
{
    const I2cSharedStats stats = I2cShared::getStats();
    gDiag.i2c = stats;

    if (stats.timeout > 0U) {
        return Status::BusBusyTimeout;
    }

    return isWrite ? Status::WriteFailed : Status::ReadFailed;
}

void setLastStatus(Status status)
{
    gDiag.lastStatus = status;
    gDiag.i2c = I2cShared::getStats();
}

void tmToDateTime(const struct tm &inTm, DateTime *outDateTime)
{
    outDateTime->year = (uint16_t)(inTm.tm_year + 1900);
    outDateTime->month = (uint8_t)(inTm.tm_mon + 1);
    outDateTime->day = (uint8_t)inTm.tm_mday;
    outDateTime->weekday = (uint8_t)inTm.tm_wday;
    outDateTime->hour = (uint8_t)inTm.tm_hour;
    outDateTime->minute = (uint8_t)inTm.tm_min;
    outDateTime->second = (uint8_t)inTm.tm_sec;
}

void dateTimeToTm(const DateTime &inDateTime, struct tm *outTm)
{
    memset(outTm, 0, sizeof(struct tm));
    outTm->tm_year = (int)inDateTime.year - 1900;
    outTm->tm_mon = (int)inDateTime.month - 1;
    outTm->tm_mday = (int)inDateTime.day;
    outTm->tm_wday = (int)inDateTime.weekday;
    outTm->tm_hour = (int)inDateTime.hour;
    outTm->tm_min = (int)inDateTime.minute;
    outTm->tm_sec = (int)inDateTime.second;
    outTm->tm_isdst = 0;
}

}  // namespace

Status begin(const Config &config)
{
    gConfig = config;

    if (gConfig.wire == nullptr) {
        setLastStatus(Status::InvalidArgument);
        return Status::InvalidArgument;
    }

    if (gConfig.i2cTimeoutMs == 0U) {
        gConfig.i2cTimeoutMs = 1U;
    }

    I2cShared::setDiagnosticsEnabled(gConfig.enableI2cDiagnostics);
    I2cShared::resetStats();

    if (gConfig.initI2cMaster) {
        I2cShared::initMaster(gConfig.wire, gConfig.sdaPin, gConfig.sclPin, gConfig.i2cClockHz);
    }

    gRtc.setTimeoutMs(gConfig.i2cTimeoutMs);

    if (!I2cShared::probe(gConfig.wire, DS3231_ADDR, gConfig.i2cTimeoutMs, gConfig.i2cRetries)) {
        gDiag.initialized = false;
        gDiag.rtcDetected = false;
        gDiag.oscillatorRunning = false;
        setLastStatus(Status::DeviceNotFound);
        return Status::DeviceNotFound;
    }

    gDiag.initialized = true;
    gDiag.rtcDetected = true;

    if (!gRtc.begin(*gConfig.wire)) {
        gDiag.oscillatorRunning = false;
        setLastStatus(Status::InternalError);
        return Status::InternalError;
    }

    gDiag.oscillatorRunning = gRtc.isRunning();

    if (!gDiag.oscillatorRunning) {
        setLastStatus(Status::OscillatorStopped);
        return Status::OscillatorStopped;
    }

    setLastStatus(Status::Ok);
    return Status::Ok;
}

bool isReady()
{
    return gDiag.initialized;
}

bool isValidDateTime(const DateTime &dateTime)
{
    if (dateTime.year < 2000U || dateTime.year > 2099U) {
        return false;
    }
    if (dateTime.month < 1U || dateTime.month > 12U) {
        return false;
    }
    if (dateTime.weekday > 6U) {
        return false;
    }
    if (dateTime.hour > 23U || dateTime.minute > 59U || dateTime.second > 59U) {
        return false;
    }

    const uint8_t maxDay = daysInMonth(dateTime.year, dateTime.month);
    return dateTime.day >= 1U && dateTime.day <= maxDay;
}

Status getTm(struct tm *outTm)
{
    if (!gDiag.initialized) {
        setLastStatus(Status::NotInitialized);
        return Status::NotInitialized;
    }
    if (outTm == nullptr) {
        setLastStatus(Status::InvalidArgument);
        return Status::InvalidArgument;
    }

    if (!I2cShared::lock(gConfig.i2cTimeoutMs)) {
        setLastStatus(Status::BusBusyTimeout);
        return Status::BusBusyTimeout;
    }

    const uint32_t startMs = millis();
    if (!gRtc.read(outTm)) {
        I2cShared::unlock();
        gDiag.readsFailed++;
        const Status status = classifyReadWriteFailure(false);
        gDiag.lastOperationDurationMs = millis() - startMs;
        setLastStatus(status);
        return status;
    }

    I2cShared::unlock();
    gDiag.readsOk++;
    gDiag.oscillatorRunning = gRtc.isRunning();
    gDiag.lastOperationDurationMs = millis() - startMs;
    setLastStatus(Status::Ok);
    return Status::Ok;
}

Status setTm(const struct tm &timeInfo)
{
    if (!gDiag.initialized) {
        setLastStatus(Status::NotInitialized);
        return Status::NotInitialized;
    }

    DateTime dt = {};
    tmToDateTime(timeInfo, &dt);
    if (!isValidDateTime(dt)) {
        gDiag.validationErrors++;
        setLastStatus(Status::InvalidDateTime);
        return Status::InvalidDateTime;
    }

    if (!I2cShared::lock(gConfig.i2cTimeoutMs)) {
        setLastStatus(Status::BusBusyTimeout);
        return Status::BusBusyTimeout;
    }

    const uint32_t startMs = millis();
    if (!gRtc.write(&timeInfo)) {
        I2cShared::unlock();
        gDiag.writesFailed++;
        const Status status = classifyReadWriteFailure(true);
        gDiag.lastOperationDurationMs = millis() - startMs;
        setLastStatus(status);
        return status;
    }

    I2cShared::unlock();
    gDiag.writesOk++;
    gDiag.oscillatorRunning = gRtc.isRunning();
    gDiag.lastOperationDurationMs = millis() - startMs;
    setLastStatus(Status::Ok);
    return Status::Ok;
}

Status getDateTime(DateTime *outDateTime)
{
    if (outDateTime == nullptr) {
        setLastStatus(Status::InvalidArgument);
        return Status::InvalidArgument;
    }

    struct tm timeInfo = {};
    const Status st = getTm(&timeInfo);
    if (st != Status::Ok) {
        return st;
    }

    tmToDateTime(timeInfo, outDateTime);
    return Status::Ok;
}

Status setDateTime(const DateTime &dateTime)
{
    if (!isValidDateTime(dateTime)) {
        gDiag.validationErrors++;
        setLastStatus(Status::InvalidDateTime);
        return Status::InvalidDateTime;
    }

    struct tm timeInfo = {};
    dateTimeToTm(dateTime, &timeInfo);
    return setTm(timeInfo);
}

Status getEpoch(time_t *outEpoch)
{
    if (!gDiag.initialized) {
        setLastStatus(Status::NotInitialized);
        return Status::NotInitialized;
    }
    if (outEpoch == nullptr) {
        setLastStatus(Status::InvalidArgument);
        return Status::InvalidArgument;
    }

    if (!I2cShared::lock(gConfig.i2cTimeoutMs)) {
        setLastStatus(Status::BusBusyTimeout);
        return Status::BusBusyTimeout;
    }

    const uint32_t startMs = millis();

    // ErriezDS3231::getEpoch() returns 0 on failure.
    const time_t epoch = gRtc.getEpoch();
    if (epoch == 0) {
        I2cShared::unlock();
        gDiag.readsFailed++;
        const Status status = classifyReadWriteFailure(false);
        gDiag.lastOperationDurationMs = millis() - startMs;
        setLastStatus(status);
        return status;
    }

    I2cShared::unlock();
    *outEpoch = epoch;
    gDiag.readsOk++;
    gDiag.oscillatorRunning = gRtc.isRunning();
    gDiag.lastOperationDurationMs = millis() - startMs;
    setLastStatus(Status::Ok);
    return Status::Ok;
}

Status setEpoch(time_t epoch, bool verify)
{
    if (!gDiag.initialized) {
        setLastStatus(Status::NotInitialized);
        return Status::NotInitialized;
    }

    // Basic sanity: avoid writing clearly invalid timestamps.
    // 2000-01-01 is 946684800.
    if (epoch < (time_t)946684800) {
        gDiag.validationErrors++;
        setLastStatus(Status::InvalidDateTime);
        return Status::InvalidDateTime;
    }

    if (!I2cShared::lock(gConfig.i2cTimeoutMs)) {
        setLastStatus(Status::BusBusyTimeout);
        return Status::BusBusyTimeout;
    }

    const uint32_t startMs = millis();
    if (!gRtc.setEpoch(epoch)) {
        I2cShared::unlock();
        gDiag.writesFailed++;
        const Status status = classifyReadWriteFailure(true);
        gDiag.lastOperationDurationMs = millis() - startMs;
        setLastStatus(status);
        return status;
    }

    if (verify) {
        const time_t readBack = gRtc.getEpoch();
        if (readBack == 0) {
            I2cShared::unlock();
            gDiag.writesFailed++;
            const Status status = classifyReadWriteFailure(false);
            gDiag.lastOperationDurationMs = millis() - startMs;
            setLastStatus(status);
            return status;
        }

        // Tolerate a 1s difference due to rollover between set and read.
        const int64_t delta = (int64_t)readBack - (int64_t)epoch;
        if (delta < -1 || delta > 1) {
            I2cShared::unlock();
            gDiag.validationErrors++;
            gDiag.lastOperationDurationMs = millis() - startMs;
            setLastStatus(Status::WriteFailed);
            return Status::WriteFailed;
        }
    }

    I2cShared::unlock();
    gDiag.writesOk++;
    gDiag.oscillatorRunning = gRtc.isRunning();
    gDiag.lastOperationDurationMs = millis() - startMs;
    setLastStatus(Status::Ok);
    return Status::Ok;
}

const Diagnostics &getDiagnostics()
{
    return gDiag;
}

void resetDiagnostics()
{
    const bool initialized = gDiag.initialized;
    const bool rtcDetected = gDiag.rtcDetected;
    const bool oscillatorRunning = gDiag.oscillatorRunning;

    gDiag = {};
    gDiag.initialized = initialized;
    gDiag.rtcDetected = rtcDetected;
    gDiag.oscillatorRunning = oscillatorRunning;
    gDiag.lastStatus = initialized ? Status::Ok : Status::NotInitialized;

    I2cShared::resetStats();
    gDiag.i2c = I2cShared::getStats();
}

const char *statusToString(Status status)
{
    switch (status) {
        case Status::Ok: return "ok";
        case Status::NotInitialized: return "not_initialized";
        case Status::InvalidArgument: return "invalid_argument";
        case Status::InvalidDateTime: return "invalid_datetime";
        case Status::BusBusyTimeout: return "bus_timeout";
        case Status::DeviceNotFound: return "device_not_found";
        case Status::ReadFailed: return "read_failed";
        case Status::WriteFailed: return "write_failed";
        case Status::OscillatorStopped: return "oscillator_stopped";
        case Status::InternalError: return "internal_error";
        default: return "unknown";
    }
}

}  // namespace RTCService
