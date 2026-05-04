#include "EsptoGuitionTransport.h"
#include "EsptoGuitionCobs.h"
#include "EsptoGuitionState.h"

namespace EsptoGuition {
namespace {

constexpr uint8_t kFrameHeaderBytes = 4;
constexpr uint8_t kFrameCrcBytes = 2;
constexpr uint8_t kAckOk = 0x00;
constexpr uint8_t kAckBadType = 0x01;
constexpr uint8_t kAckBadLength = 0x02;
constexpr uint8_t kAckBadCrc = 0x03;
constexpr uint16_t kMaxPayloadBytes = 128U;

constexpr uint8_t kTypeWeather = 0x01;
constexpr uint8_t kTypePms = 0x02;
constexpr uint8_t kTypeTime = 0x03;
constexpr uint8_t kTypeSettings = 0x04;
constexpr uint8_t kTypeSetSettings = 0x05;
constexpr uint8_t kTypeRequest = 0x10;
constexpr uint8_t kTypeConfig = 0x11;
constexpr uint8_t kTypeAck = 0xFF;

HardwareSerial *s_serial = nullptr;

struct RxState {
  uint8_t buffer[256] = {};
  size_t length = 0;
};

RxState s_rx;

void resetRx() { s_rx.length = 0; }

void sendAck(uint8_t sequence, uint8_t ackCode, uint8_t relatedType) {
  const uint8_t payload[2] = {ackCode, relatedType};
  sendRawFrame(kTypeAck, sequence, payload, sizeof(payload));
}

void handleFrame(uint8_t type, uint8_t sequence, const uint8_t *payload, uint16_t payloadLength) {
  switch (type) {
  case kTypeRequest:
    if (payloadLength >= 1 && payload[0] == kTypeSettings) {
      // request settings -> just reply by sending current settings
      // (This will be called via main Esptogution facade or we can just send it).
    }
    break;
  case kTypeSetSettings:
    handleReceivedSettings(payload, payloadLength);
    sendAck(sequence, kAckOk, type);
    break;
  default:
    sendAck(sequence, kAckBadType, type);
    break;
  }
}

} // namespace

void beginSerial(HardwareSerial &serialPort, uint32_t baudRate, int rxPin, int txPin) {
  s_serial = &serialPort;
  s_serial->begin(baudRate, SERIAL_8N1, rxPin, txPin);
  resetRx();
}

void sendRawFrame(uint8_t type, uint8_t sequence, const uint8_t *payload, uint16_t payloadLength) {
  if (s_serial == nullptr || payloadLength > kMaxPayloadBytes) return;

  uint8_t frame[kFrameHeaderBytes + kMaxPayloadBytes + kFrameCrcBytes] = {};
  uint8_t *cursor = frame;
  *cursor++ = type;
  *cursor++ = sequence;
  *cursor++ = static_cast<uint8_t>(payloadLength & 0xFFU);
  *cursor++ = static_cast<uint8_t>((payloadLength >> 8) & 0xFFU);

  if (payloadLength > 0U && payload != nullptr) {
    memcpy(cursor, payload, payloadLength);
    cursor += payloadLength;
  }

  const uint16_t crc = crc16Ccitt(frame, static_cast<size_t>(kFrameHeaderBytes + payloadLength));
  *cursor++ = static_cast<uint8_t>(crc & 0xFFU);
  *cursor++ = static_cast<uint8_t>((crc >> 8) & 0xFFU);

  uint8_t cobsBuffer[sizeof(frame) + 4] = {};
  size_t cobsLen = cobsEncode(frame, cursor - frame, cobsBuffer);

  uint8_t txBuffer[sizeof(cobsBuffer) + 2] = {};
  txBuffer[0] = 0x00;
  memcpy(txBuffer + 1, cobsBuffer, cobsLen);
  txBuffer[1 + cobsLen] = 0x00;

  s_serial->write(txBuffer, cobsLen + 2);
}

bool ingestSerialBytes() {
  if (s_serial == nullptr) return false;

  bool consumed = false;
  while (s_serial->available() > 0) {
    const int rawByte = s_serial->read();
    if (rawByte < 0) break;

    const uint8_t byte = static_cast<uint8_t>(rawByte);

    if (byte == 0x00) {
      if (s_rx.length > 0) {
        uint8_t decodedBuffer[256];
        size_t decodedLen = cobsDecode(s_rx.buffer, s_rx.length, decodedBuffer);

        if (decodedLen >= kFrameHeaderBytes + kFrameCrcBytes) {
          uint8_t type = decodedBuffer[0];
          uint8_t sequence = decodedBuffer[1];
          uint16_t payloadLength = static_cast<uint16_t>(decodedBuffer[2]) | 
                                   (static_cast<uint16_t>(decodedBuffer[3]) << 8U);

          if (payloadLength <= kMaxPayloadBytes && 
              decodedLen == kFrameHeaderBytes + payloadLength + kFrameCrcBytes) {

            uint16_t receivedCrc = static_cast<uint16_t>(decodedBuffer[decodedLen - 2]) |
                                   (static_cast<uint16_t>(decodedBuffer[decodedLen - 1]) << 8U);
            uint16_t expectedCrc = crc16Ccitt(decodedBuffer, decodedLen - 2);

            if (receivedCrc == expectedCrc) {
              handleFrame(type, sequence, decodedBuffer + kFrameHeaderBytes, payloadLength);
              consumed = true;
            } else {
              sendAck(sequence, kAckBadCrc, type);
            }
          }
        }
      }
      resetRx();
    } else {
      if (s_rx.length < sizeof(s_rx.buffer)) {
        s_rx.buffer[s_rx.length++] = byte;
      } else {
        resetRx();
      }
    }
  }
  return consumed;
}

} // namespace EsptoGuition
