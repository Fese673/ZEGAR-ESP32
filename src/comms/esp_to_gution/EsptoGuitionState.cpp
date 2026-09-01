#include "EsptoGuitionState.h"

#include <Arduino.h>
#include <math.h>
#include <Preferences.h>
#include <string.h>
#include <WiFi.h>

#include "AlarmRuntime.h"
#include "AppLog.h"
#include "AppSettings.h"
#include "core/telemetry/Dbg7Seg.h"
#include "AppState.h"
#include "AudioBT.h"
#include "BMP280Screen.h"
#include "Config.h"
#include "core/telemetry/RamTelemetry.h"
#include "core/telemetry/RuntimeTelemetry.h"
#include "ENS160AHT21Screen.h"
#include "EsptoGuitionMusicLog.h"
#include "Esptogution.h"
#include "meteoSync.h"
#include "ModeManager.h"
#include "PMS_Czujnik.h"
#include "RadioModeSwitch.h"
#include "RTCService.h"
#include "STM32_Data.h"
#include "TimerService.h"
#include "touch_buzzer_test.h"
#include "UI_Draw.h"
#include "UIState.h"
#include "WiFiSync.h"
extern uint8_t heapUsageCore0Percent;
extern uint8_t heapUsageCore1Percent;
extern uint32_t ramFreeBytes;
extern uint32_t ramTotalBytes;
extern uint32_t ramDmaFreeBytes;
extern uint32_t flashFreeBytes;

namespace EsptoGuition {
namespace {

using namespace Config;

struct SettingsSnapshot {
  bool buzzerEnabled;
  bool mqttEnabled;
  bool touchTestEnabled;
  bool backgroundMusicEnabled;
  bool pmsEnabled;
  int alarmMelodyIndex;
  /* Etap 2: 0..100. Default 100 = pelna jasnosc do czasu pierwszej komendy. */
  uint8_t sevenSegBrightness = 100;
};

volatile bool s_settingsDirty = false;
SettingsSnapshot s_pendingSettings;

bool isSummerTime(time_t epoch) {
  struct tm tm_info;
  if (gmtime_r(&epoch, &tm_info) == nullptr) return false;
  
  int year = tm_info.tm_year + 1900;
  int month = tm_info.tm_mon + 1;
  int day = tm_info.tm_mday;
  int hour = tm_info.tm_hour;

  if (month < 3 || month > 10) return false;
  if (month > 3 && month < 10) return true;

  // Last Sunday of March (starts at 1:00 UTC)
  int lastSundayMarch = 31 - ((5 * year / 4 + 4) % 7);
  if (month == 3) {
    if (day > lastSundayMarch) return true;
    if (day < lastSundayMarch) return false;
    return hour >= 1;
  }

  // Last Sunday of October (ends at 1:00 UTC)
  int lastSundayOctober = 31 - ((5 * year / 4 + 1) % 7);
  if (month == 10) {
    if (day < lastSundayOctober) return true;
    if (day > lastSundayOctober) return false;
    return hour < 1;
  }
  return false;
}

int getLocalTimeOffset(time_t epoch) {
  return isSummerTime(epoch) ? 7200 : 3600;
}

struct SyntheticWeatherState {
  int16_t temperatureCx100 = 2200;
  uint16_t humidityPctX100 = 5000;
  uint16_t pressureHpaX10 = 10130;
  uint16_t eco2 = 600;
  uint16_t tvoc = 100;
  unsigned long lastUpdateMs = 0;
};

struct SyntheticOutdoorState {
  int16_t temperatureCx100 = 1500;
  uint16_t humidityPctX100 = 7000;
  uint16_t pressureHpaX10 = 10130;
  uint16_t windSpeedMsX100 = 500;
  uint16_t windGustMsX100 = 800;
  uint8_t windDeg = 180;
  uint8_t weatherCode = 0;
  uint8_t cloudCover = 40;
  int16_t apparentTempCx100 = 1400;
  uint16_t pm25UgM3 = 150;
  uint16_t pm10UgM3 = 300;
  uint16_t co2Ppm = 420;
  uint8_t aqi = 1;
  unsigned long lastUpdateMs = 0;
};

struct SyntheticPmsState {
  uint16_t pm01 = 10;
  uint16_t pm25 = 25;
  uint16_t pm10 = 40;
  unsigned long lastUpdateMs = 0;
};

SyntheticWeatherState s_weatherState;
SyntheticOutdoorState s_outdoorState;
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
    s_weatherState.eco2 = changeByPercent(s_weatherState.eco2, Config::kSyntheticChangePercent, 400, 2000);
    s_weatherState.tvoc = changeByPercent(s_weatherState.tvoc, Config::kSyntheticChangePercent, 0, 1000);
    s_weatherState.lastUpdateMs = nowMs;
  }

  out.temperatureCx100 = s_weatherState.temperatureCx100;
  out.humidityPctX100 = s_weatherState.humidityPctX100;
  out.pressureHpaX10 = s_weatherState.pressureHpaX10;
  out.eco2 = s_weatherState.eco2;
  out.tvoc = s_weatherState.tvoc;
  out.sampleAgeMs = random(0, 5000);
  out.flags = 0xC7U;  // bits: temp|hum|press|eco2|tvoc|ens160
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

  if (BMP280Screen::runtimeData.hasTemperature &&
      isfinite(BMP280Screen::runtimeData.temperatureC)) {
    valid = true;
    return BMP280Screen::runtimeData.temperatureC;
  }

  if (ENS160AHT21Screen::runtimeData.hasClimateSample &&
      isfinite(ENS160AHT21Screen::runtimeData.temperatureC)) {
    valid = true;
    fromEns160 = true;
    return ENS160AHT21Screen::runtimeData.temperatureC;
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
  bool eco2Valid = false;

  const float temperatureC = pickWeatherTemperature(tempValid, tempFromEns);
  const float humidityPct = pickWeatherHumidity(humValid);
  const float pressureHpa = pickWeatherPressure(pressureValid);
  const uint16_t eco2 = ENS160AHT21Screen::runtimeData.hasGasSample
    ? ENS160AHT21Screen::runtimeData.eco2 : 0;
  if (ENS160AHT21Screen::runtimeData.hasGasSample && ENS160AHT21Screen::runtimeData.eco2 > 0) {
    eco2Valid = true;
  }

  out.temperatureCx100 = tempValid ? static_cast<int16_t>(lroundf(temperatureC * 100.0f)) : 0;
  out.humidityPctX100 = humValid ? static_cast<uint16_t>(lroundf(humidityPct * 100.0f)) : 0;
  out.pressureHpaX10 = pressureValid ? static_cast<uint16_t>(lroundf(pressureHpa * 10.0f)) : 0;
  out.eco2 = eco2Valid ? eco2 : 0;
  out.tvoc = ENS160AHT21Screen::runtimeData.hasGasSample
    ? ENS160AHT21Screen::runtimeData.tvoc : 0;
  out.sampleAgeMs = latestWeatherAgeMs(nowMs);
  out.flags = 0;
  if (tempValid) out.flags |= 0x01U;
  if (humValid) out.flags |= 0x02U;
  if (pressureValid) out.flags |= 0x04U;
  if (tempFromEns) out.flags |= 0x08U;
  if (ENS160AHT21Screen::runtimeData.hasClimateSample) out.flags |= 0x10U;
  if (BMP280Screen::runtimeData.hasPressure) out.flags |= 0x20U;
  if (eco2Valid) out.flags |= 0x40U;
  if (ENS160AHT21Screen::runtimeData.hasGasSample) out.flags |= 0x80U;

  return true;
}

bool buildOutdoorWeatherPayload(OutdoorWeatherPayload &out, unsigned long nowMs) {
  uint8_t flags = 0;

  meteoSync::WeatherData wd;
  if (meteoSync::getLatest(wd) && wd.valid) {
    out.temperatureCx100 = static_cast<int16_t>(lroundf(wd.temperature * 100.0f));
    flags |= 0x01U;
    out.humidityPctX100 = static_cast<uint16_t>(lroundf(static_cast<float>(wd.humidity) * 100.0f));
    flags |= 0x02U;
    out.pressureHpaX10 = static_cast<uint16_t>(lroundf(wd.pressure * 10.0f));
    flags |= 0x04U;
    out.windSpeedMsX100 = static_cast<uint16_t>(lroundf(wd.windSpeed * 100.0f));
    out.windGustMsX100 = static_cast<uint16_t>(lroundf(wd.windGust * 100.0f));
    out.windDeg = static_cast<uint8_t>(wd.windDeg / 2U);
    flags |= 0x08U;
    out.weatherCode = wd.weatherCode;
    out.cloudCover = wd.cloudCover;
    out.apparentTempCx100 = static_cast<int16_t>(lroundf(wd.apparentTemp * 100.0f));
    flags |= 0x10U;
    out.precipitationMmX10 = static_cast<uint8_t>(lroundf(wd.precipitation * 10.0f));
    out.uvIndexX10 = static_cast<uint8_t>(lroundf(wd.uvIndex * 10.0f));
    flags |= 0x40U;
    bool hasSunrise = false;
    if (wd.sunrise != 0) {
      const uint32_t sunriseLocal = wd.sunrise + getLocalTimeOffset(static_cast<time_t>(wd.sunrise));
      const uint8_t h = static_cast<uint8_t>((sunriseLocal / 3600UL) % 24UL);
      const uint8_t m = static_cast<uint8_t>((sunriseLocal / 60UL) % 60UL);
      if (h < 24 && m < 60) { out.sunriseHour = h; out.sunriseMin = m; hasSunrise = true; }
    }
    if (wd.sunset != 0) {
      const uint32_t sunsetLocal = wd.sunset + getLocalTimeOffset(static_cast<time_t>(wd.sunset));
      const uint8_t h = static_cast<uint8_t>((sunsetLocal / 3600UL) % 24UL);
      const uint8_t m = static_cast<uint8_t>((sunsetLocal / 60UL) % 60UL);
      if (h < 24 && m < 60) { out.sunsetHour = h; out.sunsetMin = m; }
    }
    if (wd.sunrise != 0 || wd.sunset != 0) flags |= 0x80U;
    out.sampleAgeMs = (wd.timestamp != 0)
      ? static_cast<uint32_t>(nowMs - min(static_cast<unsigned long>(wd.timestamp * 1000UL), nowMs))
      : 0;
  }

  meteoSync::AirQualityData aq;
  if (meteoSync::getLatestAirQuality(aq) && aq.valid) {
    out.pm25UgM3 = static_cast<uint16_t>(lroundf(aq.pm25 * 100.0f));
    out.pm10UgM3 = static_cast<uint16_t>(lroundf(aq.pm10 * 100.0f));
    out.co2Ppm = static_cast<uint16_t>(lroundf(aq.co2));
    out.aqi = (aq.europeanAqi < 255) ? static_cast<uint8_t>(aq.europeanAqi) : 255;
    out.no2UgM3 = static_cast<uint16_t>(lroundf(aq.no2UgM3));
    flags |= 0x20U;
  }

  out.flags = flags;
  return flags != 0;
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
  // Use system time (RAM) instead of hitting the I2C bus (RTCService::getEpoch).
  // The system time is already synced with RTC by ClockService.
  const time_t epoch = time(nullptr);
  if (epoch > 1000000UL) { // Basic sanity check
    out.unixSeconds = static_cast<uint32_t>(epoch);
    out.valid = 1U;
    return true;
  }

  out.unixSeconds = 0;
  out.valid = 0;
  return true;
}

bool buildWifiPayload(WifiPayload &out) {
  out.connected = (WiFi.status() == WL_CONNECTED);
  out.rssi = out.connected ? WiFiSync::getRssi() : 0;
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
  // Cache once — ESP.getSketchSize() reads SPI flash partition header,
  // disabling instruction cache on BOTH cores. Sketch size is constant.
  static uint32_t s_cachedSketchSize = 0;
  if (s_cachedSketchSize == 0) {
    s_cachedSketchSize = ESP.getSketchSize();
  }
  out.usedFlash = s_cachedSketchSize;

  const RuntimeTelemetry::Snapshot rt = RuntimeTelemetry::snapshot();
  out.underrunsAudio = static_cast<uint16_t>(rt.audio_underruns);
  out.overflowAudio = static_cast<uint16_t>(rt.audio_overflows);
  out.dropsAudio = static_cast<uint16_t>(rt.audio_drops);
  out.errorsI2c = static_cast<uint16_t>(rt.i2c_errors);
  out.timeoutsI2c = static_cast<uint16_t>(rt.i2c_timeouts);

  return true;
}

void handleReceivedSettings(const uint8_t* payload, uint16_t payloadLength) {
  DBG_7SEG_LOG("RX kTypeSetSettings len=%u", (unsigned)payloadLength);
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

    s_pendingSettings.buzzerEnabled = newBuzzer;
    s_pendingSettings.mqttEnabled = newMqtt;
    s_pendingSettings.touchTestEnabled = newTouch;
    s_pendingSettings.backgroundMusicEnabled = newMusic;
    s_pendingSettings.pmsEnabled = newPms;

    if (payloadLength >= 6U) {
      int newMelodyIndex = payload[5];
      appSettings.alarmMelodyIndex = newMelodyIndex;
      uiState.settingsAlarmMelodyMenu.index = newMelodyIndex;
      s_pendingSettings.alarmMelodyIndex = newMelodyIndex;
    }

    /* Etap 2: payload[6] = sevenSegBrightness 0..100. Opcjonalne — stary
     * GUTION (6-bajtowy) nie wysyla tego bajtu, wowczas brightness
     * pozostaje bez zmian (zachowujemy RAM/NVS). */
    if (payloadLength >= 7U) {
      uint8_t newBrightness = payload[6];
      if (newBrightness > 100U) newBrightness = 100U;
      appSettings.sevenSegBrightness = newBrightness;
      s_pendingSettings.sevenSegBrightness = newBrightness;
      /* Forward do STM32 przez COBS+CRC16 (EspSoftwareSerial @ 9600 baud).
       * STM32 w ramce 0xB0 odbiera jasnosc i ustawia PWM na PA6. */
      DBG_7SEG_LOG("parsed brightness=%u (raw=0x%02X) -> STM32", (unsigned)newBrightness, (unsigned)payload[6]);
      STM32data_sendBrightness(newBrightness);
    } else {
      DBG_7SEG_LOG("payload len=%u < 7, brightness nie zmieniony (zachowany RAM)", (unsigned)payloadLength);
    }

    s_settingsDirty = true;
    requestUiFullRedraw();
  }
}

bool buildStatusBlePayload(StatusBlePayload &out) {
  if (!ModeManager::isBtOn()) {
    out.value = 0;
    return true;
  }
  if (audioBT_isConnected()) {
    out.value = 2; // ON + connected
  } else {
    out.value = 1; // ON but not connected
  }
  return true;
}

namespace {

uint8_t s_musicVolume = 50;
uint8_t s_musicBass = 50;
uint8_t s_musicMid = 50;
uint8_t s_musicTreble = 50;
unsigned long s_musicLastWriteMs = 0;
volatile bool s_musicSettingsDirty = false;

void music_settings_save() {
    s_musicSettingsDirty = true;
}

void music_settings_flush() {
    if (!s_musicSettingsDirty) return;
    unsigned long now = millis();
    if (now - s_musicLastWriteMs < 30000UL) return;
    s_musicLastWriteMs = now;
    s_musicSettingsDirty = false;
    Preferences prefs;
    prefs.begin("zegar", false);
    prefs.putUChar("musicVol", s_musicVolume);
    prefs.putUChar("musicBass", s_musicBass);
    prefs.putUChar("musicMid", s_musicMid);
    prefs.putUChar("musicTreble", s_musicTreble);
    prefs.end();
}

void music_settings_load() {
    Preferences prefs;
    prefs.begin("zegar", true);
    s_musicVolume = prefs.getUChar("musicVol", 50);
    s_musicBass = prefs.getUChar("musicBass", 50);
    s_musicMid = prefs.getUChar("musicMid", 50);
    s_musicTreble = prefs.getUChar("musicTreble", 50);
    prefs.end();
    
    // Apply loaded EQ settings immediately to the Bluetooth EQ variables
    audioBT_setEQ(s_musicBass, s_musicMid, s_musicTreble);
    
    // Apply loaded volume settings
    audioBT_setVolume(s_musicVolume);

    if (s_musicLastWriteMs == 0) {
        s_musicLastWriteMs = millis() - 30000UL;
    }
}

} // namespace

void musicSettingsInit() {
    music_settings_load();
}

void setPendingSevenSegBrightness(uint8_t value) {
    s_pendingSettings.sevenSegBrightness = value;
}

void musicSettingsFlush() {
    music_settings_flush();
    if (!s_settingsDirty) return;
    s_settingsDirty = false;
    Preferences prefs;
    prefs.begin("zegar", false);
    prefs.putBool("buzzerEnabled", s_pendingSettings.buzzerEnabled);
    prefs.putBool("mqttEnabled", s_pendingSettings.mqttEnabled);
    prefs.putBool("touchTest", s_pendingSettings.touchTestEnabled);
    prefs.putBool("menuMusic", s_pendingSettings.backgroundMusicEnabled);
    prefs.putBool("pmsEnabled", s_pendingSettings.pmsEnabled);
    prefs.putUShort("alarmMelody", (uint16_t)s_pendingSettings.alarmMelodyIndex);
    /* Etap 2: jasnosc 7-seg (0..100). */
    prefs.putUChar("segBrightness", s_pendingSettings.sevenSegBrightness);
    prefs.end();
}

void handleReceivedMusicCommand(const uint8_t* payload, uint16_t payloadLength) {
    if (payload == nullptr || payloadLength < 1) return;
    switch (payload[0]) {
  case 0:
    MusicLog::receivedCommand(payload[0]);
    audioBT_play();
    break;
  case 1:
    MusicLog::receivedCommand(payload[0]);
    audioBT_pause();
    break;
  case 2:
    MusicLog::receivedCommand(payload[0]);
    audioBT_next();
    break;
  case 3:
    MusicLog::receivedCommand(payload[0]);
    audioBT_previous();
    break;
    default: break;
    }
}

void handleReceivedMusicVolume(const uint8_t* payload, uint16_t payloadLength) {
    if (payload == nullptr || payloadLength < 1) return;
    uint8_t vol = payload[0];
    if (vol > 100) vol = 100;
    s_musicVolume = vol;
  MusicLog::receivedVolume(vol);
    audioBT_setVolume(vol);
    music_settings_save();
}

void handleReceivedMusicEQ(const uint8_t* payload, uint16_t payloadLength) {
    if (payload == nullptr || payloadLength < 3) return;
    s_musicBass = (payload[0] > 100) ? 100 : payload[0];
    s_musicMid = (payload[1] > 100) ? 100 : payload[1];
    s_musicTreble = (payload[2] > 100) ? 100 : payload[2];
  MusicLog::receivedEQ(s_musicBass, s_musicMid, s_musicTreble);
    audioBT_setEQ(s_musicBass, s_musicMid, s_musicTreble);
    music_settings_save();
}

void handleReceivedMusicRequest(const uint8_t* payload, uint16_t payloadLength) {
    if (payload == nullptr || payloadLength < 1) return;
    uint8_t flags = payload[0];

    if (flags & 0x01U) {
        char title[128], artist[128];
        audioBT_copyMetadata(title, sizeof(title), artist, sizeof(artist));
        sendMusicTitle(title);
        sendMusicArtist(artist);
    }
    if (flags & 0x02U) {
        sendMusicStatus(audioBT_isConnected(), audioBT_isPlaying());
    }
    if (flags & 0x04U) {
        sendMusicVolumeState(s_musicVolume);
    }
    if (flags & 0x08U) {
        sendMusicEQState(s_musicBass, s_musicMid, s_musicTreble);
    }
}

void handleReceivedRadioModeSwitch(const uint8_t* payload, uint16_t payloadLength) {
    if (payload == nullptr || payloadLength < 1) return;
    if (payload[0] != 0xFF) return; // only toggle command
    RadioModeSwitchState current = RadioModeSwitch::getCurrentState();
    if (current == RADIO_STATE_WIFI) {
        RadioModeSwitch::requestModeSwitch_BT();
    } else if (current == RADIO_STATE_BT) {
        RadioModeSwitch::requestModeSwitch_WiFi();
    }
    // RADIO_STATE_TRANSITIONING -> ignore (already switching)
}

bool buildStatusBellPayload(StatusBellPayload &out) {
  if (AlarmRuntime::isAnyAlarmArmed() || TimerService::isRinging()) {
    out.value = AlarmRuntime::state().alarmRinging || TimerService::isRinging() ? 2 : 1;
  } else {
    out.value = 0;
  }
  return true;
}

uint8_t getMusicVolume() {
    return s_musicVolume;
}

} // namespace EsptoGuition
