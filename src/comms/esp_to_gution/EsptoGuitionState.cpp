#include "EsptoGuitionState.h"

#include <Arduino.h>
#include <math.h>
#include <Preferences.h>
#include <string.h>
#include <WiFi.h>

#include "AlarmRuntime.h"
#include "AlarmMelodies.h"
#include "AlarmMelodyPrefs.h"
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
#include "stepper_bridge.h"
#include "stepper_frames.h"
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
  out.gasStage = 3U;  // syntetyk = dane "w pelni skalibrowane"
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
  // Stopien kalibracji gazow dla UI Guitiona (bajt na koncu ramki 0x01).
  out.gasStage = ENS160AHT21Screen::runtimeData.hasGasSample
    ? ENS160AHT21Screen::runtimeData.gasStage : 0xFFU;

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
      /* Obrona: indeks spoza listy (np. rozjazd wersji UI) = melodia 0. */
      if (newMelodyIndex < 0 || newMelodyIndex >= (int)AlarmMelodies::kCount) {
        newMelodyIndex = 0;
      }
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
    /* Seed pending z appSettings: pierwszy flush (po zmianie z Guitiona) nie
     * moze nadpisac NVS zerami z niezainicjalizowanego s_pendingSettings.
     * Kolejnosc: wolane PO zaladowaniu appSettings w initPersistenceAndConfig. */
    const AppSettings::State& s = AppSettings::state();
    s_pendingSettings.buzzerEnabled = s.buzzerEnabled;
    s_pendingSettings.mqttEnabled = s.mqttEnabled;
    s_pendingSettings.touchTestEnabled = s.touchTestEnabled;
    s_pendingSettings.backgroundMusicEnabled = s.backgroundMusicEnabled;
    s_pendingSettings.pmsEnabled = PMS5003Sensor::isEnabled();
    s_pendingSettings.alarmMelodyIndex = s.alarmMelodyIndex;
    s_pendingSettings.sevenSegBrightness = s.sevenSegBrightness;
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
    /* Melodia: zapis DWOMA kluczami (id + indeks) jak AlarmMelodyPrefs —
     * bez tego loadIndex() czytalby stary "alarmMelodyId" po zmianie z Guitiona. */
    AlarmMelodyPrefs::saveSelection(prefs, s_pendingSettings.alarmMelodyIndex);
    /* Etap 2: jasnosc 7-seg (0..100). */
    prefs.putUChar("segBrightness", s_pendingSettings.sevenSegBrightness);
    prefs.end();
}

// --- Silnik ekranu (Guition -> ZEGAR -> STM32): forward, nie ksiegowosc ---
// WARIANT B: jedyne zrodlo prawdy to stepper_truth we flash STM32.
// ZEGAR NIE TRZYMA mapy stopni, bazy pozycji ani kalibracji w NVS —
// forwarduje komendy Guitiona (EXECUTE->MOVE_DEG, CAL_*->SET_*) i stan
// (0xB2/0xB4->0x3B). Zrodlem celu/predkosci dla UI jest teraz ECHO z STM32
// (REF v2: targetDeg + speed); tu tylko ostatnia predkosc UI do komend.
// Blok MUSI stac przed handlerem ponizej.
namespace {
uint16_t s_stepSpeed = StepperFrames::kSpeedDef; // ostatnia predkosc UI (SET_SPAN/EXECUTE)
// Ostatnia predkosc PODANA STM32 (SET_SPEED). Wysylamy tylko przy zmianie,
// zeby nie spamowac kolejki — STM32 persistuje ja dla auto-powrotu.
uint16_t s_speedSentToStm = 0xFFFF;

uint16_t rdU16LE(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

// Przyspieszenie ruchow z Guitiona (EXECUTE/HOLD/SET_SPAN-return).
// Skrypt PC podaje wlasne w ramce; Guition przyspieszenia nie ustawia, wiec
// musi byc wartosc, przy ktorej ZADANA predkosc jest realnie osiagana.
// Fizyka trapezu STM32 (stepper_motion.c): start 0->accel od razu, dalej
// +accel/cur na krok, hamowanie cur^2/2accel. Przy accel=400 ruch 45 stopni
// (512 krokow) pelzal do ~570 mimo slidera na 1000. Przy 1000: start od razu
// do vmax, hamowanie 500 krokow przy 1000 — zadana predkosc osiagana.
// Limit STM32: 50..2000. Bezpieczny kompromis szybkosc/plynnosc.
constexpr uint16_t kGuitionAccel = 1000;

// Podanie predkosci do STM32 (auto-powrot po restarcie jedzie nia, nie 400).
// Tylko przy zmianie; tor priorytetowy w moscie (flush go nie zjada).
void forwardSpeedToStm(uint16_t speed) {
  if (speed == s_speedSentToStm) return;
  if (StepperBridge::issueSetSpeed(speed)) {
    s_speedSentToStm = speed;
    LOG_I("STEPPER", "SET_SPEED %u -> STM32 (auto-powrot)", (unsigned)speed);
  }
}

uint16_t clampSpeed(uint16_t v) {
  if (v < StepperFrames::kSpeedMin || v > StepperFrames::kSpeedMax) {
    return StepperFrames::kSpeedDef;
  }
  return v;
}

// FALLBACK nieskalibrowany: STM32 odrzuca MOVE_DEG bez kalibracji (truth),
// wiec EXECUTE jedzie surowym MOVE_ABS 2048 krokow/obrot od zera licznika.
// Jawny fallback, nie prawda — po kalibracji nieuzywany.
int32_t fallbackDegToSteps(uint16_t deg) {
  int64_t steps = (static_cast<int64_t>(deg) * 2048) / 360;
  if (steps > StepperFrames::kMaxMove) steps = StepperFrames::kMaxMove;
  if (steps < -StepperFrames::kMaxMove) steps = -StepperFrames::kMaxMove;
  return static_cast<int32_t>(steps);
}
} // namespace

// Migracja ustawien steppera NVS ("zegar": stepTargetDeg/stepSpeed) ->
// flash STM32 (E2). Jednorazowa, idempotentna: brak kluczy = no-op.
// Klucze usuwane DOPIERO po zakolejkowaniu obu komend (sukces).
// Wolac PO StepperBridge::begin() — komendy ida kolejem FIFO mostu.
void stepperNvsMigrate() {
  Preferences prefs;
  prefs.begin("zegar", true);
  const bool hasT = prefs.isKey("stepTargetDeg");
  const bool hasS = prefs.isKey("stepSpeed");
  uint16_t t = prefs.getUShort("stepTargetDeg", 45);
  uint16_t sp = prefs.getUShort("stepSpeed", StepperFrames::kSpeedDef);
  prefs.end();
  if (!hasT && !hasS) return; // juz zmigrowane (albo fabryka)

  if (sp < StepperFrames::kSpeedMin || sp > StepperFrames::kSpeedMax) {
    sp = StepperFrames::kSpeedDef;
  }
  if (t > 360U) t = 45U;

  // Oba klucze -> dwa frame'y FIFO; pojedyncze -> tor priorytetowy.
  bool ok = true;
  if (hasT && hasS) {
    ok = StepperBridge::seedTargetSpeed(t, sp);
  } else if (hasT) {
    ok = StepperBridge::issueSetTarget(t);
  } else {
    ok = StepperBridge::issueSetSpeed(sp);
  }
  if (!ok) {
    LOG_W("STEPPER", "migracja NVS->STM32: FIFO zajete, retry przy starcie");
    return; // klucze zostaja — retry nastepny boot
  }

  Preferences wprefs;
  wprefs.begin("zegar", false);
  if (hasT) wprefs.remove("stepTargetDeg");
  if (hasS) wprefs.remove("stepSpeed");
  wprefs.end();
  s_stepSpeed = sp; // echo zanim wroci REF
  LOG_I("STEPPER", "migracja NVS->STM32: target=%u speed=%u (klucze usuniete)",
        (unsigned)t, (unsigned)sp);
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

void pushStepperState() {
  uint8_t flags = StepperBridge::refCalOk() ? kStepperFlagCalOk : 0;
  if (StepperBridge::refRadarEn()) flags |= kStepperFlagRadarEn;
  // Bity 2-3: czulosc OUT radaru (0=SREDNIA, 1=WYSOKA, 2=MALA).
  flags |= static_cast<uint8_t>((StepperBridge::refRadarSens() & 0x03U) << 2);
  // homeDeg z REF (0xFFFF = jeszcze nie znamy — Guition nie adoptuje).
  const uint16_t home = StepperBridge::refValid()
      ? StepperBridge::refHomeDeg() : 0xFFFFU;
  // Cel/predkosc z ECHA STM32 (REF v2). Stary STM32 bez pol: cel = 45,
  // predkosc = ostatnia UI (zgodne z poprzednim zachowaniem).
  const uint16_t refT = StepperBridge::refTargetDeg();
  const uint16_t target = (refT != 0xFFFFU) ? refT : 45U;
  const uint16_t refS = StepperBridge::refSpeed();
  const uint16_t speed = (refS != 0xFFFFU) ? refS : s_stepSpeed;
  const uint16_t autoClose = StepperBridge::refAutoCloseS(); // 0xFFFF = brak
  sendStepperState(StepperBridge::lastKnownBusy(), StepperBridge::lastKnownPos(),
                   target, speed, home, flags, autoClose);
}

// Sciezka DONE (0xB3): forward pozycji koncowej do Guitiona.
// Bez zapisu — prawde trzyma STM32 (zapis 2 s po ruchu, we flash).
void pushStepperDone(int32_t posSteps) {
  uint8_t flags = StepperBridge::refCalOk() ? kStepperFlagCalOk : 0;
  if (StepperBridge::refRadarEn()) flags |= kStepperFlagRadarEn;
  flags |= static_cast<uint8_t>((StepperBridge::refRadarSens() & 0x03U) << 2);
  const uint16_t home = StepperBridge::refValid()
      ? StepperBridge::refHomeDeg() : 0xFFFFU;
  const uint16_t refT = StepperBridge::refTargetDeg();
  const uint16_t target = (refT != 0xFFFFU) ? refT : 45U;
  const uint16_t refS = StepperBridge::refSpeed();
  const uint16_t speed = (refS != 0xFFFFU) ? refS : s_stepSpeed;
  const uint16_t autoClose = StepperBridge::refAutoCloseS(); // 0xFFFF = brak
  sendStepperState(false, posSteps, target, speed, home, flags, autoClose);
}

// Prosba o rame odniesienia (0xB4) — po HELLO i przy pollu Guitiona,
// zeby flaga kalibracji nie utknela na starcie (boot-REF STM32 mogl
// przyjsc przed naszym hookiem).
void requestStepperRef() {
  StepperBridge::requestRef();
}

// Prawda z STM32 (USB STEPREF?): zero/span/flaga/wiek + echo REF v2
// (cel/predkosc z flash STM32). Read-only.
void printStepperRef() {
  Serial.print("STEPREF zero=");
  Serial.print(StepperBridge::refZero());
  Serial.print(" span=");
  Serial.print(StepperBridge::refSpan());
  Serial.print(" ok=");
  Serial.print(StepperBridge::refCalOk() ? 1 : 0);
  Serial.print(" valid=");
  Serial.print(StepperBridge::refValid() ? 1 : 0);
  if (StepperBridge::refValid()) {
    Serial.print(" ageMs=");
    Serial.print((unsigned long)StepperBridge::refAgeMs());
  }
  Serial.print(" livePos=");
  Serial.print(StepperBridge::lastKnownPos());
  Serial.print(" busy=");
  Serial.print(StepperBridge::lastKnownBusy() ? 1 : 0);
  Serial.print(" target=");
  Serial.print(StepperBridge::refTargetDeg());
  Serial.print(" speed=");
  Serial.print(StepperBridge::refSpeed());
  Serial.print(" homeDeg=");
  Serial.println(StepperBridge::refValid() ? StepperBridge::refHomeDeg() : 9999);
}

void handleReceivedStepperCmd(const uint8_t* payload, uint16_t payloadLength) {
  if (payload == nullptr || payloadLength < 1) return;
  switch (payload[0]) {
    case kStepperSubExecute: { // [deg u16][speed u16]
      if (payloadLength < 5) return;
      const uint16_t deg = rdU16LE(payload + 1);
      const uint16_t speed = clampSpeed(rdU16LE(payload + 3));
      // Cel NIE jest tu zmieniany: to WYBOR uzytkownika (SET_TARGET ->
      // flash STM32). Inaczej zjazd automatu radaru do 0 nadpisywalby wybor.
      s_stepSpeed = speed;
      // Cel liczy STM32 z WLASNEGO spanu (MOVE_DEG, preempcja jak wczesniej).
      // Bez kalibracji STM32 odrzuca DEG — jawny fallback surowym MOVE_ABS.
      // forwardSpeedToStm DOPIERO po zakolejkowaniu ruchu (flush preempcji
      // nie moze zjesc predkosci — a tor priorytetowy i tak go chroni).
      if (StepperBridge::refCalOk()) {
        LOG_I("STEPPER", "EXECUTE deg=%u speed=%u -> MOVE_DEG",
              (unsigned)deg, (unsigned)speed);
        StepperBridge::issueMoveDegFlush(deg, speed, kGuitionAccel);
      } else {
        LOG_W("STEPPER", "EXECUTE %u bez kalibracji STM32 — fallback surowy",
              (unsigned)deg);
        StepperBridge::issueMoveAbs(fallbackDegToSteps(deg), speed,
                                    kGuitionAccel);
      }
      forwardSpeedToStm(speed); // po zakolejkowaniu ruchu (tor priorytetowy)
      pushStepperState(); // busy=1 od razu (potwierdzenie przyjecia)
      break;
    }
    case kStepperSubHoldStart: { // [dir i8][speed u16]
      if (payloadLength < 4) return;
      const int8_t dir = static_cast<int8_t>(payload[1]);
      if (dir != -1 && dir != 1) return;
      const uint16_t speed = clampSpeed(rdU16LE(payload + 2));
      s_stepSpeed = speed; // echo ostatniej predkosci (powrot SET_SPAN tez z niej)
      const int32_t chunk = (dir > 0) ? StepperFrames::kChunkDef
                                      : -StepperFrames::kChunkDef;
      StepperBridge::issueMoveRel(chunk, speed, kGuitionAccel);
      forwardSpeedToStm(speed); // po zakolejkowaniu ruchu (tor priorytetowy)
      pushStepperState();
      break;
    }
    case kStepperSubStop:
      StepperBridge::requestStop(); // priorytet przed kolejka, nigdy nie ginie
      pushStepperState();
      break;
    case kStepperSubStatusReq:
      StepperBridge::requestStatus(); // odp async 0xB2 -> forward w hooku
      StepperBridge::requestRef(); // + 0xB4 (flaga kalibracji na biezaco)
      break;
    case kStepperSubCalZero:
      // Adopcja zera W STM32 (SET_ZERO). Flaga KALIBRUJ wraca 0xB2/0xB4.
      StepperBridge::issueSetZero();
      pushStepperState();
      break;
    case kStepperSubCalSpan:
      // Adopcja 90 stopni W STM32 (SET_SPAN) + jego powrot do 0.
      // Predkosc powrotu ze slidera Guitiona.
      StepperBridge::issueSetSpan(s_stepSpeed, kGuitionAccel);
      pushStepperState();
      break;
    case kStepperSubSetHome: { // [deg u16]
      // Cel powrotu po resecie (double-click presetu). Preferencja w STM32.
      if (payloadLength < 3) return;
      const uint16_t home = rdU16LE(payload + 1);
      LOG_I("STEPPER", "SET_HOME %u -> STM32 (cel po resecie)",
            (unsigned)home);
      StepperBridge::issueSetHome(home);
      pushStepperState();
      break;
    }
    case kStepperSubSetRadar: { // [en]
      // Automat LD2410C (przycisk na Guitionie). Preferencja we flash STM32.
      if (payloadLength < 2) return;
      const bool en = payload[1] != 0;
      LOG_I("STEPPER", "SET_RADAR %u -> STM32 (automat obecności)", en ? 1 : 0);
      StepperBridge::issueSetRadar(en);
      pushStepperState();
      break;
    }
    case kStepperSubSetTarget: { // [deg u16]
      // Wybrany kat z UI (single-click presetu / WYKONAJ) -> flash STM32
      // (persist + echo w REF). ZEGAR NVS nie jest juz wlascicielem.
      if (payloadLength < 3) return;
      const uint16_t target = rdU16LE(payload + 1);
      StepperBridge::issueSetTarget(target > 360U ? 360U : target);
      pushStepperState();
      break;
    }
    case kStepperSubCalMode: { // [en]
      // Kalibracja w toku na Guitionie: wstrzymaj automat radaru w STM32,
      // zeby ruch nie przeszkodzil w ustawianiu 0/90. Brak persistencji;
      // en=0 po zapisie/wyjsciu, brak keepalive = auto-timeout STM32.
      if (payloadLength < 2) return;
      const bool en = payload[1] != 0;
      LOG_I("STEPPER", "CAL_MODE %u -> STM32 (automat radaru wstrzymany)",
            en ? 1 : 0);
      StepperBridge::issueCalMode(en);
      break;
    }
    case kStepperSubSetAutoClose: { // [s u16]
      // Czas auto-zjazdu radaru (przyciski pod kafelkiem): presety
      // 30/120/300/600 s; walidacja + persist we flash STM32 (echo REF).
      if (payloadLength < 3) return;
      const uint16_t seconds = rdU16LE(payload + 1);
      LOG_I("STEPPER", "SET_AUTOCLOSE %u s -> STM32 (auto-zjazd radaru)",
            (unsigned)seconds);
      StepperBridge::issueSetAutoClose(seconds);
      pushStepperState();
      break;
    }
    case kStepperSubSetRadarSens: { // [kod 0/1/2]
      // Czulosc OUT radaru (ekran radaru): 0=SREDNIA, 1=WYSOKA, 2=MALA;
      // walidacja + persist we flash STM32 + aplikacja w module (echo REF).
      if (payloadLength < 2) return;
      const uint8_t code = (payload[1] <= 2U) ? payload[1] : 0U;
      LOG_I("STEPPER", "SET_RADAR_SENS %u -> STM32 (czulosc OUT)", (unsigned)code);
      StepperBridge::issueSetRadarSens(code);
      pushStepperState();
      break;
    }
    default:
      break;
  }
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
