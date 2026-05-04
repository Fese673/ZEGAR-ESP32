#pragma once

#include <Arduino.h>

#include <stdint.h>

namespace EsptoGuition {

constexpr uint8_t kFrameStart = 0xAA;
constexpr uint8_t kTypeWeather = 0x01;
constexpr uint8_t kTypePms = 0x02;
constexpr uint8_t kTypeTime = 0x03;
constexpr uint8_t kTypeSettings = 0x04;
constexpr uint8_t kTypeSetSettings = 0x05;
constexpr uint8_t kTypeRequest = 0x10;
constexpr uint8_t kTypeConfig = 0x11;
constexpr uint8_t kTypeAck = 0xFF;

constexpr uint32_t kDefaultBroadcastIntervalMs = 30000UL;
constexpr uint32_t kDefaultFrameTimeoutMs = 200UL;
constexpr uint16_t kMaxPayloadBytes = 128U;

void begin(HardwareSerial &serialPort, uint32_t baudRate, int rxPin, int txPin);

void update();

void setBroadcastIntervalMs(uint32_t intervalMs);

uint32_t getBroadcastIntervalMs();

bool isReady();

void sendSettings(uint8_t sequence = 0);

} // namespace EsptoGuition
