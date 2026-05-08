#include "ENS160AHT21Sensor.h"

#include <math.h>
#include <string.h>

#include <Arduino.h>
#include <Wire.h>
#include <ScioSense_ENS16x.h>
#include "AppLog.h"
#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#include "AHTxx.h"
#include "ENS160AHT21Screen.h"
#include "I2C_bus_shared.h"
#include "TemperatureConfig.h"

namespace {

constexpr const char* TAG = "ENS160";

constexpr uint8_t ENS160_I2C_ADDRESS = 0x53;
constexpr uint8_t ENS160_I2C_ADDRESS_ALT = 0x52;
constexpr uint8_t I2C_SDA_PIN = 21;
constexpr uint8_t I2C_SCL_PIN = 22;
constexpr uint8_t ENS160_INIT_RETRIES = 1;
constexpr unsigned long ENS160_INIT_RETRY_DELAY_MS = 20;
constexpr uint8_t ENS160_REINIT_ERROR_THRESHOLD = 3;
constexpr unsigned long ENS160_REINIT_COOLDOWN_MS = 5000UL;
constexpr unsigned long ENS160_COMM_ERR_LOG_INTERVAL_MS = 2000UL;
constexpr unsigned long ENS160_NO_NEW_DATA_LOG_INTERVAL_MS = 10000UL;
constexpr unsigned long LOOP_POLL_INTERVAL_MS = ENS16X_SYSTEM_TIMING_STANDARD_MEASURE + 50UL;
constexpr unsigned long AHT_DATA_MAX_AGE_MS = 5000UL;
// NOTE: Temperature offsets are centralized in include/TemperatureConfig.h

enum Ens160RuntimeState : uint8_t {
    ENS160_STATE_OK = 0,
    ENS160_STATE_WARMUP,
    ENS160_STATE_INIT_STARTUP,
    ENS160_STATE_NO_NEW_DATA,
    ENS160_STATE_COMM_ERR,
    ENS160_STATE_RECOVERING,
    ENS160_STATE_OFFLINE,
    ENS160_STATE_INVALID_OUTPUT,
};

ENS160 s_ens160;
AHTxx s_aht21(AHTXX_ADDRESS_X38, AHT2x_SENSOR);

bool s_started = false;
bool s_ens160Present = false;
bool s_ahtPresent = false;
uint8_t s_ens160Address = ENS160_I2C_ADDRESS;
uint8_t s_ens160ErrorStreak = 0;
uint8_t s_pendingValidity = 0xFF;
uint8_t s_pendingValidityCount = 0;
unsigned long s_lastPollMs = 0;
unsigned long s_lastEns160CommErrLogMs = 0;
unsigned long s_lastEns160NoNewDataLogMs = 0;
unsigned long s_lastEns160ReinitAttemptMs = 0;

Ens160RuntimeState s_runtimeState = ENS160_STATE_OFFLINE;
bool s_hasGasSample = false;
bool s_hasClimateSample = false;
int s_lastAqi = 0;
uint16_t s_lastTvoc = 0;
uint16_t s_lastEco2 = 0;
float s_lastTemperature = 0.0f;
float s_lastRawTemperature = 0.0f;
float s_lastHumidity = 0.0f;

const char* getEns160RuntimeStateString(Ens160RuntimeState state) {
    switch (state) {
        case ENS160_STATE_OK: return "OK";
        case ENS160_STATE_WARMUP: return "WARMUP";
        case ENS160_STATE_INIT_STARTUP: return "INIT_STARTUP";
        case ENS160_STATE_NO_NEW_DATA: return "NO_NEW_DATA";
        case ENS160_STATE_COMM_ERR: return "COMM_ERR";
        case ENS160_STATE_RECOVERING: return "RECOVERING";
        case ENS160_STATE_OFFLINE: return "OFFLINE";
        case ENS160_STATE_INVALID_OUTPUT: return "INVALID_OUTPUT";
        default: return "UNKNOWN";
    }
}

uint8_t getENS160ValidityFlag(Ens16x_DeviceStatus status) {
    uint8_t validityHigh = (status & ENS16X_DEVICE_STATUS_VALID_HIGH) >> 3;
    uint8_t validityLow  = (status & ENS16X_DEVICE_STATUS_VALID_LOW) >> 2;
    return (validityHigh << 1) | validityLow;
}

Ens160RuntimeState getEns160RuntimeStateFromValidity(uint8_t validity) {
    switch (validity) {
        case 0: return ENS160_STATE_OK;
        case 1: return ENS160_STATE_WARMUP;
        case 2: return ENS160_STATE_INIT_STARTUP;
        case 3: return ENS160_STATE_INVALID_OUTPUT;
        default: return ENS160_STATE_INVALID_OUTPUT;
    }
}

bool isEns160ValidityStable(uint8_t validityFlag) {
    if (validityFlag == s_pendingValidity) {
        if (s_pendingValidityCount < 255) {
            s_pendingValidityCount++;
        }
    } else {
        s_pendingValidity = validityFlag;
        s_pendingValidityCount = 1;
    }

    return (s_pendingValidity == validityFlag) && (s_pendingValidityCount >= 2);
}

void logEns160Measurement(Ens160RuntimeState state) {
#ifdef ENS160_DEBUG
    if (s_hasGasSample && s_hasClimateSample) {
        LOG_I(TAG, "status=%s aqi=%d tvoc=%u eco2=%u temperature_c=%.2f humidity_pct=%.2f",
              getEns160RuntimeStateString(state),
              s_lastAqi,
              (unsigned)s_lastTvoc,
              (unsigned)s_lastEco2,
              s_lastTemperature,
              s_lastHumidity);
    } else if (s_hasGasSample) {
        LOG_I(TAG, "status=%s aqi=%d tvoc=%u eco2=%u",
              getEns160RuntimeStateString(state),
              s_lastAqi,
              (unsigned)s_lastTvoc,
              (unsigned)s_lastEco2);
    } else if (s_hasClimateSample) {
        LOG_I(TAG, "status=%s temperature_c=%.2f humidity_pct=%.2f",
              getEns160RuntimeStateString(state),
              s_lastTemperature,
              s_lastHumidity);
    } else {
        LOG_I(TAG, "status=%s", getEns160RuntimeStateString(state));
    }
#else
    (void)state;
#endif
}

void publishRuntimeData(bool forceDirty) {
    ENS160AHT21Screen::RuntimeData& runtime = ENS160AHT21Screen::runtimeData;
    const bool hasSample = s_hasGasSample || s_hasClimateSample;
    const char* stateText = getEns160RuntimeStateString(s_runtimeState);

    bool changed = forceDirty;
    if (runtime.aqi != (uint8_t)s_lastAqi) changed = true;
    if (runtime.tvoc != s_lastTvoc) changed = true;
    if (runtime.eco2 != s_lastEco2) changed = true;
    if (fabsf(runtime.temperatureC - s_lastTemperature) > 0.01f) changed = true;
    if (fabsf(runtime.humidityPct - s_lastHumidity) > 0.01f) changed = true;
    if (runtime.hasSample != hasSample) changed = true;
    if (runtime.hasGasSample != s_hasGasSample) changed = true;
    if (runtime.hasClimateSample != s_hasClimateSample) changed = true;
    if (strcmp(runtime.statusText, stateText) != 0) changed = true;

    if (!changed) {
        return;
    }

    runtime.aqi = (uint8_t)s_lastAqi;
    runtime.tvoc = s_lastTvoc;
    runtime.eco2 = s_lastEco2;
    runtime.temperatureC = s_lastTemperature;
    runtime.rawTemperatureC = s_lastRawTemperature;
    runtime.humidityPct = s_lastHumidity;
    runtime.hasSample = hasSample;
    runtime.hasGasSample = s_hasGasSample;
    runtime.hasClimateSample = s_hasClimateSample;
    strncpy(runtime.statusText, stateText, sizeof(runtime.statusText));
    runtime.statusText[sizeof(runtime.statusText) - 1] = '\0';
    runtime.lastUpdateMs = millis();
    ENS160AHT21Screen::markScreenDirty();
}

void setENS160OfflineState() {
    s_ens160Present = false;
    s_ens160ErrorStreak = 0;
    s_pendingValidity = 0xFF;
    s_pendingValidityCount = 0;
    s_runtimeState = ENS160_STATE_OFFLINE;
}

bool tryInitENS160AtAddress(uint8_t address) {
    for (uint8_t attempt = 0; attempt < ENS160_INIT_RETRIES; ++attempt) {
        if (!I2cShared::lock(50)) {
            return false;
        }

        if (attempt == 0) {
            s_ens160.begin(&Wire, address);
        }

        const bool initOk = s_ens160.init();

        if (initOk) {
            s_ens160Address = address;
            s_ens160ErrorStreak = 0;
            s_pendingValidity = 0xFF;
            s_pendingValidityCount = 0;

            const bool measureOk = s_ens160.startStandardMeasure() == RESULT_OK;
            I2cShared::unlock();
            return measureOk;
        }

        I2cShared::unlock();

        if ((attempt + 1) < ENS160_INIT_RETRIES) {
#ifdef ARDUINO_ARCH_ESP32
            vTaskDelay(pdMS_TO_TICKS(ENS160_INIT_RETRY_DELAY_MS));
#else
            const unsigned long retryWaitUntilMs = millis() + ENS160_INIT_RETRY_DELAY_MS;
            while ((long)(millis() - retryWaitUntilMs) < 0) {
                yield();
            }
#endif
        }
    }

    return false;
}

bool initializeENS160() {
    if (tryInitENS160AtAddress(ENS160_I2C_ADDRESS)) {
        return true;
    }
    return tryInitENS160AtAddress(ENS160_I2C_ADDRESS_ALT);
}

void updateClimateData() {
    if (!s_ahtPresent) {
        return;
    }

    if (!I2cShared::lock(50)) {
        return;
    }
    const bool newData = s_aht21.update();
    I2cShared::unlock();

    const bool freshData = s_aht21.hasFreshData(AHT_DATA_MAX_AGE_MS);
    const float temperature = s_aht21.getTemperature();
    const float humidity = s_aht21.getHumidity();

    if (freshData && !isnan(temperature) && !isnan(humidity)) {
        // Preserve raw filtered reading for diagnostics
        s_lastRawTemperature = temperature;

        // Apply single-point compensation once here (domain normalization)
        const float compensatedTemp = temperature + TempConfig::TEMPERATURE_OFFSET_C;

        const bool climateChanged = !s_hasClimateSample ||
            fabsf(s_lastTemperature - compensatedTemp) > 0.01f ||
            fabsf(s_lastHumidity - humidity) > 0.01f;

        s_lastTemperature = compensatedTemp;
        s_lastHumidity = humidity;
        s_hasClimateSample = true;

        if (newData && s_ens160Present) {
            if (I2cShared::lock(50)) {
                const uint16_t tempRaw = Ens16x_CalcTempInFromCelsius(s_lastTemperature);
                const uint16_t humRaw = Ens16x_CalcRhIn(humidity);
                const Result compResult = s_ens160.writeCompensation(tempRaw, humRaw);
                I2cShared::unlock();
                if (compResult != RESULT_OK) {
                    LOG_E(TAG, "Compensation write failed code=%d", (int)compResult);
                }
            }
        }

        if (climateChanged) {
            publishRuntimeData(false);
        }
    } else if (!freshData && s_hasClimateSample) {
        s_hasClimateSample = false;
        s_lastTemperature = 0.0f;
        s_lastRawTemperature = 0.0f;
        s_lastHumidity = 0.0f;
        publishRuntimeData(false);
    }
}

}  // namespace

namespace ENS160AHT21Sensor {

void begin() {
    ENS160AHT21Screen::resetRuntimeData();

    s_ens160Present = initializeENS160();
    if (!s_ens160Present) {
        setENS160OfflineState();
        LOG_W(TAG, "status=offline reason=not_responding action=check_wiring_power_pullups");
    } else {
        LOG_I(TAG, "status=ready address=0x%02X", s_ens160Address);
        s_runtimeState = ENS160_STATE_INIT_STARTUP;
    }

    {
        bool ahtOk = false;
        if (I2cShared::lock(50)) {
            ahtOk = s_aht21.begin(I2C_SDA_PIN, I2C_SCL_PIN);
            I2cShared::unlock();
        }
        if (ahtOk) {
            s_aht21.setKalmanParams(0.003f, 0.15f, 0.01f, 1.0f);
            s_aht21.setSelfHeatingCompensation(0.1f);
            s_aht21.setMeasurementInterval(3000);
            s_ahtPresent = true;
            LOG_I(TAG, "component=AHT21 action=init_start");
        } else {
            s_ahtPresent = false;
            LOG_W(TAG, "component=AHT21 status=not_detected");
        }
    }

    s_started = true;
    publishRuntimeData(true);
}

void update() {
    if (!s_started) {
        return;
    }

    if (!s_ens160Present && !s_ahtPresent) {
        return;
    }

    const unsigned long now = millis();
    updateClimateData();

    if ((now - s_lastPollMs) < LOOP_POLL_INTERVAL_MS) {
        return;
    }
    s_lastPollMs = now;

    if (!s_ens160Present) {
        if ((now - s_lastEns160CommErrLogMs) >= ENS160_COMM_ERR_LOG_INTERVAL_MS) {
            LOG_W(TAG, "status=OFFLINE");
            s_lastEns160CommErrLogMs = now;
        }
        s_runtimeState = ENS160_STATE_OFFLINE;
        publishRuntimeData(false);
        return;
    }

    if (!I2cShared::lock(50)) {
        s_runtimeState = ENS160_STATE_COMM_ERR;
        publishRuntimeData(false);
        return;
    }
    const Result ensResult = s_ens160.update();
    I2cShared::unlock();

    if (ensResult == RESULT_OK) {
        s_ens160ErrorStreak = 0;

        bool hasMeasurement = false;
        if (s_ens160.hasNewData()) {
            s_lastAqi = (int)(uint8_t)s_ens160.getAirQualityIndex_UBA();
            s_lastTvoc = (uint16_t)s_ens160.getTvoc();
            s_lastEco2 = (uint16_t)s_ens160.getEco2();
            s_hasGasSample = true;
            hasMeasurement = true;
        }

        const Ens16x_DeviceStatus devStatus = s_ens160.getDeviceStatus();
        const uint8_t validityFlag = getENS160ValidityFlag(devStatus);
        s_runtimeState = getEns160RuntimeStateFromValidity(validityFlag);

        if (hasMeasurement && isEns160ValidityStable(validityFlag)) {
            logEns160Measurement(s_runtimeState);
        }

        publishRuntimeData(hasMeasurement);
        return;
    }

    if (ensResult == RESULT_INVALID) {
        s_runtimeState = ENS160_STATE_NO_NEW_DATA;
        if ((now - s_lastEns160NoNewDataLogMs) >= ENS160_NO_NEW_DATA_LOG_INTERVAL_MS) {
            LOG_I(TAG, "status=NO_NEW_DATA");
            s_lastEns160NoNewDataLogMs = now;
        }
        publishRuntimeData(false);
        return;
    }

    s_ens160ErrorStreak++;
    s_runtimeState = ENS160_STATE_COMM_ERR;

    if ((now - s_lastEns160CommErrLogMs) >= ENS160_COMM_ERR_LOG_INTERVAL_MS) {
        LOG_W(TAG, "status=COMM_ERR code=%d streak=%u", (int)ensResult, s_ens160ErrorStreak);
        s_lastEns160CommErrLogMs = now;
    }

    if (ensResult == RESULT_IO_ERROR &&
        s_ens160ErrorStreak >= ENS160_REINIT_ERROR_THRESHOLD &&
        (now - s_lastEns160ReinitAttemptMs) >= ENS160_REINIT_COOLDOWN_MS) {
        s_lastEns160ReinitAttemptMs = now;
        LOG_W(TAG, "status=COMM_ERR action=restart_sensor");
        s_runtimeState = ENS160_STATE_RECOVERING;
        publishRuntimeData(true);

        setENS160OfflineState();
        s_ens160Present = initializeENS160();
        if (s_ens160Present) {
            LOG_I(TAG, "status=recovered address=0x%02X", s_ens160Address);
            s_runtimeState = ENS160_STATE_RECOVERING;
        } else {
            LOG_E(TAG, "status=offline action=reinit_failed");
            s_runtimeState = ENS160_STATE_OFFLINE;
        }
    }

    publishRuntimeData(false);
}

}  // namespace ENS160AHT21Sensor