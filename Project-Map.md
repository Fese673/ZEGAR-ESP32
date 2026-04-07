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
├── headers.md            # public header guidance for cross-module integration
├── config/               # board pins, secrets, temperature config
│   ├── BoardPins.h
│   ├── a2dp_config.h     # shared compile-time config aliases and board selection
│   ├── SecretsConfig.h
│   └── TemperatureConfig.h
├── core/                 # core application interfaces
│   ├── app/
│   │   ├── AppBoot.h
│   │   ├── AppLoop.h
│   │   ├── AppRuntime.h
│   │   ├── AppSettings.h
│   │   └── AppState.h
│   ├── services/
│   │   ├── BootIntroService.h
│   │   ├── ClockAlarmService.h
│   │   ├── ModeManager.h
│   │   ├── RtcSyncService.h
│   │   └── SystemResourcesService.h
│   └── telemetry/
│       ├── LoopBaselineTelemetry.h
│       ├── RamTelemetry.h
│       ├── RuntimeTelemetry.h
│       ├── StatsManager.h
│       └── STM32_Data.h
├── display/              # display driver and screen interfaces
│   ├── LiquidCrystal_I2C.h
│   ├── BMP280Screen.h
│   ├── ENS160AHT21Screen.h
│   ├── LCDIcons.h
│   └── LCDMirror.h
├── drivers/              # low-level hardware driver headers
│   ├── I2C_bus_shared.h
│   ├── ErriezDS3231.h
│   ├── gpio/
│   │   └── README.md
│   ├── i2c/
│   │   └── SharedBus.h
│   └── spi/
│       └── README.md
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
│   ├── MQTTSync.h
│   ├── NetworkOrchestrator.h
│   ├── RadioModeSwitch.h
│   ├── TelemetryComposer.h
│   └── WiFiSync.h
├── audio/
│   ├── AlarmMelodies.h
│   ├── AlarmMelodyPrefs.h
│   ├── AlarmMelodyPreview.h
│   ├── AlarmRuntime.h
│   └── AlarmTypes.h
├── input/
│   └── Encoder.h
├── sensors/
│   ├── AHTxx.h
│   ├── BMP280Sensor.h
│   ├── ENS160AHT21Sensor.h
│   ├── PMserial.h
│   ├── PMS_Czujnik.h
│   └── RTCService.h
└── ui/
    ├── HomeRuntime.h
    ├── UIState.h
    ├── UI_Controller.h
    └── UI_Draw.h

scripts/
├── generate_alarm_melodies.py    # build-time melody generation
├── mqtt_firebase_bridge.py       # bridge/utility script for MQTT diagnostics
└── scan_project_map.py           # workspace inventory generator / checks

src/
├── main.cpp              # application entrypoint, setup() + loop()
├── ErriezDS3231.cpp      # RTC hardware integration
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
│   ├── MQTTSync.cpp
│   ├── NetworkOrchestrator.cpp
│   ├── RadioModeSwitch.cpp
│   ├── TelemetryComposer.cpp
│   ├── WiFiSync.cpp
│   └── README.md
├── core/
│   ├── app/
│   │   ├── AppBoot.cpp
│   │   ├── AppLoop.cpp
│   │   ├── AppRuntime.cpp
│   │   ├── AppSettings.cpp
│   │   └── AppState.cpp
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
│   ├── I2C_bus_shared.cpp
│   └── README.md
├── input/
│   ├── Encoder.cpp
│   └── README.md
├── sensors/
│   ├── AHTxx.cpp
│   ├── BMP280Sensor.cpp
│   ├── ENS160AHT21Sensor.cpp
│   ├── ErriezDS3231.cpp
│   ├── PMserial.cpp
│   ├── PMS_Czujnik.cpp
│   ├── README.md
│   └── RTCService.cpp
└── ui/
    ├── HomeRuntime.cpp
    ├── README.md
    ├── UIState.cpp
    ├── UI_Controller.cpp
    └── UI_Draw.cpp

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


