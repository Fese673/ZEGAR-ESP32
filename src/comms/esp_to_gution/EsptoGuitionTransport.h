#pragma once
#include <Arduino.h>
#include <stdbool.h>
#include <stdint.h>
namespace EsptoGuition {

void beginSerial(HardwareSerial &serialPort, uint32_t baudRate, int rxPin, int txPin);
void sendRawFrame(uint8_t type, uint8_t sequence, const uint8_t *payload, uint16_t payloadLength);
bool ingestSerialBytes();
void sendHelloAck(uint8_t sequence); // Keep-alive / handshake reply

} // namespace EsptoGuition
