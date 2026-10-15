#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "WifiProvisioning.h"

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
constexpr uint8_t kTypeRadarStatus = 0x39; // Z -> G: 28 B = 26 B legacy + [detect_lo,detect_hi] na [26..27]
// Silnik ekranu (Guition <-> ZEGAR <-> STM32). Kontrakt payloadu ponizej —
// lustro po stronie Guitiona (CommunicationProtocol.h: kTypeStepperCmd/State).
// Zmieniasz tu = zmieniasz tam.
constexpr uint8_t kTypeStepperCmd = 0x3A;   // G -> Z: komenda [sub][args]
constexpr uint8_t kTypeStepperState = 0x3B; // Z -> G: stan [busy][pos i32 LE][targetDeg u16 LE][flags][speed u16 LE][homeDeg u16 LE][autoCloseS u16 LE]
//
// 0x3A sub-komendy:
//   0x01 EXECUTE    [deg u16 LE][speed u16 LE] — zapisz cel, MOVE_ABS, push stanu
//   0x02 HOLD_START [dir i8][speed u16 LE]     — MOVE_REL +-chunk, puszczenie = STOP
//   0x03 STOP                                   — priorytet przed kolejka, push stanu
//   0x04 STATUS_REQ                             — 0xB1 0x04 do STM32, odp async 0xB2
//   0x05 CAL_ZERO                                — zapisz biezacy pos jako 0 kalibracji
//   0x06 CAL_SPAN                                — zapisz rozpietosc 0..90 + MOVE_ABS 0
//   0x0A CAL_MODE   [en]                         — wstrzymaj automat radaru na czas kalibracji
//   0x0B SET_AUTOCLOSE [s u16 LE]                — czas auto-zjazdu radaru (presety 30/180/600/2700)
//   0x0C SET_RADAR_SENS [kod]                    — czulosc OUT radaru (0=SREDNIA, 1=WYSOKA, 2=MALA)
// 0x3B flagi: bit0 = kalibracja (calOk), bit1 = automat radaru (radarEn),
//             bity 2-3 = czulosc OUT radaru (0/1/2).
constexpr uint8_t kStepperSubExecute = 0x01;
constexpr uint8_t kStepperSubHoldStart = 0x02;
constexpr uint8_t kStepperSubStop = 0x03;
constexpr uint8_t kStepperSubStatusReq = 0x04;
constexpr uint8_t kStepperSubCalZero = 0x05;
constexpr uint8_t kStepperSubCalSpan = 0x06;
constexpr uint8_t kStepperSubSetHome = 0x07; // [deg u16 LE] cel powrotu po resecie
constexpr uint8_t kStepperSubSetRadar = 0x08; // [en] automat LD2410C (flash STM32)
constexpr uint8_t kStepperSubSetTarget = 0x09; // [deg u16 LE] wybrany kat (NVS UI, bez ruchu)
constexpr uint8_t kStepperSubCalMode = 0x0A;  // [en] wstrzymaj automat radaru na czas kalibracji
constexpr uint8_t kStepperSubSetAutoClose = 0x0B; // [s u16 LE] czas auto-zjazdu radaru (presety 30/180/600/2700)
constexpr uint8_t kStepperSubSetRadarSens = 0x0C; // [kod] czulosc OUT radaru 0/1/2
constexpr uint8_t kStepperSubSetOnOff = 0x0D; // [en] plynne wylaczenie (dim 7-seg)
constexpr uint8_t kStepperSubSetSpeed = 0x0E; // [speed u16 LE] auto-powrot (flash STM32)
constexpr uint8_t kStepperFlagCalOk = 0x01;
constexpr uint8_t kStepperFlagRadarEn = 0x02;
constexpr uint8_t kStepperFlagOnOff = 0x10; // bit4: plynne wylaczenie (truth STM32)
//
// 0x11 kTypeConfig: panel "Ustawienia WiFi" na Guitionie -> provisioning
// ZEGARA (skan, test hasla, zapis + adopcja bez restartu). Lustro po stronie
// Guitiona: CommunicationProtocol.h kTypeConfig + CommunicationState.cpp.
//   G -> Z: [sub][args]
//   Z -> G: [sub|0x80][args]
constexpr uint8_t kWifiSubScanReq = 0x01; // [] prosba o skan
constexpr uint8_t kWifiSubSetCred = 0x02; // [len][ssid][len][pass] test + zapis
constexpr uint8_t kWifiSubInfoReq = 0x03; // [] stan WiFi + zapisany SSID
constexpr uint8_t kWifiRespScan = 0x81;   // [status][total][index][count]+n*[rssi i8][flags][len][ssid]
constexpr uint8_t kWifiRespCred = 0x82;   // [code][reason u16 LE][len][ssid]
constexpr uint8_t kWifiRespInfo = 0x83;   // [wifiOn][btMode][connected][len][ssid]
constexpr uint8_t kWifiNetOpen = 0x01;    // flaga sieci: otwarta (bez hasla)
// Paginacja listy: 5 + 3*(3+32) = 110 B < 128 B limitu payloadu ramki.
constexpr uint8_t kWifiMaxNetworks = 12U;
constexpr uint8_t kWifiEntriesPerFrame = 3U;
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
                     uint16_t statDist, uint8_t statEnergy, uint16_t detectDist,
                     const uint8_t *movGates, const uint8_t *statGates);
void sendStepperState(bool busy, int32_t posSteps, uint16_t targetDeg,
                      uint16_t speed, uint16_t homeDeg, uint8_t flags,
                      uint16_t autoCloseS);
// Panel WiFi (0x11): wyniki skanu (paginowane), testu hasla i info.
void sendWifiScanChunk(uint8_t status, uint8_t total, uint8_t index,
                       uint8_t count, const WifiProvisioning::Network* entries);
void sendWifiCredResult(uint8_t code, uint16_t reason, const char* ssid);
void sendWifiInfo(uint8_t wifiOn, uint8_t btMode, uint8_t connected, const char* ssid);
uint8_t nextSequence();

} // namespace EsptoGuition
