/*
 * STM32_Data.cpp — odbiór/nadawanie ramek COBS+CRC16 do STM32.
 *
 * Kanał: EspSoftwareSerial (plikowe oprogramowanie UART) GPIO16/17 @ 9600 baud.
 * Serial2 jest zajęty przez Guition, Serial1 przez PMS5003 — dlatego STM32
 * musi iść softwarowym UARTem (EspSoftwareSerial).
 * Format: 0x00 | COBS([type][seq][len_lo][len_hi][payload...][crc16]) | 0x00
 * CRC: 16-bit CCITT (poly 0x1021, init 0xFFFF), liczony nad bajtami
 *      [type..payload_ostatni] (bez samych bajtów CRC).
 *
 * Algorytmy COBS/CRC reużywane z modułu EsptoGuitionCobs.
 */

#include "STM32_Data.h"
#include "comms/esp_to_gution/EsptoGuitionCobs.h"
#include "comms/esp_to_gution/Esptogution.h"
#include "config/Board_Pins.h"

int     bpmNumber     = 0;
int     spo2Number    = 0;
bool    stmDataUpdated = false;
int16_t stm32PpgDiff   = 0;
bool    stm32PpgActive = false;

// --- LD2410C radar ---
bool     ld2410Presence = false;
bool     ld2410MovingDetected = false;
uint16_t ld2410MovingDistance = 0;
uint8_t  ld2410MovingEnergy = 0;
bool     ld2410StationaryDetected = false;
uint16_t ld2410StationaryDistance = 0;
uint8_t  ld2410StationaryEnergy = 0;
uint16_t ld2410DetectionDistance = 0;
bool     ld2410DataUpdated = false;
uint32_t ld2410LastFrameMs = 0;
bool     s_ld2410_forward_pending = false;
struct Ld2410Pending {
    uint8_t presence;
    uint16_t movDist;
    uint8_t movEnergy;
    uint16_t statDist;
    uint8_t statEnergy;
    uint16_t detectDist;
} s_ld2410_pending = {};

/*--- Kanał UART (EspSoftwareSerial na GPIO16=RX, GPIO17=TX) ---*/
static EspSoftwareSerial::UART s_stmSoftSerial;
static Stream *s_stmSerial = nullptr;
static uint8_t s_stmTxSeq         = 0;   // sekwencja TX (0..255 wrap)

/*--- RX state machine ---*/
struct RxState {
    uint8_t  buffer[256];
    uint16_t length = 0;
    uint32_t lastFrameMs = 0;
};
static RxState s_rx;
static uint32_t s_lastPpgFrameMs = 0;
static bool s_ppg_forward_pending = false;
static int16_t s_ppg_forward_diff = 0;

/*--- Typy ramek (kanał STM32↔ZEGAR, prefix 0xA0..0xBF) ---*/
static constexpr uint8_t kTypePpg        = 0xA0;
static constexpr uint8_t kTypeBpm        = 0xA1;
static constexpr uint8_t kTypeAck        = 0xA2;
static constexpr uint8_t kTypeRadar      = 0xA3;
static constexpr uint8_t kTypeBrightness = 0xB0;

static constexpr uint16_t kMaxPayload   = 128;
static constexpr uint8_t  kHeaderBytes  = 4;
static constexpr uint8_t  kCrcBytes     = 2;

/* ========================================================================== */
/* TX — budowa + COBS + wysyłka                                                */
/* ========================================================================== */

static void sendFrame(uint8_t type, const uint8_t *payload, uint16_t len)
{
    if (s_stmSerial == nullptr || len > kMaxPayload) return;
    if (len > 0 && payload == nullptr) return;

    uint8_t  raw[kHeaderBytes + kMaxPayload + kCrcBytes];
    uint8_t *p = raw;
    *p++ = type;
    *p++ = s_stmTxSeq++;
    *p++ = static_cast<uint8_t>(len & 0xFFU);
    *p++ = static_cast<uint8_t>((len >> 8U) & 0xFFU);
    if (len > 0) {
        memcpy(p, payload, len);
        p += len;
    }
    uint16_t crc = EsptoGuition::crc16Ccitt(raw, static_cast<size_t>(p - raw));
    *p++ = static_cast<uint8_t>(crc & 0xFFU);
    *p++ = static_cast<uint8_t>((crc >> 8U) & 0xFFU);
    size_t rawLen = static_cast<size_t>(p - raw);

    uint8_t cobs[sizeof(raw) + sizeof(raw) / 254U + 2U];
    size_t  cobsLen = EsptoGuition::cobsEncode(raw, rawLen, cobs);

    uint8_t wire[sizeof(cobs) + 2U];
    wire[0] = 0x00U;
    memcpy(wire + 1, cobs, cobsLen);
    wire[1U + cobsLen] = 0x00U;

    s_stmSerial->write(wire, cobsLen + 2U);
}

/* ========================================================================== */
/* RX — dispatcher                                                             */
/* ========================================================================== */

static void dispatchFrame(uint8_t type, uint8_t seq,
                          const uint8_t *payload, uint16_t len)
{
    (void)seq;
    switch (type) {
    case kTypePpg: {
        if (len != 2U) return;
        int16_t diff = static_cast<int16_t>(
            static_cast<uint16_t>(payload[0]) |
            (static_cast<uint16_t>(payload[1]) << 8U));
        s_lastPpgFrameMs = millis();
        /* Deferuj forward do nastepnego cyklu STM32data_update(),
         * tak jak w starej wersji ASCII, zeby nie wykonywac write
         * do GUTION w srodku petli odbiorczej STM32. */
        s_ppg_forward_diff = diff;
        s_ppg_forward_pending = true;
        break;
    }
    case kTypeBpm: {
        if (len != 2U) return;
        bpmNumber     = payload[0];
        spo2Number    = payload[1];
        stmDataUpdated = true;
        EsptoGuition::sendBpmStatus(bpmNumber, spo2Number);
        break;
    }
    case kTypeAck: {
        /* optional — log via serial if you need */
        (void)payload;
        break;
    }
    case kTypeRadar: {
        if (len < 8U) return;
        // Defer forward to ZEGAR->GUTION poza ISR-hot path
        s_ld2410_pending.presence  = payload[0];
        s_ld2410_pending.movDist   = static_cast<uint16_t>(payload[1] | (static_cast<uint16_t>(payload[2]) << 8U));
        s_ld2410_pending.movEnergy = payload[3];
        s_ld2410_pending.statDist  = static_cast<uint16_t>(payload[4] | (static_cast<uint16_t>(payload[5]) << 8U));
        s_ld2410_pending.statEnergy= payload[6];
        s_ld2410_pending.detectDist= payload[7];
        ld2410LastFrameMs = millis();
        ld2410DataUpdated = true;
        s_ld2410_forward_pending = true;
        break;
    }
    default:
        break;
    }
}

static void tryDecode(void)
{
    if (s_rx.length == 0U) return;

    uint8_t decoded[256];
    size_t  decLen = EsptoGuition::cobsDecode(
        s_rx.buffer, s_rx.length, decoded);
    if (decLen < (kHeaderBytes + kCrcBytes)) return;

    uint16_t plen = static_cast<uint16_t>(decoded[2]) |
                    (static_cast<uint16_t>(decoded[3]) << 8U);
    if (plen > kMaxPayload) return;
    if (decLen != (kHeaderBytes + plen + kCrcBytes)) return;

    uint16_t receivedCrc = static_cast<uint16_t>(decoded[decLen - 2U]) |
                           (static_cast<uint16_t>(decoded[decLen - 1U]) << 8U);
    uint16_t expectedCrc = EsptoGuition::crc16Ccitt(decoded, decLen - 2U);
    if (receivedCrc != expectedCrc) return;

    dispatchFrame(decoded[0], decoded[1],
                 decoded + kHeaderBytes, plen);
}

static void ingestByte(uint8_t b)
{
    if (b == 0x00U) {
        tryDecode();
        s_rx.length = 0;
    } else if (s_rx.length < sizeof(s_rx.buffer)) {
        s_rx.buffer[s_rx.length++] = b;
    } else {
        s_rx.length = 0;  /* overflow — odrzuć ramkę */
    }
}

/* ========================================================================== */
/* PUBLIC API                                                                  */
/* ========================================================================== */

void STM32data_begin(int rxPin, int txPin, uint32_t baudRate)
{
    /* bufCapacity/isrBufCapacity=512 — ESPSW wzmacnia bufory RX/TX (PPG 20ms
     * + radar ~100ms + ACK). begin() ustawia te bufory, bo v8.x nie ma
     * setRxBufferSize(). */
    s_stmSoftSerial.begin(baudRate, SWSERIAL_8N1, rxPin, txPin, false, 512UL, 512UL);
    s_stmSerial = &s_stmSoftSerial;

    s_rx.length = 0;
    s_rx.lastFrameMs = 0;
    s_lastPpgFrameMs = 0;
    s_stmTxSeq = 0;
    bpmNumber = 0;
    spo2Number = 0;
    stmDataUpdated = false;
    stm32PpgDiff = 0;
    stm32PpgActive = false;
    ld2410Presence = false;
    ld2410MovingDetected = false;
    ld2410MovingDistance = 0;
    ld2410MovingEnergy = 0;
    ld2410StationaryDetected = false;
    ld2410StationaryDistance = 0;
    ld2410StationaryEnergy = 0;
    ld2410DetectionDistance = 0;
    ld2410DataUpdated = false;
    ld2410LastFrameMs = 0;
    s_ld2410_forward_pending = false;
}

void STM32data_update()
{
    if (s_stmSerial == nullptr) return;

    /* Aktywność PPG: ramka w ostatnich 500ms */
    stm32PpgActive = (millis() - s_lastPpgFrameMs) < 500UL;

    /* RX — przetwarzaj wszystkie dostępne bajty */
    while (s_stmSerial->available() > 0) {
        int c = s_stmSerial->read();
        if (c < 0) break;
        ingestByte(static_cast<uint8_t>(c));
    }

    /* Deferred PPG forward do GUTION (poza hot RX path). */
    if (s_ppg_forward_pending) {
        s_ppg_forward_pending = false;
        stm32PpgDiff = s_ppg_forward_diff;
        EsptoGuition::sendPpgImpl(stm32PpgDiff);
    }
    if (s_ld2410_forward_pending) {
        s_ld2410_forward_pending = false;
        ld2410Presence = (s_ld2410_pending.presence & 0x01U) != 0;
        ld2410MovingDetected = (s_ld2410_pending.presence & 0x02U) != 0;
        ld2410MovingDistance = s_ld2410_pending.movDist;
        ld2410MovingEnergy = s_ld2410_pending.movEnergy;
        ld2410StationaryDetected = (s_ld2410_pending.presence & 0x04U) != 0;
        ld2410StationaryDistance = s_ld2410_pending.statDist;
        ld2410StationaryEnergy = s_ld2410_pending.statEnergy;
        ld2410DetectionDistance = s_ld2410_pending.detectDist;
        // Forward do GUTION (COBS) - nie blokuje RX
        EsptoGuition::sendRadarStatus(
            s_ld2410_pending.presence,
            s_ld2410_pending.movDist, s_ld2410_pending.movEnergy,
            s_ld2410_pending.statDist, s_ld2410_pending.statEnergy,
            s_ld2410_pending.detectDist);
    }
}

void STM32data_sendBrightness(uint8_t percent)
{
    if (percent > 100U) percent = 100U;
    uint8_t payload = percent;
    /* Wysylamy 3 kopie dla niezawodnosci. STM32 po kazdej ustawia
     * ten sam PWM, wiec duplikaty sa nieszkodliwe. */
    for (int i = 0; i < 3; ++i) {
        sendFrame(kTypeBrightness, &payload, 1U);
    }
}
