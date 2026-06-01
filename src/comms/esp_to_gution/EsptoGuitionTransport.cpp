#include "EsptoGuitionTransport.h"
#include "BoardPins.h"
#include "EsptoGuitionCobs.h"
#include "EsptoGuitionState.h"
#include "Esptogution.h"
#include "TimeSyncProtocol.h"
#include "AppLog.h"
#include <Arduino.h>

namespace EsptoGuition {
namespace {

constexpr uint8_t kFrameHeaderBytes = 4;
constexpr uint8_t kFrameCrcBytes = 2;
constexpr uint8_t kAckOk = 0x00;
constexpr uint8_t kAckBadType = 0x01;
constexpr uint8_t kAckBadLength = 0x02;
constexpr uint8_t kAckBadCrc = 0x03;
constexpr uint8_t kProtocolVersion = 1;
constexpr uint8_t kDeviceRoleSensor = 1;

HardwareSerial *s_serial = nullptr;
uint32_t s_bootId = 0;
PeerSyncState s_syncState = kSyncBooting;

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

void handleFrame(uint8_t type, uint8_t sequence, const uint8_t *payload,
                 uint16_t payloadLength) {
  switch (type) {
  case kTypeHello:
    // Guition się zgłosił – odpowiadamy HELLO_ACK i od razu wysyłamy pełny stan
    // (Real-time Sync) + alarmy + timer
    sendHelloAck(sequence);
    s_syncState = kSyncPeerDetected;

    // Natychmiastowy push wszystkiego na start
    EsptoGuition::sendSettings(EsptoGuition::nextSequence());
    EsptoGuition::sendWeather(EsptoGuition::nextSequence());
    EsptoGuition::sendPms(EsptoGuition::nextSequence());
    EsptoGuition::sendTime(EsptoGuition::nextSequence());
    EsptoGuition::sendWifiStatus(EsptoGuition::nextSequence());
    EsptoGuition::sendSystemResources(EsptoGuition::nextSequence());

    // NOWE: alarmy + timer + stan dzwonka
    TimeSync::sendAlarmList();
    TimeSync::sendTimerState();
    EsptoGuition::sendStatusBell(EsptoGuition::nextSequence());
    break;
  case kTypeRequest:
    if (payloadLength >= 1) {
      if (payload[0] == kTypeSettings) {
        EsptoGuition::sendSettings(sequence);
      } else if (payload[0] == kTypeSystemResources) {
        EsptoGuition::sendSystemResources(sequence);
      } else if (payload[0] == TimeSync::kTypeAlarmList) {
        TimeSync::sendAlarmList();
      }
    }
    break;
  case kTypeSetSettings:
    handleReceivedSettings(payload, payloadLength);
    sendAck(sequence, kAckOk, type);
    break;
  case kTypeMusicCommand:
    handleReceivedMusicCommand(payload, payloadLength);
    sendAck(sequence, kAckOk, type);
    break;
  case kTypeMusicVolume:
    handleReceivedMusicVolume(payload, payloadLength);
    sendAck(sequence, kAckOk, type);
    break;
  case kTypeMusicEQ:
    handleReceivedMusicEQ(payload, payloadLength);
    sendAck(sequence, kAckOk, type);
    break;
  case kTypeMusicRequest:
    handleReceivedMusicRequest(payload, payloadLength);
    sendAck(sequence, kAckOk, type);
    break;
  case kTypeRadioMode:
    handleReceivedRadioModeSwitch(payload, payloadLength);
    sendAck(sequence, kAckOk, type);
    break;
  // NOWE: TimeSyncProtocol — ramki Gution→Zegar
  case TimeSync::kTypeAlarmList:
    TimeSync::handleAlarmListSync(payload, payloadLength);
    sendAck(sequence, kAckOk, type);
    break;
  case TimeSync::kTypeSetAlarm:
    TimeSync::handleSetAlarm(payload, payloadLength);
    sendAck(sequence, kAckOk, type);
    break;
  case TimeSync::kTypeTimerCmd:
    TimeSync::handleTimerCmd(payload, payloadLength);
    sendAck(sequence, kAckOk, type);
    break;
  case TimeSync::kTypeAlarmAction:
    TimeSync::handleAlarmAction(payload, payloadLength);
    sendAck(sequence, kAckOk, type);
    break;
  case TimeSync::kTypeStopwatchCmd:
    TimeSync::handleStopwatchCmd(payload, payloadLength);
    sendAck(sequence, kAckOk, type);
    break;
  default:
    sendAck(sequence, kAckBadType, type);
    break;
  }
}

} // namespace

void sendHelloAck(uint8_t sequence) {
  uint8_t buf[11];
  uint8_t *c = buf;
  *c++ = kProtocolVersion;
  *c++ = kDeviceRoleSensor;
  *c++ = static_cast<uint8_t>(s_bootId & 0xFF);
  *c++ = static_cast<uint8_t>((s_bootId >> 8) & 0xFF);
  *c++ = static_cast<uint8_t>((s_bootId >> 16) & 0xFF);
  *c++ = static_cast<uint8_t>((s_bootId >> 24) & 0xFF);
  uint32_t uptime = static_cast<uint32_t>(millis());
  *c++ = static_cast<uint8_t>(uptime & 0xFF);
  *c++ = static_cast<uint8_t>((uptime >> 8) & 0xFF);
  *c++ = static_cast<uint8_t>((uptime >> 16) & 0xFF);
  *c++ = static_cast<uint8_t>((uptime >> 24) & 0xFF);
  *c++ = static_cast<uint8_t>(s_syncState);
  sendRawFrame(kTypeHelloAck, sequence, buf, static_cast<uint16_t>(c - buf));
}

void beginSerial(HardwareSerial &serialPort, uint32_t baudRate, int rxPin,
                 int txPin) {
  // Wymuszamy wyłączenie buzzera na starcie (GPIO 2 / LED_BUILTIN)
  pinMode(BoardPins::kBuzzer, OUTPUT);
  digitalWrite(BoardPins::kBuzzer, LOW);

  // Unikamy esp_random(), bo używa ADC2 (konflikt z GPIO 2 / Buzzerem)
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  s_bootId = (static_cast<uint32_t>(mac[5]) << 24) |
             (static_cast<uint32_t>(mac[4]) << 16) |
             (static_cast<uint32_t>(mac[3]) << 8) |
             static_cast<uint32_t>(millis());

  s_syncState = kSyncUartReady;
  s_serial = &serialPort;
  s_serial->begin(baudRate, SERIAL_8N1, rxPin, txPin);
  s_serial->setTxBufferSize(1024);
  resetRx();
}

void sendRawFrame(uint8_t type, uint8_t sequence, const uint8_t *payload,
                  uint16_t payloadLength) {
  if (s_serial == nullptr || payloadLength > kMaxPayloadBytes)
    return;

  uint8_t frame[kFrameHeaderBytes + kMaxPayloadBytes + kFrameCrcBytes];
  uint8_t *cursor = frame;
  *cursor++ = type;
  *cursor++ = sequence;
  *cursor++ = static_cast<uint8_t>(payloadLength & 0xFFU);
  *cursor++ = static_cast<uint8_t>((payloadLength >> 8) & 0xFFU);

  if (payloadLength > 0U && payload != nullptr) {
    memcpy(cursor, payload, payloadLength);
    cursor += payloadLength;
  }

  const uint16_t crc =
      crc16Ccitt(frame, static_cast<size_t>(kFrameHeaderBytes + payloadLength));
  *cursor++ = static_cast<uint8_t>(crc & 0xFFU);
  *cursor++ = static_cast<uint8_t>((crc >> 8) & 0xFFU);

  uint8_t cobsBuffer[sizeof(frame) + 4];
  size_t cobsLen = cobsEncode(frame, cursor - frame, cobsBuffer);

  uint8_t txBuffer[sizeof(cobsBuffer) + 2];
  txBuffer[0] = 0x00;
  memcpy(txBuffer + 1, cobsBuffer, cobsLen);
  txBuffer[1 + cobsLen] = 0x00;

  const size_t wireLen = cobsLen + 2;
  if (s_serial->availableForWrite() >= wireLen) {
    s_serial->write(txBuffer, wireLen);
  } else {
    LOG_W("UART", "Frame dropped type=%u seq=%u len=%u reason=tx_full",
          (unsigned)type, (unsigned)sequence, (unsigned)payloadLength);
  }
}

bool ingestSerialBytes() {
  if (s_serial == nullptr)
    return false;

  bool consumed = false;
  while (s_serial->available() > 0) {
    const int rawByte = s_serial->read();
    if (rawByte < 0)
      break;

    const uint8_t byte = static_cast<uint8_t>(rawByte);

    if (byte == 0x00) {
      if (s_rx.length > 0) {
        uint8_t decodedBuffer[256];
        size_t decodedLen = cobsDecode(s_rx.buffer, s_rx.length, decodedBuffer);

        if (decodedLen >= kFrameHeaderBytes + kFrameCrcBytes) {
          uint8_t type = decodedBuffer[0];
          uint8_t sequence = decodedBuffer[1];
          uint16_t payloadLength =
              static_cast<uint16_t>(decodedBuffer[2]) |
              (static_cast<uint16_t>(decodedBuffer[3]) << 8U);

          if (payloadLength <= kMaxPayloadBytes &&
              decodedLen ==
                  kFrameHeaderBytes + payloadLength + kFrameCrcBytes) {

            uint16_t receivedCrc =
                static_cast<uint16_t>(decodedBuffer[decodedLen - 2]) |
                (static_cast<uint16_t>(decodedBuffer[decodedLen - 1]) << 8U);
            uint16_t expectedCrc = crc16Ccitt(decodedBuffer, decodedLen - 2);

            if (receivedCrc == expectedCrc) {
              handleFrame(type, sequence, decodedBuffer + kFrameHeaderBytes,
                          payloadLength);
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
