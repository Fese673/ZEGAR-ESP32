# Project map

The map is organized by functional domains, not by implementation detail. It is intended to be easy to extend as the firmware grows.

Top-level files
- platformio.ini            # build config, lib_deps, build_flags
- README                    # high-level project overview for humans
- Project-Map.md            # this file
- TODO                      # task list and outstanding work
- .gitignore                # git ignore rules
- .vscode/                  # workspace editor settings
    ├── settings.json
    ├── extensions.json
    ├── c_cpp_properties.json
    └── launch.json
- .pio/                     # PlatformIO build environment and cache
- .venv/                    # local Python virtual environment
- __pycache__/              # temporary Python bytecode cache
- .analysis/                # local analysis/tooling artifacts
- .git/                     # Git repository metadata
- _baseline/                # baseline/reference artifacts

docs/
├── index.md              # docs index and navigation guidance
├── telemetry/            # runtime and diagnostic telemetry docs
│   └── telemetry.md
├── reports/              # architecture/refactor/analysis reports
│   └── old/              # archived or superseded reports
└── reports_ram/          # RAM and heap analysis reports

hardware/
├── overview.md           # hardware directory guide
├── datasheets/           # component datasheets and sensor references
│   ├── plantower-pms5003-manual_v2-3.pdf
│   ├── esp32-wroom-32d_datasheet.pdf
│   └── dht11.pdf
├── GPIO/                 # GPIO reference materials and pinouts
│   ├── GPIO-PIN-ESP32.xlsx
│   └── ESP32-DevBoard-Pinout.jpg
└── kicad/                # KiCad project files and PCB design outputs
    ├── Datasheety/
    ├── ESP32C3-CC1101/   # CC1101/ESP32 board project
    │   ├── ESP32C3-CC1101.kicad_pro
    │   ├── ESP32C3-CC1101.kicad_pcb
    │   └── Outputs/       # compiled outputs (BOM, fabrication files)
    ├── Główny projekt/   # main PCB project and versions/backups
    └── ZEGAR ESP32-backups/

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
│   └── WiFiSync.h
├── config/
│   ├── a2dp_config.h
│   ├── BoardPins.h
│   ├── SecretsConfig.h
│   ├── TaskConfig.h
│   └── TemperatureConfig.h
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
│   │   ├── ModeManager.h
│   │   ├── RtcSyncService.h
│   │   └── SystemResourcesService.h
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

scripts/
├── count_loc.py
├── find_unused_static.py
├── generate_alarm_melodies.py
├── mqtt_firebase_bridge.py
├── scan_project_map.py
└── update_project_map.py

src/
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
│   └── BluetoothA2DPSource.cpp
├── comms/
│   ├── esp_to_gution/
│   │   ├── Config.h
│   │   ├── EsptoGuitionCobs.cpp
│   │   ├── EsptoGuitionCobs.h
│   │   ├── EsptoGuitionState.cpp
│   │   ├── EsptoGuitionState.h
│   │   ├── EsptoGuitionTransport.cpp
│   │   ├── EsptoGuitionTransport.h
│   │   ├── Esptogution.cpp
│   │   └── Esptogution.h
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
│   │   ├── ModeManager.cpp
│   │   ├── RtcSyncService.cpp
│   │   └── SystemResourcesService.cpp
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
└── main.cpp

test/
├── test.md
├── logi.txt
└── verify_bt_no_wifi_mqtt.py

third_party/
├── third_party.md
├── archive/
│   └── library_snapshot/
└── vendor/

lib/
└── README

Notes:
- This map lists public headers and primary source modules for quick navigation.
- Use `scripts/scan_project_map.py` to verify `include/` and `src/` sections automatically.


