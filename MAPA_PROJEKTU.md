# Mapa projektu

Mapa jest zorganizowana wg domen funkcjonalnych, a nie szczegółów implementacji.
Ma ułatwiać nawigację i być łatwa do rozszerzania wraz z rozwojem firmware.

## Pliki nadrzędne

- platformio.ini            # konfiguracja buildu, zależności bibliotek, flagi kompilacji
- README                    # wysokopoziomowy opis projektu
- MAPA_PROJEKTU.md          # ten plik
- TODO                      # lista zadań i znanych problemów
- AGENTS.md                 # instrukcje dla agentów AI (budowanie, architektura, konwencje)
- build_zegar.py            # skrypt budowania firmware
- .gitignore                # reguły ignorowania przez Git
- .vscode/                  # ustawienia edytora workspace
    └── extensions.json
- .git/                     # metadane repozytorium Git

## hardware/

Dokumentacja sprzętowa — datasheety, referencje GPIO, projekty KiCad.

```
hardware/
├── overview.md                     # przewodnik po katalogu hardware
├── datasheets/                     # datasheety komponentów i czujników
│   ├── bst-bmp280-ds001.pdf
│   ├── plantower-pms5003-manual_v2-3.pdf
│   ├── esp32-wroom-32d_datasheet.pdf
│   └── dht11.pdf
├── GPIO/                           # materiały referencyjne GPIO i wyprowadzenia pinów
│   ├── GPIO-PIN-ESP32.xlsx
│   └── ESP32-DevBoard-Pinout.jpg
└── kicad/                          # pliki projektowe KiCad i wyjścia PCB
    ├── Datasheety/                 # datasheety w formacie KiCad
    │   └── 74860_0d2671659de625a9213c07e20588b5fd.pdf
    ├── ESP32C3-CC1101/             # projekt płyty CC1101/ESP32
    │   ├── ESP32C3-CC1101.kicad_pro
    │   ├── ESP32C3-CC1101.kicad_pcb
    │   ├── ESP32C3-CC1101.kicad_sch
    │   ├── ESP32C3-CC1101.kicad_prl
    │   ├── ESP32C3-CC1101-backups/
    │   ├── assets/
    │   ├── Outputs/                # wygenerowane pliki (BOM, file produkcyjne)
    │   ├── PCBLib/
    │   ├── fp-lib-table
    │   ├── sym-lib-table
    │   ├── .gitignore
    │   └── LICENSE
    └── Główny projekt/             # główny projekt PCB
        ├── ZEGAR ESP32.kicad_pro
        ├── ZEGAR ESP32.kicad_pcb
        ├── ZEGAR ESP32.kicad_sch
        ├── ZEGAR ESP32.kicad_sch-bak
        ├── ZEGAR ESP32.kicad_prl
        ├── ZEGAR ESP32 2.1 .zip
        ├── 1 EKRAN.kicad_sch
        ├── 2 EKRAN.kicad_sch
        ├── 3 EKRAN.kicad_sch
        ├── 4EKRAN.kicad_sch
        ├── 5 EKRAN.kicad_sch
        ├── 6 EKRAN.kicad_sch
        └── 74HC595.kicad_sch
```

## include/

Nagłówki publiczne — interfejsy domenowe, typy, konfiguracja.

```
include/
├── audio/
│   ├── AlarmMelodies.h
│   ├── AlarmMelodyPrefs.h
│   ├── AlarmMelodyPreview.h
│   ├── AlarmRuntime.h
│   └── AlarmTypes.h
├── bluetooth/
│   ├── A2DPVolumeControl.h
│   ├── AudioBT.h
│   ├── BluetoothA2DP.h
│   ├── BluetoothA2DPCommon.h
│   ├── BluetoothA2DPOutput.h
│   ├── BluetoothA2DPSink.h
│   ├── BluetoothA2DPSinkQueued.h
│   └── BluetoothA2DPSource.h
├── comms/
│   ├── meteoSync.h
│   ├── MQTTSync.h
│   ├── NetworkOrchestrator.h
│   ├── RadioModeSwitch.h
│   ├── TelemetryComposer.h
│   ├── TimeSyncProtocol.h
│   └── WiFiSync.h
├── config/
│   ├── A2DP_Config.h
│   ├── Board_Pins.h
│   ├── Secrets_Config.h
│   ├── Sensor_Conifg.h
│   ├── Task_Config.h
│   └── Temperature_Config.h
├── core/
│   ├── app/
│   │   ├── AppBoot.h
│   │   ├── AppLoop.h
│   │   ├── AppRuntime.h
│   │   ├── AppSettings.h
│   │   └── AppState.h
│   ├── events/
│   │   ├── EventBus.h
│   │   └── EventTypes.h
│   ├── services/
│   │   ├── BootIntroService.h
│   │   ├── ClockAlarmService.h
│   │   ├── ClockService.h
│   │   ├── ModeManager.h
│   │   ├── RtcSyncService.h
│   │   ├── StopwatchService.h
│   │   ├── SystemResourcesService.h
│   │   └── TimerService.h
│   └── telemetry/
│       ├── AppLog.h
│       ├── LoopBaselineTelemetry.h
│       ├── RamTelemetry.h
│       ├── RuntimeTelemetry.h
│       ├── StatsManager.h
│       └── STM32_Data.h
├── display/
│   ├── BMP280Screen.h
│   ├── ENS160AHT21Screen.h
│   ├── LCDIcons.h
│   ├── LCDMirror.h
│   └── LiquidCrystal_I2C.h
├── drivers/
│   ├── gpio/
│   │   └── README.md
│   ├── i2c/
│   │   └── SharedBus.h
│   ├── spi/
│   │   └── README.md
│   ├── ErriezDS3231.h
│   └── I2C_bus_shared.h
├── games/
│   ├── TANK-GAMES/
│   │   └── TankGame.h
│   └── SafeCracker.h
├── input/
│   ├── Encoder.h
│   └── touch_buzzer_test.h
├── sensors/
│   ├── AHTxx.h
│   ├── BMP280Sensor.h
│   ├── ENS160AHT21Sensor.h
│   ├── PMS_Czujnik.h
│   ├── PMserial.h
│   └── RTCService.h
├── ui/
│   ├── HomeRuntime.h
│   ├── UI_Controller.h
│   ├── UI_Draw.h
│   └── UIState.h
└── headers.md
```

## src/

Źródła — implementacje domenowe, logika biznesowa, sterowniki.

```
src/
├── main.cpp                       # punkt wejścia — setup() i loop()
├── audio/
│   ├── AlarmMelodies.cpp
│   ├── AlarmMelodies.generated.inc
│   ├── AlarmMelodyPrefs.cpp
│   ├── AlarmMelodyPreview.cpp
│   ├── AlarmRuntime.cpp
│   └── README.md
├── bluetooth/
│   ├── AudioBT.cpp
│   ├── BluetoothA2DPCommon.cpp
│   ├── BluetoothA2DPOutput.cpp
│   ├── BluetoothA2DPSink.cpp
│   ├── BluetoothA2DPSinkQueued.cpp
│   ├── BluetoothA2DPSource.cpp
│   ├── EQFilter.cpp
│   └── EQFilter.h
├── comms/
│   ├── esp_to_gution/
│   │   ├── Config.h
│   │   ├── EsptoGuitionCobs.cpp
│   │   ├── EsptoGuitionCobs.h
│   │   ├── EsptoGuitionMusicLog.h
│   │   ├── EsptoGuitionState.cpp
│   │   ├── EsptoGuitionState.h
│   │   ├── EsptoGuitionTransport.cpp
│   │   ├── EsptoGuitionTransport.h
│   │   ├── Esptogution.cpp
│   │   └── Esptogution.h
│   ├── time_sync/
│   │   └── TimeSyncProtocol.cpp
│   ├── meteoSync.cpp
│   ├── MQTTSync.cpp
│   ├── NetworkOrchestrator.cpp
│   ├── RadioModeSwitch.cpp
│   ├── README.md
│   ├── TelemetryComposer.cpp
│   └── WiFiSync.cpp
├── core/
│   ├── app/
│   │   ├── AppBoot.cpp
│   │   ├── AppLoop.cpp
│   │   ├── AppRuntime.cpp
│   │   ├── AppSettings.cpp
│   │   └── AppState.cpp
│   ├── events/
│   │   └── EventBus.cpp
│   ├── services/
│   │   ├── BootIntroService.cpp
│   │   ├── ClockAlarmService.cpp
│   │   ├── ClockService.cpp
│   │   ├── ModeManager.cpp
│   │   ├── RtcSyncService.cpp
│   │   ├── StopwatchService.cpp
│   │   ├── SystemResourcesService.cpp
│   │   └── TimerService.cpp
│   ├── telemetry/
│   │   ├── LoopBaselineTelemetry.cpp
│   │   ├── RamTelemetry.cpp
│   │   ├── RuntimeTelemetry.cpp
│   │   ├── StatsManager.cpp
│   │   └── STM32_Data.cpp
│   └── README.md
├── display/
│   ├── BMP280Screen.cpp
│   ├── ENS160AHT21Screen.cpp
│   ├── LCDIcons.cpp
│   ├── LCDMirror.cpp
│   └── README.md
├── drivers/
│   ├── ErriezDS3231.cpp
│   ├── I2C_bus_shared.cpp
│   └── README.md
├── games/
│   ├── TANK-GAMES/
│   │   └── TankGame.cpp
│   └── SafeCracker.cpp
├── input/
│   ├── Encoder.cpp
│   ├── README.md
│   └── touch_buzzer_test.cpp
├── sensors/
│   ├── AHTxx.cpp
│   ├── BMP280Sensor.cpp
│   ├── ENS160AHT21Sensor.cpp
│   ├── PMS_Czujnik.cpp
│   ├── PMserial.cpp
│   ├── README.md
│   └── RTCService.cpp
├── ui/
│   ├── HomeRuntime.cpp
│   ├── README.md
│   ├── UI_Controller.cpp
│   ├── UI_Draw.cpp
│   └── UIState.cpp
```

## scripts/

Skrypty narzędziowe — budowanie, analiza, generowanie kodu.

```
scripts/
├── count_loc.py                   # zliczanie linii kodu źródłowego
├── find_unused_static.py          # wyszukiwanie nieużywanych funkcji static
├── generate_alarm_melodies.py     # generowanie AlarmMelodies.generated.inc
├── mqtt_firebase_bridge.py        # most MQTT → Firebase
├── scan_project_map.py            # skanowanie struktury i walidacja Project-Map.md
└── update_project_map.py          # automatyczna aktualizacja Project-Map.md
```

## lib/

Lokalne biblioteki PlatformIO.

```
lib/
├── README                         # opis katalogu lib
└── open-meteo-arduino/            # biblioteka klienta Open-Meteo API
```

---

Uwagi:
- Mapa zawiera publiczne nagłówki i główne moduły źródłowe do szybkiej nawigacji.
- Użyj `scripts/scan_project_map.py`, aby automatycznie zweryfikować sekcje `include/` i `src/`.
- Katalogi `docs/`, `.kilo/`, `third_party/`, `test/`, `.analysis/` są ignorowane przez `.gitignore` i nie są opisane w mapie.
