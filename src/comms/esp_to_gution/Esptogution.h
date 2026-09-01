#pragma once

#include <Arduino.h>
#include <stdint.h>
namespace EsptoGuition {

constexpr uint8_t kFrameStart = 0xAA;
constexpr uint8_t kTypeIndoorWeather = 0x01;
constexpr uint8_t kTypePms = 0x02;
constexpr uint8_t kTypeTime = 0x03;
constexpr uint8_t kTypeSettings = 0x04;
constexpr uint8_t kTypeSetSettings = 0x05;
constexpr uint8_t kTypeSystemResources = 0x06;
constexpr uint8_t kTypeWifiStatus = 0x07;
constexpr uint8_t kTypeOutdoorWeather = 0x08;
constexpr uint8_t kTypePpg = 0x09;
constexpr uint8_t kTypeBpmStatus = 0x0A;
constexpr uint8_t kTypeStatusBle = 0x0B;
constexpr uint8_t kTypeStatusBell = 0x0C;
constexpr uint8_t kTypeRequest = 0x10;
constexpr uint8_t kTypeConfig = 0x11;
constexpr uint8_t kTypeHello    = 0x20;
constexpr uint8_t kTypeHelloAck = 0x21;
constexpr uint8_t kTypeMusicMetadata = 0x30;
constexpr uint8_t kTypeMusicStatus = 0x31;
constexpr uint8_t kTypeMusicCommand = 0x32;
constexpr uint8_t kTypeMusicVolume = 0x33;
constexpr uint8_t kTypeMusicEQ = 0x34;
constexpr uint8_t kTypeMusicRequest = 0x35;
constexpr uint8_t kTypeMusicVolumeState = 0x36;
constexpr uint8_t kTypeMusicEQState = 0x37;
constexpr uint8_t kTypeRadioMode = 0x38;
constexpr uint8_t kTypeRadarStatus = 0x39;
constexpr uint8_t kTypeAck = 0xFF;

// Stany synchronizacji – identyczne po obu stronach
enum PeerSyncState : uint8_t {
  kSyncBooting          = 0,
  kSyncUartReady        = 1,
  kSyncPeerDetected     = 2,
  kSyncFirstSyncSent    = 3,
  kSyncFirstSyncConfirmed = 4,
  kSyncSteadyState      = 5
};

// Payload ramki HELLO / HELLO_ACK (packed, 11 bajtów)
struct HelloPayload {
  uint8_t  protocolVersion; // 1
  uint8_t  deviceRole;      // 0 = Display, 1 = Sensor
  uint32_t bootId;          // Losowe ID generowane przy każdym starcie
  uint32_t uptimeMs;        // Czas od startu w ms
  uint8_t  syncState;       // PeerSyncState
};

constexpr uint32_t kDefaultBroadcastIntervalMs = 200UL;
constexpr uint32_t kDefaultFrameTimeoutMs = 200UL;
constexpr uint16_t kMaxPayloadBytes = 128U;

void begin(HardwareSerial &serialPort, uint32_t baudRate, int rxPin, int txPin);

void update();

void setBroadcastIntervalMs(uint32_t intervalMs);

uint32_t getBroadcastIntervalMs();

bool isReady();

void sendSettings(uint8_t sequence = 0);
void sendSystemResources(uint8_t sequence = 0);
void sendWeather(uint8_t sequence = 0);
void sendPms(uint8_t sequence = 0);
void sendTime(uint8_t sequence = 0);
void sendWifiStatus(uint8_t sequence = 0);
void sendOutdoorWeather(uint8_t sequence = 0);
void sendPpgImpl(int16_t diff);
void sendBpmStatus(int bpm, int spo2);
void sendStatusBle(uint8_t sequence = 0);
void sendStatusBell(uint8_t sequence = 0);
void sendMusicTitle(const char* title);
void sendMusicArtist(const char* artist);
void sendMusicStatus(bool connected, bool playing);
void sendMusicVolumeState(uint8_t volume);
void sendMusicEQState(uint8_t bass, uint8_t mid, uint8_t treble);
void sendRadioModeState(uint8_t sequence = 0);
void sendRadarStatus(uint8_t presence, uint16_t movDist, uint8_t movEnergy,
                     uint16_t statDist, uint8_t statEnergy, uint16_t detectDist);
uint8_t nextSequence();

} // namespace EsptoGuition
