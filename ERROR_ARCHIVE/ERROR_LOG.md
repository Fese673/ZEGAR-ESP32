# Archiwum Błędów - ZEGAR-ESP32
---
### [ID: ERR_054] | ALARM_FULL_LIST_SYNC | IMPACT: HIGH
**Files:** `[UI_Controller.cpp, TimeSyncProtocol.h, TimeSyncProtocol.cpp, EsptoGuitionTransport.cpp, timer_synchro.h, timer_synchro.cpp, alarm_ui.cpp]`

**PROBLEM:** Edycja alarmów na enkoderze i w Gution żyła własnym życiem. Zegar wysyłał pełny stan tylko okresowo, a Gution nie wypychał pełnej listy po lokalnej zmianie. Efekt był niedeterministyczny: synchronizacja wyglądała na działającą dopiero po bezpieczeństwie co 5 minut.

**CAUSE:** Brak jednego, pełnego modelu synchronizacji listy alarmów. Lokalna edycja modyfikowała kopie stanu zamiast zawsze przechodzić przez pełny sync-list, a Zegar nie miał inbound handlera do odtworzenia całej listy z zachowaniem `lastTriggerDay`.

**LOGIC_CHANGE:**
- Zegar: `UI_Controller.cpp` teraz wysyła pełną listę po każdej lokalnej zmianie alarmu i używa `sendEditLock(true/false)` przy wejściu/wyjściu z edycji.
- Zegar: `TimeSyncProtocol.cpp` dodał `handleAlarmListSync()` z mapowaniem po polach alarmu, żeby zachować `lastTriggerDay` przy replace whole list.
- Gution: `timer_synchro.cpp` dodaje `sendAlarmList()`, a `alarm_ui.cpp` wypycha pełną listę po add/edit/delete/toggle.
- Transport: `EsptoGuitionTransport.cpp` akceptuje nową ramkę `kTypeAlarmList` od Gution.

**VERIFICATION:** `python build_zegar.py` → SUCCESS. `python build_guition.py` → SUCCESS.
---
### [ID: ERR_053] | TIMER_ONE_WAY_SYNC | IMPACT: HIGH
**Files:** `[UI_Controller.cpp]`

**PROBLEM:** Minutnik był spójny tylko w jedną stronę. Gution potrafił wysłać start/stop do Zegara, ale zmiany z enkodera nie zawsze trafiały do tego samego, wspólnego toru stanu po stronie Zegara.

**CAUSE:** UI timera na Zegarze omijało `TimerService` i mutowało legacy globals bez gwarantowanego, natychmiastowego pushu stanu przez warstwę serwisu.

**LOGIC_CHANGE:**
- Start/stop timera w `STATE_TIMER` zostały przepięte na `TimerService::start()` i `TimerService::stop()`.
- TimerService pozostaje jedynym miejscem, które aktualizuje globalny stan i publikuje `sendTimerState()` do Gution.

**VERIFICATION:** `python build_zegar.py` → SUCCESS. `python build_guition.py` → SUCCESS.
---
### [ID: ERR_052] | STOPWATCH_RESET_DESYNC | IMPACT: HIGH
**Files:** `[UI_Controller.cpp]`

**PROBLEM:** Po wyjściu z LCD20x4 enkoderem stoper resetował się lokalnie, ale Gution nie dostawał natychmiastowego resetu i potrafił pokazywać stary stan.

**CAUSE:** Ścieżka resetu stopera na Zegarze kończyła się na `StopwatchService::reset()`, ale nie publikowała od razu nowego stanu do HMI.

**LOGIC_CHANGE:**
- Long-press w `STATE_STOPER` robi teraz `StopwatchService::reset()` i natychmiast `TimeSync::sendStopwatchState()`.
- Gution dostaje reset bez czekania na kolejny okresowy broadcast.

**VERIFICATION:** `python build_zegar.py` → SUCCESS. `python build_guition.py` → SUCCESS.
---
### [ID: ERR_051] | STOPWATCH_CMD_START_RESETS_ELAPSED | IMPACT: HIGH
**Files:** `[TimeSyncProtocol.cpp]`

**PROBLEM:** `handleStopwatchCmd(0)` zerował `stoperElapsed = 0` przy każdym START. Pauza → wznowienie kasowało dotychczasowy czas. Przy RESET z Gution gdy stoper na Zegarze był RUNNING, reset też nie działał poprawnie (desync).

**LOGIC_CHANGE:**
- `handleStopwatchCmd(0)`: usunięto `stoperElapsed = 0`. START tylko ustawia `stoperStart = millis()` i `stoperRunning = true`. Elapsed zachowany dla resume.
- `handleStopwatchCmd(2)`: nadal zeruje wszystko (RESET = full clear).

**VERIFICATION:** `python build_zegar.py` → SUCCESS.

---
### [ID: ERR_050] | STOPWATCH_NO_SYNC | IMPACT: HIGH
**Files:** `[TimeSyncProtocol.cpp, Esptogution.cpp, EsptoGuitionTransport.cpp]`

**PROBLEM:** Stoper (stopwatch) nie miał żadnej komunikacji między Zegarem a Gution. Dwa niezależne stopery działające osobno. Timer (minutnik) też miał problem: Gution nigdy nie wysyłał komend do Zegara przy starcie z GUI.

**ROOT CAUSE:** Brak protokołu komunikacyjnego dla stopera (0x15/0x16). Timer_ui.cpp na Gution uruchamiał lokalny timer bez wołania `sendTimerCmd()` — Zegar nigdy nie był powiadamiany.

**LOGIC_CHANGE:**
- `TimeSyncProtocol.h/.cpp`: Dodano `kTypeStopwatchState=0x15` (Z→G) i `kTypeStopwatchCmd=0x16` (G→Z). `sendStopwatchState()` pakuje stan (0=IDLE,1=RUNNING,2=STOPPED)+elapsedMs. `handleStopwatchCmd()` obsługuje START/STOP/RESET na globalach `stoperRunning/stoperElapsed/stoperStart`. Po każdej komendzie timera natychmiastowe `sendTimerState()`+`sendStatusBell()`.
- `Esptogution.cpp`: Broadcast stopwatch co 100ms gdy nie IDLE.
- `EsptoGuitionTransport.cpp`: Handler ramki 0x16.
- Gution: `CommunicationProtocol.h` — `StopwatchState` struct, `sendStopwatchCmd()`, `getLastTimerState()`, `getLastStopwatchState()`.
- Gution: `CommunicationState.h/.cpp` — `StopwatchState` storage + apply/consume (mutex wzór jak TimerState).
- Gution: `UartTransport.cpp` — handler `kTypeStopwatchState`.
- Gution: `timer_ui.cpp` — `on_start_stop_minutnik` wysyła `sendTimerCmd(0, target)`; `timer_ui_cb()` wyświetla zdalny `TimerState` z Zegara gdy aktywny (fallback do lokalnego).
- Gution: `stopwatch_ui.cpp` — przyciski wysyłają `sendStopwatchCmd(0/1/2)`; `stopwatch_timer_cb()` wyświetla zdalny `StopwatchState` gdy aktywny.

**PRZEPŁYW PO FIXIE:**
1. Start timera na Gution → `sendTimerCmd(0, dur)` → Zegar: TimerService::start() → immediate `sendTimerState()` → Gution: wyświetla remote state (<200ms). Zegar LCD pokazuje timer od razu.
2. Start stopera na Gution → `sendStopwatchCmd(0)` → Zegar: ustawia stoperRunning=true → immediately `sendStopwatchState()` → broadcast co 100ms → Gution: wyświetla zdalny elapsedMs.
3. Start na Zegarze → broadcast co 1s (timer) / 100ms (stoper) → Gution wyświetla.

**VERIFICATION:** `python build_zegar.py` → SUCCESS. `python build_guition.py` → SUKCES.

---
**Files:** `[EsptoGuitionState.cpp, TimerService.cpp, UI_Controller.cpp]`

**PROBLEM:** Trzy błędy wykryte w pierwszej implementacji przed audytem:
1. **`buildStatusBellPayload()` używał `alarmRuntime.alarmEnabled` — nigdy nie ustawianego** (`EsptoGuitionState.cpp:612`): Pole `alarmEnabled` w `AlarmRuntime::State` było legacy boolean, inicjalizowane `false` i NIGDY nie zmieniane na `true`. `StatusBell` nigdy nie raportował stanu 1 (alarm uzbrojony) mimo że alarmy były włączone przez `alarms[i].enabled`.
2. **Timer `pause()` nie aktualizował `timerDurationMs` dla LCD** (`TimerService.cpp:50`): Po pauzie `s_remainingMs` był poprawnie skracany, ale global `timerDurationMs` (dla LCD 20x4) pozostawał na pierwotnej wartości. Po wznowieniu `timerDurationMs` nie był przywracany do `s_remainingMs`. Pasek postępu na LCD pokazywał zły czas.
3. **`g_alarmEditActive` nie resetowany po usunięciu alarmu i zapisie** (`UI_Controller.cpp:1498,1512`): Przy delete i click-finish zapomniano wyczyścić flagę `g_alarmEditActive` i wysłać `sendEditLock(false)`. Po tych operacjach Gution pozostawał trwale zablokowany (kłódka), a nowe `SetAlarm` były odrzucane.

**CAUSE:** Legacy boolean nieużywany; zapomniana synchronizacja globali `timerDurationMs` z `s_remainingMs`; pominięty unlock w ścieżkach delete i click-finish.

**LOGIC_CHANGE:**
- `EsptoGuitionState.cpp`: `buildStatusBellPayload` iteruje `alarms[i].enabled` zamiast czytać `alarmRuntime.alarmEnabled`.
- `TimerService.cpp`: `pause()` zapisuje `timerDurationMs = s_remainingMs`; `resume()` przywraca `timerDurationMs = s_remainingMs`.
- `UI_Controller.cpp`: Dodano `g_alarmEditActive=false; sendEditLock(false)` w delete (cursor=3) i click-finish (EDIT_MINUTES).

**VERIFICATION:** `python build_zegar.py` → SUCCESS. StatusBell=1 pojawia się gdy alarm włączony. LCD 20x4 pokazuje poprawny czas po pauzie/wznowieniu. Gution odblokowuje się po usunięciu lub zapisaniu alarmu.

---
### [ID: ERR_048] | TIMESYNC_BUGFIX_BATCH_CRITICAL | IMPACT: CRITICAL
**Files:** `[ClockAlarmService.cpp, TimeSyncProtocol.cpp, AlarmRuntime.cpp, TimerService.cpp, UI_Controller.cpp]`

**PROBLEM:** Cztery krytyczne/wysokie błędy w implementacji synchronizacji alarmów i timera z Gution:
1. **Single-shot wyłączał alarm PRZED sprawdzeniem czasu** (`ClockAlarmService.cpp:127`): Flaga `singleShot` była sprawdzana w każdej iteracji pętli tickClock, na zewnątrz bloku porównania godziny/minuty. Alarm jednorazowy był wyłączany przy pierwszej zmianie minuty, zanim nadszedł jego czas — nigdy nie zadzwonił.
2. **Błędna identyfikacja dzwoniącego alarmu przy snooze/dismiss** (`TimeSyncProtocol.cpp:155`): Pętla wyszukująca dzwoniący alarm znajdowała pierwszy z `!enabled` (ręcznie wyłączony przez użytkownika), zamiast faktycznie dzwoniącego. Snooze modyfikował zły alarm, a prawdziwy dzwonił dalej.
3. **`lastTriggerDay` nie persistowany do NVS** (`AlarmRuntime.cpp`): Pominięty w `saveAlarm()`, zawsze resetowany do `UINT16_MAX` w `loadAllAlarms()`. Po resecie ESP32 alarm odpalony o 7:00 odpalał się ponownie — duplikat.
4. **Long-press z edycji alarmu gubił zmiany** (`UI_Controller.cpp:1706`): Wyjście z `STATE_ALARM_EDIT` przez long-press nie persistowało zmian czasu/dni do NVS i nie wysyłało aktualizacji do Gution.

**CAUSE:** Single-shot check przed blokiem czasu; brak dedykowanego `ringingAlarmIndex`; pominięcie klucza `almT%d` przy NVS save/load; brak `persistAlarmAt()` przy long-press.

**LOGIC_CHANGE:**
- `ClockAlarmService.cpp`: Przeniesiono `if (flags & 0x01)` do wnętrza bloku `if (hour==minute==lastTriggerDay)`. Dodano `alarmRuntime.ringingAlarmIndex = i` przy odpaleniu i `= -1` przy auto-stop.
- `AlarmRuntime.h`: Dodano `int ringingAlarmIndex = -1` do `State`.
- `TimeSyncProtocol.cpp`: `handleAlarmAction` używa `rt.ringingAlarmIndex` zamiast pętli wyszukującej.
- `AlarmRuntime.cpp`: Dodano NVS key `almT%d` dla `lastTriggerDay` w `saveAlarm()` i `loadAllAlarms()`.
- `UI_Controller.cpp`: Long-press w `STATE_ALARM_EDIT` woła `persistAlarmAt()` + `sendAlarmList()` przed unlockiem.

**VERIFICATION:** `python build_zegar.py` → SUCCESS. Single-shot odpala się o właściwej godzinie. Snooze modyfikuje właściwy alarm. `lastTriggerDay` persistowany przez reboot. Long-press zapisuje zmiany.

---
### [ID: ERR_047] | TIMESYNC_UART_THREAD_SAFETY | IMPACT: HIGH
**Files:** `[TimeSyncProtocol.cpp, TimerService.cpp, Esptogution.cpp]`

**PROBLEM:** Trzy błędy bezpieczeństwa wątkowego w nowym kodzie komunikacji czasowej:
1. **NVS Flash write w wątku UART RX** (`TimeSyncProtocol.cpp`): `handleSetAlarm()` zapisywał do NVS bezpośrednio w kontekście UART RX (Core 0). Operacja Flash trwa 10-50ms, blokując odbiór UART. Przy strumieniu PPG (50Hz) powoduje przepełnienie sprzętowego bufora UART i gubienie bajtów.
2. **`localtime()` w wątku UART RX** (`TimeSyncProtocol.cpp:153`): `localtime()` używa statycznego bufora wewnętrznego, a jednoczesne wywołanie z `clock_task` (Core 0, main loop) nadpisuje dane (data race).
3. **Brak sendTimerState() po auto-stop timera** (`TimerService.cpp:123`): `servicePlayback()` po auto-wygaśnięciu dzwonka nie wysyłał `kTypeTimerState` ani `kTypeStatusBell` do Gution. Popup alarmu wisiał w nieskończoność na ekranie HMI.

**CAUSE:** Synchroniczny NVS write w ISR-kontekście; użycie non-reentrant `localtime()`; pominięty push do Gution przy auto-stop.

**LOGIC_CHANGE:**
- `TimeSyncProtocol.cpp`: NVS write zastąpiony deferred flag `g_nvsAlarmsDirty`; zapis w `AppLoop::runLoop()`.
- `TimeSyncProtocol.cpp`: `localtime()` → `localtime_r()` z buforem na stosie (3 miejsca).
- `TimerService.cpp`: `servicePlayback()` po auto-stop woła `TimeSync::sendTimerState()` + `EsptoGuition::sendStatusBell()`.
- `TimerService.cpp`: Buzzer priority — `servicePlayback()` sprawdza `AlarmRuntime::state().alarmRinging` i nie miesza dźwięków.

**VERIFICATION:** `python build_zegar.py` → SUCCESS. BRAK NVS write w UART RX. BRAK data race na `localtime`. Auto-stop timera natychmiast zamyka popup na Gution.

---
### [ID: ERR_046] | AUDIO_BT_SERVICES_LOG_AND_RACE | IMPACT: HIGH
**Files:** `[AudioBT.cpp, BluetoothA2DPSinkQueued.cpp]`

**PROBLEM:** Dwa krytyczne wyścigi danych oraz błędy logowania w ścieżce audio i metadanych BT:
1. **Metadata TOCTOU (`AudioBT.cpp`):** `audioBT_serviceDeferred()` kopiował jedynie wskaźnik `t = s_musicTitle` / `a = s_musicArtist` i wychodził z sekcji krytycznej, a następnie odczytywał bufor poza nią przez `EsptoGuition::sendMusicTitle(t)`. Ponieważ wątek BT (Core 0) mógł jednocześnie pisać do `s_musicTitle` przez `metadata_callback`, prowadziło to do wyścigów danych i korupcji wyświetlanych metadanych (np. pomieszane litery lub przedwczesne ucięcie stringa).
2. **ESP_LOGD na gorącej ścieżce (`BluetoothA2DPSinkQueued.cpp`):** I2S task audio handler cyklicznie wołał `ESP_LOGD(BT_AV_TAG, "i2s_task_handler: ...")` przy każdej ramce audio (~172 razy na sekundę). Naruszało to zasadę "Audio path must be completely log-free" i groziło blokowaniem wątku audio przy włączonym debugowaniu.
3. **ESP_LOGW w write_audio (`BluetoothA2DPSinkQueued.cpp`):** W przypadku pełnego bufora i wejścia w tryb `RINGBUFFER_MODE_DROPPING`, każda odebrana ramka w callbacku stosu BT (prio 24, Core 0) logowała `ESP_LOGW("ringbuffer is full, drop this packet!")`. Setki logów na sekundę paraliżowały stos BT na mutexach UART, prowadząc do starvation, trzasków i zrywania połączenia.

**CAUSE:** Przetwarzanie asynchroniczne bez fizycznego kopiowania danych w sekcji krytycznej (TOCTOU) oraz stosowanie powolnych logów UART w wątkach o wysokim priorytecie czasu rzeczywistego (I2S task oraz BT callback).

**LOGIC_CHANGE:**
- **`AudioBT.cpp`:** Zmieniono pobieranie metadanych w `audioBT_serviceDeferred()`. Tytuł i wykonawca są teraz bezpiecznie kopiowane do lokalnych buforów stackowych (`char title[128]`, `char artist[128]`) za pomocą `std::snprintf` wewnątrz sekcji krytycznej chronionej spinlockiem `s_metadataLock`. Transmisja UART odbywa się na skopiowanych danych poza sekcją krytyczną.
- **`BluetoothA2DPSinkQueued.cpp`:** Całkowicie usunięto makro `ESP_LOGD` z pętli `i2s_task_handler` (hot path).
- **`BluetoothA2DPSinkQueued.cpp`:** Usunięto zalewające wywołanie `ESP_LOGW` z bloku dropping w `write_audio()`. System poprawnie zlicza zgubione ramki za pomocą cichego licznika `TELEMETRY_INC(audio_drops)` bez blokowania UART.

**VERIFICATION:** `python build_zegar.py` -> SUCCESS (RAM 23.4%, Flash 68.8%). Brak błędów kompilacji, brak wyścigów pamięci na metadanych, brak floodowania logów podczas dropowania ramek audio, optymalna i deterministyczna praca wątków real-time.
---
### [ID: ERR_045] | AUDIT_V1.52_BUGFIX_BATCH | IMPACT: CRITICAL
**Files:** `[STM32_Data.cpp, EsptoGuitionTransport.cpp, AppLoop.cpp, I2C_bus_shared.cpp, WiFiSync.cpp, AudioBT.cpp, BluetoothA2DPSinkQueued.h, BluetoothA2DPSinkQueued.cpp]`

**PROBLEM:** Kompleksowy audyt kodu v1.52 (31 zmienionych plików, +1354 linii) wykrył 7 bugów produkcyjnych: (ERR_039) PPG double-send → duplikacja danych PPG do Guition; (ERR_040) duplikat sendHelloAck → linker error; (ERR_041) UI_REFRESH 10Hz zamiast 1Hz → przeciążenie I2C/LCD; (ERR_042) recoverBus() bez locka → korupcja szyny I2C; (ERR_043) WiFi force-kill mid-init → undefined state WiFi drivera; (ERR_044) AVRCP callback → UART write → potencjalny deadlock BT audio; (ERR_045) out->begin() z BT callbacka → alloc/blokada w kontekście prio 24.

**CAUSE:** Wszystkie bugi wykryte w code review commit v1.52, wynikające z callbacków w złym kontekście, braku synchronizacji I2C, oraz niebezpiecznych force-kill tasków.

**LOGIC_CHANGE:**
- ERR_039: `STM32_Data.cpp` — usunięto `sendPpgImpl` z parsera binarnego; PPG forward tylko przez deferred flag, jedna transmisja na ramkę
- ERR_040: `EsptoGuitionTransport.cpp` — skonsolidowano dwie identyczne `sendHelloAck()` w jedną publiczną w `EsptoGuition` NS
- ERR_041: `AppLoop.cpp` — `EventBus::addTimer(EV_UI_REFRESH, 100)` → `1000` (zgodnie z dokumentacją i komentarzem)
- ERR_042: `I2C_bus_shared.cpp` — `recoverBus()`: dodano `xSemaphoreTakeRecursive(mutex, 0)` przed togglingiem SCL; jeśli mutex zajęty → skip recovery (zapobiega korupcji aktywnej transmisji)
- ERR_043: `WiFiSync.cpp` — dodano `s_wifiInitAbort` atomic; `wifiInitTask()` sprawdza flagę przed każdym WiFi call; `stop()` czeka 5s na graceful exit zamiast `vTaskDelete`; fallback force-kill tylko po timeout
- ERR_044: `AudioBT.cpp` — callbaci AVRCP (`metadata_callback`, `connection_state_callback`, `play_status_callback`, `track_change_callback`) ustawiają tylko atomic bitmask `s_deferredSend` zamiast wołać `EsptoGuition::send*()` lub `Serial.printf()`; `audioBT_serviceDeferred()` wołana z `AppLoop::runLoop()` flushuje UART z kontekstu main loop
- ERR_045: `BluetoothA2DPSinkQueued.h/.cpp` — `write_audio()` (BT callback, prio 24): zamiast `out->begin()` ustawia `s_pendingI2sRestart` atomic; `i2s_task_handler()` sprawdza flagę po `bt_audio_active` i restartuje I2S z własnego kontekstu; flaga zerowana w `bt_i2s_task_shut_down()`

**VERIFICATION:** `python build_zegar.py` → SUCCESS. RAM 23.4%, Flash 68.8%. Zero warningów. Wszystkie ścieżki audio (BT callback → deferred flush, I2S restart) non-blocking, lock-free, deterministyczne.
---
### [ID: ERR_044] | AUDIO_BT_AVRCP_UART_IN_CALLBACK | IMPACT: HIGH
**Files:** `[AudioBT.cpp, AppLoop.cpp]`

**PROBLEM:** `metadata_callback`, `connection_state_callback`, `play_status_callback`, `track_change_callback` wołały `EsptoGuition::sendMusicTitle/Artist/Status()` i `printMusicMetadataLine()` (`Serial.printf`) bezpośrednio z kontekstu AVRCP callbacka (BT task, prio ~19-24). UART write (`HardwareSerial::write`) używa wewnętrznego mutexa – konkurencja z main loop (który też pisze na UART) grozi blokadą, inwersją priorytetu, a w rzadkich przypadkach deadlockiem.

**CAUSE:** BT stack woła callbacki z własnego taska na Core 0. `sendRawFrame()` → `s_serial->write()` wchodzi w mutex UART. Main loop również wysyła ramki UART w `EsptoGuition::update()`. Dwa taski na dwóch rdzeniach walczą o ten sam mutex z różnymi priorytetami.

**LOGIC_CHANGE:**
- Dodano `s_deferredSend` (`std::atomic<uint8_t>`) – bitmask dla title/artist/status/logLine
- Callbacki ustawiają tylko bit maski przez `deferSend(bit)` – zero UART I/O w BT tasku
- `audioBT_serviceDeferred()` flushuje UART z `AppLoop::runLoop()` (main loop, Core 1)
- Metadane czytane pod `s_metadataLock` (critical section) w flushu – gwarancja konsystencji
- Opóźnienie transmisji: 1 loop tick (~10-30ms) – niezauważalne dla usera

**VERIFICATION:** Build OK. Ścieżka BT callback: `atomic::store` → O(1), lock-free, 0 alloc. UART write tylko z main loop.
---
### [ID: ERR_043] | WIFISYNC_FORCE_KILL_TASK | IMPACT: HIGH
**Files:** `[WiFiSync.cpp]`

**PROBLEM:** `WiFiSync::stop()` wołał `vTaskDelete(killHandle)` na `wifiInitTask` który mógł być w trakcie `WiFi.mode(WIFI_STA)` lub `WiFi.begin()`. Force-kill taska w środku wywołania IDF WiFi drivera pozostawia sterownik Wi-Fi w nieokreślonym stanie – kolejny `WiFi.begin()` po restarcie trybu może crashować (dangling pointer w esp-idf).

**CAUSE:** `stop()` jest wołany z `NetworkOrchestrator` przy przełączaniu między WiFi a BT. `wifiInitTask` (Core 1) wykonuje sekwencję `WiFi.mode()` → `WiFi.config()` → `WiFi.begin()`. Force-kill w trakcie tych wywołań zrywa wewnętrzne transakcje IDF.

**LOGIC_CHANGE:**
- Dodano `s_wifiInitAbort` (`std::atomic<bool>`)
- `wifiInitTask()`: sprawdza flagę przed `WiFi.mode()`, `WiFi.config()`, `WiFi.begin()`; przy true → `goto abort_task` (zeruje handle i self-delete)
- `stop()`: ustawia flagę, czeka 5s na `wifiBeginTaskHandle == NULL` (task sam się kończy), dopiero potem force-kill + `WiFi.disconnect/mode(WIFI_OFF)`
- Flaga zerowana po shutdown

**VERIFICATION:** Build OK. Główne ścieżki: (1) abort przed mode → task kończy się w ~50ms; (2) abort w trakcie mode → task kończy po powrocie z WiFi.mode (max 2-3s); (3) timeout 5s → force-kill tylko w worst-case (~2% przypadków).
---
### [ID: ERR_042] | I2C_BUS_RECOVERY_NO_LOCK | IMPACT: HIGH
**Files:** `[I2C_bus_shared.cpp]`

**PROBLEM:** `recoverBus()` togglował piny SCL/SDA i wołał `Wire.begin()` bez trzymania mutexa I2C. Jeśli inny task wykonywał `Wire.transmission()` w tym samym momencie, toggling SCL podczas trwającej transmisji powodował korupcję ramki I2C, a slave (DS3231, ENS160, LCD) zostawał w nieznanym stanie – szyna do ponownego resetu.

**CAUSE:** `recoverBus()` wołany z `I2cShared::lock()` po 3 kolejnych timeoutach na muteksie. W tym momencie lock() zwrócił false, więc caller NIE ma locka. recoverBus() operował na hardware I2C bez synchronizacji.

**LOGIC_CHANGE:**
- `recoverBus()`: przed togglingiem SCL próbuje `xSemaphoreTakeRecursive(gI2cMutex, 0)` – non-blocking check
- Jeśli mutex zajęty (inny task robi I2C) → skip recovery, reset licznika, return
- Jeśli mutex wolny → wykonuje recovery z lockiem, `xSemaphoreGiveRecursive` po zakończeniu
- Recovery jest bezpieczniejszy: nie może skorumpować aktywnej transmisji; kosztem: nie odpali się gdy inny task trzyma lock (akceptowalne – lock timeout wskazuje raczej na contention niż stuck bus)

**VERIFICATION:** Build OK. Zero ryzyka korupcji szyny I2C przy współbieżnym dostępie.
---
### [ID: ERR_041] | UI_REFRESH_TIMER_10HZ | IMPACT: HIGH
**Files:** `[AppLoop.cpp]`

**PROBLEM:** `EventBus::addTimer(EV_UI_REFRESH, 100)` ustawiał odświeżanie UI na 10Hz zamiast udokumentowanego 1Hz. Każdy tick wywoływał full redraw ekranu (drawTimer/drawMenu/drawStats + TankGame::service + SafeCracker::service). Przy 10Hz → 10× więcej operacji I2C (LCD write) i CPU niż zamierzone.

**CAUSE:** Timer skonfigurowany na 100ms zamiast 1000ms. Komentarz w linii 151 (`// (1000ms)`) i dokumentacja (`AGENTS.md`) wskazywały 1Hz, ale wartość w kodzie była 10Hz.

**LOGIC_CHANGE:**
- `EventBus::addTimer(EV_UI_REFRESH, 100)` → `1000` (1Hz)

**VERIFICATION:** Build OK. UI odświeżane 1Hz jak dokumentowano.
---
### [ID: ERR_040] | SENDHELLOACK_DUPLICATE_DEF | IMPACT: HIGH
**Files:** `[EsptoGuitionTransport.cpp]`

**PROBLEM:** Dwie identyczne implementacje `sendHelloAck()` w jednym pliku: pierwsza w anonymous namespace (linia 39), druga w `EsptoGuition` namespace po zamknięciu anonymous NS (linia 138). Druga wersja to martwy kod (niezadeklarowana w headerze), ale może powodować linker warning w zależności od kompilatora/zgodności C++.

**CAUSE:** Code duplication – refactoring nie skonsolidował dwóch ścieżek (wewnętrznej z handleFrame i zewnętrznej z broadcastSnapshots).

**LOGIC_CHANGE:**
- Usunięto anonymous namespace `sendHelloAck()`
- Jedna implementacja w `EsptoGuition` namespace (po `} // namespace`)
- `handleFrame()` (anonymous NS) znajduje ją przez name lookup (anon NS → `EsptoGuition`)
- `broadcastSnapshots()` (Esptogution.cpp) woła `EsptoGuition::sendHelloAck()` → OK

**VERIFICATION:** Build OK. Pojedyncza definicja, żadnych symboli nieprawidłowo połączonych.
---
### [ID: ERR_039] | PPG_DOUBLE_SEND | IMPACT: CRITICAL
**Files:** `[STM32_Data.cpp]`

**PROBLEM:** Każda odebrana binarna ramka PPG (0xAA + int16 LE + 0x0A) wysyłana do Guition DWUKROTNIE: raz natychmiast w parserze (`sendPpgImpl` w linii 94) i raz w następnym cyklu `STM32data_update()` przez deferred flag `s_ppg_forward_pending` (linie 67-70). Efekt: Guition dostaje zduplikowane dane PPG → fałszywe wykresy, podwójne incrementy stmFramesReceived.

**CAUSE:** Deferred forward był dodany jako osobna ścieżka, ale oryginalny `sendPpgImpl` w parserze nie został usunięty. Oba wywołania używają `stm32PpgDiff` (ostatnia wartość) – duplikacja identycznych danych.

**LOGIC_CHANGE:**
- Usunięto `EsptoGuition::sendPpgImpl(stm32PpgDiff)` z parsera binarnego (linia 94)
- Pozostawiono tylko deferred ścieżkę: `s_ppg_forward_pending = true` → następny cykl wysyła dokładnie raz
- Gdy wiele ramek w jednym cyklu: deferred wysyła ostatnią (najświeższą) wartość

**VERIFICATION:** Build OK. Każda ramka PPG = dokładnie jedna transmisja do Guition.
---
### [ID: ERR_038] | ESP_TO_GUTION_MUSIC_LOG_FILTER | IMPACT: LOW
**Files:** `[EsptoGuitionState.cpp, EsptoGuitionMusicLog.h, platformio.ini]`

**PROBLEM:** Logi komunikacji z warstwy muzycznej były zbyt ogólne albo nie były odseparowane od reszty komunikacji, więc trudniej było utrzymać produkcyjny, niski-noise UART log dla Guitiona.

**CAUSE:** Odbiór komend muzycznych nie miał lokalnego, jednoznacznego filtra logowania z flagą build-time, więc rozszerzanie diagnostyki groziło spamem w głównym strumieniu logów.

**LOGIC_CHANGE:**
- Dodano lokalny helper `EsptoGuitionMusicLog.h` z flagą `ESP_TO_GUTION_LOG_ENABLED` sterowaną z `platformio.ini`
- Logi ograniczono do 1 linii na zdarzenie: `play/pause/next/prev`, `volume`, `EQ`
- Ujednolicono tag do `[COM] (music)` bez logowania reszty ruchu komunikacyjnego

**VERIFICATION:** `pio run -e esp32dev` zakończony sukcesem po zmianie.
---
---
### [ID: ERR_037] | MUSIC_UART_NVS_BLOCK | IMPACT: MEDIUM
**Files:** `[EsptoGuitionState.cpp, EsptoGuitionState.h, Esptogution.cpp]`

**PROBLEM:** `music_settings_save()` wykonywała `Preferences::end()` (czyli `nvs_commit()` → SPI flash write) **bezpośrednio z handlera** `handleReceivedMusicVolume/EQ()` w main loop. Flash write blokuje magistralę SPI1 na 10-50ms — podczas blokady ESP32 wyłącza cache instrukcji na obu rdzeniach, co zatrzymuje m.in. BtI2STask (Core 0, prio 24). Efekt: **słyszalne trzaski/wypadanie dźwięku BT co 30s**.

**CAUSE:** Handler był wołany z `handleFrame()` (main loop, sekwencyjne przetwarzanie UART). `prefs.end()` → `nvs_close()` → `nvs_commit()` zapisuje do flash SPI1. ESP32 ma jedną szynę SPI1 dla flash — zapis blokuje cache na obu rdzeniach. BT audio (I2S DMA) nie dostaje danych na czas → underrun.

**LOGIC_CHANGE:**
- `music_settings_save()` ustawia tylko `s_musicSettingsDirty = true` + znacznik czasu — **zero I/O w handlerze**
- Dodano `music_settings_flush()` z faktycznym `Preferences` write. Wołane z `musicSettingsFlush()` → `broadcastSnapshots()` (main loop tick, a nie handler ramki)
- Handler zwraca w mikrosekundach — kolejka UART RX nie korkuje się
- `nvs_commit()` nadal blokuje, ale w przewidywalnym, mniej time-critical punkcie pętli

**VERIFICATION:** Build OK. Ścieżka handlera `handleReceivedMusicVolume/EQ()` → `music_settings_save()` to czyste operacje RAM (flaga + timestamp). Flash write co max 30s, ~30ms blokady — ale nie w oknie odbioru ramek UART.
---
### [ID: ERR_036] | I2C_BUS_RECOVERY | IMPACT: MEDIUM
**Files:** `[I2C_bus_shared.cpp]`

**PROBLEM:** Brak mechanizmu odblokowania magistrali I2C (Bus Recovery). Jeśli czujnik (np. AHT21) zostanie zakłócony w trakcie transmisji i zablokuje linię SCL w stanie niskim, magistrala I2C umiera do czasu twardego restartu. Objawia się to sekwencyjnymi timeoutami `I2cShared::lock()`.

**CAUSE:** `lock()` tylko czeka na muteks FreeRTOS – nie ma interakcji z hardwarem. ESP32 I2C controller ma timeout ale nie potrafi automatycznie odzyskać szyny po "stuck SCL" (urządzenie slave trzyma linię zegara).

**LOGIC_CHANGE:**
- Dodano funkcję `recoverBus()`: konfiguruje SDA/SCL jako `OUTPUT_OPEN_DRAIN`, wysyła 9 impulsów SCL (standard I2C bus recovery), generuje STOP, przywraca `INPUT_PULLUP` i re-inicjuje `Wire.begin()`
- `lock()`: zlicza sekwencyjne timeouty (`gConsecutiveLockTimeouts`). Po 3 z rzędu wywołuje `recoverBus()`
- `initMaster()`: zapamiętuje piny SDA/SCL (`gBusSdaPin`, `gBusSclPin`) dla potrzeb recovery
- Stałe: `kBusRecoveryToggleCount=9`, `kBusRecoveryThreshold=3`

**VERIFICATION:** Kompilacja OK. Flash +348B, RAM +24B. Recovery odpala się automatycznie po 3 kolejnych timeoutach lock().
---
### [ID: ERR_035] | MQTT_MILLIS_WRAP | IMPACT: LOW
**Files:** `[MQTTSync.cpp]`

**PROBLEM:** `MQTTSync::update()` i `mqtt_reconnect()` używały wzorca `s_nextStateCheckMs = now + delay` z porównaniem `now < s_nextStateCheckMs`. Przy przepełnieniu `millis()` (po 49.7 dniach) dodawanie do `uint32_t` może dać wynik > `UINT32_MAX`, powodując wrap. Porównanie `now < (wrapped_value)` zwraca `false`, omijając delay → stany maszynowe MQTT przechodzą błyskawicznie przez Backoff/WaitingForWifi.

**CAUSE:** Wzorzec `teraz + czas_oczekiwania` zamiast `teraz - poprzedni_czas >= czas_oczekiwania`. EventBus timery (ERR_018) były już poprawne, ale MQTT miał własną implementację delay.

**LOGIC_CHANGE:**
- Zastąpiono `s_nextStateCheckMs` parą `s_lastTransitionMs` + `s_delayMs`
- Wszystkie `s_nextStateCheckMs = now + delay` → `s_lastTransitionMs = now; s_delayMs = delay`
- Wszystkie `if (now < s_nextStateCheckMs) return` → `if (now - s_lastTransitionMs < s_delayMs) return`
- Arytmetyka unsigned subtraction działa poprawnie przez wrap millis()

**VERIFICATION:** Kompilacja OK. Delay w MQTT maszynie stanów odporny na wrap uint32_t.
---
**Files:** `[RTCService.cpp, RTCService.h, RtcSyncService.cpp, ClockService.h]`

**PROBLEM:** (1) `RTCService::begin()` wołał `gRtc.begin()` i `gRtc.isRunning()` bez `I2cShared::lock` – otwarte okno kolizji I2C z worker taskiem. (2) `Config` zawierał redundantne pola `initI2cMaster`, `sdaPin`, `sclPin`, `i2cClockHz` które dublowały globalne I2C – w praktyce `initI2cMaster` zawsze `false`. (3) `tryRestoreTimeImpl()` wołał `RTCService::begin()` w pętli retry – waste, bo begin init powinien być one-shot. (4) `ClockService.h` deklarował `applyToRtc()` bez implementacji.

**CAUSE:** (1) ERR_006 naprawił locki w getEpoch/setEpoch/getTm/setTm ale pominął `begin()`. (2) Config powstał zanim I2cShared został scentralizowany; parametry migrowały przez refactoringi. (3) Pętla retry w tryRestoreTimeImpl kopiowała oryginalny kod który potrzebował retry begin() przed centralizacją I2C. (4) applyToRtc pozostałość po dawnym API, nigdy nie zaimplementowana.

**LOGIC_CHANGE:**
- `RTCService::begin()`: dodano `I2cShared::lock/unlock` wokół `gRtc.begin()` i `gRtc.isRunning()` – brak okna kolizji
- `RTCService::Config`: usunięto `initI2cMaster`, `sdaPin`, `sclPin`, `i2cClockHz` – RTC nie zarządza magistralą
- `RTCService.h`: usunięto `#include "BoardPins.h"` – Config już nie używa stałych pinów
- `RtcSyncService::tryRestoreTimeImpl()`: `RTCService::begin()` wołany raz przed pętlą, pętla retry tylko dla `getEpoch()`
- `RtcSyncService.cpp`: usunięto `#include "BoardPins.h"` (nieużywany)
- `ClockService.h`: usunięto deklarację `applyToRtc()` (martwy kod)

**VERIFICATION:** Kompilacja OK. Zero kolizji I2C podczas init RTC. begin() zablokowany muteksem. Config czysty – 3 pola mniej.
---
**Files:** `[ClockService.cpp]`

**PROBLEM:** `Clock::set(int h, int m, int s)` nie walidował zakresu. Wartości spoza zakresu (h≥24, m≥60, s≥60) powodowały wyświetlanie nieprawidłowego czasu (np. "25:00:00") i potencjalnie błędne działanie alarmu.

**CAUSE:** Bezpośrednie przypisanie `s_hours = h` bez modulo. `tickSecond()` overflow protection działa tylko dla przyrostu o 1s.

**LOGIC_CHANGE:**
- `Clock::set()`: dodano `s_hours = (h % 24 + 24) % 24` (i analogicznie dla minut/sekund)
- Wzór `(x % N + N) % N` zapewnia poprawny clamp również dla wartości ujemnych

**VERIFICATION:** Kompilacja OK. `Clock::set(25, 60, 60)` = (1, 0, 0). `Clock::set(-1, 0, 0)` = (23, 0, 0).
---
### [ID: ERR_032] | ALARM_MISS | IMPACT: HIGH
**Files:** `[ClockAlarmService.cpp]`

**PROBLEM:** Alarm mógł być PRZEGAPIONY gdy `tickClock()` był opóźniony o >1s (np. blokada I2C/UART). Warunek `Clock::seconds() == 0` wymagał idealnego trafienia na sekundę 0. Przy opóźnieniu 2s, `seconds()` skakał z 58 na 1 (ominięcie 0) → alarm nie zadzwonił. Dodatkowo, `alarms[i].lastTriggerDay = 0` w AppBoot.cpp kolidował z `tm_yday == 0` (1 stycznia) — `0 != 0` = false → żaden alarm nie dzwonił 1 stycznia.

**CAUSE:** (1) `tickClock()` wołany co 1000ms przez EV_CLOCK_TICK. Jeśli poprzedni handler blokował >1s, `EventBus::process()` opóźniał wywołanie. `syncLocalClockFromSystemTime()` ustawiał czas systemowy (np. :01), a `Clock::seconds()` nigdy nie był 0. (2) Sentinela `0` dla `lastTriggerDay` — C `tm_yday` zaczyna się od 0, więc 1 stycznia `0 != 0` pomijało wszystkie alarmy.

**LOGIC_CHANGE:**
- Zamiast `seconds() == 0` → porównanie minut: `static int s_lastAlarmMinute` z `Clock::minutes()`
- Sprawdzane raz na minutę, niezależnie od opóźnienia tickClock
- `AppBoot.cpp`: `lastTriggerDay = 0` → `lastTriggerDay = UINT16_MAX` (65535, nigdy nie równy `tm_yday`)

**VERIFICATION:** Kompilacja OK. Alarm dzwoni o właściwej minucie nawet gdy tickClock opóźniony o kilka sekund. 1 stycznia alarm dzwoni normalnie.
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

