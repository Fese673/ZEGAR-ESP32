/*
 * MIT License
 *
 * Copyright (c) 2020 Erriez
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*!
 * \file ErriezDS3231.cpp
 * \brief DS3231 high precision RTC library for Arduino
 * \details
 *      Source:         https://github.com/Erriez/ErriezDS3231
 *      Documentation:  https://erriez.github.io/ErriezDS3231
 */

#if (defined(__AVR__) || defined(ARDUINO_ARCH_SAM))
#include <avr/pgmspace.h>
#else
#include <pgmspace.h>
#endif

#include <string.h>

#include <Wire.h>

#include "ErriezDS3231.h"
#include "I2C_bus_shared.h"

#define DS3231_DEFAULT_I2C_TIMEOUT_MS 10U

namespace {

uint8_t bcdToDecImpl(uint8_t bcd)
{
    return (uint8_t)(10 * ((bcd & 0xF0) >> 4) + (bcd & 0x0F));
}

uint8_t decToBcdImpl(uint8_t dec)
{
    return (uint8_t)(((dec / 10) << 4) | (dec % 10));
}

bool decodeDateTimeBuffer(const uint8_t *buffer, struct tm *dt)
{
    memset(dt, 0, sizeof(struct tm));

    dt->tm_sec = bcdToDecImpl(buffer[0] & 0x7F);
    dt->tm_min = bcdToDecImpl(buffer[1] & 0x7F);
    dt->tm_hour = bcdToDecImpl(buffer[2] & 0x3F);
    dt->tm_wday = bcdToDecImpl(buffer[3] & 0x07);
    dt->tm_mday = bcdToDecImpl(buffer[4] & 0x3F);
    dt->tm_mon = bcdToDecImpl(buffer[5] & 0x1F);
    dt->tm_year = bcdToDecImpl(buffer[6]) + 100;
    dt->tm_isdst = 0;

    if (dt->tm_mon) {
        dt->tm_mon--;
    }
    if (dt->tm_wday) {
        dt->tm_wday--;
    }

    return (dt->tm_sec <= 59) &&
           (dt->tm_min <= 59) &&
           (dt->tm_hour <= 23) &&
           (dt->tm_mday >= 1) &&
           (dt->tm_mday <= 31) &&
           (dt->tm_mon <= 11) &&
           (dt->tm_year >= 100) &&
           (dt->tm_year <= 199) &&
           (dt->tm_wday <= 6);
}

void encodeDateTimeBuffer(const struct tm *dt, uint8_t *buffer)
{
    buffer[0] = decToBcdImpl((uint8_t)dt->tm_sec) & 0x7F;
    buffer[1] = decToBcdImpl((uint8_t)dt->tm_min) & 0x7F;
    buffer[2] = decToBcdImpl((uint8_t)dt->tm_hour) & 0x3F;
    buffer[3] = decToBcdImpl((uint8_t)(dt->tm_wday + 1)) & 0x07;
    buffer[4] = decToBcdImpl((uint8_t)dt->tm_mday) & 0x3F;
    buffer[5] = decToBcdImpl((uint8_t)(dt->tm_mon + 1)) & 0x1F;
    buffer[6] = decToBcdImpl((uint8_t)(dt->tm_year % 100));
}

bool epochToUtcTm(time_t epoch, struct tm *dt)
{
#if defined(ARDUINO_ARCH_ESP32)
    return gmtime_r(&epoch, dt) != nullptr;
#else
    struct tm *tmp = gmtime(&epoch);
    if (tmp == nullptr) {
        return false;
    }
    *dt = *tmp;
    return true;
#endif
}

int64_t daysFromCivil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = (unsigned)(year - era * 400);
    const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return (int64_t)era * 146097 + (int64_t)dayOfEra - 719468;
}

time_t utcTmToEpoch(struct tm *dt)
{
    const int64_t days = daysFromCivil(dt->tm_year + 1900,
                                       (unsigned)(dt->tm_mon + 1),
                                       (unsigned)dt->tm_mday);
    const int64_t seconds = days * 86400LL +
                            (int64_t)dt->tm_hour * 3600LL +
                            (int64_t)dt->tm_min * 60LL +
                            (int64_t)dt->tm_sec;
    return (time_t)seconds;
}

} // namespace

ErriezDS3231::ErriezDS3231() :
    _wire(nullptr)
    , _timeoutMs(DS3231_DEFAULT_I2C_TIMEOUT_MS)
{
}

bool ErriezDS3231::initializeBus(TwoWire *wire)
{
    if (wire != nullptr) {
        _wire = wire;
    } else if (_wire == nullptr) {
        _wire = &Wire;
    }

    if (_wire == nullptr) {
        return false;
    }

    return true;
}

bool ErriezDS3231::lockBus()
{
    if (!initializeBus(nullptr)) {
        return false;
    }

    return I2cShared::lock(_timeoutMs);
}

void ErriezDS3231::unlockBus()
{
    I2cShared::unlock();
}

bool ErriezDS3231::validateDateTime(const struct tm *dt) const
{
    if (dt == nullptr) {
        return false;
    }

    return (dt->tm_sec >= 0) && (dt->tm_sec <= 59) &&
           (dt->tm_min >= 0) && (dt->tm_min <= 59) &&
           (dt->tm_hour >= 0) && (dt->tm_hour <= 23) &&
           (dt->tm_mday >= 1) && (dt->tm_mday <= 31) &&
           (dt->tm_mon >= 0) && (dt->tm_mon <= 11) &&
           (dt->tm_year >= 100) && (dt->tm_year <= 199) &&
           (dt->tm_wday >= 0) && (dt->tm_wday <= 6);
}

bool ErriezDS3231::validateAlarmId(AlarmId alarmId) const
{
    return (alarmId == Alarm1) || (alarmId == Alarm2);
}

bool ErriezDS3231::begin()
{
    return begin(Wire);
}

bool ErriezDS3231::begin(TwoWire &wire)
{
    uint8_t statusReg = 0;

    if (!initializeBus(&wire) || !lockBus()) {
        return false;
    }

    const bool ok = readBufferLocked(DS3231_REG_STATUS, &statusReg, 1);
    unlockBus();

    return ok && ((statusReg & 0x70U) == 0U);
}

void ErriezDS3231::setTimeoutMs(uint32_t timeoutMs)
{
    _timeoutMs = (timeoutMs == 0U) ? 1U : timeoutMs;
}

uint32_t ErriezDS3231::getTimeoutMs() const
{
    return _timeoutMs;
}

bool ErriezDS3231::clockEnable(bool enable)
{
    uint8_t regs[2] = {0};

    if (!lockBus()) {
        return false;
    }

    bool ok = readBufferLocked(DS3231_REG_CONTROL, regs, sizeof(regs));
    if (ok) {
        if (enable) {
            regs[0] &= (uint8_t)~(1 << DS3231_CTRL_EOSC);
        } else {
            regs[0] |= (uint8_t)(1 << DS3231_CTRL_EOSC);
        }

        regs[1] &= (uint8_t)~(1 << DS3231_STAT_OSF);
        ok = writeBufferLocked(DS3231_REG_CONTROL, regs, sizeof(regs));
    }

    unlockBus();
    return ok;
}

bool ErriezDS3231::isRunning()
{
    bool ok = false;
    const uint8_t statusReg = readRegisterLocked(DS3231_REG_STATUS, &ok);
    return ok && ((statusReg & (1 << DS3231_STAT_OSF)) == 0);
}

time_t ErriezDS3231::getEpoch()
{
    struct tm dt = {};

    if (!read(&dt)) {
        return 0;
    }

    return utcTmToEpoch(&dt);
}

bool ErriezDS3231::setEpoch(time_t t)
{
    struct tm dt = {};

    if (!epochToUtcTm(t, &dt)) {
        return false;
    }

    dt.tm_isdst = 0;
    return write(&dt);
}

bool ErriezDS3231::read(struct tm *dt)
{
    uint8_t buffer[7];

    if (dt == nullptr) {
        return false;
    }

    if (!lockBus()) {
        memset(dt, 0, sizeof(struct tm));
        return false;
    }

    const bool ok = readBufferLocked(DS3231_REG_SECONDS, buffer, sizeof(buffer)) &&
                    decodeDateTimeBuffer(buffer, dt);

    unlockBus();

    if (!ok) {
        memset(dt, 0, sizeof(struct tm));
    }

    return ok;
}

bool ErriezDS3231::write(const struct tm *dt)
{
    uint8_t dateTimeBuffer[7];
    uint8_t regs[2] = {0};

    if (!validateDateTime(dt)) {
        return false;
    }

    encodeDateTimeBuffer(dt, dateTimeBuffer);

    if (!lockBus()) {
        return false;
    }

    bool ok = readBufferLocked(DS3231_REG_CONTROL, regs, sizeof(regs));
    if (ok) {
        regs[0] &= (uint8_t)~(1 << DS3231_CTRL_EOSC);
        regs[1] &= (uint8_t)~(1 << DS3231_STAT_OSF);
        ok = writeBufferLocked(DS3231_REG_CONTROL, regs, sizeof(regs));
    }
    if (ok) {
        ok = writeBufferLocked(DS3231_REG_SECONDS, dateTimeBuffer, sizeof(dateTimeBuffer));
    }

    unlockBus();
    return ok;
}

bool ErriezDS3231::setTime(uint8_t hour, uint8_t min, uint8_t sec)
{
    struct tm dt = {};

    if (!read(&dt)) {
        return false;
    }

    dt.tm_hour = hour;
    dt.tm_min = min;
    dt.tm_sec = sec;
    return write(&dt);
}

bool ErriezDS3231::getTime(uint8_t *hour, uint8_t *min, uint8_t *sec)
{
    uint8_t buffer[3];
    uint8_t localHour = 0;
    uint8_t localMin = 0;
    uint8_t localSec = 0;

    if (hour == nullptr || min == nullptr || sec == nullptr) {
        return false;
    }

    if (!lockBus()) {
        return false;
    }

    bool ok = readBufferLocked(DS3231_REG_SECONDS, buffer, sizeof(buffer));
    if (ok) {
        localSec = bcdToDecImpl(buffer[0] & 0x7F);
        localMin = bcdToDecImpl(buffer[1] & 0x7F);
        localHour = bcdToDecImpl(buffer[2] & 0x3F);
        ok = (localSec <= 59) && (localMin <= 59) && (localHour <= 23);
    }

    unlockBus();

    if (!ok) {
        *hour = 0;
        *min = 0;
        *sec = 0;
        return false;
    }

    *hour = localHour;
    *min = localMin;
    *sec = localSec;
    return true;
}

bool ErriezDS3231::setDateTime(uint8_t hour, uint8_t min, uint8_t sec,
                               uint8_t mday, uint8_t mon, uint16_t year,
                               uint8_t wday)
{
    struct tm dt = {};

    dt.tm_hour = hour;
    dt.tm_min = min;
    dt.tm_sec = sec;
    dt.tm_mday = mday;
    dt.tm_mon = (int)mon - 1;
    dt.tm_year = (int)year - 1900;
    dt.tm_wday = wday;
    dt.tm_isdst = 0;

    return write(&dt);
}

bool ErriezDS3231::getDateTime(uint8_t *hour, uint8_t *min, uint8_t *sec,
                               uint8_t *mday, uint8_t *mon, uint16_t *year,
                               uint8_t *wday)
{
    struct tm dt = {};

    if (hour == nullptr || min == nullptr || sec == nullptr ||
        mday == nullptr || mon == nullptr || year == nullptr || wday == nullptr) {
        return false;
    }

    if (!read(&dt)) {
        return false;
    }

    *hour = (uint8_t)dt.tm_hour;
    *min = (uint8_t)dt.tm_min;
    *sec = (uint8_t)dt.tm_sec;
    *mday = (uint8_t)dt.tm_mday;
    *mon = (uint8_t)(dt.tm_mon + 1);
    *year = (uint16_t)(dt.tm_year + 1900);
    *wday = (uint8_t)dt.tm_wday;
    return true;
}

bool ErriezDS3231::setAlarm1(Alarm1Type alarmType,
                             uint8_t dayDate, uint8_t hours, uint8_t minutes, uint8_t seconds)
{
    uint8_t buffer[4];

    if (dayDate < 1 || dayDate > 31 || hours > 23 || minutes > 59 || seconds > 59) {
        return false;
    }

    buffer[0] = decToBcdImpl(seconds);
    buffer[1] = decToBcdImpl(minutes);
    buffer[2] = decToBcdImpl(hours);
    buffer[3] = decToBcdImpl(dayDate);

    if (alarmType & 0x01) { buffer[0] |= (1 << DS3231_A1M1); }
    if (alarmType & 0x02) { buffer[1] |= (1 << DS3231_A1M2); }
    if (alarmType & 0x04) { buffer[2] |= (1 << DS3231_A1M3); }
    if (alarmType & 0x08) { buffer[3] |= (1 << DS3231_A1M4); }
    if (alarmType & 0x10) { buffer[3] |= (1 << DS3231_DYDT); }

    if (!lockBus()) {
        return false;
    }

    bool ok = writeBufferLocked(DS3231_REG_ALARM1_SEC, buffer, sizeof(buffer));
    if (ok) {
        uint8_t statusReg = readRegisterLocked(DS3231_REG_STATUS, &ok);
        if (ok) {
            statusReg &= (uint8_t)~(1 << DS3231_STAT_A1F);
            ok = writeRegisterLocked(DS3231_REG_STATUS, statusReg);
        }
    }

    unlockBus();
    return ok;
}

bool ErriezDS3231::setAlarm2(Alarm2Type alarmType, uint8_t dayDate, uint8_t hours, uint8_t minutes)
{
    uint8_t buffer[3];

    if (dayDate < 1 || dayDate > 31 || hours > 23 || minutes > 59) {
        return false;
    }

    buffer[0] = decToBcdImpl(minutes);
    buffer[1] = decToBcdImpl(hours);
    buffer[2] = decToBcdImpl(dayDate);

    if (alarmType & 0x02) { buffer[0] |= (1 << DS3231_A2M2); }
    if (alarmType & 0x04) { buffer[1] |= (1 << DS3231_A2M3); }
    if (alarmType & 0x08) { buffer[2] |= (1 << DS3231_A2M4); }
    if (alarmType & 0x10) { buffer[2] |= (1 << DS3231_DYDT); }

    if (!lockBus()) {
        return false;
    }

    bool ok = writeBufferLocked(DS3231_REG_ALARM2_MIN, buffer, sizeof(buffer));
    if (ok) {
        uint8_t statusReg = readRegisterLocked(DS3231_REG_STATUS, &ok);
        if (ok) {
            statusReg &= (uint8_t)~(1 << DS3231_STAT_A2F);
            ok = writeRegisterLocked(DS3231_REG_STATUS, statusReg);
        }
    }

    unlockBus();
    return ok;
}

bool ErriezDS3231::alarmInterruptEnable(AlarmId alarmId, bool enable)
{
    uint8_t regs[2] = {0};
    const uint8_t enableBit = (alarmId == Alarm1) ? DS3231_CTRL_A1IE : DS3231_CTRL_A2IE;
    const uint8_t flagBit = (alarmId == Alarm1) ? DS3231_STAT_A1F : DS3231_STAT_A2F;

    if (!validateAlarmId(alarmId) || !lockBus()) {
        return false;
    }

    bool ok = readBufferLocked(DS3231_REG_CONTROL, regs, sizeof(regs));
    if (ok) {
        regs[1] &= (uint8_t)~(1 << flagBit);
        regs[0] |= (1 << DS3231_CTRL_INTCN);

        if (enable) {
            regs[0] |= (1 << enableBit);
        } else {
            regs[0] &= (uint8_t)~(1 << enableBit);
        }

        ok = writeBufferLocked(DS3231_REG_CONTROL, regs, sizeof(regs));
    }

    unlockBus();
    return ok;
}

bool ErriezDS3231::getAlarmFlag(AlarmId alarmId)
{
    bool ok = false;
    uint8_t flagMask = 0;

    if (!validateAlarmId(alarmId)) {
        return false;
    }

    flagMask = (alarmId == Alarm1) ? (1 << DS3231_STAT_A1F) : (1 << DS3231_STAT_A2F);
    const uint8_t statusReg = readRegisterLocked(DS3231_REG_STATUS, &ok);
    return ok && ((statusReg & flagMask) != 0);
}

bool ErriezDS3231::clearAlarmFlag(AlarmId alarmId)
{
    bool ok = false;
    uint8_t statusReg = 0;
    uint8_t flagMask = 0;

    if (!validateAlarmId(alarmId) || !lockBus()) {
        return false;
    }

    flagMask = (alarmId == Alarm1) ? (1 << DS3231_STAT_A1F) : (1 << DS3231_STAT_A2F);
    statusReg = readRegisterLocked(DS3231_REG_STATUS, &ok);
    if (ok) {
        statusReg &= (uint8_t)~flagMask;
        ok = writeRegisterLocked(DS3231_REG_STATUS, statusReg);
    }

    unlockBus();
    return ok;
}

bool ErriezDS3231::setSquareWave(SquareWave squareWave)
{
    bool ok = false;
    uint8_t controlReg = 0;

    if (!lockBus()) {
        return false;
    }

    controlReg = readRegisterLocked(DS3231_REG_CONTROL, &ok);
    if (ok) {
        controlReg &= (uint8_t)~((1 << DS3231_CTRL_BBSQW) |
                                 (1 << DS3231_CTRL_INTCN) |
                                 (1 << DS3231_CTRL_RS2) |
                                 (1 << DS3231_CTRL_RS1));
        controlReg |= (uint8_t)squareWave;
        ok = writeRegisterLocked(DS3231_REG_CONTROL, controlReg);
    }

    unlockBus();
    return ok;
}

bool ErriezDS3231::outputClockPinEnable(bool enable)
{
    bool ok = false;
    uint8_t statusReg = 0;

    if (!lockBus()) {
        return false;
    }

    statusReg = readRegisterLocked(DS3231_REG_STATUS, &ok);
    if (ok) {
        if (enable) {
            statusReg |= (1 << DS3231_STAT_EN32KHZ);
        } else {
            statusReg &= (uint8_t)~(1 << DS3231_STAT_EN32KHZ);
        }
        ok = writeRegisterLocked(DS3231_REG_STATUS, statusReg);
    }

    unlockBus();
    return ok;
}

bool ErriezDS3231::setAgingOffset(int8_t val)
{
    const uint8_t regVal = (uint8_t)val;

    if (!lockBus()) {
        return false;
    }

    bool ok = writeRegisterLocked(DS3231_REG_AGING_OFFSET, regVal);
    if (ok) {
        uint8_t regs[2] = {0};
        ok = readBufferLocked(DS3231_REG_CONTROL, regs, sizeof(regs));
        if (ok && ((regs[1] & (1 << DS3231_STAT_BSY)) == 0)) {
            regs[0] |= (1 << DS3231_CTRL_CONV);
            ok = writeRegisterLocked(DS3231_REG_CONTROL, regs[0]);
        } else if (ok) {
            ok = false;
        }
    }

    unlockBus();
    return ok;
}

int8_t ErriezDS3231::getAgingOffset()
{
    bool ok = false;
    const uint8_t regVal = readRegisterLocked(DS3231_REG_AGING_OFFSET, &ok);
    return ok ? (int8_t)regVal : 0;
}

bool ErriezDS3231::startTemperatureConversion()
{
    uint8_t regs[2] = {0};

    if (!lockBus()) {
        return false;
    }

    bool ok = readBufferLocked(DS3231_REG_CONTROL, regs, sizeof(regs));
    if (ok) {
        if (regs[1] & (1 << DS3231_STAT_BSY)) {
            ok = false;
        } else {
            regs[0] |= (1 << DS3231_CTRL_CONV);
            ok = writeRegisterLocked(DS3231_REG_CONTROL, regs[0]);
        }
    }

    unlockBus();
    return ok;
}

bool ErriezDS3231::getTemperature(int8_t *temperature, uint8_t *fraction)
{
    uint8_t temp[2];

    if (temperature == nullptr || fraction == nullptr) {
        return false;
    }

    if (!lockBus()) {
        return false;
    }

    const bool ok = readBufferLocked(DS3231_REG_TEMP_MSB, temp, sizeof(temp));
    unlockBus();

    if (!ok) {
        return false;
    }

    *temperature = (int8_t)temp[0];
    *fraction = (uint8_t)((temp[1] >> 6) * 25U);
    return true;
}

uint8_t ErriezDS3231::bcdToDec(uint8_t bcd)
{
    return bcdToDecImpl(bcd);
}

uint8_t ErriezDS3231::decToBcd(uint8_t dec)
{
    return decToBcdImpl(dec);
}

uint8_t ErriezDS3231::readRegister(uint8_t reg)
{
    return readRegisterLocked(reg, nullptr);
}

uint8_t ErriezDS3231::readRegisterLocked(uint8_t reg, bool *ok)
{
    uint8_t value = 0;
    bool status = false;

    if (lockBus()) {
        status = readBufferLocked(reg, &value, 1);
        unlockBus();
    }

    if (ok != nullptr) {
        *ok = status;
    }

    return value;
}

bool ErriezDS3231::writeRegister(uint8_t reg, uint8_t value)
{
    if (!lockBus()) {
        return false;
    }

    const bool ok = writeRegisterLocked(reg, value);
    unlockBus();
    return ok;
}

bool ErriezDS3231::writeRegisterLocked(uint8_t reg, uint8_t value)
{
    return writeBufferLocked(reg, &value, 1);
}

bool ErriezDS3231::readBuffer(uint8_t reg, void *buffer, uint8_t len)
{
    if (!lockBus()) {
        return false;
    }

    const bool ok = readBufferLocked(reg, buffer, len);
    unlockBus();
    return ok;
}

bool ErriezDS3231::readBufferLocked(uint8_t reg, void *buffer, uint8_t len)
{
    if (len == 0) {
        return true;
    }
    if (_wire == nullptr || buffer == nullptr) {
        return false;
    }
    if (reg >= DS3231_NUM_REGS || (uint16_t)reg + len > DS3231_NUM_REGS) {
        return false;
    }

    return I2cShared::writeRead(_wire,
                                DS3231_ADDR,
                                &reg,
                                1,
                                (uint8_t *)buffer,
                                len,
                                _timeoutMs,
                                3);
}

bool ErriezDS3231::writeBuffer(uint8_t reg, const void *buffer, uint8_t len)
{
    if (!lockBus()) {
        return false;
    }

    const bool ok = writeBufferLocked(reg, buffer, len);
    unlockBus();
    return ok;
}

bool ErriezDS3231::writeBufferLocked(uint8_t reg, const void *buffer, uint8_t len)
{
    if (len == 0) {
        return true;
    }
    if (_wire == nullptr || buffer == nullptr) {
        return false;
    }
    if (reg >= DS3231_NUM_REGS || (uint16_t)reg + len > DS3231_NUM_REGS) {
        return false;
    }

    uint8_t tx[DS3231_NUM_REGS + 1];
    tx[0] = reg;
    memcpy(&tx[1], buffer, len);
    return I2cShared::write(_wire,
                            DS3231_ADDR,
                            tx,
                            (size_t)len + 1,
                            true,
                            _timeoutMs,
                            3);
}

bool ErriezDS3231::readControlStatus(uint8_t *controlReg, uint8_t *statusReg)
{
    uint8_t regs[2] = {0};

    if (controlReg == nullptr || statusReg == nullptr || !lockBus()) {
        return false;
    }

    const bool ok = readBufferLocked(DS3231_REG_CONTROL, regs, sizeof(regs));
    unlockBus();

    if (!ok) {
        return false;
    }

    *controlReg = regs[0];
    *statusReg = regs[1];
    return true;
}

bool ErriezDS3231::writeControlStatus(uint8_t controlReg, uint8_t statusReg)
{
    const uint8_t regs[2] = {controlReg, statusReg};

    if (!lockBus()) {
        return false;
    }

    const bool ok = writeBufferLocked(DS3231_REG_CONTROL, regs, sizeof(regs));
    unlockBus();
    return ok;
}
