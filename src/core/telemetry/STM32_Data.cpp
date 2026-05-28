#include "STM32_Data.h"
#include "comms/esp_to_gution/Esptogution.h"

// Global variables
int bpmNumber = 0;
int spo2Number = 0;
bool stmDataUpdated = false;
int16_t stm32PpgDiff = 0;
bool stm32PpgActive = false;

// (opcjonalne) statystyki / info
unsigned long stmLastReceivedMs = 0;
unsigned long stmFramesReceived = 0;

// Wskazanie potrzebnego portu (ustawiany w begin)
static Stream *stmSerial = nullptr;
static EspSoftwareSerial::UART *stmSoftwareSerial = nullptr;

// --- Parser tekstowy BPM:xxx,SPO2:yyy ---
static const size_t RECV_BUF_SIZE = 128;
static char recvBuf[RECV_BUF_SIZE];
static size_t recvIndex = 0;

// Timeout na niedokończoną ramkę (jeśli ciąg znaków jest przerywany długo)
static const unsigned long FRAME_TIMEOUT_MS = 1500;
static unsigned long lastByteReceiveMs = 0;

// --- Parser binarny PPG: 0xAA + int16 LE + 0x0A ---
static const uint8_t PPG_FRAME_HEAD = 0xAA;
static const uint8_t PPG_FRAME_TAIL = 0x0A;
static bool s_ppg_frame_started = false;
static uint8_t s_ppg_frame_buf[2];
static uint8_t s_ppg_frame_index = 0;
static unsigned long s_ppg_last_diff_ms = 0;
static bool s_ppg_forward_pending = false;

// Funkcja inicjualizujaca wskazany UART
// Wybór obywa sie dzięki HardwareSerial & serialPort który przekazuje port jako referencje 
// Dzięki takiemu rozwiąznia nie musimy dawać np Serial2 na stałe 
// Wskazanie referencji zajmuje sie znak [&]
void STM32data_begin(int rxPin, int txPin, uint32_t baudRate) {
    static EspSoftwareSerial::UART serial(rxPin, txPin);
    stmSoftwareSerial = &serial;
    stmSerial = stmSoftwareSerial;
    stmSoftwareSerial->begin(baudRate, SWSERIAL_8N1);
    // zerowanie bufora
    recvIndex = 0;
    recvBuf[0] = '\0';
    stmLastReceivedMs = 0;
    stmFramesReceived = 0;
    stmDataUpdated = false;
}

void STM32data_update() {
    if (!stmSerial) return;

    // Timeout na fragment tekstowy
    if (recvIndex > 0 && (millis() - lastByteReceiveMs) > FRAME_TIMEOUT_MS) {
        recvIndex = 0;
        recvBuf[0] = '\0';
    }

    // PPG aktywny jeśli ramka była w ostatnich 500ms
    stm32PpgActive = (millis() - s_ppg_last_diff_ms < 500UL);

    // Forward pending PPG diff z poprzedniego cyklu (jeśli sendRawFrame deferred)
    if (s_ppg_forward_pending) {
        s_ppg_forward_pending = false;
        EsptoGuition::sendPpgImpl(stm32PpgDiff);
    }

    while (stmSerial->available() > 0) {
        int cInt = stmSerial->read();
        if (cInt < 0) break;
        uint8_t c = (uint8_t)cInt;

        lastByteReceiveMs = millis();

        // --- Parser binarny PPG (priorytet) ---
        if (s_ppg_frame_started) {
            if (s_ppg_frame_index < 2) {
                s_ppg_frame_buf[s_ppg_frame_index++] = c;
                continue;
            }
            if (c == PPG_FRAME_TAIL && s_ppg_frame_index == 2) {
                // Kompletna ramka: 0xAA + lo + hi + 0x0A
                stm32PpgDiff = (int16_t)(s_ppg_frame_buf[0] | (s_ppg_frame_buf[1] << 8));
                s_ppg_frame_started = false;
                s_ppg_frame_index = 0;
                s_ppg_last_diff_ms = millis();

                // Forward deferred to next cycle — avoids UART write
                // from the receive hot path (single send per frame).
                s_ppg_forward_pending = true;

                stmFramesReceived++;
                stmLastReceivedMs = s_ppg_last_diff_ms;
                continue; // pomiń parser tekstowy dla tych bajtów
            }
            // Błąd ramki — reset
            s_ppg_frame_started = false;
            s_ppg_frame_index = 0;
        }

        if (c == PPG_FRAME_HEAD) {
            s_ppg_frame_started = true;
            s_ppg_frame_index = 0;
            continue;
        }

        // --- Parser tekstowy BPM:xxx,SPO2:yyy (tylko dla bajtów spoza ramki binarnej) ---
        if (c == '\n') {
            if (recvIndex >= RECV_BUF_SIZE) recvIndex = RECV_BUF_SIZE - 1;
            recvBuf[recvIndex] = '\0';

            while (recvIndex > 0 && (recvBuf[recvIndex - 1] == '\r' || recvBuf[recvIndex - 1] == ' ')) {
                recvIndex--;
                recvBuf[recvIndex] = '\0';
            }

            char *commaPtr = strchr(recvBuf, ',');
            if (commaPtr != NULL) {
                *commaPtr = '\0';
                char *bpmPart = recvBuf;
                char *spo2Part = commaPtr + 1;

                char *colonB = strchr(bpmPart, ':');
                char *colonS = strchr(spo2Part, ':');

                if (colonB != NULL && colonS != NULL) {
                    int parsedBpm = atoi(colonB + 1);
                    int parsedSpo2 = atoi(colonS + 1);

                    if (parsedBpm >= 0 && parsedBpm <= 300 && parsedSpo2 >= 0 && parsedSpo2 <= 100) {
                        bpmNumber = parsedBpm;
                        spo2Number = parsedSpo2;
                        stmDataUpdated = true;

                        // Forward BPM/SpO2 do Gution natychmiast (heartbeat rate)
                        EsptoGuition::sendBpmStatus(parsedBpm, parsedSpo2);
                    }
                }
            }

            recvIndex = 0;
            recvBuf[0] = '\0';
        } else {
            if (recvIndex < (RECV_BUF_SIZE - 1)) {
                recvBuf[recvIndex++] = (char)c;
                recvBuf[recvIndex] = '\0';
            } else {
                recvIndex = 0;
                recvBuf[0] = '\0';
            }
        }
    }
}
