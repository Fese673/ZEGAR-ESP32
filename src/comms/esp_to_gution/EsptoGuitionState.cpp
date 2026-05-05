#include "EsptoGuitionState.h"
#include "Config.h"
#include <math.h>
#include <string.h>
#include <Arduino.h>
#include <WiFi.h>
#include "AppLog.h"
#include "BMP280Screen.h"
#include "ENS160AHT21Screen.h"
#include "PMS_Czujnik.h"
#include "RTCService.h"
#include "AppSettings.h"
#include "touch_buzzer_test.h"
#include <Preferences.h>
#include "UIState.h"
#include "UI_Draw.h"
#include "core/telemetry/RamTelemetry.h"
#include "core/telemetry/RuntimeTelemetry.h"

extern uint8_t heapUsageCore0Percent;
extern uint8_t heapUsageCore1Percent;
extern uint32_t ramFreeBytes;
extern uint32_t ramTotalBytes;
extern uint32_t ramDmaFreeBytes;
extern uint32_t flashFreeBytes;

namespace EsptoGuition {
namespace {

using namespace Config;

struct SyntheticWeatherState {
  int16_t temperatureCx100 = 2200;
  uint16_t humidityPctX100 = 5000;
  uint16_t pressureHpaX10 = 10130;
  unsigned long lastUpdateMs = 0;
};

struct SyntheticPmsState {
  uint16_t pm01 = 10;
  uint16_t pm25 = 25;
  uint16_t pm10 = 40;
  unsigned long lastUpdateMs = 0;
};

SyntheticWeatherState s_weatherState;
SyntheticPmsState s_pmsState;

void seedSyntheticRandom() {
  static bool s_seeded = false;
  if (!s_seeded) {
    randomSeed(esp_random());
    s_seeded = true;
  }
}

template<typename T>
T changeByPercent(T value, float percent, int minVal, int maxVal) {
  float delta = static_cast<float>(value) * (percent / 100.0f);
  int change = random(-static_cast<int>(delta), static_cast<int>(delta) + 1);
  int newValue = static_cast<int>(value) + change;
  if (newValue < minVal) newValue = minVal;
  if (newValue > maxVal) newValue = maxVal;
  return static_cast<T>(newValue);
}

void buildSyntheticWeatherPayload(WeatherPayload &out) {
  unsigned long nowMs = millis();

  if (nowMs - s_weatherState.lastUpdateMs >= Config::kSyntheticUpdateIntervalMs) {
    seedSyntheticRandom();
    s_weatherState.temperatureCx100 = changeByPercent(s_weatherState.temperatureCx100, Config::kSyntheticChangePercent, -400, 3800);
    s_weatherState.humidityPctX100 = changeByPercent(s_weatherState.humidityPctX100, Config::kSyntheticChangePercent, 0, 10000);
    s_weatherState.pressureHpaX10 = changeByPercent(s_weatherState.pressureHpaX10, Config::kSyntheticChangePercent, 8700, 10840);
    s_weatherState.lastUpdateMs = nowMs;
  }

  out.temperatureCx100 = s_weatherState.temperatureCx100;
  out.humidityPctX100 = s_weatherState.humidityPctX100;
  out.pressureHpaX10 = s_weatherState.pressureHpaX10;
  out.sampleAgeMs = random(0, 5000);
  out.flags = 0x07U;
}

void buildSyntheticPmsPayload(PmsPayload &out) {
  unsigned long nowMs = millis();

  if (nowMs - s_pmsState.lastUpdateMs >= Config::kSyntheticUpdateIntervalMs) {
    seedSyntheticRandom();
    s_pmsState.pm01 = changeByPercent(s_pmsState.pm01, Config::kSyntheticChangePercent, 0, 200);
    s_pmsState.pm25 = changeByPercent(s_pmsState.pm25, Config::kSyntheticChangePercent, 0, 300);
    s_pmsState.pm10 = changeByPercent(s_pmsState.pm10, Config::kSyntheticChangePercent, 0, 400);
    s_pmsState.lastUpdateMs = nowMs;
  }

  out.pm01 = s_pmsState.pm01;
  out.pm25 = s_pmsState.pm25;
  out.pm10 = s_pmsState.pm10;
  out.count0p3 = static_cast<uint16_t>(random(100, 3000));
  out.count0p5 = static_cast<uint16_t>(random(50, 2000));
  out.count1p0 = static_cast<uint16_t>(random(20, 1000));
  out.count2p5 = static_cast<uint16_t>(random(10, 500));
  out.count5p0 = static_cast<uint16_t>(random(5, 200));
  out.count10p0 = static_cast<uint16_t>(random(0, 100));
  out.sampleAgeMs = random(0, 5000);
  out.flags = 0x01U;
}

float pickWeatherTemperature(bool &valid, bool &fromEns160) {
  valid = false;
  fromEns160 = false;

  if (ENS160AHT21Screen::runtimeData.hasClimateSample &&
      isfinite(ENS160AHT21Screen::runtimeData.temperatureC)) {
    valid = true;
    fromEns160 = true;
    return ENS160AHT21Screen::runtimeData.temperatureC;
  }

  if (BMP280Screen::runtimeData.hasTemperature &&
      isfinite(BMP280Screen::runtimeData.temperatureC)) {
    valid = true;
    return BMP280Screen::runtimeData.temperatureC;
  }

  return 0.0f;
}

float pickWeatherHumidity(bool &valid) {
  valid = false;
  if (ENS160AHT21Screen::runtimeData.hasClimateSample &&
      isfinite(ENS160AHT21Screen::runtimeData.humidityPct)) {
    valid = true;
    return ENS160AHT21Screen::runtimeData.humidityPct;
  }

  return 0.0f;
}

float pickWeatherPressure(bool &valid) {
  valid = false;
  if (BMP280Screen::runtimeData.hasPressure &&
      isfinite(BMP280Screen::runtimeData.pressureHpa)) {
    valid = true;
    return BMP280Screen::runtimeData.pressureHpa;
  }

  return 0.0f;
}

uint32_t latestWeatherAgeMs(unsigned long nowMs) {
  unsigned long lastSampleMs = 0;
  if (BMP280Screen::runtimeData.hasSample) {
    lastSampleMs = BMP280Screen::runtimeData.lastSampleMs;
  }
  if (ENS160AHT21Screen::runtimeData.hasSample &&
      ENS160AHT21Screen::runtimeData.lastUpdateMs > lastSampleMs) {
    lastSampleMs = ENS160AHT21Screen::runtimeData.lastUpdateMs;
  }

  if (lastSampleMs == 0UL || nowMs < lastSampleMs) {
    return 0;
  }

  return static_cast<uint32_t>(nowMs - lastSampleMs);
}

} // namespace

bool buildWeatherPayload(WeatherPayload &out, unsigned long nowMs) {
  if (kUseSyntheticPayloads) {
    buildSyntheticWeatherPayload(out);
    return true;
  }

  bool tempValid = false;
  bool tempFromEns = false;
  bool humValid = false;
  bool pressureValid = false;

  const float temperatureC = pickWeatherTemperature(tempValid, tempFromEns);
  const float humidityPct = pickWeatherHumidity(humValid);
  const float pressureHpa = pickWeatherPressure(pressureValid);

  out.temperatureCx100 = tempValid ? static_cast<int16_t>(lroundf(temperatureC * 100.0f)) : 0;
  out.humidityPctX100 = humValid ? static_cast<uint16_t>(lroundf(humidityPct * 100.0f)) : 0;
  out.pressureHpaX10 = pressureValid ? static_cast<uint16_t>(lroundf(pressureHpa * 10.0f)) : 0;
  out.sampleAgeMs = latestWeatherAgeMs(nowMs);
  out.flags = 0;
  if (tempValid) out.flags |= 0x01U;
  if (humValid) out.flags |= 0x02U;
  if (pressureValid) out.flags |= 0x04U;
  if (tempFromEns) out.flags |= 0x08U;
  if (ENS160AHT21Screen::runtimeData.hasClimateSample) out.flags |= 0x10U;
  if (BMP280Screen::runtimeData.hasPressure) out.flags |= 0x20U;

  return true;
}

bool buildPmsPayload(PmsPayload &out, unsigned long nowMs) {
  if (kUseSyntheticPayloads) {
    buildSyntheticPmsPayload(out);
    return true;
  }

  if (PMS5003Sensor::isOk()) {
    const PMS5003Sensor::MassReadings atmospheric = PMS5003Sensor::getAtmospheric();
    const PMS5003Sensor::ParticleCounts particles = PMS5003Sensor::getParticleCounts();
    const PMS5003Sensor::Stats stats = PMS5003Sensor::getStats();

    out.pm01 = atmospheric.pm01;
    out.pm25 = atmospheric.pm25;
    out.pm10 = atmospheric.pm10;
    out.count0p3 = particles.count0p3;
    out.count0p5 = particles.count0p5;
    out.count1p0 = particles.count1p0;
    out.count2p5 = particles.count2p5;
    out.count5p0 = particles.count5p0;
    out.count10p0 = particles.count10p0;
    out.sampleAgeMs = (stats.lastFrameTime != 0UL && nowMs >= stats.lastFrameTime)
                        ? static_cast<uint32_t>(nowMs - stats.lastFrameTime)
                        : 0U;
    out.flags = 0x01U;
  }

  return true;
}

bool buildTimePayload(TimePayload &out) {
  time_t epoch = 0;
  if (RTCService::getEpoch(&epoch) == RTCService::Status::Ok && epoch > 0) {
    out.unixSeconds = static_cast<uint32_t>(epoch);
    out.valid = 1U;
    return true;
  }

  const time_t systemEpoch = time(nullptr);
  if (systemEpoch > 0) {
    out.unixSeconds = static_cast<uint32_t>(systemEpoch);
    out.valid = 1U;
    return true;
  }

  out.unixSeconds = 0;
  out.valid = 0;
  return true;
}

bool buildWifiPayload(WifiPayload &out) {
  out.connected = (WiFi.status() == WL_CONNECTED);
  out.rssi = out.connected ? WiFi.RSSI() : 0;
  if (out.connected) {
    IPAddress ip = WiFi.localIP();
    out.ip[0] = ip[0];
    out.ip[1] = ip[1];
    out.ip[2] = ip[2];
    out.ip[3] = ip[3];
  } else {
    out.ip[0] = 0;
    out.ip[1] = 0;
    out.ip[2] = 0;
    out.ip[3] = 0;
  }
  return true;
}

bool buildSystemResourcesPayload(SystemResourcesPayload &out) {
  out.freeRam = ramFreeBytes;
  out.heapRam = ramTotalBytes;
  out.dmaRam = ramDmaFreeBytes;
  out.core0Cpu = heapUsageCore0Percent;
  out.core1Cpu = heapUsageCore1Percent;
  out.freeFlash = flashFreeBytes;
  out.usedFlash = ESP.getSketchSize();

  const RuntimeTelemetry::Snapshot rt = RuntimeTelemetry::snapshot();
  out.underrunsAudio = static_cast<uint16_t>(rt.audio_underruns);
  out.overflowAudio = static_cast<uint16_t>(rt.audio_overflows);
  out.dropsAudio = static_cast<uint16_t>(rt.audio_drops);
  out.errorsI2c = static_cast<uint16_t>(rt.i2c_errors);
  out.timeoutsI2c = static_cast<uint16_t>(rt.i2c_timeouts);

  return true;
}

void handleReceivedSettings(const uint8_t* payload, uint16_t payloadLength) {
  if (payloadLength >= 5U) {
    AppSettings::State& appSettings = AppSettings::mutableState();
    bool newBuzzer = (payload[0] != 0);
    bool newMqtt = (payload[1] != 0);
    bool newTouch = (payload[2] != 0);
    bool newMusic = (payload[3] != 0);
    bool newPms = (payload[4] != 0);

    appSettings.buzzerEnabled = newBuzzer;
    appSettings.mqttEnabled = newMqtt;
    appSettings.touchTestEnabled = newTouch;
    appSettings.backgroundMusicEnabled = newMusic;

    TouchBuzzerTest::setEnabled(newTouch);
    PMS5003Sensor::setEnabled(newPms);

    UIState::State& uiState = UIState::mutableState();
    uiState.settingsBuzzerMenu.index = newBuzzer ? 0 : 1;
    uiState.settingsMqttMenu.index = newMqtt ? 0 : 1;
    uiState.settingsTouchMenu.index = newTouch ? 0 : 1;
    uiState.settingsBackgroundMusicMenu.index = newMusic ? 0 : 1;
    uiState.settingsPmsMenu.index = newPms ? 0 : 1;

    Preferences localPrefs;
    localPrefs.begin("zegar", false);
    localPrefs.putBool("buzzerEnabled", newBuzzer);
    localPrefs.putBool("mqttEnabled", newMqtt);
    localPrefs.putBool("touchTest", newTouch);
    localPrefs.putBool("menuMusic", newMusic);
    localPrefs.putBool("pmsEnabled", newPms);

    if (payloadLength >= 6U) {
      int newMelodyIndex = payload[5];
      appSettings.alarmMelodyIndex = newMelodyIndex;
      uiState.settingsAlarmMelodyMenu.index = newMelodyIndex;
      localPrefs.putUShort("alarmMelody", (uint16_t)newMelodyIndex);
    }

    localPrefs.end();
    requestUiFullRedraw();
  }
}

} // namespace EsptoGuition
