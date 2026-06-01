#include "Esptogution.h"
#include "Config.h"
#include "EsptoGuitionState.h"
#include "EsptoGuitionTransport.h"
#include "AppSettings.h"
#include "STM32_Data.h"
#include "RadioModeSwitch.h"
#include "TimeSyncProtocol.h"
#include "StopwatchService.h"
#include <atomic>

namespace EsptoGuition {

using namespace Config;

uint32_t s_broadcastIntervalMs = kDefaultBroadcastIntervalMs;
uint32_t s_lastBroadcastMs = 0;
std::atomic<uint8_t> s_sequence{0};

WeatherPayload s_lastSentWeather;
OutdoorWeatherPayload s_lastSentOutdoorWeather;
PmsPayload s_lastSentPms;
WifiPayload s_lastSentWifi;
StatusBlePayload s_lastSentBle;
StatusBellPayload s_lastSentBell;
SystemResourcesPayload s_lastSentResources;
uint8_t s_lastSentRadioMode = 0xFF; // force first send
unsigned long s_lastTimeSentS = 0;

namespace {

void appendU8(uint8_t *&cursor, uint8_t value) { *cursor++ = value; }

void appendU16(uint8_t *&cursor, uint16_t value) {
  *cursor++ = static_cast<uint8_t>(value & 0xFFU);
  *cursor++ = static_cast<uint8_t>((value >> 8) & 0xFFU);
}

void appendS16(uint8_t *&cursor, int16_t value) {
  appendU16(cursor, static_cast<uint16_t>(value));
}

void appendU32(uint8_t *&cursor, uint32_t value) {
  *cursor++ = static_cast<uint8_t>(value & 0xFFU);
  *cursor++ = static_cast<uint8_t>((value >> 8) & 0xFFU);
  *cursor++ = static_cast<uint8_t>((value >> 16) & 0xFFU);
  *cursor++ = static_cast<uint8_t>((value >> 24) & 0xFFU);
}

} // namespace

void sendWeather(uint8_t sequence) {
  WeatherPayload payload;
  if (!buildWeatherPayload(payload, millis())) return;

  uint8_t buffer[18] = {};
  uint8_t *cursor = buffer;
  appendS16(cursor, payload.temperatureCx100);
  appendU16(cursor, payload.humidityPctX100);
  appendU16(cursor, payload.pressureHpaX10);
  appendU16(cursor, payload.eco2);
  appendU32(cursor, payload.sampleAgeMs);
  appendU8(cursor, payload.flags);
  appendU16(cursor, payload.tvoc);

  sendRawFrame(kTypeIndoorWeather, sequence, buffer, static_cast<uint16_t>(cursor - buffer));
}

void sendOutdoorWeather(uint8_t sequence) {
  OutdoorWeatherPayload payload;
  if (!buildOutdoorWeatherPayload(payload, millis())) return;

  uint8_t buffer[48] = {};
  uint8_t *cursor = buffer;
  appendS16(cursor, payload.temperatureCx100);
  appendU16(cursor, payload.humidityPctX100);
  appendU16(cursor, payload.pressureHpaX10);
  appendU16(cursor, payload.windSpeedMsX100);
  appendU16(cursor, payload.windGustMsX100);
  appendU8(cursor, payload.windDeg);
  appendU8(cursor, payload.weatherCode);
  appendU8(cursor, payload.cloudCover);
  appendS16(cursor, payload.apparentTempCx100);
  appendU16(cursor, payload.pm25UgM3);
  appendU16(cursor, payload.pm10UgM3);
  appendU16(cursor, payload.co2Ppm);
  appendU8(cursor, payload.aqi);
  appendU8(cursor, payload.precipitationMmX10);
  appendU8(cursor, payload.uvIndexX10);
  appendU8(cursor, payload.sunriseHour);
  appendU8(cursor, payload.sunriseMin);
  appendU8(cursor, payload.sunsetHour);
  appendU8(cursor, payload.sunsetMin);
  appendU32(cursor, payload.sampleAgeMs);
  appendU8(cursor, payload.flags);
  appendU16(cursor, payload.no2UgM3);

  sendRawFrame(kTypeOutdoorWeather, sequence, buffer, static_cast<uint16_t>(cursor - buffer));
}

void sendPms(uint8_t sequence) {
  PmsPayload payload;
  if (!buildPmsPayload(payload, millis())) return;

  uint8_t buffer[23] = {};
  uint8_t *cursor = buffer;
  appendU16(cursor, payload.pm01);
  appendU16(cursor, payload.pm25);
  appendU16(cursor, payload.pm10);
  appendU16(cursor, payload.count0p3);
  appendU16(cursor, payload.count0p5);
  appendU16(cursor, payload.count1p0);
  appendU16(cursor, payload.count2p5);
  appendU16(cursor, payload.count5p0);
  appendU16(cursor, payload.count10p0);
  appendU32(cursor, payload.sampleAgeMs);
  appendU8(cursor, payload.flags);

  sendRawFrame(kTypePms, sequence, buffer, static_cast<uint16_t>(cursor - buffer));
}

void sendTime(uint8_t sequence) {
  TimePayload payload;
  if (!buildTimePayload(payload)) return;

  uint8_t buffer[5] = {};
  uint8_t *cursor = buffer;
  appendU32(cursor, payload.unixSeconds);
  appendU8(cursor, payload.valid);

  sendRawFrame(kTypeTime, sequence, buffer, static_cast<uint16_t>(cursor - buffer));
}

void sendWifiStatus(uint8_t sequence) {
  WifiPayload payload;
  if (!buildWifiPayload(payload)) return;

  uint8_t buffer[6] = {};
  uint8_t *cursor = buffer;
  appendU8(cursor, payload.connected ? 1 : 0);
  appendU8(cursor, static_cast<uint8_t>(payload.rssi));
  appendU8(cursor, payload.ip[0]);
  appendU8(cursor, payload.ip[1]);
  appendU8(cursor, payload.ip[2]);
  appendU8(cursor, payload.ip[3]);

  sendRawFrame(kTypeWifiStatus, sequence, buffer, static_cast<uint16_t>(cursor - buffer));
}

void sendStatusBle(uint8_t sequence) {
  StatusBlePayload payload;
  if (!buildStatusBlePayload(payload)) return;

  uint8_t buffer[1] = { payload.value };
  sendRawFrame(kTypeStatusBle, sequence, buffer, sizeof(buffer));
}

void sendStatusBell(uint8_t sequence) {
  StatusBellPayload payload;
  if (!buildStatusBellPayload(payload)) return;

  uint8_t buffer[1] = { payload.value };
  sendRawFrame(kTypeStatusBell, sequence, buffer, sizeof(buffer));
}

void broadcastSnapshots(unsigned long nowMs) {
  if (nowMs - s_lastBroadcastMs < s_broadcastIntervalMs) {
    return;
  }
  s_lastBroadcastMs = nowMs;

  static unsigned long lastKeepaliveMs = 0;
  static unsigned long lastSafetyRefreshMs = 0;

  constexpr unsigned long kKeepaliveIntervalMs = Config::kKeepaliveIntervalMs;
  constexpr unsigned long kSafetyRefreshIntervalMs = Config::kSafetyRefreshIntervalMs;
  constexpr unsigned long kOutdoorIntervalMs = 5000UL;

  WeatherPayload currentWeather;
  if (buildWeatherPayload(currentWeather, nowMs)) {
    bool changed = (currentWeather.temperatureCx100 != s_lastSentWeather.temperatureCx100) ||
                  (currentWeather.humidityPctX100 != s_lastSentWeather.humidityPctX100) ||
                  (currentWeather.pressureHpaX10 != s_lastSentWeather.pressureHpaX10);
    if (changed) {
      sendWeather(s_sequence.fetch_add(1, std::memory_order_relaxed));
      s_lastSentWeather = currentWeather;
      lastKeepaliveMs = nowMs;
    }
  }

  OutdoorWeatherPayload currentOutdoor;
  static unsigned long lastOutdoorCheckMs = 0;
  if (buildOutdoorWeatherPayload(currentOutdoor, nowMs)) {
    bool periodElapsed = (nowMs - lastOutdoorCheckMs >= kOutdoorIntervalMs);
    bool changed = (currentOutdoor.temperatureCx100 != s_lastSentOutdoorWeather.temperatureCx100) ||
                  (currentOutdoor.humidityPctX100 != s_lastSentOutdoorWeather.humidityPctX100) ||
                  (currentOutdoor.pressureHpaX10 != s_lastSentOutdoorWeather.pressureHpaX10);
    if (changed || periodElapsed) {
      sendOutdoorWeather(s_sequence.fetch_add(1, std::memory_order_relaxed));
      s_lastSentOutdoorWeather = currentOutdoor;
      lastOutdoorCheckMs = nowMs;
      lastKeepaliveMs = nowMs;
    }
  }

  PmsPayload currentPms;
  if (buildPmsPayload(currentPms, nowMs)) {
    bool changed = (currentPms.pm01 != s_lastSentPms.pm01) ||
                 (currentPms.pm25 != s_lastSentPms.pm25) ||
                 (currentPms.pm10 != s_lastSentPms.pm10);
    if (changed) {
      sendPms(s_sequence.fetch_add(1, std::memory_order_relaxed));
      s_lastSentPms = currentPms;
      lastKeepaliveMs = nowMs;
    }
  }

  TimePayload currentTime;
  if (buildTimePayload(currentTime)) {
    if (currentTime.unixSeconds != s_lastTimeSentS) {
      sendTime(s_sequence.fetch_add(1, std::memory_order_relaxed));
      s_lastTimeSentS = currentTime.unixSeconds;
      lastKeepaliveMs = nowMs;
    }
  }

  static unsigned long lastWifiSendMs = 0;
  bool timeElapsed = (nowMs - lastWifiSendMs >= 2000UL);
  
  if (timeElapsed) {
    WifiPayload currentWifi;
    if (buildWifiPayload(currentWifi)) {
      bool rssiChanged = abs(static_cast<int>(currentWifi.rssi) - static_cast<int>(s_lastSentWifi.rssi)) >= 2;
      bool stateChanged = (currentWifi.connected != s_lastSentWifi.connected) ||
                          (currentWifi.ip[0] != s_lastSentWifi.ip[0]) ||
                          (currentWifi.ip[1] != s_lastSentWifi.ip[1]) ||
                          (currentWifi.ip[2] != s_lastSentWifi.ip[2]) ||
                          (currentWifi.ip[3] != s_lastSentWifi.ip[3]);
                          
      if (stateChanged || rssiChanged) {
        sendWifiStatus(s_sequence.fetch_add(1, std::memory_order_relaxed));
        s_lastSentWifi = currentWifi;
        lastWifiSendMs = nowMs;
        lastKeepaliveMs = nowMs;
      } else {
        // Zaktualizuj czas nawet jeśli nie wysłano, żeby nie odpytywać WiFi co każdą pętlę
        lastWifiSendMs = nowMs;
      }
    }
  }

  {
    static unsigned long lastBleSendMs = 0;
    if (nowMs - lastBleSendMs >= 2000UL) {
      lastBleSendMs = nowMs;
      StatusBlePayload currentBle;
      if (buildStatusBlePayload(currentBle)) {
        if (currentBle.value != s_lastSentBle.value) {
          sendStatusBle(s_sequence.fetch_add(1, std::memory_order_relaxed));
          s_lastSentBle = currentBle;
          lastKeepaliveMs = nowMs;
        }
      }
    }
  }

  {
    static unsigned long lastBellSendMs = 0;
    if (nowMs - lastBellSendMs >= 2000UL) {
      lastBellSendMs = nowMs;
      StatusBellPayload currentBell;
      if (buildStatusBellPayload(currentBell)) {
        if (currentBell.value != s_lastSentBell.value) {
          sendStatusBell(s_sequence.fetch_add(1, std::memory_order_relaxed));
          s_lastSentBell = currentBell;
          lastKeepaliveMs = nowMs;
        }
      }
    }
  }

  {
    static unsigned long lastRadioModeSendMs = 0;
    if (nowMs - lastRadioModeSendMs >= 2000UL) {
      lastRadioModeSendMs = nowMs;
      RadioModeSwitchState currentMode = RadioModeSwitch::getCurrentState();
      uint8_t modeByte;
      switch (currentMode) {
        case RADIO_STATE_WIFI:  modeByte = 0; break;
        case RADIO_STATE_BT:    modeByte = 1; break;
        default:                modeByte = 2; break;
      }
      if (modeByte != s_lastSentRadioMode) {
        sendRadioModeState(s_sequence.fetch_add(1, std::memory_order_relaxed));
        s_lastSentRadioMode = modeByte;
        lastKeepaliveMs = nowMs;
      }
    }
  }

  static unsigned long lastResourcesCheckMs = 0;
  if (nowMs - lastResourcesCheckMs >= Config::kSignals[Config::SIGNAL_SYSTEM_RESOURCES].periodicIntervalMs) {
    lastResourcesCheckMs = nowMs;
    SystemResourcesPayload currentRes;
    if (buildSystemResourcesPayload(currentRes)) {
      bool ramChanged = abs(static_cast<int32_t>(currentRes.freeRam) - static_cast<int32_t>(s_lastSentResources.freeRam)) > 1024;
      bool changed = (currentRes.core0Cpu != s_lastSentResources.core0Cpu) ||
                    (currentRes.core1Cpu != s_lastSentResources.core1Cpu) ||
                    ramChanged;
      if (changed) {
        sendSystemResources(s_sequence.fetch_add(1, std::memory_order_relaxed));
        s_lastSentResources = currentRes;
        lastKeepaliveMs = nowMs;
      }
    }
  }

  // Timer state — co 1 sekundę
  {
    static unsigned long lastTimerStateMs = 0;
    if (nowMs - lastTimerStateMs >= 1000UL) {
      lastTimerStateMs = nowMs;
      TimeSync::sendTimerState();
    }
  }

  // Stopwatch state — co 100ms gdy nie IDLE (lub do pierwszej synchronizacji)
  {
    static unsigned long lastStopwatchMs = 0;
    if (nowMs - lastStopwatchMs >= 100UL) {
      lastStopwatchMs = nowMs;
      if (StopwatchService::isRunning() || StopwatchService::getElapsedMs() > 0) {
        TimeSync::sendStopwatchState();
      }
    }
  }

  if (nowMs - lastKeepaliveMs >= kKeepaliveIntervalMs) {
    lastKeepaliveMs = nowMs;
    sendHelloAck(s_sequence.fetch_add(1, std::memory_order_relaxed));
  }

  if (nowMs - lastSafetyRefreshMs >= kSafetyRefreshIntervalMs) {
    lastSafetyRefreshMs = nowMs;
    sendWeather(s_sequence.fetch_add(1, std::memory_order_relaxed));
    sendOutdoorWeather(s_sequence.fetch_add(1, std::memory_order_relaxed));
    sendPms(s_sequence.fetch_add(1, std::memory_order_relaxed));
    sendTime(s_sequence.fetch_add(1, std::memory_order_relaxed));
    sendWifiStatus(s_sequence.fetch_add(1, std::memory_order_relaxed));
    sendSystemResources(s_sequence.fetch_add(1, std::memory_order_relaxed));
    sendSettings(s_sequence.fetch_add(1, std::memory_order_relaxed));
    sendStatusBle(s_sequence.fetch_add(1, std::memory_order_relaxed));
    sendStatusBell(s_sequence.fetch_add(1, std::memory_order_relaxed));
    sendRadioModeState(s_sequence.fetch_add(1, std::memory_order_relaxed));
    TimeSync::sendAlarmList();
    TimeSync::sendTimerState();
    TimeSync::sendStopwatchState();
    buildWeatherPayload(s_lastSentWeather, nowMs);
    buildOutdoorWeatherPayload(s_lastSentOutdoorWeather, nowMs);
    buildPmsPayload(s_lastSentPms, nowMs);
    buildWifiPayload(s_lastSentWifi);
    buildStatusBlePayload(s_lastSentBle);
    buildStatusBellPayload(s_lastSentBell);
    buildSystemResourcesPayload(s_lastSentResources);
    {
      RadioModeSwitchState mode = RadioModeSwitch::getCurrentState();
      switch (mode) {
        case RADIO_STATE_WIFI:  s_lastSentRadioMode = 0; break;
        case RADIO_STATE_BT:    s_lastSentRadioMode = 1; break;
        default:                s_lastSentRadioMode = 2; break;
      }
    }
  }

  // Deferred NVS flush — actual flash write happens here (not in UART handler)
  EsptoGuition::musicSettingsFlush();
}

void sendSystemResources(uint8_t sequence) {
  SystemResourcesPayload payload;
  if (!buildSystemResourcesPayload(payload)) return;

  uint8_t buffer[32] = {};
  uint8_t *cursor = buffer;
  appendU32(cursor, payload.freeRam);
  appendU32(cursor, payload.heapRam);
  appendU32(cursor, payload.dmaRam);
  appendU8(cursor, payload.core0Cpu);
  appendU8(cursor, payload.core1Cpu);
  appendU32(cursor, payload.freeFlash);
  appendU32(cursor, payload.usedFlash);
  appendU16(cursor, payload.underrunsAudio);
  appendU16(cursor, payload.overflowAudio);
  appendU16(cursor, payload.dropsAudio);
  appendU16(cursor, payload.errorsI2c);
  appendU16(cursor, payload.timeoutsI2c);

  sendRawFrame(kTypeSystemResources, sequence, buffer, static_cast<uint16_t>(cursor - buffer));
}

void begin(HardwareSerial &serialPort, uint32_t baudRate, int rxPin, int txPin) {
  beginSerial(serialPort, baudRate, rxPin, txPin);
}

void update() {
  ingestSerialBytes();
  broadcastSnapshots(millis());
}

void setBroadcastIntervalMs(uint32_t intervalMs) {
  s_broadcastIntervalMs = intervalMs;
}

uint32_t getBroadcastIntervalMs() {
  return s_broadcastIntervalMs;
}

bool isReady() {
  return true;
}

void sendSettings(uint8_t sequence) {
  AppSettings::State& state = AppSettings::mutableState();
  uint8_t payload[6];
  payload[0] = state.buzzerEnabled ? 1 : 0;
  payload[1] = state.mqttEnabled ? 1 : 0;
  payload[2] = state.touchTestEnabled ? 1 : 0;
  payload[3] = state.backgroundMusicEnabled ? 1 : 0;
  payload[4] = true; // PMS always enabled / default true
  payload[5] = static_cast<uint8_t>(state.alarmMelodyIndex);
  sendRawFrame(kTypeSettings, sequence, payload, sizeof(payload));
}

void sendPpgImpl(int16_t diff) {
  uint8_t buffer[3];
  uint8_t seq = s_sequence.fetch_add(1, std::memory_order_relaxed);
  buffer[0] = seq;
  buffer[1] = static_cast<uint8_t>(diff & 0xFF);
  buffer[2] = static_cast<uint8_t>((diff >> 8) & 0xFF);
  sendRawFrame(kTypePpg, seq, buffer, sizeof(buffer));
}

void sendBpmStatus(int bpm, int spo2) {
  uint8_t buf[2] = { static_cast<uint8_t>(bpm),
                     static_cast<uint8_t>(spo2) };
  sendRawFrame(kTypeBpmStatus, s_sequence.fetch_add(1, std::memory_order_relaxed), buf, sizeof(buf));
}

void sendMusicTitle(const char* title) {
  uint8_t buf[128];
  buf[0] = 0;
  size_t len = strlen(title);
  if (len > 120) len = 120;
  buf[1] = static_cast<uint8_t>(len);
  if (len > 0) memcpy(buf + 2, title, len);
  sendRawFrame(kTypeMusicMetadata, s_sequence.fetch_add(1, std::memory_order_relaxed), buf, static_cast<uint16_t>(2 + len));
}

void sendMusicArtist(const char* artist) {
  uint8_t buf[128];
  buf[0] = 1;
  size_t len = strlen(artist);
  if (len > 120) len = 120;
  buf[1] = static_cast<uint8_t>(len);
  if (len > 0) memcpy(buf + 2, artist, len);
  sendRawFrame(kTypeMusicMetadata, s_sequence.fetch_add(1, std::memory_order_relaxed), buf, static_cast<uint16_t>(2 + len));
}

void sendMusicStatus(bool connected, bool playing) {
  uint8_t buf[1] = {
    static_cast<uint8_t>((connected ? 0x01U : 0) | (playing ? 0x02U : 0))
  };
  sendRawFrame(kTypeMusicStatus, s_sequence.fetch_add(1, std::memory_order_relaxed), buf, 1);
}

void sendMusicVolumeState(uint8_t volume) {
  if (volume > 100) volume = 100;
  uint8_t buf[1] = { volume };
  sendRawFrame(kTypeMusicVolumeState, s_sequence.fetch_add(1, std::memory_order_relaxed), buf, 1);
}

void sendMusicEQState(uint8_t bass, uint8_t mid, uint8_t treble) {
  if (bass > 100) bass = 100;
  if (mid > 100) mid = 100;
  if (treble > 100) treble = 100;
  uint8_t buf[3] = { bass, mid, treble };
  sendRawFrame(kTypeMusicEQState, s_sequence.fetch_add(1, std::memory_order_relaxed), buf, 3);
}

void sendRadioModeState(uint8_t sequence) {
  RadioModeSwitchState mode = RadioModeSwitch::getCurrentState();
  uint8_t modeByte;
  switch (mode) {
    case RADIO_STATE_WIFI:  modeByte = 0; break;
    case RADIO_STATE_BT:    modeByte = 1; break;
    default:                modeByte = 2; break; // TRANSITIONING
  }
  uint8_t buf[1] = { modeByte };
  sendRawFrame(kTypeRadioMode, sequence, buf, 1);
}

uint8_t nextSequence() {
  return s_sequence.fetch_add(1, std::memory_order_relaxed);
}

} // namespace EsptoGuition
