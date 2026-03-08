/***************************************************************************************************/
/*
   Non-blocking AHT2x library for ESP32-WROOM-32D
   Based on enjoyneering AHTxx (GPL), fully rewritten as state machine

   Features:
   - 100% non-blocking -- zero delay() calls anywhere
   - CRC8 validation on every read (AHT2x)
   - Built-in 5-sample median filter for outlier rejection
   - Built-in 1D Kalman filter for optimal estimation
   - Self-heating compensation model
   - Configurable measurement interval (default 3s)

   GNU GPL license
*/
/***************************************************************************************************/

#ifndef AHTXX_h
#define AHTXX_h

#include <Arduino.h>
#include <Wire.h>

#define AHTXX_ADDRESS_X38                 0x38
#define AHT10_ADDRESS_X39                 0x39

#define AHT1X_INIT_REG                    0xE1
#define AHT2X_INIT_REG                    0xBE
#define AHTXX_STATUS_REG                  0x71
#define AHTXX_START_MEASUREMENT_REG       0xAC
#define AHTXX_SOFT_RESET_REG              0xBA

#define AHT1X_INIT_CTRL_NORMAL_MODE       0x00
#define AHTXX_INIT_CTRL_CAL_ON            0x08
#define AHTXX_INIT_CTRL_NOP               0x00

#define AHTXX_STATUS_CTRL_BUSY            0x80
#define AHTXX_STATUS_CTRL_CAL_ON          0x08

typedef enum : uint8_t {
        AHTXX_STATUS_NO_VALID_OUTPUT   = 0x00,
        AHTXX_STATUS_WARM_UP           = 0x01,
        AHTXX_STATUS_NORMAL_OPERATION  = 0x02,
        AHTXX_STATUS_INITIAL_STARTUP   = 0x03,
} AHTStatusFlag;

#define AHTXX_START_MEASUREMENT_CTRL      0x33
#define AHTXX_START_MEASUREMENT_CTRL_NOP  0x00

#define AHTXX_CMD_DELAY_MS        10
#define AHTXX_MEASUREMENT_MS      85
#define AHT2X_POWER_ON_MS         100
#define AHTXX_SOFT_RESET_MS       25

#define AHTXX_FORCE_READ_DATA    true
#define AHTXX_USE_READ_DATA      false

#define AHTXX_NO_ERROR           0x00
#define AHTXX_BUSY_ERROR         0x01
#define AHTXX_ACK_ERROR          0x02
#define AHTXX_DATA_ERROR         0x03
#define AHTXX_CRC8_ERROR         0x04
#define AHTXX_ERROR              0xFF

#define AHT_MEDIAN_WINDOW        5
#define AHT_DEFAULT_INTERVAL_MS  3000

typedef enum : uint8_t {
    AHT1x_SENSOR = 0x00,
    AHT2x_SENSOR = 0x01,
} AHTXX_I2C_SENSOR;

typedef enum : uint8_t {
    AHT_STATE_IDLE = 0,
    AHT_STATE_POWER_WAIT,
    AHT_STATE_RESET_SENT,
    AHT_STATE_RESET_WAIT,
    AHT_STATE_INIT_SENT,
    AHT_STATE_INIT_WAIT,
    AHT_STATE_CAL_CHECK,
    AHT_STATE_READY,
    AHT_STATE_MEAS_CMD_SENT,
    AHT_STATE_MEAS_WAIT,
    AHT_STATE_ERROR,
} AHTState;

struct Kalman1D {
    float x;
    float P;
    float Q;
    float R;
    bool  inited;

    Kalman1D() : x(0), P(1.0f), Q(0.005f), R(0.25f), inited(false) {}

    void configure(float processNoise, float measurementNoise) {
        Q = processNoise;
        R = measurementNoise;
    }

    float update(float z) {
        if (!inited) { x = z; P = 1.0f; inited = true; return x; }
        P += Q;
        float K = P / (P + R);
        x += K * (z - x);
        P *= (1.0f - K);
        return x;
    }

    void reset() { inited = false; P = 1.0f; }
};

struct MedianFilter {
    float buf[AHT_MEDIAN_WINDOW];
    uint8_t idx;
    uint8_t count;

    MedianFilter() : idx(0), count(0) {}

    float update(float val) {
        buf[idx] = val;
        idx = (idx + 1) % AHT_MEDIAN_WINDOW;
        if (count < AHT_MEDIAN_WINDOW) count++;

        float sorted[AHT_MEDIAN_WINDOW];
        memcpy(sorted, buf, sizeof(float) * count);
        for (uint8_t i = 1; i < count; i++) {
            float key = sorted[i];
            int8_t j = i - 1;
            while (j >= 0 && sorted[j] > key) {
                sorted[j + 1] = sorted[j];
                j--;
            }
            sorted[j + 1] = key;
        }
        return sorted[count / 2];
    }

    void reset() { idx = 0; count = 0; }
};

class AHTxx {
  public:
    AHTxx(uint8_t address = AHTXX_ADDRESS_X38, AHTXX_I2C_SENSOR sensorType = AHT2x_SENSOR);

    bool begin(int32_t sda = SDA, int32_t scl = SCL,
               uint32_t speed = 100000, uint32_t stretch = 1000);
    bool update();

    bool isReady() const { return _state == AHT_STATE_READY; }

    float getTemperature() const { return _filteredTemp; }
    float getHumidity()    const { return _filteredHum; }
    float getRawTemperature() const { return _rawTemp; }
    float getRawHumidity()    const { return _rawHum; }

    float readTemperature(bool readAHT = AHTXX_FORCE_READ_DATA);
    float readHumidity(bool readAHT = AHTXX_USE_READ_DATA);

    void  setMeasurementInterval(unsigned long ms) { _intervalMs = ms; }
    void  setKalmanParams(float tempQ, float tempR, float humQ, float humR);
    void  setSelfHeatingCompensation(float offsetC) { _selfHeatOffset = offsetC; }

    bool     softReset();
    uint8_t  getStatus() const { return _status; }
    AHTStatusFlag getStatusFlag() const { return _appStatus; }
    AHTState getState()  const { return _state; }
    bool     hasValidSample() const { return _sampleValid; }
    bool     hasFreshData(unsigned long maxAgeMs) const;
    unsigned long getLastSampleMs() const { return _lastGoodSampleMs; }
    void     setType(AHTXX_I2C_SENSOR t) { _sensorType = t; }
    bool     setNormalMode();

  private:
    AHTXX_I2C_SENSOR _sensorType;
    uint8_t   _address;
    uint8_t   _status;
    AHTStatusFlag _appStatus;
    uint8_t   _rawData[7];
    AHTState  _state;
    unsigned long _stateTs;
    unsigned long _lastMeasMs;
    unsigned long _lastGoodSampleMs;
    unsigned long _intervalMs;
    uint32_t  _measureCount;
    bool      _sampleValid;

    float _selfHeatOffset;
    float _rawTemp, _rawHum;
    float _filteredTemp, _filteredHum;

    MedianFilter _medTemp, _medHum;
    Kalman1D     _kalTemp, _kalHum;

    bool _sendResetCmd();
    bool _sendInitRegister(uint8_t value);
    bool _sendMeasurementCmd();
    bool _readStatusByte(uint8_t &out);
    bool _readMeasurementData();
    bool _parseRawData(float &temp, float &hum);
    bool _checkCRC8();
    bool _probeConnection();
    void _invalidateSample(bool resetFilters);
    void _updateAppStatus(uint8_t statusByte);
};

#endif