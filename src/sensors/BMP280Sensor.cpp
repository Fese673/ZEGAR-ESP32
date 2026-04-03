#include "BMP280Sensor.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <string.h>

#include "BMP280Screen.h"
#include "i2c/SharedBus.h"
#include <Preferences.h>

namespace {

constexpr uint8_t BMP280_ADDR_LOW = 0x76;
constexpr uint8_t BMP280_ADDR_HIGH = 0x77;
constexpr uint8_t BMP280_CHIP_ID = 0x58;
constexpr uint8_t BMP280_REG_ID = 0xD0;
constexpr uint8_t BMP280_REG_RESET = 0xE0;
constexpr uint8_t BMP280_REG_STATUS = 0xF3;
constexpr uint8_t BMP280_REG_CTRL_MEAS = 0xF4;
constexpr uint8_t BMP280_REG_CONFIG = 0xF5;
constexpr uint8_t BMP280_REG_PRESS_MSB = 0xF7;
constexpr uint8_t BMP280_RESET_CMD = 0xB6;
constexpr uint8_t BMP280_CTRL_SLEEP = 0x00;
constexpr uint8_t BMP280_CTRL_FORCED = 0x01;
constexpr uint8_t BMP280_OSRS_X1 = 0x01;
constexpr uint8_t BMP280_OSRS_X2 = 0x02;
constexpr uint8_t BMP280_OSRS_X4 = 0x03;
constexpr uint8_t BMP280_FILTER_X4 = 0x02;
constexpr unsigned long BMP280_REQUEST_INTERVAL_MS = 1000UL;
constexpr unsigned long BMP280_CONVERSION_TIMEOUT_MS = 50UL;
constexpr unsigned long BMP280_STALE_AFTER_MS = 5000UL;
constexpr unsigned long BMP280_RETRY_COOLDOWN_MS = 5000UL;
constexpr unsigned long BMP280_I2C_TIMEOUT_MS = 10UL;
constexpr uint8_t BMP280_I2C_RETRIES = 2;
constexpr float BMP280_TEMP_THRESHOLD_C = 0.05f;
constexpr float BMP280_PRESSURE_THRESHOLD_HPA = 0.10f;
constexpr float BMP280_ALTITUDE_THRESHOLD_M = 0.20f;
constexpr float BMP280_SEA_LEVEL_PRESSURE_HPA = 0.0f;

struct CalibrationData {
  uint16_t digT1;
  int16_t digT2;
  int16_t digT3;
  uint16_t digP1;
  int16_t digP2;
  int16_t digP3;
  int16_t digP4;
  int16_t digP5;
  int16_t digP6;
  int16_t digP7;
  int16_t digP8;
  int16_t digP9;
};

enum class DriverState : uint8_t {
  Init = 0,
  Idle,
  Waiting,
  Ready,
  Missing,
  Error,
};

struct SensorRuntime {
  DriverState driverState = DriverState::Init;
  uint8_t address = BMP280_ADDR_LOW;
  bool started = false;
  bool altitudeEnabled = false;
  bool hasCalibration = false;
  bool measurementPending = false;
  unsigned long lastRequestMs = 0;
  unsigned long lastGoodSampleMs = 0;
  unsigned long lastStateChangeLogMs = 0;
  unsigned long lastRecoveryAttemptMs = 0;
  int32_t tFine = 0;
  CalibrationData calib = {};
  float lastTemperatureC = 0.0f;
  float lastPressureHpa = 0.0f;
  float lastAltitudeM = 0.0f;
};

SensorRuntime s_runtime;
static float s_pressureOffsetHpa = 0.0f;

bool readBytes(uint8_t address, uint8_t reg, uint8_t *buffer, size_t length) {
  if (buffer == nullptr || length == 0) {
    return false;
  }

  if (!I2cShared::lock(BMP280_I2C_TIMEOUT_MS)) {
    return false;
  }

  Wire.setTimeOut((uint16_t)BMP280_I2C_TIMEOUT_MS);
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    I2cShared::unlock();
    return false;
  }

  const size_t received = Wire.requestFrom((int)address, (int)length, (int)true);
  if (received != length) {
    while (Wire.available() > 0) {
      (void)Wire.read();
    }
    I2cShared::unlock();
    return false;
  }

  for (size_t i = 0; i < length; ++i) {
    buffer[i] = (uint8_t)Wire.read();
  }

  I2cShared::unlock();
  return true;
}

bool writeReg(uint8_t address, uint8_t reg, uint8_t value) {
  if (!I2cShared::lock(BMP280_I2C_TIMEOUT_MS)) {
    return false;
  }

  Wire.setTimeOut((uint16_t)BMP280_I2C_TIMEOUT_MS);
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  const bool ok = (Wire.endTransmission(true) == 0);
  I2cShared::unlock();
  return ok;
}

bool readU8(uint8_t address, uint8_t reg, uint8_t &value) {
  return readBytes(address, reg, &value, 1);
}

bool readCalibration(uint8_t address, CalibrationData &calib) {
  uint8_t raw[24] = {};
  if (!readBytes(address, 0x88, raw, sizeof(raw))) {
    return false;
  }

  calib.digT1 = (uint16_t)((uint16_t)raw[1] << 8 | raw[0]);
  calib.digT2 = (int16_t)((uint16_t)raw[3] << 8 | raw[2]);
  calib.digT3 = (int16_t)((uint16_t)raw[5] << 8 | raw[4]);
  calib.digP1 = (uint16_t)((uint16_t)raw[7] << 8 | raw[6]);
  calib.digP2 = (int16_t)((uint16_t)raw[9] << 8 | raw[8]);
  calib.digP3 = (int16_t)((uint16_t)raw[11] << 8 | raw[10]);
  calib.digP4 = (int16_t)((uint16_t)raw[13] << 8 | raw[12]);
  calib.digP5 = (int16_t)((uint16_t)raw[15] << 8 | raw[14]);
  calib.digP6 = (int16_t)((uint16_t)raw[17] << 8 | raw[16]);
  calib.digP7 = (int16_t)((uint16_t)raw[19] << 8 | raw[18]);
  calib.digP8 = (int16_t)((uint16_t)raw[21] << 8 | raw[20]);
  calib.digP9 = (int16_t)((uint16_t)raw[23] << 8 | raw[22]);
  return calib.digP1 != 0;
}

int32_t compensateTemperature(int32_t adcT) {
  const int32_t var1 = ((((adcT >> 3) - ((int32_t)s_runtime.calib.digT1 << 1))) * ((int32_t)s_runtime.calib.digT2)) >> 11;
  const int32_t var2 = (((((adcT >> 4) - ((int32_t)s_runtime.calib.digT1)) * ((adcT >> 4) - ((int32_t)s_runtime.calib.digT1))) >> 12) * ((int32_t)s_runtime.calib.digT3)) >> 14;
  s_runtime.tFine = var1 + var2;
  return (s_runtime.tFine * 5 + 128) >> 8;
}

bool compensatePressure(int32_t adcP, float &pressureHpa) {
  int64_t var1 = (int64_t)s_runtime.tFine - 128000;
  int64_t var2 = var1 * var1 * (int64_t)s_runtime.calib.digP6;
  var2 += (var1 * (int64_t)s_runtime.calib.digP5) << 17;
  var2 += ((int64_t)s_runtime.calib.digP4) << 35;
  var1 = ((var1 * var1 * (int64_t)s_runtime.calib.digP3) >> 8) + ((var1 * (int64_t)s_runtime.calib.digP2) << 12);
  var1 = ((((int64_t)1 << 47) + var1) * (int64_t)s_runtime.calib.digP1) >> 33;
  if (var1 == 0) {
    return false;
  }

  int64_t p = 1048576 - adcP;
  p = (((p << 31) - var2) * 3125) / var1;
  var1 = ((int64_t)s_runtime.calib.digP9 * (p >> 13) * (p >> 13)) >> 25;
  var2 = ((int64_t)s_runtime.calib.digP8 * p) >> 19;
  p = ((p + var1 + var2) >> 8) + (((int64_t)s_runtime.calib.digP7) << 4);

  pressureHpa = (float)p / 25600.0f;
  return pressureHpa > 0.0f;
}

float computeAltitude(float pressureHpa) {
  if (!s_runtime.altitudeEnabled || BMP280_SEA_LEVEL_PRESSURE_HPA <= 0.0f || pressureHpa <= 0.0f) {
    return 0.0f;
  }

  return 44330.0f * (1.0f - powf(pressureHpa / BMP280_SEA_LEVEL_PRESSURE_HPA, 0.190294957f));
}

const char *sensorStateText(BMP280Screen::SensorState state) {
  switch (state) {
    case BMP280Screen::SensorState::Init: return "INIT";
    case BMP280Screen::SensorState::Ready: return "READY";
    case BMP280Screen::SensorState::Stale: return "STALE";
    case BMP280Screen::SensorState::Error: return "ERROR";
    case BMP280Screen::SensorState::Missing: return "MISSING";
    default: return "UNKNOWN";
  }
}

void publishState(BMP280Screen::SensorState newState, bool forceDirty) {
  BMP280Screen::RuntimeData &runtime = BMP280Screen::runtimeData;
  const bool stateChanged = runtime.sensorState != newState;
  runtime.sensorState = newState;
  runtime.hasSample = runtime.hasTemperature && runtime.hasPressure;
  runtime.lastSampleMs = millis();
  strncpy(runtime.statusText, sensorStateText(newState), sizeof(runtime.statusText));
  runtime.statusText[sizeof(runtime.statusText) - 1] = '\0';

  if (stateChanged || forceDirty) {
    runtime.sampleRevision++;
    BMP280Screen::markScreenDirty();
  }
}

bool significantChange(float oldValue, float newValue, float threshold) {
  if (!isfinite(oldValue) || !isfinite(newValue)) {
    return true;
  }
  return fabsf(oldValue - newValue) >= threshold;
}

void publishMeasurement(float temperatureC, float pressureHpa, bool altitudeEnabled, bool forceDirty) {
  BMP280Screen::RuntimeData &runtime = BMP280Screen::runtimeData;
  const float altitudeM = altitudeEnabled ? computeAltitude(pressureHpa) : 0.0f;

  bool changed = forceDirty;
  changed = changed || !runtime.hasTemperature || significantChange(runtime.temperatureC, temperatureC, BMP280_TEMP_THRESHOLD_C);
  changed = changed || !runtime.hasPressure || significantChange(runtime.pressureHpa, pressureHpa, BMP280_PRESSURE_THRESHOLD_HPA);
  if (altitudeEnabled) {
    changed = changed || !runtime.hasAltitude || significantChange(runtime.altitudeM, altitudeM, BMP280_ALTITUDE_THRESHOLD_M);
  }

  runtime.temperatureC = temperatureC;
  // Apply user calibration offset (hPa)
  runtime.pressureHpa = pressureHpa + s_pressureOffsetHpa;
  runtime.altitudeM = altitudeM;
  runtime.hasTemperature = true;
  runtime.hasPressure = true;
  runtime.hasAltitude = altitudeEnabled;
  runtime.altitudeAvailable = altitudeEnabled;
  runtime.hasSample = true;
  runtime.lastSampleMs = millis();
  strncpy(runtime.statusText, "READY", sizeof(runtime.statusText));
  runtime.statusText[sizeof(runtime.statusText) - 1] = '\0';

  if (changed || runtime.sensorState != BMP280Screen::SensorState::Ready) {
    runtime.sensorState = BMP280Screen::SensorState::Ready;
    runtime.sampleRevision++;
    BMP280Screen::markScreenDirty();
  }
}

bool startForcedMeasurement() {
  const uint8_t ctrlMeas = (BMP280_OSRS_X2 << 5) | (BMP280_OSRS_X4 << 2) | BMP280_CTRL_FORCED;
  return writeReg(s_runtime.address, BMP280_REG_CTRL_MEAS, ctrlMeas);
}

bool readMeasurement(float &temperatureC, float &pressureHpa) {
  uint8_t raw[6] = {};
  if (!readBytes(s_runtime.address, BMP280_REG_PRESS_MSB, raw, sizeof(raw))) {
    return false;
  }

  const int32_t adcP = (int32_t)(((uint32_t)raw[0] << 12) | ((uint32_t)raw[1] << 4) | ((uint32_t)raw[2] >> 4));
  const int32_t adcT = (int32_t)(((uint32_t)raw[3] << 12) | ((uint32_t)raw[4] << 4) | ((uint32_t)raw[5] >> 4));

  const int32_t tempHundredths = compensateTemperature(adcT);
  temperatureC = (float)tempHundredths / 100.0f;
  return compensatePressure(adcP, pressureHpa);
}

bool chipReady(uint8_t address) {
  uint8_t chipId = 0;
  if (!readU8(address, BMP280_REG_ID, chipId)) {
    return false;
  }
  return chipId == BMP280_CHIP_ID;
}

bool initializeAtAddress(uint8_t address) {
  if (!I2cShared::probeAddress(address, BMP280_I2C_TIMEOUT_MS, BMP280_I2C_RETRIES)) {
    return false;
  }

  if (!chipReady(address)) {
    return false;
  }

  if (!writeReg(address, BMP280_REG_RESET, BMP280_RESET_CMD)) {
    return false;
  }

  const unsigned long resetStartMs = millis();
  while ((millis() - resetStartMs) < 10UL) {
    uint8_t status = 0;
    if (!readU8(address, BMP280_REG_STATUS, status)) {
      break;
    }
    if ((status & 0x01U) == 0) {
      break;
    }
    yield();
  }

  if (!readCalibration(address, s_runtime.calib)) {
    return false;
  }

  const uint8_t config = (0x00U << 5) | (BMP280_FILTER_X4 << 2) | 0x00U;
  if (!writeReg(address, BMP280_REG_CONFIG, config)) {
    return false;
  }

  const uint8_t ctrlMeas = (BMP280_OSRS_X2 << 5) | (BMP280_OSRS_X4 << 2) | BMP280_CTRL_SLEEP;
  if (!writeReg(address, BMP280_REG_CTRL_MEAS, ctrlMeas)) {
    return false;
  }

  s_runtime.address = address;
  s_runtime.hasCalibration = true;
  s_runtime.driverState = DriverState::Idle;
  s_runtime.measurementPending = false;
  return true;
}

void tryRecover() {
  const unsigned long now = millis();
  if ((now - s_runtime.lastRecoveryAttemptMs) < BMP280_RETRY_COOLDOWN_MS) {
    return;
  }
  s_runtime.lastRecoveryAttemptMs = now;

  if (initializeAtAddress(BMP280_ADDR_LOW) || initializeAtAddress(BMP280_ADDR_HIGH)) {
    BMP280Screen::RuntimeData &runtime = BMP280Screen::runtimeData;
    runtime.sensorState = BMP280Screen::SensorState::Init;
    strncpy(runtime.statusText, "INIT", sizeof(runtime.statusText));
    runtime.statusText[sizeof(runtime.statusText) - 1] = '\0';
    runtime.sampleRevision++;
    BMP280Screen::markScreenDirty();
    return;
  }

  BMP280Screen::RuntimeData &runtime = BMP280Screen::runtimeData;
  const BMP280Screen::SensorState nextState = (s_runtime.driverState == DriverState::Missing)
      ? BMP280Screen::SensorState::Missing
      : BMP280Screen::SensorState::Error;
  if (runtime.sensorState != nextState) {
    runtime.sensorState = nextState;
    strncpy(runtime.statusText, sensorStateText(nextState), sizeof(runtime.statusText));
    runtime.statusText[sizeof(runtime.statusText) - 1] = '\0';
    runtime.sampleRevision++;
    BMP280Screen::markScreenDirty();
  }
}

}  // namespace

namespace BMP280Sensor {

void begin() {
  BMP280Screen::resetRuntimeData();
  s_runtime = SensorRuntime{};
  s_runtime.altitudeEnabled = (BMP280_SEA_LEVEL_PRESSURE_HPA > 0.0f);
  BMP280Screen::runtimeData.altitudeAvailable = s_runtime.altitudeEnabled;

  // Load persisted pressure offset for simple calibration
  {
    Preferences prefs;
    if (prefs.begin("bmp280", true)) {
      s_pressureOffsetHpa = prefs.getFloat("offset", 0.0f);
      prefs.end();
    }
  }

  if (initializeAtAddress(BMP280_ADDR_LOW) || initializeAtAddress(BMP280_ADDR_HIGH)) {
    s_runtime.started = true;
    s_runtime.driverState = DriverState::Idle;
    publishState(BMP280Screen::SensorState::Init, true);
    return;
  }

  s_runtime.started = true;
  s_runtime.driverState = DriverState::Missing;
  BMP280Screen::RuntimeData &runtime = BMP280Screen::runtimeData;
  runtime.sensorState = BMP280Screen::SensorState::Missing;
  strncpy(runtime.statusText, "MISSING", sizeof(runtime.statusText));
  runtime.statusText[sizeof(runtime.statusText) - 1] = '\0';
  runtime.sampleRevision++;
  BMP280Screen::markScreenDirty();
}

void setPressureOffset(float hpa) {
  s_pressureOffsetHpa = hpa;
  Preferences prefs;
  if (prefs.begin("bmp280", false)) {
    prefs.putFloat("offset", s_pressureOffsetHpa);
    prefs.end();
  }
}

float getPressureOffset() {
  return s_pressureOffsetHpa;
}

void update() {
  if (!s_runtime.started) {
    return;
  }

  BMP280Screen::RuntimeData &runtime = BMP280Screen::runtimeData;
  const unsigned long now = millis();

  if (runtime.sensorState == BMP280Screen::SensorState::Missing || runtime.sensorState == BMP280Screen::SensorState::Error) {
    tryRecover();
    return;
  }

  if (runtime.hasSample && (now - runtime.lastSampleMs) > BMP280_STALE_AFTER_MS && runtime.sensorState != BMP280Screen::SensorState::Stale) {
    runtime.sensorState = BMP280Screen::SensorState::Stale;
    strncpy(runtime.statusText, "STALE", sizeof(runtime.statusText));
    runtime.statusText[sizeof(runtime.statusText) - 1] = '\0';
    runtime.sampleRevision++;
    BMP280Screen::markScreenDirty();
  }

  if (s_runtime.driverState == DriverState::Idle) {
    if ((now - s_runtime.lastRequestMs) < BMP280_REQUEST_INTERVAL_MS) {
      return;
    }

    if (!startForcedMeasurement()) {
      s_runtime.driverState = DriverState::Error;
      runtime.sensorState = BMP280Screen::SensorState::Error;
      strncpy(runtime.statusText, "ERROR", sizeof(runtime.statusText));
      runtime.statusText[sizeof(runtime.statusText) - 1] = '\0';
      runtime.sampleRevision++;
      BMP280Screen::markScreenDirty();
      return;
    }

    s_runtime.measurementPending = true;
    s_runtime.lastRequestMs = now;
    s_runtime.driverState = DriverState::Waiting;
    if (runtime.sensorState != BMP280Screen::SensorState::Init && runtime.sensorState != BMP280Screen::SensorState::Ready) {
      runtime.sensorState = BMP280Screen::SensorState::Init;
      strncpy(runtime.statusText, "INIT", sizeof(runtime.statusText));
      runtime.statusText[sizeof(runtime.statusText) - 1] = '\0';
      runtime.sampleRevision++;
      BMP280Screen::markScreenDirty();
    }
    return;
  }

  if (s_runtime.driverState != DriverState::Waiting) {
    return;
  }

  uint8_t status = 0;
  if (!readU8(s_runtime.address, BMP280_REG_STATUS, status)) {
    if ((now - s_runtime.lastRequestMs) >= BMP280_CONVERSION_TIMEOUT_MS) {
      s_runtime.driverState = DriverState::Error;
      runtime.sensorState = BMP280Screen::SensorState::Error;
      strncpy(runtime.statusText, "ERROR", sizeof(runtime.statusText));
      runtime.statusText[sizeof(runtime.statusText) - 1] = '\0';
      runtime.sampleRevision++;
      BMP280Screen::markScreenDirty();
    }
    return;
  }

  if ((status & 0x01U) != 0U) {
    if ((now - s_runtime.lastRequestMs) >= BMP280_CONVERSION_TIMEOUT_MS) {
      s_runtime.driverState = DriverState::Error;
      runtime.sensorState = BMP280Screen::SensorState::Error;
      strncpy(runtime.statusText, "ERROR", sizeof(runtime.statusText));
      runtime.statusText[sizeof(runtime.statusText) - 1] = '\0';
      runtime.sampleRevision++;
      BMP280Screen::markScreenDirty();
    }
    return;
  }

  float temperatureC = 0.0f;
  float pressureHpa = 0.0f;
  if (!readMeasurement(temperatureC, pressureHpa)) {
    s_runtime.driverState = DriverState::Error;
    runtime.sensorState = BMP280Screen::SensorState::Error;
    strncpy(runtime.statusText, "ERROR", sizeof(runtime.statusText));
    runtime.statusText[sizeof(runtime.statusText) - 1] = '\0';
    runtime.sampleRevision++;
    BMP280Screen::markScreenDirty();
    return;
  }

  s_runtime.driverState = DriverState::Idle;
  s_runtime.measurementPending = false;
  publishMeasurement(temperatureC, pressureHpa, s_runtime.altitudeEnabled, false);
}

uint8_t menuItemCount() {
  return s_runtime.altitudeEnabled ? 4U : 3U;
}

bool altitudeViewEnabled() {
  return s_runtime.altitudeEnabled;
}

}  // namespace BMP280Sensor
