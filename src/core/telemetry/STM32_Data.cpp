/*
 * STM32_Data.cpp — odbiór/nadawanie ramek COBS+CRC16 do STM32.
 *
 * Kanał: EspSoftwareSerial (plikowe oprogramowanie UART) GPIO16/17 @ 19200 baud.
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
static uint8_t s_ld2410_logged_presence = 0xFF;  // ostatni zalogowany stan (Serial)
static uint32_t s_ld2410_logged_ms = 0;          // kiedy ostatni log (powtorka co 10 s)
struct Ld2410Pending {
    uint8_t presence;
    uint16_t movDist;
    uint8_t movEnergy;
    uint16_t statDist;
    uint8_t statEnergy;
    uint16_t detectDist;
    uint8_t movGates[9];
    uint8_t statGates[9];
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
// STEPPER_BRIDGE: 0xB1 ZEGAR->STM32 komenda steppera (kontrakt:
// include/stepper/stepper_frames.h). 0xB2/0xB3 lecą w drugą stronę
// i lądują w hooku poniżej — istniejące typy nietknięte.
static constexpr uint8_t kTypeStepperCmd = 0xB1;
static STM32StepperHook_t s_stepperHook = nullptr;  // STEPPER_BRIDGE

static constexpr uint16_t kMaxPayload   = 128;
static constexpr uint8_t  kHeaderBytes  = 4;
static constexpr uint8_t  kCrcBytes     = 2;

/*--- Diagnostyka linku STM32 (1 s na USB Serial). Przelacznik:
 * -DSTM32_LINK_DIAG=1 w platformio.ini; 0 = logi wylaczone. ---*/
#ifndef STM32_LINK_DIAG
#define STM32_LINK_DIAG 1
#endif
#if STM32_LINK_DIAG
static uint32_t s_diag_rxBytes   = 0;  // bajty odebrane w oknie
static uint32_t s_diag_framesOk  = 0;  // ramki poprawne (COBS+len+CRC)
static uint32_t s_diag_framesBad = 0;  // ramki odrzucone (COBS/len/CRC)
static uint32_t s_diag_ovf       = 0;  // przepelnienia bufora soft-UART
static uint32_t s_diag_ppg       = 0;  // ramki 0xA0
static uint32_t s_diag_bpm       = 0;  // ramki 0xA1
static uint32_t s_diag_radar     = 0;  // ramki 0xA3
static uint32_t s_diag_other     = 0;  // pozostale typy (hook itp.)
static uint32_t s_lastBpmFrameMs = 0;  // znacznik ostatniej 0xA1 (0 = brak)
#endif

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
#if STM32_LINK_DIAG
        ++s_diag_ppg;
#endif
        s_lastPpgFrameMs = millis();
        /* Heartbeat linku: kazda ramka PPG znaczy "STM32 zyje". Bez tego
         * ekran debug pokazywal OFFLINE mimo dzialajacego linku
         * (stmDataUpdated ustawiala tylko 0xA1, a ta leci wylacznie
         * przy wykrytym uderzeniu). */
        stmDataUpdated = true;
        /* Deferuj forward do nastepnego cyklu STM32data_update(),
         * tak jak w starej wersji ASCII, zeby nie wykonywac write
         * do GUTION w srodku petli odbiorczej STM32. */
        s_ppg_forward_diff = diff;
        s_ppg_forward_pending = true;
        break;
    }
    case kTypeBpm: {
        if (len != 2U) return;
#if STM32_LINK_DIAG
        ++s_diag_bpm;
        s_lastBpmFrameMs = millis();
#endif
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
#if STM32_LINK_DIAG
        ++s_diag_radar;
#endif
        // Defer forward to ZEGAR->GUTION poza ISR-hot path
        s_ld2410_pending.presence  = payload[0];
        s_ld2410_pending.movDist   = static_cast<uint16_t>(payload[1] | (static_cast<uint16_t>(payload[2]) << 8U));
        s_ld2410_pending.movEnergy = payload[3];
        s_ld2410_pending.statDist  = static_cast<uint16_t>(payload[4] | (static_cast<uint16_t>(payload[5]) << 8U));
        s_ld2410_pending.statEnergy= payload[6];
        s_ld2410_pending.detectDist= payload[7]; // legacy dolny bajt (<=255 cm)
        // Bramki (26 B od nowego STM32); stary 8 B -> zera.
        if (len >= 26U) {
            memcpy(s_ld2410_pending.movGates, payload + 8, 9);
            memcpy(s_ld2410_pending.statGates, payload + 17, 9);
        } else {
            memset(s_ld2410_pending.movGates, 0, 9);
            memset(s_ld2410_pending.statGates, 0, 9);
        }
        // Pelny detectDist u16 LE (28 B, append-only po 26 B); zasięg LD2410C
        // do 600 cm nie miesci sie w [7] (zawijal sie mod 256).
        if (len >= 28U) {
            s_ld2410_pending.detectDist = static_cast<uint16_t>(
                payload[26] | (static_cast<uint16_t>(payload[27]) << 8U));
        }
        ld2410LastFrameMs = millis();
        ld2410DataUpdated = true;
        s_ld2410_forward_pending = true;
        break;
    }
    default:
#if STM32_LINK_DIAG
        ++s_diag_other;
#endif
        // STEPPER_BRIDGE: nieznane typy (0xB2/0xB3 steppera) do hooka mostu.
        // Bez hooka zachowanie jak wcześniej (ignoruj).
        if (s_stepperHook != nullptr) {
            s_stepperHook(type, payload, len);
        }
        break;
    }
}

static void tryDecode(void)
{
    if (s_rx.length == 0U) return;

    uint8_t decoded[256];
    size_t  decLen = EsptoGuition::cobsDecode(
        s_rx.buffer, s_rx.length, decoded);
    if (decLen < (kHeaderBytes + kCrcBytes)) {
#if STM32_LINK_DIAG
        ++s_diag_framesBad;
#endif
        return;
    }

    uint16_t plen = static_cast<uint16_t>(decoded[2]) |
                    (static_cast<uint16_t>(decoded[3]) << 8U);
    if (plen > kMaxPayload) {
#if STM32_LINK_DIAG
        ++s_diag_framesBad;
#endif
        return;
    }
    if (decLen != (kHeaderBytes + plen + kCrcBytes)) {
#if STM32_LINK_DIAG
        ++s_diag_framesBad;
#endif
        return;
    }

    uint16_t receivedCrc = static_cast<uint16_t>(decoded[decLen - 2U]) |
                           (static_cast<uint16_t>(decoded[decLen - 1U]) << 8U);
    uint16_t expectedCrc = EsptoGuition::crc16Ccitt(decoded, decLen - 2U);
    if (receivedCrc != expectedCrc) {
#if STM32_LINK_DIAG
        ++s_diag_framesBad;
#endif
        return;
    }

#if STM32_LINK_DIAG
    ++s_diag_framesOk;
#endif
    dispatchFrame(decoded[0], decoded[1],
                 decoded + kHeaderBytes, plen);
}

static void ingestByte(uint8_t b)
{
#if STM32_LINK_DIAG
    ++s_diag_rxBytes;
#endif
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
    s_stepperHook = nullptr;  // STEPPER_BRIDGE (most rejestruje się w begin())
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
        // Podglad na USB-Serial: zmiana stanu zawsze; martwe lacze (b7)
        // przypomina co 10 s; zywa obecnosc bez zmian nie spamuje.
        uint32_t nowMs = millis();
        bool linkBad = (s_ld2410_pending.presence & 0x80U) != 0;
        if ((s_ld2410_pending.presence != s_ld2410_logged_presence) ||
            (linkBad && ((nowMs - s_ld2410_logged_ms) >= 10000UL))) {
            s_ld2410_logged_presence = s_ld2410_pending.presence;
            s_ld2410_logged_ms = nowMs;
            if ((s_ld2410_pending.presence & 0x80U) != 0) {
                if ((s_ld2410_pending.presence & 0x40U) != 0) {
                    Serial.println("[RADAR][W] LINK-GARBAGE (cos gada na PA3, brak ramek: zly baud? TX/RX?)");
                } else {
                    Serial.println("[RADAR][W] LINK-BAD (cisza na PA3: zasilanie 5V? TX radaru?)");
                }
            } else {
                Serial.printf("[RADAR][I] P=%c%c%c mov=%ucm/%u stat=%ucm/%u\r\n",
                    ld2410Presence ? 'P' : '-',
                    ld2410MovingDetected ? 'M' : '-',
                    ld2410StationaryDetected ? 'S' : '-',
                    ld2410MovingDistance, ld2410MovingEnergy,
                    ld2410StationaryDistance, ld2410StationaryEnergy);
            }
        }
        // Forward do GUTION (COBS) - nie blokuje RX.
        // Maska 0x8F: bity 0-2 obecnosc + b3 OUT + b7 LINK-BAD. Guition
        // uzywa b7 do statusu OFFLINE/"---" (HMI nie moze zamarznac na
        // starych danych, a ramka przycisku nie swieci zielono bez linku).
        // b6 (GARBAGE) zostaje lokalnie - HMI go nie potrzebuje.
        EsptoGuition::sendRadarStatus(
            static_cast<uint8_t>(s_ld2410_pending.presence & 0x8FU),
            s_ld2410_pending.movDist, s_ld2410_pending.movEnergy,
            s_ld2410_pending.statDist, s_ld2410_pending.statEnergy,
            s_ld2410_pending.detectDist,
            s_ld2410_pending.movGates, s_ld2410_pending.statGates);
    }

#if STM32_LINK_DIAG
    /* Link STM32 co 1 s: rx/ok/bad/ovf + liczniki typow + wiek ramek.
     * ovf = flaga przepelnienia bufora soft-UART (czyszczona odczytem). */
    {
        static uint32_t diagLastMs = 0;
        const uint32_t nowDiagMs = millis();
        if ((nowDiagMs - diagLastMs) >= 1000U) {
            diagLastMs = nowDiagMs;
            if (s_stmSoftSerial.overflow()) ++s_diag_ovf;
            Serial.printf("[STMDIAG] rx=%luB ok=%lu bad=%lu ovf=%lu | ppg=%lu bpm=%lu radar=%lu oth=%lu | agePpg=",
                          (unsigned long)s_diag_rxBytes,
                          (unsigned long)s_diag_framesOk,
                          (unsigned long)s_diag_framesBad,
                          (unsigned long)s_diag_ovf,
                          (unsigned long)s_diag_ppg,
                          (unsigned long)s_diag_bpm,
                          (unsigned long)s_diag_radar,
                          (unsigned long)s_diag_other);
            if (s_lastPpgFrameMs == 0U) Serial.print(F("none"));
            else Serial.printf("%lums", (unsigned long)(nowDiagMs - s_lastPpgFrameMs));
            Serial.print(F(" ageBpm="));
            if (s_lastBpmFrameMs == 0U) Serial.print(F("none"));
            else Serial.printf("%lums", (unsigned long)(nowDiagMs - s_lastBpmFrameMs));
            Serial.println();

            s_diag_rxBytes = 0; s_diag_framesOk = 0; s_diag_framesBad = 0;
            s_diag_ovf = 0; s_diag_ppg = 0; s_diag_bpm = 0;
            s_diag_radar = 0; s_diag_other = 0;
        }
    }
#endif
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

// STEPPER_BRIDGE: nadawca + hook (implementacja — deklaracje w STM32_Data.h).
void STM32data_setStepperHook(STM32StepperHook_t hook)
{
    s_stepperHook = hook;
}

bool STM32data_sendStepperCmd(const uint8_t *payload, uint16_t len)
{
    if (s_stmSerial == nullptr) return false;
    sendFrame(kTypeStepperCmd, payload, len);
    return true;
}
