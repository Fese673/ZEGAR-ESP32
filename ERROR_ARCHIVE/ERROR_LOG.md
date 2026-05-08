# Archiwum Błędów - ZEGAR-ESP32
---
---
---
### [ID: ERR_031] | ALARM_CHOPPY_PLAYBACK | IMPACT: MEDIUM
**Files:** `[AppLoop.cpp]`

**PROBLEM:** Melodia alarmu była "szarpana" – buzzer grał tylko jedną nutę na sekundę, po czym następowała długa cisza aż do kolejnego "tyknięcia" zegara. Melodia brzmiała jak błąd systemu, a nie zamierzony utwór.

**CAUSE:** Wywołanie `ClockAlarmService::serviceAlarmPlayback()` znajdowało się w handlerze `onClockTick`, który jest wyzwalany przez `EventBus` z interwałem 1000ms. Buzzer nie miał szansy przejść do kolejnej nuty melodii częściej niż raz na sekundę, co niszczyło tempo utworu.

**LOGIC_CHANGE:**
- `AppLoop.cpp`: Usunięto `serviceAlarmPlayback` z handlera `onClockTick` (EV_CLOCK_TICK).
- `AppLoop.cpp`: Dodano `ClockAlarmService::serviceAlarmPlayback(BUZZER_PIN, ALARM_DURATION_MS)` bezpośrednio do pętli `runLoop()`. Dzięki temu serwis buzzera jest odpytywany dziesiątki tysięcy razy na sekundę, co zapewnia idealną płynność melodii.

**VERIFICATION:** Melodie (np. Cantina Band) grają płynnie, z zachowaniem właściwego rytmu i bez przerw między nutami.

**Files:** `[SystemResourcesService.cpp, EsptoGuitionState.cpp, UI_Draw.cpp, Esptogution.cpp]`

**PROBLEM:** Periodyczne zamrożenia systemu (600ms - 1.2s) widoczne na stoperze (przeskoki klatek). W logach telemetrycznych `body_max_us` osiągało 1.2s. Zamrożenia korelowały z logami `Status` (co 2s) oraz odświeżaniem statystyk systemowych. System tracił responsywność dokładnie w momentach odczytu danych technicznych o pamięci Flash.

**CAUSE:** Wywołania `ESP.getSketchSize()` i `ESP.getFreeSketchSpace()` odczytują dane z partycji SPI Flash. Na ESP32 odczyt Flash wymaga wyłączenia Cache instrukcji. Powoduje to natychmiastowe zatrzymanie OBU rdzeni CPU i zablokowanie wszystkich przerwań (WiFi, UART, Timery) na czas operacji SPI. Ponieważ te wartości są stałe w runtime, odczytywanie ich co 1-2s było krytycznym błędem. Dodatkowo `buildWifiPayload()` odpytywał sterownik WiFi (RSSI/IP) ~34 000 razy na sekundę, powodując potężną rywalizację o muteksy stosu sieciowego.

**LOGIC_CHANGE:**
- `SystemResourcesService.cpp`: Dodano `static` cache — `ESP.getFreeSketchSpace()` wołany tylko raz przy pierwszym zapytaniu.
- `EsptoGuitionState.cpp`: Dodano `static` cache — `ESP.getSketchSize()` wołany tylko raz.
- `UI_Draw.cpp`: Dodano cache dla obu wartości flash w funkcji `drawSystemResources()`.
- `Esptogution.cpp`: Dodano guard czasowy (2000ms) dla `buildWifiPayload()` — redukcja z 34 000 wywołań/s do 1 wywołania co 2 sekundy.

**VERIFICATION:** Kompilacja OK. Stoper płynny, brak przeskoków klatek przy logach `Status`. `body_max_us` spadło z 1.2s do <10ms. Całkowite wyeliminowanie "globalnego zamrożenia" rdzeni.

**Files:** `[ENS160AHT21Sensor.cpp, AHTxx.cpp, AHTxx.h]`

**PROBLEM:** Mimo flag `s_ens160Present` / `s_ahtPresent`, ENS160::update() wciąż wołał `updateClimateData()` PRZED checkiem `!s_ens160Present`. Jeśli AHT21 był wykryty przy starcie (`s_ahtPresent=true`) a potem padł, `AHTxx::update()` wchodził w I2C przez `_sendResetCmd()` (timeout 1ms). Przy backlogu eventów (catch-up po blokadzie poprzednika), kaskada `onSensorRead` × 200+ zdarzeń w jednym `process()` = sumarycznie 1.2s I2C blokady.

**CAUSE:** `ENS160::update()` — `updateClimateData()` wołany bez sprawdzenia czy JAKIKOLWIEK sensor I2C jest obecny. `AHTxx::update()` — brak wyraźnej flagi `_initialized`, polegał na stanie maszynowym `AHT_STATE_IDLE`.

**LOGIC_CHANGE:**
- `ENS160AHT21Sensor::update()`: dodano `if (!s_ens160Present && !s_ahtPresent) return;` — zombie guard PRZED `updateClimateData()`
- `AHTxx::begin()`: ustawia `_initialized = true` po udanym probe
- `AHTxx::update()`: pierwsza linia `if (!_initialized) return false;` — zombie guard niezależny od stanu maszynowego

**VERIFICATION:** Kompilacja OK. Gdy oba sensory I2C odłączone: ENS160::update() zwraca w <1µs. AHTxx::update() zwraca w <1µs dzięki `_initialized` flag.
---
### [ID: ERR_028] | SENSOR_STARTUP | IMPACT: MEDIUM
**Files:** `[ENS160AHT21Sensor.cpp]`

**PROBLEM:** Startup ENS160 init blokował system na ~900ms. `initializeENS160()` próbował 2 adresy (0x53, 0x52) × 3 retries × 150ms vTaskDelay między próbami = ~720ms samych opóźnień + ~120ms I2C timeoutów. Stoper "przeskakiwał" o ~1.7s przy starcie z odłączonym ENS160.

**CAUSE:** `ENS160_INIT_RETRIES = 3` z `ENS160_INIT_RETRY_DELAY_MS = 150` dla każdego z 2 adresów. Dla odłączonego sensora to 6 prób × 150ms delay = czysty waste.

**LOGIC_CHANGE:**
- `ENS160_INIT_RETRIES`: 3 → 1 (jedna próba na adres, jeśli sensor jest, odpowiada za pierwszym razem)
- `ENS160_INIT_RETRY_DELAY_MS`: 150 → 20 (tylko bezpieczeństwo na wypadek zmiany retries w przyszłości)

**VERIFICATION:** Kompilacja OK. ENS160 init dla odłączonego sensora: 1 próba × 2 adresy × ~20ms I2C = ~40ms zamiast ~900ms.
---
### [ID: ERR_027] | COMMS_GUITION | IMPACT: HIGH
**Files:** `[EsptoGuitionTransport.cpp]`

**PROBLEM:** `sendRawFrame()` → `Serial2.write()` blokował pętlę główną gdy bufor TX był pełny. ESP32 `HardwareSerial::write()` ma domyślny timeout `portMAX_DELAY` — czeka w nieskończoność aż UART wyśle dane. W logach ~690ms blokady co ~2s korelują z wysyłką batcha ramek do Guition.

**CAUSE:** Podwójny bug:
1. `setTxBufferSize(1024)` wołany PRZED `begin()` → `begin()` alokuje nowy bufor (domyślny 256B), gubiąc ustawienie
2. `write()` bez guarda — gdy Guition backloguje, blokada kumuluje się

**LOGIC_CHANGE:**
- `beginSerial()`: przeniesiono `setTxBufferSize(1024)` PO `begin()` — bufor faktycznie 1024B
- `sendRawFrame()`: dodano `if (availableForWrite() >= wireLen)` guard przed `write()` — ramka pomijana (drop) jeśli bufor pełny, zamiast blokować pętlę

**VERIFICATION:** Kompilacja OK. TX buffer = 1024B. `sendRawFrame()` nigdy nie blokuje — drops frame jeśli UART nie nadąża.
---
### [ID: ERR_026] | COMMS_MQTT | IMPACT: MEDIUM
**Files:** `[MQTTSync.cpp]`

**PROBLEM:** `MQTT_SOCKET_TIMEOUT_SEC = 3` — `PubSubClient::loop()` → `_client->read()` blokował do 3s przy każdym wywołaniu, gdy gniazdo TCP było otwarte ale broker nie wysyłał danych.

**CAUSE:** Socket timeout 3s na WiFiClientSecure. `mqttClient.loop()` wołany z `onSensorRead()` (200ms) → `MQTTSync::update()`.

**LOGIC_CHANGE:**
- `MQTT_SOCKET_TIMEOUT_SEC`: 3 → 1

**VERIFICATION:** Kompilacja OK. MQTT loop blokuje max 1s (vs 3s). Async connect task na Core 0 (ERR_014) kontynuuje działanie bez wpływu.
---
### [ID: ERR_025] | LIB_OPENMETEO | IMPACT: MEDIUM
**Files:** `[OpenMeteo.cpp, OpenMeteo.h]`

**PROBLEM:** `getStringRequest()` w OpenMeteo.cpp wciąż używał `http.getString()` (heap contention landmine). Funkcja była nieużywana przez projekt, ale stanowiła zagrożenie dla przyszłych developerów. Dodatkowo biblioteka zawierała ~200 linii martwego kodu (commented-out overloads, nieużywane `getHourlyForecast`/`getAirQualityForecast`, 2 struktury OM_HourlyForecast/OM_AirQualityForecast).

**CAUSE:** Refaktor ERR_024 naprawił `getCurrentWeather()` i `storeLatestAirQuality()`, ale nie usunął źródła problemu (`getStringRequest`) i dead code.

**LOGIC_CHANGE:**
- Usunięto `getStringRequest()` — całe HTTP + streaming JSON przez nowe `fetchAndParseJson()`
- Usunięto `getHourlyForecast()` / `getAirQualityForecast()` — nieużywane, wołały `getStringRequest()`
- Usunięto `OM_HourlyForecast`, `OM_AirQualityForecast`, `HOURLY_API_LINK`, `AIR_HOURLY_API_LINK`
- Usunięto ~200 linii zakomentowanych overloadów
- `unixToDateOM()` usunięta (tylko używana przez nieistniejący `getHourlyForecast`)
- `OpenMeteo.h`: ~180 linii → ~20 linii (tylko `OM_CurrentWeather` + `getCurrentWeather`)

**VERIFICATION:** Kompilacja OK. Flash usage decreased. Zero `http.getString()` w całym projekcie.
---
### [ID: ERR_024] | COMMS_METEO | IMPACT: CRITICAL
**Files:** `[OpenMeteo.cpp, meteoSync.cpp]`

**PROBLEM:** **Heap Mutex Contention** — zamrożenie pętli głównej na ~600ms. MeteoSync (Core 0) pobiera JSON przez `http.getString()`, alokując 2-4KB ciągłego bloku na stercie. Trzyma Heap Mutex podczas alokacji. W tym czasie Core 1 (AppLoop) w `onDiagnostics` woła `ESP.getFreeHeap()` → blokada na Heap Mutex → UI/encder zamrożony.

**CAUSE:** `OpenMeteo.cpp:getCurrentWeather()` → `getStringRequest()` → `http.getString()` kopiuje całą odpowiedź HTTP do String. `meteoSync.cpp:storeLatestAirQuality()` → `deserializeJson(doc, http.getString())` — to samo. Oba alokują duży String, trzymając Heap Mutex.

**LOGIC_CHANGE:**
- `OpenMeteo.cpp:getCurrentWeather()`: zastąpiono `getStringRequest()` inline HTTP + `http.getStream()` → `deserializeJson()` — dane płyną bajt po bajcie z sieci do parsera JSON, zero pośredniej alokacji String
- `meteoSync.cpp:storeLatestAirQuality()`: `deserializeJson(doc, http.getString())` → `deserializeJson(doc, http.getStream())`
- Usunięto `kHttpTimeoutMs` z `getStringRequest()` (przeniesiono do scope pliku)

**VERIFICATION:** Kompilacja OK. Żaden fetch pogody nie alokuje już String z całą odpowiedzią HTTP. Parser JSON czyta strumieniowo z WiFiClient. Heap Mutex trzymany tylko na drobne alokacje JsonDocument.
---
### [ID: ERR_023] | SENSOR_I2C_TIMEOUT | IMPACT: MEDIUM
**Files:** `[ENS160AHT21Sensor.cpp]`

**PROBLEM:** `I2cShared::lock(1000)` w ENS160AHT21Sensor — timeout 1000ms na muteks I2C był ekstremalnie konserwatywny. Gdy muteks był zajęty przez inny task (np. uszkodzony czujnik blokujący magistralę), pętla główna blokowała się na pełną sekundę.

**CAUSE:** Wszystkie 5 wywołań `I2cShared::lock()` w ENS160AHT21Sensor.cpp używało 1000ms timeoutu, podczas gdy rzeczywiste operacje I2C trwają <1ms, a maksymalny czas trzymania muteksu wynosi ~50µs (z timeoutem 10ms na Wire).

**LOGIC_CHANGE:**
- `I2cShared::lock(1000)` → `I2cShared::lock(50)` we wszystkich 5 miejscach
- Przy zablokowanym muteksie (awaria/kolizja I2C) task czeka max 50ms zamiast 1000ms

**VERIFICATION:** Kompilacja OK. Muteks I2C zwalniany w <1ms przy normalnej pracy. 50ms timeoutu w zupełności wystarcza na obsłużenie skrajnego przypadku kolizji.
---
### [ID: ERR_022] | BOOT_INTRO | IMPACT: HIGH
**Files:** `[BootIntroService.cpp, AppLoop.cpp, ENS160AHT21Sensor.cpp]`

**PROBLEM:** Zacinanie się i ekstremalne skoki klatek (lagi do 1,4 sekundy) podczas animacji powitalnej (Boot Intro) na ekranie LCD. Animacja zatrzymywała się, przez co cały efekt "wypisywania teksu" ulegał zniszczeniu (np. wyświetlało "TICKI", następnie "wiszenie" na ponad sekundę, po czym od razu "TICKIN").

**CAUSE:**
1. Animacja z `BootIntroService` była "napędzana" poprzez synchroniczne wywołanie `BootIntroService::service()` wewnątrz głównej pętli `AppLoop::runLoop()`.
2. W trakcie wyświetlania intra, system przechodzi do sekwencji `initSensors()` w `AppBoot.cpp`, co aktywuje cykliczne czytanie czujników przez zdarzenie `EV_SENSOR_READ` (200ms).
3. Jeżeli czujnik `ENS160` (lub inny I2C) nie odpowiadał (np. brak podłączenia), jego wewnętrzna logika re-inicjalizacji (z delayami) blokowała pętlę na kilkaset milisekund, a nawet ponad 1 sekundę.
4. Zamrożenie pętli głównej skutkowało całkowitym zablokowaniem odświeżania `BootIntroService` – stąd gigantyczny lag widoczny w terminalu i na ekranie.

**LOGIC_CHANGE:**
- Zmieniono architekturę `BootIntroService`. Cała logika animacji i maszyny stanów została przeniesiona do własnego zadania **FreeRTOS (`introTask`)** działającego na rdzeniu aplikacyjnym (Core 1) z **wysokim priorytetem (15)**.
- Dzięki wysokiemu priorytetowi, zadanie wideo wywłaszcza resztę systemu, co gwarantuje stałe interwały rysowania (np. co 200ms) i całkowicie uniezależnia płynność intra od timeoutów sprzętowych magistrali I2C lub odczytów czujników w tle.

**VERIFICATION:** Animacja odtwarza się ze stałą, przewidywalną szybkością niezależnie od tego, czy czujniki mają problemy z inicjalizacją, czy nie.
---
### [ID: ERR_021] | SYSTEM_RESOURCES | IMPACT: CRITICAL
**Files:** `[SystemResourcesService.cpp, Esptogution.cpp]`

**PROBLEM:** Ostre zacięcia (lagi) co 1-2 sekundy na łączu UART2 (do Guition), objawiające się gubieniem bajtów i błędami CRC na odbieranych ramkach.

**CAUSE:**
1. Funkcja `SystemResourcesService::update()` co 1 sekundę wywoływała `heap_caps_get_largest_free_block()`.
2. Ta funkcja we FreeRTOS działa ze złożonością **O(N)** – musi przejść przez całą listę wolnych bloków pamięci w stercie, zakładając przy tym **globalną blokadę (mutex) wyłączającą przełączanie kontekstu**.
3. Ponieważ biblioteka Meteo bardzo mocno fragmentuje pamięć (setki małych alokacji z ArduinoJson), ta operacja blokowała procesor (Core 1) na **ponad 15-20 ms**.
4. Wbudowany w ESP32 sprzętowy bufor FIFO dla UART ma tylko 128 bajtów. Przy prędkości 115200 bps zapełnia się on w **11.1 ms**. 
5. Zamrożenie procesora na 20 ms powodowało **przepełnienie sprzętowego bufora UART** (hardware overflow) – fizyczne gubienie bajtów przychodzących z Guition i w efekcie "lagi" oraz powtórzenia transmisji.

**LOGIC_CHANGE:**
- Usunięto wywołania `O(N)` (`heap_caps_get_largest_free_block` oraz `heap_caps_get_free_size(MALLOC_CAP_DMA)`).
- Zastąpiono je wywołaniami `O(1)`: `ESP.getMaxAllocHeap()` oraz użyto `freeHeap` jako aproksymacji dla DMA.
- W `Esptogution.cpp` dodano **histerezę (1024 bajty)**, aby przestać spamować ramką `SystemResources` przy mikroskopijnych fluktuacjach RAM-u.

**VERIFICATION:** UART hardware FIFO już nie ulega przepełnieniu. Komunikacja działa płynnie.
---
### [ID: ERR_020] | ESP_TO_GUTION | IMPACT: LOW/MEDIUM
**Files:** `[EsptoGuitionCobs.cpp]`

**PROBLEM:** Nieefektywne obliczanie sumy kontrolnej CRC-16 w pętli głównej (bit-by-bit). Dla dużych ramek (np. 134 bajty) generowało to >1000 iteracji procesora na każdą ramkę. Przy burstach (7 ramek naraz) mogło to powodować mikro-opóźnienia rzędu 0.5-1ms.

**CAUSE:** Implementacja algorytmiczna bez użycia tablic przeglądowych (LUT).

**LOGIC_CHANGE:**
- **Table-Driven CRC:** Wprowadzono 256-elementową tablicę `constexpr` (umieszczoną w pamięci Flash).
- **O(1) per byte:** Algorytm przetwarza teraz jeden bajt w jednej operacji XOR + odczyt z pamięci, co daje 8-krotny wzrost prędkości obliczeń CRC.

**VERIFICATION:** Brak mierzalnych opóźnień obliczeniowych przy wysyłaniu pakietów do Guition.
---
### [ID: ERR_019] | WIFI_SYNC | IMPACT: HIGH
**Files:** `[WiFiSync.cpp, WiFiSync.h, TaskConfig.h, EsptoGuitionState.cpp]`

**PROBLEM:** Mikro-zacięcia UI (lagi 2-10ms) występujące kilka razy na sekundę, szczególnie widoczne przy aktywnej komunikacji z Guition.

**CAUSE:** Funkcja `buildWifiPayload` wywoływała `WiFi.RSSI()` bezpośrednio w pętli głównej (UI thread). Sterownik WiFi na ESP32 blokuje procesor podczas odpytywania o siłę sygnału. Przy częstotliwości 5 Hz (broadcast Guition) lag sumował się do znaczących wartości.

**LOGIC_CHANGE:**
- **Asynchroniczny monitoring:** Utworzono zadanie tła `WifiMonitorTask` na **Core 0** (rdzeń systemowy), które odpytuje RSSI co 5 sekund.
- **Atomic Cache:** Wynik pomiaru jest zapisywany w `std::atomic<int8_t>`.
- **Non-blocking read:** Moduł Guition pobiera teraz gotową wartość z RAM przez `WiFiSync::getRssi()`, co zajmuje nanosekundy zamiast milisekund.

**VERIFICATION:** UI reaguje natychmiastowo na enkoder. Brak mierzalnych lagów związanych ze stosem WiFi w pętli głównej.
---
### [ID: ERR_018] | CORE_EVENTBUS | IMPACT: HIGH
**Files:** `[EventBus.cpp]`

**PROBLEM:** Spam logów — ten sam wpis `Status wifi=...` powtarzany ~14× z tym samym timestampem. Timer `EV_DIAGNOSTICS` (2000ms) produkował wielokrotne eventy w jednym cyklu `process()`.

**CAUSE:** Błąd arytmetyki `uint32_t` w `addTimer()`. Ustawienie `lastFireMs = millis() + offsetMs` powodowało, że w `process()` odejmowanie `now - lastFireMs` skutkowało ogromną liczbą (overflow), co wymuszało natychmiastowe odpalenie timera w każdym obiegu pętli, dopóki czas nie "dogonił" offsetu. Duplikaty timerów (brak checka) jedynie potęgowały ten efekt.

**LOGIC_CHANGE:**
- `addTimer()`: poprawiono inicjalizację na `lastFireMs = millis() + offsetMs - (offsetMs == 0 ? 0 : periodMs)`.
- `addTimer()`: zachowano duplicate check dla bezpieczeństwa.

**VERIFICATION:** Kompilacja OK. Każdy timer istnieje w dokładnie jednej kopii. Handler EV_DIAGNOSTICS wołany raz na 2000ms.
---
### [ID: ERR_017] | COMMS_GUITION & METEO | IMPACT: HIGH
**Files:** `[EsptoGuitionState.cpp, EsptoGuitionTransport.cpp, TaskConfig.h]`

**PROBLEM:** Cykliczne "zamarzanie" UI (co 1-2s) mimo throttlingu. Zidentyfikowano trzy źródła blokad:
1. **I2C Saturation:** `buildTimePayload` odczytywał czas z RTC (I2C) zamiast z RAM.
2. **UART Blocking:** Bufor TX Serial2 (128B) zapełniał się, blokując pętlę `loop()`.
3. **Core 1 Contention:** Zadanie Meteo (SSL/JSON) rywalizowało o Core 1 z procesem UI.

**CAUSE:** Zbyt częste synchroniczne operacje I2C/Serial w pętli głównej oraz rywalizacja o zasoby procesora na jednym rdzeniu.

**LOGIC_CHANGE:**
1. **RAM-Time Sync:** `buildTimePayload` czyta teraz wyłącznie z systemowego `time(nullptr)` (RAM).
2. **UART Expansion:** Zwiększono bufor TX dla `Serial2` do 1024 bajtów.
3. **Core Offloading:** Przeniesiono `MeteoSyncTask` na Core 0.

**VERIFICATION:** UI reaguje płynnie na enkoder podczas aktywnej komunikacji z Guition i pobierania danych pogodowych.
---
### [ID: ERR_016] | COMMS_GUITION | IMPACT: HIGH
**Files:** `[Esptogution.cpp, Esptogution.h]`

**PROBLEM:** `broadcastSnapshots()` wołana w każdej pętli głównej (50-100 Hz) bez throttlingu, mimo że zmienne `s_broadcastIntervalMs` i `s_lastBroadcastMs` były do tego przeznaczone. Każdy cykl budował wszystkie payloady — w tym `buildTimePayload()` → `RTCService::getEpoch()` → `I2cShared::lock()` oraz `buildOutdoorWeatherPayload()` → `taskENTER_CRITICAL`. Niepotrzebna kontencja I2C i wyłączanie przerwań ~100 razy/s.

**CAUSE:** `s_broadcastIntervalMs` istniała (z defaultem 30s), ale `broadcastSnapshots()` nigdy nie sprawdzała `s_lastBroadcastMs`. Funkcja działała bez ogranicznika, budując payloady w każdej iteracji pętli.

**LOGIC_CHANGE:**
- `broadcastSnapshots()`: dodano throttle `if (nowMs - s_lastBroadcastMs < s_broadcastIntervalMs) return;` na samym początku
- `kDefaultBroadcastIntervalMs`: zmieniono z 30000ms (30s) na **200ms** — sensowny cykl odświeżania danych dla Guition
- Teraz payloady są budowane 5 Hz zamiast 50-100 Hz — redukcja o 90-95%
- Indywidualne throttle wysyłek (WiFi 2s, outdoor 5s, resources 2s) pozostają bez zmian

**VERIFICATION:** Kompilacja OK. broadcastSnapshots() wykonuje build*Payload co 200ms zamiast w każdej pętli (ok. 10ms). Kontencja I2C i CRITICAL sekcje zredukowane do 5 Hz.
---
### [ID: ERR_015] | CORE_APP | IMPACT: HIGH
**Files:** `[TaskConfig.h, AppLoop.cpp]`

**PROBLEM:** Cykliczne **"zamarzanie" UI (obrazu i enkodera) co 2 sekundy**, szczególnie dotkliwe w menu. System przestawał reagować na ruchy użytkownika na kilkaset milisekund.

**CAUSE:** 
1. **Konflikt priorytetów:** `MeteoSyncTask` (priorytet 4) na Core 1 wywłaszczał pętlę główną `loopTask` (priorytet 1). Ciężkie operacje SSL/JSON przy pobieraniu pogody całkowicie blokowały UI.
2. **Kolizja UART:** Zdarzenie `EV_DIAGNOSTICS` (2s) i synchronizacja Guition (2s) uderzały w UART w tej samej milisekundzie, zapychając bufor TX i blokując pętlę główną.
3. **Kosztowna Diagnostyka:** Skanowanie sterty i stosów w `onDiagnostics` trwało zbyt długo w kontekście UI.

**LOGIC_CHANGE:**
- **Priorytety:** Obniżono `MeteoSyncTask` z 4 na **1**. Podniesiono `loopTask` z 1 na **2** (przez `vTaskPrioritySet`). Teraz UI ma absolutne pierwszeństwo przed pogodą.
- **Jitter/Offset:** Dodano **500ms offsetu** do timera `EV_DIAGNOSTICS`. Diagnostyka i Guition są teraz rozdzielone czasowo, co zapobiega zatorom UART.
- **Safe I2C:** Potwierdzono, że zmiana priorytetów nie wpływa na `I2cWorkerTask` (priorytet 12), który nadal wyprzedza UI.

**VERIFICATION:** Nawigacja w menu jest płynna. Pobieranie pogody w tle nie powoduje już przeskakiwania kroków enkodera ani zamarzania animacji.
---
### [ID: ERR_014] | COMMS_MQTT | IMPACT: CRITICAL
**Files:** `[MQTTSync.cpp]`

**PROBLEM:** `mqttClient.connect()` blokował pętlę główną na **3-5 sekund** podczas łączenia z brokerem MQTT (TCP 3s + TLS handshake 2s). Podczas blokady enkoder, LCD i sensory były zamrożone. Szczególnie bolesne przy pierwszym starcie i słabym WiFi.

**CAUSE:** `MQTTSync::update()` wołana z `NetworkOrchestrator::update()` co 200ms wywoływała `mqtt_reconnect()` → `mqttClient.connect()` synchronicznie w kontekście taska AppLoop.

**LOGIC_CHANGE:**
- Dodano `mqttConnectTask()` — one-shot FreeRTOS task na **Core 0** (nie blokuje AppLoop na Core 1)
- `mqtt_reconnect()` tworzy task zamiast wołać `connect()` bezpośrednio
- `MQTTSync::update()`: dodano obsługę stanu `Connecting` — sprawdza `s_connectResult` atomic flag
- `stopCore1Task()`: czeka do 2s na zakończenie taska łączącego (z `vTaskDelay`)
- Task tworzony z priorytetem 1, stack 4096, pinned do Core 0
- Gdy `xTaskCreatePinnedToCore` fail (brak pamięci) — fallback do synchroniznego connect

**VERIFICATION:** Podczas łączenia MQTT (`mqttClient.connect()` blokuje na Core 0), AppLoop kontynuuje obsługę enkodera, LCD i sensorów bez przerwy. Brak widocznych zacięć.
---
### [ID: ERR_013] | I2C_SHARED | IMPACT: MEDIUM
**Files:** `[I2C_bus_shared.cpp]`

**PROBLEM:** Ryzyko **Use-after-free** przy timeoutach I2C dla ścieżki worker-async (task nie trzymający mutexu). Gdy `submitRequest()` timeoutował, a worker przetwarzał request później, `writeData` wskazywał na bufor na stosie wołającego — potencjalnie już zwolniony.

**CAUSE:** `I2cRequest.writeData` był surowym wskaźnikiem do danych wołającego. Gdy wołający timeoutował i zwolnił stos, worker mógł czytać z nieprawidłowej pamięci.

**LOGIC_CHANGE:**
- Dodano `uint8_t writeDataBuf[64]` do `struct I2cRequest` — wewnętrzny bufor na dane do zapisu
- `I2cShared::write()` i `writeRead()`: kopiują dane do `writeDataBuf` przez `memcpy` przed queue
- Worker odczytuje z `writeDataBuf` (bezpieczny, własność requesta)
- Limituje copy do `min(len, kWriteBufSize)` — brak overflow
- `acquireRequest()` i `releaseRequest()` odpowiednio zaktualizowane

**VERIFICATION:** Nawet przy timeoutzie submitRequest, dane do zapisu są bezpiecznie skopiowane do bufora w puli requestów. Worker zawsze czyta poprawne dane. Brak ryzyka dangling pointer.
---
### [ID: ERR_012] | I2C_SHARED | IMPACT: CRITICAL
**Files:** `[I2C_bus_shared.cpp]`

**PROBLEM:** Ryzyko **Use-after-free** (uszkodzenie pamięci) przy timeoutach I2C. Funkcja `submitRequest` mogła zwrócić błąd i pozwolić taskowi wywołującemu na zwolnienie buforów (np. ze stosu), podczas gdy `i2cWorkerTask` nadal posiadał wskaźnik do tych danych i próbował do nich pisać/czytać po czasie.
**CAUSE:** Czas oczekiwania na semafor `done` był identyczny z timeoutem operacji I2C. Przez narzut systemu (Context Switching), task główny mógł "odpuścić" dokładnie w tym samym momencie, w którym worker dopiero kończył pracę.
**LOGIC_CHANGE:**
- Dodano `safetyMarginMs = 100` do czasu oczekiwania `xSemaphoreTake`.
- Gwarantuje to, że wątek główny czeka **zawsze dłużej** niż maksymalny dopuszczalny czas pracy wątku roboczego.
- Task główny wychodzi z funkcji tylko wtedy, gdy ma 100% pewności, że worker już nie dotyka jego buforów.
**VERIFICATION:** Analiza logiczna potwierdzona. Brak wyścigów przy symulowanych timeoutach.

---
### [ID: ERR_011] | CORE_EVENTBUS | IMPACT: HIGH
**Files:** `[EventBus.cpp]`

**PROBLEM:** Kumulatywny dryft czasu systemowego i **"znikające sekundy"** (np. przeskoki z `:59` na `:01`).
**CAUSE:** Timer w `EventBus::process()` resetował się za pomocą `lastFireMs = now;`. Każde opóźnienie pętli głównej (np. 15ms) dodawało się do interwału. Po ok. 60 sekundach dryft osiągał pełną sekundę, co powodowało, że `tickClock` czytał z systemu czas już o 2 sekundy nowszy.
**LOGIC_CHANGE:**
- Zmieniono reset na `lastFireMs += s_timers[i].periodMs;`.
- System teraz "pamięta" planowany czas odpalenia i nadrabia ewentualne spóźnienia w kolejnym cyklu.
**VERIFICATION:** Zegar tyka w idealnym rytmie 1.000s, niezależnie od obciążenia pętli głównej.

---
### [ID: ERR_010] | I2C_LCD | IMPACT: MEDIUM
**Files:** `[AppBoot.cpp]`

**PROBLEM:** Zawieszanie/lagowanie startu systemu przy odłączonym wyświetlaczu LCD 20x4.
**CAUSE:** `lcd.init()` był wywoływany w ciemno. Brak urządzenia na szynie powodował serię blokujących timeoutów I2C przy każdej komendzie inicjalizacyjnej, spowalniając boot o kilka sekund.
**LOGIC_CHANGE:**
- Przeniesiono inicjalizację `I2cShared::initMaster` przed LCD.
- Dodano `I2cShared::probe(0x27)` przed startem LCD.
- Wprowadzono flagę `s_lcdDetected`. Jeśli LCD nie ma, `lcd.init()` i wszystkie funkcje `backlight/clear` są pomijane.
**VERIFICATION:** System wstaje błyskawicznie bez podpiętego LCD. Brak niepotrzebnego ruchu na I2C.

---
### [ID: ERR_009] | SENSOR_I2C | IMPACT: HIGH
**Files:** `[BMP280Sensor.cpp]`

**PROBLEM:** BMP280Sensor::update() wołała `tryRecover()` co 5s nawet gdy sensor był nieobecny od startu. Każda próba `initializeAtAddress()` → `I2cShared::probeAddress()` z timeoutem 10ms × 2 retries × 2 adresy = **40ms blokady I2C** co 5 sekund. Przy 3 odłączonych sensorach blokada sumowałaby się do 120ms, zamrażając enkoder i ekran.

**CAUSE:** Brakowało flagi `s_isPresent` ustawianej raz w `begin()` po próbie detekcji. `update()` nie odróżniał "sensor nigdy nie był obecny" (nie ma sensu retry) od "sensor był obecny i padł" (warto retry). `tryRecover()` wykonywała pełną sekwencję I2C probe dla obu adresów (0x76, 0x77) przy każdej próbie.

**LOGIC_CHANGE:**
- Dodano `static bool s_isPresent` w anonimowej przestrzeni nazw
- `begin()`: `s_isPresent = true` po udanym `initializeAtAddress()`, `s_isPresent = false` po failure
- `update()`: pierwsza linia po guardzie `started` → `if (!s_isPresent) return;` — **zero I2C traffic** jeśli sensor nie był wykryty przy starcie
- `tryRecover()` wołana tylko dla `SensorState::Error` (sensor był obecny, ale przestał działać), nigdy dla `Missing` (nie był obecny od początku)
- ENS160/AHT21/PMS5003 już miały analogiczne flagi (`s_ens160Present`, `s_ahtPresent`, `s.enabled`) — potwierdzono poprawność

**VERIFICATION:** Kompilacja OK. Z odłączonym BMP280: `update()` zwraca w <1µs, brak I2C probe w tle. Ekran pokazuje "MISSING" ustawione w `begin()`.
---
### [ID: ERR_008] | I2C_ENS160 | IMPACT: MEDIUM
**Files:** `[ENS160AHT21Sensor.cpp]`

**PROBLEM:** Sekwencja inicjalizacji ENS160 (`begin()` → `init()` → `startStandardMeasure()`) wykonywana z przerwami blokady I2C — `begin()` bez lock, `init()` pod lock ale unlock przed `startStandardMeasure()`. Pomiędzy unlock a kolejnym lock, i2cWorkerTask może wykonać operacje na magistrali, zakłócając proces budzenia ENS160.
**CAUSE:** `tryInitENS160AtAddress()` wołała `s_ens160.begin()` bez I2cShared::lock, a `init()` i `startStandardMeasure()` w osobnych lock/unlock parach. Okno między unlock a lock = ryzyko kolizji I2C.
**LOGIC_CHANGE:**
- `begin(&Wire, address)` przeniesione pod tę samą blokadę co `init()`
- `init()` + `startStandardMeasure()` objęte jedną parą lock/unlock bez przerwy
- `begin()` wołane tylko w pierwszej próbie pętli (attempt==0), kolejne próby init nie wymagają ponownego begin
- `vTaskDelay` między retry poza blokadą (nie blokuje magistrali podczas oczekiwania)
**VERIFICATION:** Kompilacja OK. Sekwencja init nieprzerywalna przez i2cWorkerTask.
---
### [ID: ERR_007] | I2C_TIMEOUT | IMPACT: HIGH
**Files:** `[I2C_bus_shared.cpp, BMP280Sensor.cpp]`

**PROBLEM:** `wire->setTimeOut((uint16_t)...)` rzutuje `uint32_t` na `uint16_t` bez `constrain()`. Jeśli timeout > 65535ms (np. 70000ms z konfiguracji), wartość jest obcinana modulo 65536 (→ ~4464ms). Operacje I2C przerywane znacznie wcześniej niż oczekiwano → losowe "Bus Busy".
**CAUSE:** `Wire.setTimeOut()` przyjmuje `uint16_t`, ale kod przekazuje `uint32_t` bez zakreskowania. Występuje w 3 miejscach w `I2C_bus_shared.cpp` (executeProbe, executeWrite, executeWriteRead) i 2 w `BMP280Sensor.cpp` (readBytes, writeReg).
**LOGIC_CHANGE:**
- Wszystkie 5 wywołań `setTimeOut()` opakowane w `constrain(value, 1UL, 65535UL)` przed rzutowaniem
**VERIFICATION:** Kompilacja OK. Czasownik timeout bezpiecznie zakreskowany do uint16_t.
---
### [ID: ERR_006] | I2C_RTC | IMPACT: HIGH
**Files:** `[RTCService.cpp]`

**PROBLEM:** Operacje I2C na układzie DS3231 (RTC) wykonywane bez `I2cShared::lock/unlock`. Jeśli w tym samym czasie i2cWorkerTask wykonuje zlecone operacje (probe, write, read) na współdzielonej magistrali I2C, dochodzi do kolizji → zawieszenie komunikacji I2C.
**CAUSE:** `RTCService::getEpoch()`, `setEpoch()`, `getTm()`, `setTm()` wołały metody `gRtc` (`ErriezDS3231`) bezpośrednio na `Wire`, bez uprzedniego przejęcia muteksu przez `I2cShared::lock()`.
**LOGIC_CHANGE:**
- `getEpoch()`: `I2cShared::lock(gConfig.i2cTimeoutMs)` przed `gRtc.getEpoch()`, unlock po.
- `setEpoch()`: lock przed `gRtc.setEpoch()` i ewentualnym verify read, unlock po.
- `getTm()`: lock przed `gRtc.read()`, unlock po.
- `setTm()`: lock przed `gRtc.write()`, unlock po.
- Wszystkie ścieżki błędu (early return) wykonują unlock przed zwróceniem statusu.
**VERIFICATION:** Kompilacja OK. Ryzyko kolizji I2C wyeliminowane.
---
### [ID: ERR_005] | CORE_CLOCK | IMPACT: CRITICAL
**Files:** `[ClockAlarmService.cpp, UI_Controller.cpp, UI_Draw.cpp]`

**PROBLEM:** Zegar zatrzymywał się podczas edycji czasu (`STATE_SET_TIME`). Budzik mógł nie zadzwonić jeśli alarm minął w trakcie edycji. Po wyjściu z edycji catch-up limit `loops<60` gubił nadmiarowe sekundy.
**CAUSE:** `tickClock()` miał guard `if (appState == STATE_SET_TIME) return;`. `adjustTime_internal()` modyfikował `ClockService` bezpośrednio (zamiast zamrożonej kopii). Alarm check `seconds==0` był poza pętlą catch-up.
**LOGIC_CHANGE:**
- Usunięto guard `STATE_SET_TIME` z `tickClock()` — zegar tyka w tle podczas edycji.
- Wprowadzono frozen edit time (`g_editH/g_editM/g_editS` snapshot z `Clock::hms()` przy wejściu w edycję).
- `adjustTime_internal()` modyfikuje frozen copy, nie `ClockService`.
- `drawSetTime`/`printTime`/`updateSevenSeg` czytają z frozen copy podczas `STATE_SET_TIME`.
- Na `EDIT_DONE`: `Clock::set(g_editH, g_editM, g_editS)` + applyToSystemTime + scheduleRtcWrite.
- Usunięto limit `loops < 60` w catch-up loop — wszystkie stracone sekundy są odrabiane.
**VERIFICATION:** Budzik dzwoni o właściwej porze nawet przy długiej edycji. 7-seg pokazuje edytowany czas, nie rzeczywisty.
---
### [ID: ERR_004] | CORE_CLOCK | IMPACT: HIGH
**Files:** `[UI_Draw.cpp, UI_Controller.cpp, ClockService.cpp, RtcSyncService.cpp]`

**PROBLEM:** Potrójny bug: (1) Ghost Time — wyświetlacz priorytetyzował `localtime_r` (system time) nad `ClockService`, więc manualna edycja znikała. (2) Brak backward sync — `applyToSystemTime()` i `scheduleRtcWrite()` nie były wołane po edycji. (3) 7-seg niespójny — alarm czytał z system time, normal z ClockService.
**CAUSE:**
- `updateSevenSeg()`: gałąź `if (systemTimeValid)` czytała `localtime_r`, ignorując `Clock::adjust()`.
- `UI_Controller::EDIT_DONE`: tylko `Clock::setLastTick(millis())` + `markClockSeeded()`, brak `settimeofday` i DS3231 write.
- `drawExtremeEnvironmentScreen` używał `snprintf(row2, "%02d:%02d:%02d", timeinfo.tm_hour...)` zamiast `Clock::formatHms()`.
**LOGIC_CHANGE:**
- Usunięto wszystkie gałęzie `if (systemTimeValid)` z `updateSevenSeg()` i `drawExtremeEnvironmentScreen()` — zawsze `Clock::hms()` lub `Clock::formatHms()`.
- Dodano `Clock::applyToSystemTime()`: snapshot ClockService → `settimeofday` z zachowaniem daty.
- Dodano `RtcSyncService::scheduleRtcWrite()`: wrapper na `scheduleRtcWriteFromSystemTime()`.
- `UI_Controller::EDIT_DONE`: woła `Clock::set(g_editH...)` + `applyToSystemTime()` + `scheduleRtcWrite()`.
**VERIFICATION:** Po manualnej zmianie czasu: wyświetlacz pokazuje nową godzinę, `time(nullptr)` zwraca zaktualizowany czas, DS3231 scheduluje write. Po resecie czas z DS3231 jest poprawny.
---
### [ID: ERR_003] | COMMS_WIFI | IMPACT: CRITICAL
**Files:** `[WiFiSync.cpp, WiFiSync.h, AppBoot.cpp]`

**PROBLEM:** Synchronizacja NTP nigdy nie aktualizowała `ClockService`. Zegar na wyświetlaczu pozostawał na 12:00 na zawsze po starcie, mimo poprawnego połączenia WiFi i pobrania czasu z sieci.
**CAUSE:** `AppBoot::setTimeRefs(Clock::hours(), ...)` — `Clock::hours()` zwraca `int` (rvalue), więc kompilator wybrał value-overload zamiast reference-overload. `pHours = &s_hours` wskazywał na LOKALNĄ kopię w `WiFiSync.cpp`, nie na dane w `ClockService`. Po NTP: `*pHours = timeinfo.tm_hour` zapisywał do lokalnej kopii. ClockService nigdy nie widział zsynchronizowanego czasu.
**LOGIC_CHANGE:**
- Usunięto cały mechanizm wskaźnikowy (`pHours/pMinutes/pSeconds/pLastTick`, lokalne `s_hours/s_minutes/s_seconds`, oba overloady `setTimeRefs`).
- `WiFiSync::update()` po udanym NTP woła bezpośrednio `Clock::set(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec)` + `Clock::setLastTick(millis())`.
- `AppBoot.cpp`: usunięto wywołanie `WiFiSync::setTimeRefs(...)`.
**VERIFICATION:** Po NTP: ClockService ma poprawną godzinę, wyświetlacz przeskakuje na właściwy czas. Budziki dzwonią o dobrej porze.
---
### [ID: ERR_002] | BLUETOOTH | IMPACT: CRITICAL
**Files:** `[src/bluetooth/AudioBT.cpp]`

**PROBLEM:** Błąd `esp_bt_controller_init failed: 259` podczas startu Bluetooth. System nie mógł zainicjować kontrolera BT.
**CAUSE:** Arduino core automatycznie zwalniał pamięć kontrolera Bluetooth (BT memory release), ponieważ brakowało poprawnej implementacji hooka `btInUse()`. Framework uznawał, że BT nie jest używane i czyścił zasoby przed ich inicjalizacją przez bibliotekę audio.
**LOGIC_CHANGE:**
- Zdefiniowano `extern "C" bool btInUse() { return true; }`, co blokuje mechanizm zwalniania pamięci BT.
- Start BT przechodzi teraz pełną ścieżkę: `BT enabled` -> `controller initialized` -> `bluedroid initialized` -> `Init Compl`.
**VERIFICATION:** Bluetooth startuje poprawnie, brak błędów alokacji kontrolera w logach szeregowych.
---
### [ID: ERR_001] | CORE_APP | IMPACT: CRITICAL
**Files:** `[AppLoop.cpp, HomeRuntime.cpp, EsptoGuition.cpp]`

**PROBLEM:** Zacinanie się animacji boot intro, przerywanie dźwięku i resety Watchdog podczas startupu.
**CAUSE:** Blokujący `return` w pętli głównej wstrzymywał system. Dodatkowo moduł `EsptoGuition` zalewał szynę I2C zapytaniami do RTC w każdym cyklu pętli.
**LOGIC_CHANGE:**
- Usunięto blokadę `return`, pozwalając na nieblokujące działanie intro.
- Wprowadzono **Throttling (100ms)** dla `EsptoGuition::update()`.
- Dodano scentralizowaną ochronę `if (BootIntroService::isActive()) return;` w handlerach `EventBus`.
**VERIFICATION:** Animacja i dźwięk płynne przy aktywnym tle systemowym (WiFi/Sensory).
---

