# Raport z Audytu Stabilności i Zarządzania Pamięcią — ZEGAR-ESP32

Ten raport zawiera analizę krytycznych zagrożeń dla stabilności oprogramowania układowego (firmware), w tym wycieków pamięci, hazardów współbieżności i problemów z bezpieczeństwem stosu.

## Podsumowanie Ryzyka

| Nazwa Problemu | Ryzyko | Opis Problemu | Dlaczego jest podstępny? | Proponowany Fix/Diff |
| :--- | :--- | :--- | :--- | :--- |
| **Przepełnienie Stosu TLS (MQTT)** | **KRYTYCZNE** | Task `mqttConn` ma tylko 4096 bajtów stosu. Handshake TLS (WiFiClientSecure) na ESP32 wymaga często **16KB+**. | Działa w 90% przypadków. Wywala system losowo tylko przy specyficznych certyfikatach lub zmianie szyfrowania przez brokera. | `TaskConfig.h`: Zwiększ `kStackBytes` dla MQTT do `16384`. |
| **Stack Bloat w Transporcie UART** | **WYSOKIE** | `sendRawFrame` alokuje ~1KB lokalnych buforów na stosie. Wywołanie z `EncoderTask` (2KB stosu) zostawia <500B na przerwania. | Powoduje ciche nadpisywanie pamięci sąsiednich zadań. Zamiast czystego crasha, nagle zmieniają się wartości zmiennych w innym module. | Przenieś `frame` i `cobsBuffer` do `static` lub na stertę (DMA). |
| **Race Condition Stanu Globalnego** | **WYSOKIE** | `appState`, `editState` i `radioMode` są modyfikowane na Core 1 i czytane na Core 0 bez bariery pamięciowej. | Rdzeń 0 może odczytać "pół-zmienioną" wartość lub nieprawidłowy enum w trakcie przełączania, co prowadzi do HardFault. | Użyj `std::atomic<AppState>` zamiast zwykłego enuma. |
| **Ciche Porzucanie Ramek (UART)** | **ŚREDNIE** | `sendRawFrame` porzuca pakiet bez logu, jeśli bufor TX jest pełny. Brak mechanizmu ponowienia (retry). | Ustawienia lub czas po prostu "nie docierają" do ekranu, a deweloper nie wie dlaczego (brak błędów w logach). | Dodaj `RingBuffer` dla TX lub pętlę retry z timeoutem 1ms. |
| **Fragmentacja Stringów (Meteo)** | **ŚREDNIE** | Budowanie URL w `meteoSync.cpp` przez operator `+` na obiektach `String` tworzy mnwo małych alokacji. | Po kilku tygodniach pracy sterta jest tak "dziurawa", że alokacja dużej ramki dla SSL/TLS zawodzi mimo wolnej pamięci. | Zastąp `String` przez `snprintf` i stały bufor `char[256]`. |
| **Błędne Koło Recovery I2C** | **NISKIE** | `recoverBus()` nie woła `Wire.end()` przed togglowaniem pinów. Hardware I2C może zostać w stanie błędu. | Recovery odblokowuje Slave'a, ale kontroler ESP32 I2C zostaje zawieszony wewnętrznie do czasu twardego restartu. | Dodaj `Wire.end()` na początku i `Wire.begin()` na końcu `recoverBus()`. |
| **Przepełnienie JSON (MQTT)** | **ŚREDNIE** | `MQTT_JSON_DOC_CAPACITY` (384) jest na styku. Rozszerzenie danych o pogodzie spowoduje ciche obcięcie JSONa. | Dane pogodowe przestają się aktualizować mimo statusu 200 OK, bo parser ArduinoJson zwraca błąd `NoMemory`. | Zwiększ pojemność do `768` bajtów. |

---

## Szczegóły Techniczne i Poprawki

### 1. Bezpieczeństwo Stosu TLS
W pliku `include/config/TaskConfig.h` należy zwiększyć zasoby dla zadań sieciowych.
```diff
 namespace BtAppTask {
-  constexpr size_t kStackBytes = 8192;
+  constexpr size_t kStackBytes = 16384; 
 }
```

### 2. Eliminacja Stack Pressure w UART
W pliku `src/comms/esp_to_gution/EsptoGuitionTransport.cpp`:
```diff
 void sendRawFrame(uint8_t type, uint8_t sequence, const uint8_t *payload, uint16_t payloadLength) {
-  uint8_t frame[kFrameHeaderBytes + kMaxPayloadBytes + kFrameCrcBytes] = {};
+  static uint8_t frame[kFrameHeaderBytes + kMaxPayloadBytes + kFrameCrcBytes]; 
+  memset(frame, 0, sizeof(frame));
```
*Uwaga: Użycie `static` jest bezpieczne, jeśli funkcja nie jest reentrantna (wywoływana z wielu tasków jednocześnie).*

### 3. Synchronizacja Stanów Między Rdzeniami
W pliku `include/core/app/AppState.h`:
```diff
-#include <atomic>
-extern AppState appState;
+extern std::atomic<AppState> appState;
```

---

## Wnioski Audytu
Głównym problemem systemu nie są wycieki pamięci (leaks), ale **"Stack Fragility"** (kruchość stosu). Wiele zadań działa na granicy swoich limitów. Kombinacja SSL (MQTT) oraz głębokiego parsowania JSON stwarza wysokie ryzyko kolizji stosu ze stertą. Wdrożenie powyższych poprawek krytycznie zwiększy MTBF (Mean Time Between Failures) urządzenia.
