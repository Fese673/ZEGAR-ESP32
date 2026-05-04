#include "STM32_Data.h"

// Global variables
int bpmNumber = 0;
int spo2Number = 0;
bool stmDataUpdated = false;

// (opcjonalne) statystyki / info
unsigned long stmLastReceivedMs = 0;
unsigned long stmFramesReceived = 0;

// Wskazanie potrzebnego portu (ustawiany w begin)
static Stream *stmSerial = nullptr;
static EspSoftwareSerial::UART *stmSoftwareSerial = nullptr;

// Bufor do składania przychodzącej linii (bez dynamicznego String)
static const size_t RECV_BUF_SIZE = 128;
static char recvBuf[RECV_BUF_SIZE];
static size_t recvIndex = 0;

// Timeout na niedokończoną ramkę (jeśli ciąg znaków jest przerywany długo)
static const unsigned long FRAME_TIMEOUT_MS = 1500;
static unsigned long lastByteReceiveMs = 0;

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

// Rzeczywista funkcja bibloteki
// Odczyt odebranych danych z formy tekstowej
// I wyodrębnienie ich do zmiennych które będziemy mogli
// Użyć do wyświetlenia na segmentowych modułach 
// Funkcja non-blocking wywoływana w loop()
void STM32data_update() {
    if (!stmSerial) return; // UART nie ustawiony

    // 1) Jeśli mamy w buforze jakiś fragment i minął timeout - porzuć fragment
    if (recvIndex > 0) {
        if ((millis() - lastByteReceiveMs) > FRAME_TIMEOUT_MS) {
            // Porzucamy częściową ramkę (bez blokowania)
            recvIndex = 0;
            recvBuf[0] = '\0';
        }
    }

    // Odczytaj wszystkie dostępne bajty (nie czekamy)
    while (stmSerial->available() > 0) {
        int cInt = stmSerial->read(); // zwraca -1 jeśli brak, inaczej 0..255
        if (cInt < 0) break;
        char c = (char)cInt;

        lastByteReceiveMs = millis(); // aktualizujemy czas ostatniego bajtu

        // Jeżeli mamy koniec linii -> parsuj
        if (c == '\n') {
            // Zakończ bufor poprawnym terminatorem C-string
            if (recvIndex >= RECV_BUF_SIZE) recvIndex = RECV_BUF_SIZE - 1;
            recvBuf[recvIndex] = '\0';

            // Usuń spacje/CR z końca i początku (prosta trim)
            // Trim right (usuń \r i spacje z końca)
            while (recvIndex > 0 && (recvBuf[recvIndex - 1] == '\r' || recvBuf[recvIndex - 1] == ' ')) {
                recvIndex--;
                recvBuf[recvIndex] = '\0';
            }

            // Teraz mamy pełną linię w recvBuf
            // Parsowanie bez użycia String (łatwe i szybkie)
            // Oczekujemy formatu: BPM:64,SPO2:97
            char *commaPtr = strchr(recvBuf, ',');
            if (commaPtr != NULL) {
                // rozdzielenie na dwie części - zastępujemy przecinek '\0'
                *commaPtr = '\0';
                char *bpmPart = recvBuf;         // "BPM:64"
                char *spo2Part = commaPtr + 1;   // "SPO2:97"

                // znajdź ':'
                char *colonB = strchr(bpmPart, ':');
                char *colonS = strchr(spo2Part, ':');

                if (colonB != NULL && colonS != NULL) {
                    // przesuwamy wskazanie na tekst liczby
                    char *bpmValStr = colonB + 1;
                    char *spo2ValStr = colonS + 1;

                    // prosta konwersja na int (atoi jest OK tutaj)
                    int parsedBpm = atoi(bpmValStr);
                    int parsedSpo2 = atoi(spo2ValStr);

                    // dodatkowa walidacja (opcjonalnie): zakresy sensowne
                    if (parsedBpm >= 0 && parsedBpm <= 300 && parsedSpo2 >= 0 && parsedSpo2 <= 100) {
                        bpmNumber = parsedBpm;
                        spo2Number = parsedSpo2;

                        // oznaczamy, że mamy nowe dane
                        stmDataUpdated = true;

                        // statystyki
                        stmFramesReceived++;
                        stmLastReceivedMs = millis();
                    }
                }
            }

            // wyczyść bufor do następnej ramki
            recvIndex = 0;
            recvBuf[0] = '\0';
        } else {
            // 4) normalny bajt — dodaj do bufora, ale pilnuj rozmiaru
            if (recvIndex < (RECV_BUF_SIZE - 1)) {
                recvBuf[recvIndex++] = c;
                recvBuf[recvIndex] = '\0';
            } else {
                // bufor przepełniony — porzuć całość (albo możesz przesunąć, ale proste porzucenie)
                recvIndex = 0;
                recvBuf[0] = '\0';
            }
        }
    } // koniec while available
}
// wersję non-blocking
