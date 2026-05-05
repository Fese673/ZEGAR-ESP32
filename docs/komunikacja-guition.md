# Analiza komunikacji z ekranem zewnętrznym Guition (ESP32-S3)

**Projekt:** ZEGAR-ESP32  
**Moduł:** `EsptoGuition` – dwukierunkowa komunikacja UART z ekranem zewnętrznym  
**Mikrokontroler:** ESP32-S3  
**Data analizy:** 2026-05-05

---

## 1. Architektura komunikacji

### 1.1 Przegląd

Ekran zewnętrzny **Guition** komunikuje się z ESP32-S3 poprzez dedykowane **UART2** z własnym, lekkim protokołem binarnym opartym o:

- **Framing:** COBS (Consistent Overhead Byte Stuffing)
- **Walidacja:** CRC16-CCITT
- **Synchronizacja:** handshake HELLO/HELLO_ACK + stan maszyny stanów
- **Kierunek:** ESP32 pełni rolę **sensoryczną (role=1)**, Guition jest **wyświetlaczem (role=0)**

```
┌─────────────────┐      UART2 (GPIO35/19)      ┌─────────────────┐
│   ESP32-S3      │◄────────────────────────────►│   Guition       │
│  (Sensor Role)  │   115200 baud, 8N1, COBS    │  (Display Role) │
│                 │   + CRC16 + Ack              │                 │
│ • BMP280        │                               │ • LCD 20×4      │
│ • PMS5003       │                               │ • Ikony CGRAM   │
│ • ENS160/AHT21  │                               │ • Menu UI       │
│ • RTC           │                               │                 │
└─────────────────┘                               └─────────────────┘
```

---

## 2. Fizyczna warstwa (Hardware)

### 2.1 Przypisanie pinów (BoardPins.h)

| Sygnał | Pin ESP32-S3 | Uwagi |
|--------|-------------|-------|
| UART2_RX | GPIO 35 | Odbiór danych od Guition |
| UART2_TX | GPIO 19 | Nadawanie danych do Guition |
| Prędkość | 115200 baud | `GUITION_BAUD` w `AppBoot.cpp` |
| Format | 8N1 | 8 bitów danych, brak parity, 1 stop-bit |

**Uwaga:** UART2 współdzieli domyślnie piny z innymi peryferiami (np. ADC2), dlatego inicjalizacja odbywa się **po** konfiguracji I2C i innych modułów (patrz `initSensors()` w `AppBoot.cpp`).

---

## 3. Protokół binarny – specyfikacja

### 3.1 Format ramki

```
[Type:1][Seq:1][Len:2][Payload:N][CRC16:2]  │  przed COBS
     │       │       │        │
     ▼       ▼       ▼        ▼
  0x??   0x??   0x??     dane...
```

Po **COBS encode** ramka jest opakowana w:

```
[0x00][COBS-frame][0x00]
```

Decoder Guition szuka `0x00`, dekoduje COBS, weryfikuje CRC16, a następnie dispatcheuje wg `Type`.

### 3.2 Typy wiadomości (Esptogution.h)

| Typ | Nazwa | Kierunek | Opis |
|-----|-------|---------|------|
| `0x01` | `kTypeWeather` | ESP → Guition | Dane pogodowe (temp, wilgotność, ciśnienie) |
| `0x02` | `kTypePms` | ESP → Guition | Pyły PM1.0/2.5/10 + liczniki cząstek |
| `0x03` | `kTypeTime` | ESP → Guition | Czas UNIX (epoch seconds) |
| `0x04` | `kTypeSettings` | ESP → Guition | Konfiguracja: buzzer, MQTT, touch, PMS, melodia |
| `0x05` | `kTypeSetSettings` | Guition → ESP | Nowe ustawienia (ack) |
| `0x06` | `kTypeSystemResources` | ESP → Guition | Telemetria: RAM, CPU, flash, audio/I2C stats |
| `0x10` | `kTypeRequest` | Guition → ESP | Żądanie: settings lub systemResources |
| `0x11` | `kTypeConfig` | – | (zarezerwowane) |
| `0x20` | `kTypeHello` | Guition → ESP | Powitanie + handshake |
| `0x21` | `kTypeHelloAck` | ESP → Guition | Odpowiedź HELLO (keep-alive) |
| `0xFF` | `kTypeAck` | ESP → Guition | Potwierdzenie/negatyw (ackCode) |

### 3.3 Payloads

#### Weather (0x01) – 13 bajtów
```cpp
struct {
  int16_t  temperatureCx100;   // ×100, np. 2350 = 23.50°C
  uint16_t humidityPctX100;    // ×100, np. 4500 = 45.00%
  uint16_t pressureHpaX10;     // ×10,  np. 10132 = 1013.2 hPa
  uint32_t sampleAgeMs;        // wiek próbki [ms]
  uint8_t  flags;              // bitmask: 0x01 temp, 0x02 hum, 0x04 press, 0x08 tempFromENS, ...
}  // ──总共 2+2+2+4+1 = 11 + CRC16
```

#### PMS (0x02) – 25 bajtów
```cpp
struct {
  uint16_t pm01, pm25, pm10;
  uint16_t count0p3, count0p5, count1p0, count2p5, count5p0, count10p0;
  uint32_t sampleAgeMs;
  uint8_t  flags;  // 0x01 = valid
}  // ──总共 18 + 1 = 19 + CRC16
```

#### Time (0x03) – 5 bajtów
```cpp
struct {
  uint32_t unixSeconds;  // epoch UTC
  uint8_t  valid;        // 1 = poprawny, 0 = brak RTC
}
```

#### Settings (0x04) – 6 bajtów
```cpp
struct {
  uint8_t buzzerEnabled;            // 0/1
  uint8_t mqttEnabled;              // 0/1
  uint8_t touchTestEnabled;         // 0/1
  uint8_t backgroundMusicEnabled;   // 0/1
  uint8_t pmsEnabled;               // 0/1 (zawsze 1 w aktualnej implementacji)
  uint8_t alarmMelodyIndex;         // index z AlarmMelodies
}
```

#### SystemResources (0x06) – 30 bajtów
```cpp
struct {
  uint32_t freeRam;      // wolna RAM (wolna DMA-capable)
  uint32_t heapRam;      // całkowita RAM
  uint32_t dmaRam;       // wolna RAM z DMA
  uint8_t  core0Cpu;     // obciążenie CPU core 0 [%]
  uint8_t  core1Cpu;     // obciążenie CPU core 1 [%]
  uint32_t freeFlash;    // wolna flash (niektóre systemy plików)
  uint32_t usedFlash;   // used = ESP.getSketchSize()
  uint16_t underrunsAudio;
  uint16_t overflowAudio;
  uint16_t dropsAudio;
  uint16_t errorsI2c;
  uint16_t timeoutsI2c;
}
```

#### Hello (0x20) / HelloAck (0x21) – 11 bajtów
```cpp
struct HelloPayload {
  uint8_t  protocolVersion; // 1
  uint8_t  deviceRole;      // 0=Display, 1=Sensor
  uint32_t bootId;          // losowe ID z MAC + millis()
  uint32_t uptimeMs;        // millis() od startu
  uint8_t  syncState;       // PeerSyncState enum
};
```

#### Ack (0xFF) – 3 bajty payload
```cpp
struct {
  uint8_t ackCode;    // 0x00=OK, 0x01=badType, 0x02=badLength, 0x03=badCrc
  uint8_t relatedType; // typ oryginalnej wiadomości
}
```

### 3.4 Maszyna stanów synchronizacji

```cpp
enum PeerSyncState : uint8_t {
  kSyncBooting          = 0,  // ESP: przed startem UART
  kSyncUartReady        = 1,  // UART skonfigurowany
  kSyncPeerDetected     = 2,  // odebrano HELLO
  kSyncFirstSyncSent    = 3,  // (rezerwa)
  kSyncFirstSyncConfirmed = 4, // (rezerwa)
  kSyncSteadyState      = 5   // regularna komunikacja
};
```

---

## 4. Przepływ danych (data flow)

### 4.1 Inicjalizacja (AppBoot.cpp::initSensors)

```cpp
void initSensors(RuntimeContext& ctx) {
  // [...]
  EsptoGuition::begin(
      Serial2,                 // HardwareSerial & (dedykowany UART)
      GUITION_BAUD,            // 115200
      BoardPins::kGuitionUartRx, // GPIO 35
      BoardPins::kGuitionUartTx  // GPIO 19
  );
  // [...]
}
```

### 4.2 Pętla główna (AppLoop.cpp::runLoop)

```cpp
void runLoop() {
  // [...]
  serviceComms(ctx, nowMs);  // wywołane co ~10-20ms
  // [...]
}

void serviceComms(RuntimeContext& ctx, unsigned long nowMs) {
  // 1. Inkrementacja zegara
  ClockAlarmService::tickClock(...);

  // 2. Sieć (WiFi/MQTT) – niezwiązana z Guition
  NetworkOrchestrator::update();

  // 3. RTC sync (NTP → DS3231)
  RtcSyncService::processPendingWrite();

  // 4. ★ KOMMUNIKACJA Z GUITION ★
  EsptoGuition::update();   // ← tutaj następuje pełny cykl

  // 5. MQTT publish (jeśli włączony)
  if (ctx.mqttEnabled && NetworkOrchestrator::isMqttInitialized()) {
    MQTTSync::publishSensorData(...);
  }
}
```

### 4.3 Cykl `EsptoGuition::update()`

```cpp
void update() {
  ingestSerialBytes();        // 1. Odbierz i zdekoduj wszystkie ramki z UART
  broadcastSnapshots(millis()); // 2. Wyślij cycliczne broadcasty
}
```

**Szczegóły `ingestSerialBytes()`:**

1. Czyta `Serial2` dopóki są bajty
2. Zbiera bajty aż do `0x00` (delimiter COBS)
3. Dekoduje COBS (`cobsDecode`)
4. Weryfikuje długość (`>= header + CRC` i `<= MaxPayload`)
5. Oblicza `CRC16-CCITT` i porównuje z odebranym
6. Jeśli OK → `handleFrame(type, sequence, payload, length)`
7. Jeśli NOK → `sendAck(sequence, kAckBadCrc, type)`

**`handleFrame()` dispatch:**

```cpp
switch (type) {
  case kTypeHello:
    sendHelloAck(seq);      // odpowiedź HELLO_ACK
    s_syncState = kSyncPeerDetected;
    // Full sync natychmiast – nie czekamy na cykl broadcast
    EsptoGuition::sendSettings(seq);
    EsptoGuition::sendSystemResources(seq);
    break;

  case kTypeRequest:
    if (payload[0] == kTypeSettings)        sendSettings(seq);
    else if (payload[0] == kTypeSystemResources) sendSystemResources(seq);
    break;

  case kTypeSetSettings:
    handleReceivedSettings(payload, length);  // decode i zapisz w Preferences
    sendAck(seq, kAckOk, type);
    break;

  default:
    sendAck(seq, kAckBadType, type);
}
```

**`broadcastSnapshots()` (okresowe wysyłki):**

```cpp
constexpr broadcastInterval = 30s;  // domyślnie
if (now - lastBroadcast >= interval) {
  sendWeather(seq++);      // weather
  sendPms(seq++);          // pms
  sendTime(seq++);         // time
}
// Co 10s:
if (now - lastResources >= 10s) {
  sendSystemResources(seq++);
}
// Co 30s (keep-alive):
if (now - lastKeepalive >= 30s) {
  sendHelloAck(seq++);   // role=1 (sensor), uptime, syncState
}
```

**Częstotliwość nadawania:**

| Wiadomość | Interwał | Uwagi |
|-----------|----------|-------|
| Weather | 30 s | `broadcastIntervalMs` (można zmienić przez `setBroadcastIntervalMs()` |
| PMS | 30 s | ten sam interwał |
| Time | 30 s | przesunięcie sekwencyjnie |
| SystemResources | 10 s | niezależny timer |
| HelloAck (keep-alive) | 30 s | niezależny timer |

---

## 5. Implementacja CRC16-CCITT i COBS

### 5.1 CRC16-CCITT (poly 0x1021, init 0xFFFF)

```cpp
uint16_t crc16Ccitt(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFFU;
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000U) ? (crc << 1) ^ 0x1021U : (crc << 1);
    }
  }
  return crc;
}
```

**Weryfikacja:** CRC liczony jest nad `header+payload` (bez własnych 2 bajtów CRC).

### 5.2 COBS (Consistent Overhead Byte Stuffing)

- **`cobsEncode()`** – zastępuje wszystkie `0x00` w danych przez specjalne kodowanie z `code` byte.
- Wyjście: `[0x00][ encoded-data ][0x00]` – bezpieczne dla UART.
- **`cobsDecode()`** – odwraca proces, zwraca długość oryginalnego bufora.
- Maksymalna pojemność payload: `kMaxPayloadBytes = 128`.

---

## 6. Obsługa ustawień z ekranu Guition

### 6.1 Odbieranie `kTypeSetSettings` (0x05)

```cpp
void handleReceivedSettings(const uint8_t* payload, uint16_t payloadLength) {
  if (payloadLength >= 5) {
    AppSettings::State& s = AppSettings::mutableState();

    bool newBuzzer       = (payload[0] != 0);
    bool newMqtt         = (payload[1] != 0);
    bool newTouch        = (payload[2] != 0);
    bool newMusic        = (payload[3] != 0);
    bool newPms          = (payload[4] != 0);

    // Zapisujemy stan w pamięci Preferences (NVS)
    Preferences p; p.begin("zegar", false);
    p.putBool("buzzerEnabled",   newBuzzer);
    p.putBool("mqttEnabled",     newMqtt);
    p.putBool("touchTest",       newTouch);
    p.putBool("menuMusic",       newMusic);
    p.putBool("pmsEnabled",      newPms);
    if (payloadLength >= 6) {
      int newMelody = payload[5];
      s.alarmMelodyIndex = newMelody;
      p.putUShort("alarmMelody", (uint16_t)newMelody);
    }
    p.end();

    // Aktualizacja modułów
    TouchBuzzerTest::setEnabled(newTouch);
    PMS5003Sensor::setEnabled(newPms);

    // Odświeżenie UI (synchronizacja stanów menu)
    requestUiFullRedraw();
  }
}
```

**Efekt:** Zmiana ustawień z ekranu zewnętrznego od razu zapisuje się w NVS i odświeża LCD.

---

## 7. Integracja z wewnętrznym ekranem I2C (20×4)

### 7.1 Podwójny bufor – `LcdFrameBuffer20x4`

Klasa `LcdFrameBuffer20x4` (LCDMirror.h):

- **Dwa bufory:** `frame[4][20]` (cel) i `shadow[4][20]` (ostatnio wysłane)
- **`commit()`:** porównuje `frame` vs `shadow`, Aleksander tylko zmienione znaki przez I2C.
- **Zabezpieczenie I2C:** `I2cShared::lock(1ms)` – mutex dla współdzielonej magistrali I2C.
- **Statystyki (debug):** `lastCommitUs`, `maxCommitUs`, `commitCount`.

**Przykład użycia w `UI_Draw.cpp`:**
```cpp
LCD_SET(0, 0);
LCD_PRINT("Temp: 23.5 C");
LCD_DUMP();  // → lcdFrame.commit() + ewentualnie lcdMirror.dumpUART()
```

### 7.2 Ikony w CGRAM (LCDIcons)

- 8 slotów CGRAM × 5×8 pikseli każdy.
- Definicje w `kGlyphs[73][8]` (71 ikon + placeholder).
- 5 palet: `Home`, `HomeWifi`, `Weather`, `System`, `Media`.
- `loadPalette(lcd, Palette::Home)` ładuje zestaw ikon do slotów 0-7.

**Dostępne ikony (wybrane):**
```
0=Alarm/Ntp, 1=Heart, 2=Bell, 3=Cross, 4=Check, 5=Smile, 6=Sad,
7=Note, 8=Wifi, 9=ArrowUp, 10=ArrowDown, 11=BatteryFull, ...
72=Clock2, 73=Count
```

### 7.3 I2C współdzielony

- **Klasa:** `I2cShared` (nie pokazana w pełni, ale używana przez `LCDIcons` i `LcdFrameBuffer`).
- **Timeout:** 1 ms (`LCD_I2C_LOCK_TIMEOUT_MS`).
- W przypadku blokady I2C (np. długie operacje innych modułów) `commit()` po prostu zwraca `false` – nie blokuje pętli.

---

## 8. Ocena jakości komunikacji

### 8.1 Mocne strony ✅

1. **Lekki, deterministyczny protokół** – COBS + CRC16 to prosty, sprawdzony mechanizm bez nakładu TCP/IP.
2. **Dwukierunkowość** – ESP może odbierać polecenia (`SetSettings`, `Request`).
3. **Synchronizacja stanów** – `PeerSyncState` pozwala wykryć restart/odłączenie.
4. **Sequence numbers** – pozwalają na detekcję zagubionych/duplikatów ramek (choć nie wykorzystane do retransmisji).
5. **Odporność na błędy** – CRC16 wykrywa uszkodzone ramki, ACK/NACK dla typów.
6. **Modułowość** – osobne namespace'y: `Transport` (niskopoziomowy), `State` (payload buildery), `EsptoGuition` (API publiczne).
7. **Thread-safe I2C** – `I2cShared::lock()` chroni magistralę przed kolizjami z innymi sensorami (BMP280, ENS160, RTC).
8. **Caching ikon** – `LCDIcons` śledzi, które ikony już załadowane, redukując I2C trafic.
9. **Incrementalne commitowanie LCD** – `LcdFrameBuffer20x4` wypisuje tylko zmienione znaki, oszczędzając I2C bandwidth.

### 8.2 Potencjalne słabe punkty ⚠️

1. **Brak retransmisji** – jeśli CRC się nie zgadza, wysyłany jest NACK, ale nie ma mechanizmu ponowienia (zależy od aplikacji, aby wysłać ponownie w kolejnym cyklu).
2. **No flow control** – UART bez RTS/CTS; przy pełnym buforze Guition może dojść do overflow (ESP nie czeka na gotowość odbiorcy).
3. **Fixed message ordering** – weather/pms/time wysyłane w stałej kolejności, nawet jeśli jedno z danych jest nieaktualne (syntetyczne fallback w `EsptoGuitionState.cpp`).
4. **Heavy interrupt disabling** – `ingestSerialBytes()` może blokować długo (do 256 bajtów), w zależności od prędkości UART i obciążenia.
5. **No encryption/authentication** – komunikacja w open, każdy może podsłuchać/podrobić ramki (dla ograniczonego otoczenia może być OK).
6. **Single-point failure** – jeśli Guition się rozłączy, ESP nadal będzie wysyłać hejta co 30s, ale nie ma backoff-a (stały 30s keep-alive).
7. **Hardcoded baud** – 115200 nie jest konfigurowalny w runtime (chyba że dodać `setBaud()`).
8. **Magic numbers** – `kMaxPayloadBytes = 128` może być niewystarczający w przyszłości przy rozbudowanej telemetrii.
9. **Brak timeoutów na połączenie** – nie ma detekcji "odłączony Guition" na poziomie API (tylko statystyki).

### 8.3 Wydajność i timing

| Operacja | Czas (szac.) | Uwagi |
|----------|-------------|-------|
| `ingestSerialBytes()` | < 1 ms (przy 1-2 ramkach) | zależne od szybkości UART i bufora |
| `sendRawFrame()` | ~0.5-1 ms | COBS encode + CRC + UART write |
| `lcdFrame.commit()` | ~1.5 ms (typ.) | I2C 400 kHz, 20×4, incremental |
| Pełny okresWeather/PMS/Time | 30 000 ms | | |

**Telemetria wydajnościowa** (jeśli `CORE_DEBUG_LEVEL > 0`):
- `LoopBaselineTelemetry` – mierzy `networkUpdateUs`, `mqttPublishUs`, `uiRefreshUs`.
- `lcdFrame.reportTiming()` – średni/max czas commitowania LCD.

---

## 9. Wnioski i rekomendacje

### 9.1 Ocena ogólna: **7.5/10**

Architektura komunikacji jest **solidna, ale nie nadzorowana**. Protokół binarny COBS+CRC sprawdza się w embedded, brakuje jednak mechanizmów odporności na problemy sieciowe (retransmit, flow control).

**Plusy:**
- Czytelny, podzielony na warstwy kod.
- Synchronizacja stanów pozwala na naprawę rozłączeń.
- Telemetria system resources pomaga monitorować obciążenie.
- I2C sharing dobrze zaimplementowany (mutex).

**Minusy:**
- Brak zabezpieczeń przed zatokami (buffer overflow po stronie Guition).
- Brak możliwości dynamicznej konfiguracji prędkości UART.
- Syntetyczne fallbacki (random) w `EsptoGuitionState.cpp` mogą wprowadzać błąd przy debugowaniu (trudno odróżnić fake od real).
- Mało metadanych (nie ma wersji firmware Guition, modelu wyświetlacza itp.).

### 9.2 Sugestie ulepszeń

1. **Dodaj flow control (XON/XOFF lub hardware RTS/CTS)** – jeśli Guition ma mały bufor.
2. **Implementuj exponential backoff** przy NACK-ach / braku HELLO.
3. **Rozszerz typy wiadomości** o `kTypeFirmwareVersion`, `kTypeDisplayModel` – do debugowania.
4. **Dodaj timestampy do wszystkich payloads** – pozwala na detekcję opóźnień.
5. **Zamień magiczne liczby na consteval** – np. `constexpr auto kGuitionUart = Serial2;`.
6. **Logika syntetycznych danych** – użyj flagi `kUseSyntheticPayloads` tylko w debug build, w prod wymagaj real sensor data.
7. **ACK handling** – obecnie NACK jest wysyłany, ale nie ma kolejki do retransmisji (można dodać proste retry 2-3 razy).
8. **Konfiguracja przez UART** – pozwoli Guition ustawiać `broadcastIntervalMs` na starcie.

---

## 10. Diagram sekwencji (Message Sequence Chart)

```
ESP32 (Sensor)                     Guition (Display)
-----------------                  -----------------
   │                                   │
   │  (UART idle)                      │
   │                                   │
   │◄────── HELLO (0x20) ────────────►│  (discovery)
   │                                   │
   │─── HELLO_ACK (0x21) ────────────►│  (ack + role=1)
   │                                   │
   │◄──── REQUEST (0x10, Settings) ──►│  (pobierz ustawienia)
   │                                   │
   │─── SETTINGS (0x04) ─────────────►│  (buzzer, mqtt,…)
   │                                   │
   │◄──── REQUEST (0x10, SysRes) ────►│  (pobierz telemetrię)
   │                                   │
   │─── SYS_RES (0x06) ──────────────►│  (RAM/CPU stats)
   │                                   │
   │  (cykl 30 s)                      │
   │─── WEATHER (0x01) ──────────────►│
   │─── PMS (0x02) ──────────────────►│
   │─── TIME (0x03) ─────────────────►│
   │                                   │
   │◄──── SET_SETTINGS (0x05) ───────►│  (user zmienił ustawienie)
   │                                   │
   │─── ACK (0xFF, OK) ──────────────►│
   │                                   │
   │  (keep-alive co 30 s)            │
   │─── HELLO_ACK (0x21) ────────────►│  (uart żyje?)
   │                                   │
```

---

## 11. Podsumowanie

Komunikacja z ekranem zewnętrznym **Guition** jest zrealizowana w sposób **modularny i czytelny**, z wyraźnym rozdzieleniem warstw:

- **Transport** – `EsptoGuitionTransport` (COBS, CRC, UART)
- **State/builders** – `EsptoGuitionState` (tworzenie payloads z sensorów)
- **API publiczne** – `EsptoGuition::begin/update/send*`
- **UI mirror** – `LCDMirror` (opcjonalny debug przez Serial)
- **Ikony/framebuffer** – `LCDIcons`, `LcdFrameBuffer20x4`

Protokół jest **prosty do implementacji** na drugim końcu (Guition), ale brakuje mu niektórych mechanizmów odporności typowych dla protocoli typu **SLIP/PPP** czy **MAVLink**. Dla zamkniętego, przewodowego połączenia UART to jednak **wystarczające rozwiązanie**.

**Rekomendacja:** kod maintainowalny, warto dodać lepszą detekcję rozłączeń i retransmisje dla krytycznych wiadomości (np. ustawień). Obsługa wielu ekranów równolegle (Guition ×2) wymagałaby osobnych instancji `EsptoGuition` – obecnie singleton.
