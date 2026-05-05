# Komunikacja ESP32 → Guition (ESP to Guition)

Dokumentacja techniczna protokołu komunikacyjnego dwukierunkowej transmisji danych sensorycznych i sterujących między ESP32 (ZEGAR) a Guition (STM32 ekran).

**Wersja**: 1.0  
**Ostatnia aktualizacja**: 2026-05-05

---

## Spis Treści

1. [Architektura Ogólna](#1-architektura-ogólna)
2. [Protokół Ramkowy](#2-protokół-ramkowy)
3. [Parametry Konfigurowalne](#3-parametry-konfigurowalne)
4. [Logika Wysyłania](#4-logika-wysyłania)
5. [Stany Synchronizacji](#5-stany-synchronizacji)
6. [Obsługa Błędów](#6-obsługa-błędów)
7. [Payloady](#7-payloady)
8. [Tryby Pracy](#8-tryby-pracy)

---

## 1. Architektura Ogólna

### 1.1 Warstwy

```
┌─────────────────────────────────────────────────────────────┐
│                    APLIKACJA (ESP32)                       │
├─────────────────────────────────────────────────────────────┤
│  EsptoGuitionState      │ Warstwa danych i sensorów        │
│  - buildWeatherPayload  │ - odczyt z BMP280/ENS160         │
│  - buildPmsPayload     │ - budowanie payloadów             │
│  - buildTimePayload    │ - obsługa ustawień z Guition     │
├─────────────────────────────────────────────────────────────┤
│  EsptoGuitionTransport  │ Warstwa transportowa             │
│  - sendRawFrame        │ - serial write (non-blocking)    │
│  - ingestSerialBytes   │ - serial read (non-blocking)        │
│  - handleFrame       │ - dispatch ramki               │
├─────────────────────────────────────────────────────────────┤
│  EsptoGuitionCobs       │ Warstwa kodowania               │
│  - cobsEncode        │ - kodowanie COBS               │
│  - cobsDecode      │ - dekodowanie COBS             │
│  - crc16Ccitt     │ - suma kontrolna CRC16         │
└─────────────────────────────────────────────────────────────┘
                              │
                    HardwareSerial (UART)
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    GUITION (STM32)                          │
└─────────────────────────────────────────────────────────────┘
```

### 1.2 Przepływ Danych

```
ODBIÓR (Guition → ESP32):
  Serial.available() → while available → cobsDecode → CRC check → handleFrame

WYSYŁANIE (ESP32 → Guition):
  broadcastSnapshots() → Reactive check → build payload → CRC → COBS → Serial.write()
```

### 1.3 Model Non-Blocking

- **Odbiór**: Pętla `while(serial.available())` - czyta co jest w buforze, nie czeka
- **Wysyłanie**: Sprawdzanie zmian przez `millis()`, brak `delay()`
- **ACK**: Wysyłane asynchronicznie, nie blokuje na odpowiedź

---

## 2. Protokół Ramkowy

### 2.1 Format Ramki

```
┌────────┬─────��──────┬─────────┬─────────────┬───────────┐
│  Type  │ Sequence │ Length  │  Payload  │   CRC    │
│  1 B   │   1 B    │  2 B    │  N Bytes  │  2 B     │
└────────┴────────────┴─────────┴─────────────┴───────────┘
 ↑                               │           ▲
 │                               │           │
 │      COBS framing ─────── 0x00 ──────────┘
```

### 2.2 Typy Ramkek

| Typ (Hex) | Nazwa | Kierunek | Opis |
|----------|-------|----------|------|
| 0x01 | `kTypeHello` | → | Guition się zgłasza po starcie |
| 0x02 | `kTypeHelloAck` | ← | ESP odpowiada HELLO z bootId |
| 0x10 | `kTypeWeather` | ← | Dane pogodowe (temp humid pressure) |
| 0x11 | `kTypePms` | ← | Dane pyłów (PM1/2.5/10 + liczniki) |
| 0x12 | `kTypeTime` | ← | Znacznik czasu Unix |
| 0x13 | `kTypeSystemResources` | ← | RAM CPU Flash audio stats |
| 0x20 | `kTypeSettings` | ← | Ustawienia (buzzer MQTT touch music PMS) |
| 0x30 | `kTypeSetSettings` | → | Guition zmienia ustawienia |
| 0x40 | `kTypeRequest` | → | Guition request konkretnego typu |
| 0x50 | `kTypeAck` | ↔ | Potwierdzenie (ok / bad type / length / crc) |

### 2.3 Kodowanie COBS

- Znak delimitera: `0x00`
- Kodowanie: każdy blok poprzedzony offsetem do następnego `0x00`
- Ramka zawsze zaczyna się i kończy `0x00`

### 2.4 CRC16 CCITT

- Funkcja: `crc16Ccitt(buffer, length)`
- Wielomian: `0x1021`
- Inicjalizacja: `0xFFFF`
- Suma liczona na polu `Type + Sequence + Length + Payload` (przed CRC)

---

## 3. Parametry Konfigurowalne

### 3.1 Interwały Czasowe

```cpp
// Plik: Esptogution.cpp

// --- Interwał BROADCASTU ---
// Jak często wysyłać keepalive (nawet bez zmiany danych)
CONSTEXPR unsigned long KEEPALIVE_INTERVAL_MS = 30000UL;  // 30 sekund

// --- Interwał SAFETY REFRESH ---
// Pełny refresh wszystkich danych (resync stanu)
CONSTEXPR unsigned long SAFETY_REFRESH_INTERVAL_MS = 300000UL;  // 5 minut

// --- Interwał RESOURCES CHECK ---
// Sprawdzanie zmiany zasobów systemowych
CONSTEXPR unsigned long RESOURCES_CHECK_INTERVAL_MS = 2000UL;  // 2 sekundy
```

### 3.2 Progi Histerezy (Reactive Triggers)

**ZASADA**: Jeśli dane są inne niż poprzednie → wyślij. Dowolna zmiana = wysłanie.

Brak progów procentowych ani minimalnych wartości. Każda nowa wartość = istotna zmiana.

```cpp
// Plik: Esptogution.cpp

// --- WEATHER ---
// Wyslij jesli ktokolwiek z: temp, humid, pressure jest INNY niz poprzednio
bool changed = (currentWeather.temperatureCx100 != s_lastSentWeather.temperatureCx100) ||
               (currentWeather.humidityPctX100 != s_lastSentWeather.humidityPctX100) ||
               (currentWeather.pressureHpaX10 != s_lastSentWeather.pressureHpaX10);

// --- PMS ---
// Wyslij jesli ktokolwiek z: pm01, pm25, pm10 jest INNY niz poprzednio
bool changed = (currentPms.pm01 != s_lastSentPms.pm01) ||
              (currentPms.pm25 != s_lastSentPms.pm25) ||
              (currentPms.pm10 != s_lastSentPms.pm10);

// --- TIME ---
// Wyslij jesli sekunda sie zmienila
bool changed = (currentTime.unixSeconds != s_lastTimeSentS);

// --- SYSTEM RESOURCES ---
// Sprawdzaj co 2s, wyslij jesli cokolwiek sie zmienilo
bool changed = (currentRes.core0Cpu != s_lastSentResources.core0Cpu) ||
              (currentRes.core1Cpu != s_lastSentResources.core1Cpu) ||
              (currentRes.freeRam != s_lastSentResources.freeRam);
```

> **UWAGA**: Stare progi zostały usunięte (`abs() > threshold`). Teraz każda zmiana wartości powoduje wysłanie.

### 3.3 Timeout'y i Limity

```cpp
// Plik: EsptoGuitionTransport.cpp

// Timeout ramki (ile ms czekać na pełną ramkę)
CONSTEXPR unsigned long FRAME_TIMEOUT_MS = 1000UL;  // 1 sekunda

// Maksymalny rozmiar payloadu
CONSTEXPR uint16_t MAX_PAYLOAD_BYTES = 240;

// Rozmiar bufora RX
CONSTEXPR size_t RX_BUFFER_SIZE = 256;
```

### 3.4 Zakresy Danych Losowych (Synthetic Mode)

```cpp
// Plik: EsptoGuitionState.cpp

// Temperatura: -40°C do +38°C (x100)
SYNTH_MIN_TEMP_X100 = -4000;   // -40.00°C
SYNTH_MAX_TEMP_X100 = 3800;    // +38.00°C

// Wilgotność: 20% do 90% (x100)
SYNTH_MIN_HUMID_X100 = 2000;    // 20%
SYNTH_MAX_HUMID_X100 = 9000;    // 90%

// Ciśnienie: 980 hPa do 1046 hPa (x10)
SYNTH_MIN_PRESSURE_X10 = 9800;   // 980 hPa
SYNTH_MAX_PRESSURE_X10 = 10460;  // 1046 hPa

// PM1.0: 0 do 85 µg/m³
SYNTH_PM01_MAX = 85;

// PM2.5: 0 do 150 µg/m³
SYNTH_PM25_MAX = 150;

// PM10: 0 do 180 µg/m³
SYNTH_PM10_MAX = 180;

// Sample age (kiedy dane zostały zebrane): 0 do 120 sekund
SYNTH_SAMPLE_AGE_MS_MAX = 120000;
```

---

## 4. Logika Wysyłania

### 4.1 Broadcast Snapshots (Główna Pętla)

```
┌─────────────────────────────────────────┐
│        broadcastSnapshots(nowMs)          │
├─────────────────────────────────────────┤
│ 1. Check WEATHER changed?              │
│    IF (temp Δ > threshold OR            │
│        humid Δ > threshold OR          │
│        pressure Δ > threshold)        │
│    THEN sendWeather() + update cache   │
├─────────────────────────────────────────┤
│ 2. Check PMS changed?                   │
│    IF (pm01 Δ > threshold OR           │
│        pm25 Δ > threshold OR           │
│        pm10 Δ > threshold)            │
│    THEN sendPms() + update cache        │
├─────────────────────────────────────────┤
│ 3. Check TIME changed?                   │
│    IF (unixSeconds != last)            │
│    THEN sendTime() + update           │
├─────────────────────────────────────────┤
│ 4. Check RESOURCES (co 2s)             │
│    IF (cpu Δ > threshold OR            │
│        ram Δ > threshold)              │
│    THEN sendSystemResources()           │
├─────────────────────────────────────────┤
│ 5. Keepalive (co 30s)                  │
│    IF (now - lastKeepalive > 30s)     │
│    THEN sendHelloAck()                   │
├─────────────────────────────────────────┤
│ 6. Safety Refresh (co 5 min)           │
│    IF (now - lastSafety > 5min)       │
│    THEN send ALL + full resync           │
└─────────────────────────────────────────┘
```

### 4.2 Reactive vs Periodic

- **Reactive** (domyślnie): Wysyłaj tylko gdy zmiana przekroczy próg histerezy
- **Periodic fallback**: Keepalive co 30s, Safety co 5 min wymusza wysłanie wszystkiego

### 4.3 Handling HELLO (Guition Boot)

```
Gujtion wysyła HELLO (0x01):
  │
  ▼
ESP receive HELLO →
  │
  ▼
1. sendHelloAck() - bootId, uptime, syncState
  │
  ▼
2. Natychmiastowy push pełnego stanu:
   - sendSettings()
   - sendWeather()
   - sendPms()
   - sendTime()
   - sendSystemResources()
```

---

## 5. Stany Synchronizacji

### 5.1 PeerSyncState (po stronie ESP)

```cpp
enum PeerSyncState : uint8_t {
  kSyncUartReady = 0,      // UART zainicjowany, czekamy na Guition
  kSyncPeerDetected = 1,   // Guition się zgłosił (HELLO)
  kSyncActive = 2,        // Aktywna komunikacja
  kSyncError = 3         // Błąd komunikacji
};
```

### 5.2 Flow Stanów

```
STARTUP:
  s_syncState = kSyncUartReady
        │
        ▼
GUITION BOOT (wysyła HELLO):
        │
        ▼
  handleFrame(kTypeHello) →
        │
        ▼
  s_syncState = kSyncPeerDetected
  sendHelloAck() + broadcast all
        │
        ▼
  s_syncState = kSyncActive
        │
        ▼
  Normal operation: reactive broadcasts
```

---

## 6. Obsługa Błędów

### 6.1 Kody ACK (Ack Codes)

```cpp
enum AckCode : uint8_t {
  kAckOk = 0x00,         // OK
  kAckBadType = 0x01,   // Nieznany typ ramki
  kAckBadLength = 0x02, // Nieprawidłowa długość
  kAckBadCrc = 0x03     // Błędna suma CRC
};
```

### 6.2 Scenariusze Błędów

| Scenariusz | Reakcja ESP |
|------------|-------------|
| Nieznany typ ramki | `sendAck(kAckBadType)` |
| Payload za długi | `sendAck(kAckBadLength)` |
| CRC nie pasuje | `sendAck(kAckBadCrc)`, discard ramki |
| Bufor RX overflow | Reset RX, rozpcznij od nowa |
| Serial unavailable | Ignore, continue |

### 6.3 Brak Retry

**OBECNA IMPLEMENTACJA**: Brak mechanizmu retransmisji!

- Jeśli ramka utracona po drodze → Guition ma stare dane
- Brak potwierdzenia delivery (no ACK od Guition dla danych)
- Dane "stare" mogą wisieć na Guition do następnej zmiany

**To jest znany problem** - do naprawy w przyszłych wersjach.

---

## 7. Payloady

### 7.1 Weather Payload (0x10)

```
Offset  Size  Type     Field                    Unit
───────────────────────────────────────────────────
  0      2    int16_t  temperatureCx100        ×0.01°C
  2      2    uint16_t humidityPctX100        ×0.01%
  4      2    uint16_t pressureHpaX10         ×0.1 hPa
  6      4    uint32_t sampleAgeMs              ms
 10      1    uint8_t  flags                   bitfield
───────────────────────────────────────────────────
Total: 11 bytes
```

**Flags:**
```
bit 0 (0x01): temperature valid
bit 1 (0x02): humidity valid
bit 2 (0x04): pressure valid
bit 3 (0x08): temperature from ENS160
bit 4 (0x10): ENS160 sample available
bit 5 (0x20): BMP280 pressure available
```

### 7.2 PMS Payload (0x11)

```
Offset  Size  Type     Field                    Unit
───────────────────────────────────────────────────
  0      2    uint16_t pm01                    µg/m³
  2      2    uint16_t pm25                   µg/m³
  4      2    uint16_t pm10                   µg/m³
  6      2    uint16_t count0p3               count
  8      2    uint16_t count0p5               count
 10      2    uint16_t count1p0               count
 12      2    uint16_t count2p5               count
 14      2    uint16_t count5p0               count
 16      2    uint16_t count10p0               count
 18      4    uint32_t sampleAgeMs              ms
 22      1    uint8_t  flags                   bitfield
───────────────────────────────────────────────────
Total: 23 bytes
```
jak 
### 7.3 Time Payload (0x12)

```
Offset  Size  Type     Field                    Unit
───────────────────────────────────────────────────
  0      4    uint32_t unixSeconds            epoch
  4      1    uint8_t  valid                  0/1
───────────────────────────────────────────────────
Total: 5 bytes
```

### 7.4 System Resources Payload (0x13)

```
Offset  Size  Type     Field                    Unit
───────────────────────────────────────────────────
  0      4    uint32_t freeRam               bytes
  4      4    uint32_t heapRam               bytes
 14      4    uint32_t freeFlash             bytes
 18      4    uint32_t usedFlash             bytes
 22      2    uint16_t underrunsAudio         count
 24      2    uint16_t overflowAudio         count
 26      2    uint16_t dropsAudio            count
 28      2    uint16_t errorsI2c             count
 30      2    uint16_t timeoutsI2c           count
───────────────────────────────────────────────────
Total: 32 bytes
```

### 7.5 Settings Payload (0x20 / 0x30)

```
Offset  Size  Type     Field
───────────────────────────────────────────────────
  0      1    uint8_t  buzzerEnabled    (0/1)
  1      1    uint8_t  mqttEnabled     (0/1)
  2      1    uint8_t  touchTestEnabled (0/1)
  3      1    uint8_t  musicEnabled     (0/1)
  4      1    uint8_t  pmsEnabled       (0/1)
  5      1    uint8_t  alarmMelodyIndex (0-15)
───────────────────────────────────────────────────
Total: 6 bytes
```

---

## 8. Tryby Pracy

### 8.1 Synthetic (Demo) vs Real Sensors

```
// Plik: EsptoGuitionState.cpp, linia 28
CONSTEXPR bool K_USE_SYNTHETIC_PAYLOADS = true;  // <-- ZMIEŃ NA false DLA REAL SENSORÓW
```

**TRYB SYNTHETIC** (`= true`):
- Wszystkie dane są generowane losowo
- Używane gdy czujniki NIE są podłączone
- Przydatne do testowania komunikacji bez hardware

**TRYB REAL** (`= false`):
- Odczyt z prawdziwych czujników (BMP280, ENS160, PMS5003)
- Jeśli czujnik nie odpowiada → dane = 0, flags = 0

### 8.2 Źródła Danych (Tryb Real)

```
Weather:
  temperatura: ENS160 > BMP280 (ENS160 pierwszy, fallback na BMP280)
  wilgotność:  ENS160 (tylko ENS160 ma wilgotność)
  ciśnienie:  BMP280 (tylko BMP280 ma ciśnienie)

PMS:
  PMS5003 sensor (particle sensor)
```

---

## 9. TODO / Do Zrobienia

- [ ] Dodanie mechanizmu request/response z retransmisją
- [ ] ACK od Guition dla danych (delivery confirmation)
- [ ] Exponential backoff przy błędach CRC
- [ ] Konfigurowalny interwał broadcast przez Guition
- [ ] Obsługa trybu low-power (zmniejszona częstotliwość)

---

## 10. Pliki Źródłowe

| Plik | Odpowiedzialność |
|------|----------------|
| `EsptoGuitionCobs.h/cpp` | COBS encode/decode, CRC16 |
| `EsptoGuitionState.h/cpp` | Budowanie payloadów, sensory |
| `EsptoGuitionTransport.h/cpp` | Serial I/O, ramki |
| `Esptogution.cpp` | Główna pętla update() |

---

## 11. Zależności

### 11.1 Biblioteki

- `<Arduino.h>` - base
- `<math.h>` - finite checks
- `<string.h>` - memcpy

### 11.2 Moduły Projektu

- `AppSettings` - stan aplikacji (persistence)
- `AppLog` - logging
- `Preferences` - flash storage
- `BMP280Sensor` / `ENS160AHT21Sensor` - sensory
- `PMS_Czujnik` - czujnik pyłów
- `RTCService` - czas

---

*End of Document*