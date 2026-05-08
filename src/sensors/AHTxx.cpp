/***************************************************************************************************/
/*
    Non-blocking AHT2x library -- ESP32 implementation
    100% state-machine driven, zero hard waits

   GNU GPL license
*/
/***************************************************************************************************/

#include "AHTxx.h"

AHTxx::AHTxx(uint8_t address, AHTXX_I2C_SENSOR sensorType)
        : _sensorType(sensorType),
            _address(address),
            _status(AHTXX_NO_ERROR),
            _appStatus(AHTXX_STATUS_NO_VALID_OUTPUT),
      _state(AHT_STATE_IDLE),
      _stateTs(0),
      _lastMeasMs(0),
    _lastGoodSampleMs(0),
      _intervalMs(AHT_DEFAULT_INTERVAL_MS),
      _measureCount(0),
    _sampleValid(false),
      _selfHeatOffset(0.0f),
      _rawTemp(NAN),
      _rawHum(NAN),
      _filteredTemp(NAN),
      _filteredHum(NAN)
{
    memset(_rawData, 0, sizeof(_rawData));
}

bool AHTxx::begin(int32_t sda, int32_t scl, uint32_t speed, uint32_t stretch)
{
    (void)sda;
    (void)scl;
    (void)speed;

    uint32_t timeoutMs = stretch / 1000;
    if (timeoutMs == 0) timeoutMs = 1;
    Wire.setTimeout(timeoutMs);

    _initialized = false;
    _status = AHTXX_NO_ERROR;
    _appStatus = AHTXX_STATUS_NO_VALID_OUTPUT;
    _invalidateSample(true);

    if (!_probeConnection()) {
        _status = AHTXX_ACK_ERROR;
        _state = AHT_STATE_IDLE;
        _stateTs = millis();
        return false;
    }

    _state = AHT_STATE_POWER_WAIT;
    _stateTs = millis();
    _initialized = true;
    return true;
}

bool AHTxx::hasFreshData(unsigned long maxAgeMs) const
{
    if (!_sampleValid) return false;
    return (millis() - _lastGoodSampleMs) <= maxAgeMs;
}

bool AHTxx::_sendResetCmd()
{
    Wire.beginTransmission(_address);
    Wire.write(AHTXX_SOFT_RESET_REG);
    return (Wire.endTransmission(true) == 0);
}

bool AHTxx::_sendInitRegister(uint8_t value)
{
    Wire.beginTransmission(_address);
    Wire.write((_sensorType == AHT1x_SENSOR) ? AHT1X_INIT_REG : AHT2X_INIT_REG);
    Wire.write(value);
    Wire.write(AHTXX_INIT_CTRL_NOP);
    return (Wire.endTransmission(true) == 0);
}

bool AHTxx::_sendMeasurementCmd()
{
    Wire.beginTransmission(_address);
    Wire.write(AHTXX_START_MEASUREMENT_REG);
    Wire.write(AHTXX_START_MEASUREMENT_CTRL);
    Wire.write(AHTXX_START_MEASUREMENT_CTRL_NOP);
    return (Wire.endTransmission(true) == 0);
}

bool AHTxx::_probeConnection()
{
    Wire.beginTransmission(_address);
    return (Wire.endTransmission(true) == 0);
}

void AHTxx::_invalidateSample(bool resetFilters)
{
    _sampleValid = false;
    _lastGoodSampleMs = 0;
    _rawTemp = NAN;
    _rawHum = NAN;
    _filteredTemp = NAN;
    _filteredHum = NAN;

    if (resetFilters) {
        _medTemp.reset();
        _medHum.reset();
        _kalTemp.reset();
        _kalHum.reset();
    }
}

void AHTxx::_updateAppStatus(uint8_t statusByte)
{
    if (statusByte & AHTXX_STATUS_CTRL_BUSY) {
        _appStatus = AHTXX_STATUS_NO_VALID_OUTPUT;
        return;
    }

    if (statusByte & AHTXX_STATUS_CTRL_CAL_ON) {
        _appStatus = AHTXX_STATUS_NORMAL_OPERATION;
        return;
    }

    _appStatus = AHTXX_STATUS_NO_VALID_OUTPUT;
}

bool AHTxx::_readStatusByte(uint8_t &out)
{
    Wire.requestFrom(_address, (uint8_t)1, (uint8_t)true);
    if (Wire.available() < 1) return false;
    out = Wire.read();
    _updateAppStatus(out);
    return true;
}

bool AHTxx::_readMeasurementData()
{
    uint8_t dataSize = (_sensorType == AHT1x_SENSOR) ? 6 : 7;
    Wire.requestFrom(_address, dataSize, (uint8_t)true);
    if (Wire.available() < dataSize) return false;
    Wire.readBytes(_rawData, dataSize);
    return true;
}

bool AHTxx::_parseRawData(float &temp, float &hum)
{
    if (_rawData[0] & AHTXX_STATUS_CTRL_BUSY) return false;

    uint32_t rawH = _rawData[1];
    rawH <<= 8;  rawH |= _rawData[2];
    rawH <<= 4;  rawH |= (_rawData[3] >> 4);
    if (rawH > 0x100000) rawH = 0x100000;
    hum = ((float)rawH / 0x100000) * 100.0f;

    uint32_t rawT = _rawData[3] & 0x0F;
    rawT <<= 8;  rawT |= _rawData[4];
    rawT <<= 8;  rawT |= _rawData[5];
    temp = ((float)rawT / 0x100000) * 200.0f - 50.0f;

    return true;
}

bool AHTxx::_checkCRC8()
{
    if (_sensorType != AHT2x_SENSOR) return true;

    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < 6; i++) {
        crc ^= _rawData[i];
        for (uint8_t b = 8; b > 0; --b) {
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x31) : (crc << 1);
        }
    }
    return (crc == _rawData[6]);
}

bool AHTxx::update()
{
    if (!_initialized) {
        return false;
    }

    unsigned long now = millis();

    switch (_state) {
    case AHT_STATE_POWER_WAIT:
        if ((now - _stateTs) >= AHT2X_POWER_ON_MS) {
            if (_sendResetCmd()) {
                _state = AHT_STATE_RESET_WAIT;
                _stateTs = now;
            } else {
                _status = AHTXX_ACK_ERROR;
                _invalidateSample(true);
                _state = AHT_STATE_ERROR;
                _stateTs = now;
            }
        }
        return false;

    case AHT_STATE_RESET_WAIT:
        if ((now - _stateTs) >= AHTXX_SOFT_RESET_MS) {
            if (_sendInitRegister(AHTXX_INIT_CTRL_CAL_ON | AHT1X_INIT_CTRL_NORMAL_MODE)) {
                _state = AHT_STATE_INIT_WAIT;
                _stateTs = now;
            } else {
                _status = AHTXX_ACK_ERROR;
                _invalidateSample(true);
                _state = AHT_STATE_ERROR;
                _stateTs = now;
            }
        }
        return false;

    case AHT_STATE_INIT_WAIT:
        if ((now - _stateTs) >= AHTXX_CMD_DELAY_MS) {
            uint8_t st;
            if (_readStatusByte(st)) {
                if (st & AHTXX_STATUS_CTRL_CAL_ON) {
                    _status = AHTXX_NO_ERROR;
                    _updateAppStatus(st);
                    _state = AHT_STATE_READY;
                    _lastMeasMs = 0;
                    _measureCount = 0;
                } else {
                    _status = AHTXX_ERROR;
                    _invalidateSample(true);
                    _state = AHT_STATE_ERROR;
                    _stateTs = now;
                }
            } else {
                _status = AHTXX_ACK_ERROR;
                _invalidateSample(true);
                _state = AHT_STATE_ERROR;
                _stateTs = now;
            }
        }
        return false;

    case AHT_STATE_READY:
        if (_lastMeasMs == 0 || (now - _lastMeasMs) >= _intervalMs) {
            if (_sendMeasurementCmd()) {
                _state = AHT_STATE_MEAS_CMD_SENT;
                _stateTs = now;
                _lastMeasMs = now;
            } else {
                _status = AHTXX_ACK_ERROR;
                _invalidateSample(true);
            }
        }
        return false;

    case AHT_STATE_MEAS_CMD_SENT:
        if ((now - _stateTs) >= AHTXX_MEASUREMENT_MS) {
            _state = AHT_STATE_MEAS_WAIT;
        }
        return false;

    case AHT_STATE_MEAS_WAIT: {
        uint8_t st;
        if (!_readStatusByte(st)) {
            _status = AHTXX_ACK_ERROR;
            _invalidateSample(true);
            _state = AHT_STATE_READY;
            return false;
        }
        if (st & AHTXX_STATUS_CTRL_BUSY) {
            if ((now - _stateTs) > 200) {
                _status = AHTXX_BUSY_ERROR;
                _invalidateSample(true);
                _state = AHT_STATE_READY;
            }
            return false;
        }
        if (!_readMeasurementData()) {
            _status = AHTXX_DATA_ERROR;
            _invalidateSample(true);
            _state = AHT_STATE_READY;
            return false;
        }

        if (!_checkCRC8()) {
            _status = AHTXX_CRC8_ERROR;
            _invalidateSample(true);
            _state = AHT_STATE_READY;
            return false;
        }

        float rawT, rawH;
        if (!_parseRawData(rawT, rawH)) {
            _status = AHTXX_BUSY_ERROR;
            _invalidateSample(true);
            _state = AHT_STATE_READY;
            return false;
        }

        _rawTemp = rawT;
        _rawHum  = rawH;
        _lastGoodSampleMs = now;
        _sampleValid = true;
        _measureCount++;

        float compensatedT = rawT - _selfHeatOffset;
        float medT = _medTemp.update(compensatedT);
        float medH = _medHum.update(rawH);

        _filteredTemp = _kalTemp.update(medT);
        _filteredHum  = _kalHum.update(medH);

        if (_filteredHum > 100.0f) _filteredHum = 100.0f;
        if (_filteredHum < 0.0f)   _filteredHum = 0.0f;

        _status = AHTXX_NO_ERROR;
        _state = AHT_STATE_READY;
        return true;
    }

    case AHT_STATE_ERROR:
        if ((now - _stateTs) >= 2000) {
            _invalidateSample(true);
            _state = AHT_STATE_POWER_WAIT;
            _stateTs = now;
        }
        return false;

    default:
        _state = AHT_STATE_IDLE;
        return false;
    }
}

void AHTxx::setKalmanParams(float tempQ, float tempR, float humQ, float humR)
{
    _kalTemp.configure(tempQ, tempR);
    _kalHum.configure(humQ, humR);
}

bool AHTxx::setNormalMode()
{
    return _sendInitRegister(AHTXX_INIT_CTRL_CAL_ON | AHT1X_INIT_CTRL_NORMAL_MODE);
}

bool AHTxx::softReset()
{
    if (!_sendResetCmd()) return false;
    _state = AHT_STATE_RESET_WAIT;
    _stateTs = millis();
    _invalidateSample(true);
    return true;
}

float AHTxx::readTemperature(bool readAHT)
{
    if (readAHT == AHTXX_FORCE_READ_DATA) {
        ESP_LOGW("AHTxx", "readTemperature(FORCE_READ): blocking path, avoid in callbacks/UI tasks");

        const unsigned long readyDeadline = millis() + 500;
        while (_state != AHT_STATE_READY && _state != AHT_STATE_ERROR && millis() < readyDeadline) {
            update();
            yield();
        }
        if (_state != AHT_STATE_READY) return AHTXX_ERROR;

        _lastMeasMs = 0;

        const unsigned long measureDeadline = millis() + 300;
        while (millis() < measureDeadline) {
            if (update()) break;
            yield();
        }
    }
    return isnan(_filteredTemp) ? AHTXX_ERROR : _filteredTemp;
}

float AHTxx::readHumidity(bool readAHT)
{
    if (readAHT == AHTXX_FORCE_READ_DATA) {
        readTemperature(true);
    }
    return isnan(_filteredHum) ? AHTXX_ERROR : _filteredHum;
}