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
constexpr uint8_t kTypeRequest = 0x10;
constexpr uint8_t kTypeConfig = 0x11;
constexpr uint8_t kTypeHello    = 0x20;
constexpr uint8_t kTypeHelloAck = 0x21;
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
uint8_t nextSequence();

} // namespace EsptoGuition
