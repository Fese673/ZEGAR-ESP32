# Project map

The map is organized by functional domains, not by implementation detail. It is intended to be easy to extend as the firmware grows.

platformio.ini            # build config, lib_deps, build_flags
README                    # high-level project overview for humans
Project-Map.md            # this file

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
├── GPIO/                 # GPIO reference materials
└── kicad/                # KiCad project files and PCB design outputs
    ├── Datasheety/
    ├── ESP32C3-CC1101/
    ├── Główny projekt/
    └── ZEGAR ESP32-backups/

include/
├── headers.md           # public header guidance for cross-module integration
├── AppLog.h             # shared logging API for application diagnostics
├── config.h             # shared compile-time config aliases and board selection
├── LiquidCrystal_I2C.h  # external LCD driver public header
├── config/              # board pins, secrets, temperature config
│   ├── BoardPins.h
│   ├── SecretsConfig.h
│   └── TemperatureConfig.h
├── core/                # core application interfaces
│   ├── AlarmTypes.h
│   ├── AppSettings.h
│   ├── AppState.h
│   ├── ClockAlarmService.h
│   ├── ModeManager.h
│   ├── RamTelemetry.h
│   ├── RtcSyncService.h
│   ├── RuntimeTelemetry.h
│   ├── StatsManager.h
│   └── STM32_Data.h
├── display/             # display driver and screen interfaces
│   ├── BMP280Screen.h
│   ├── ENS160AHT21Screen.h
│   ├── LCDIcons.h
│   └── LCDMirror.h
├── drivers/             # low-level hardware driver headers
│   ├── ErriezDS3231.h
│   ├── I2C_bus_shared.h
│   ├── gpio/
│   ├── i2c/
│   └── spi/
├── bluetooth/           # Bluetooth API headers
│   ├── A2DPVolumeControl.h
│   ├── AudioBT.h
│   ├── BluetoothA2DP.h
│   ├── BluetoothA2DPCommon.h
│   ├── BluetoothA2DPOutput.h
│   ├── BluetoothA2DPSink.h
│   ├── BluetoothA2DPSinkQueued.h
│   └── BluetoothA2DPSource.h
├── comms/               # networking and telemetry composer headers
│   ├── MQTTSync.h
│   ├── NetworkOrchestrator.h
│   ├── RadioModeSwitch.h
│   ├── TelemetryComposer.h
│   └── WiFiSync.h
├── audio/               # audio playback and melody definitions
│   ├── AlarmMelodies.h
│   └── AlarmMelodyPrefs.h
├── input/               # input controller headers
│   └── Encoder.h
├── sensors/             # sensor abstraction headers
│   ├── AHTxx.h
│   ├── BMP280Sensor.h
│   ├── ENS160AHT21Sensor.h
│   ├── PMserial.h
│   ├── PMS_Czujnik.h
│   └── RTCService.h
└── ui/                  # UI and menu headers
    ├── HomeRuntime.h
    ├── UIState.h
    ├── UI_Controller.h
    └── UI_Draw.h

scripts/
├── generate_alarm_melodies.py    # build-time melody generation
├── mqtt_firebase_bridge.py       # bridge/utility script for MQTT diagnostics
└── scan_project_map.py           # workspace inventory generator

src/
├── main.cpp              # application entrypoint, setup() + loop()
├── ErriezDS3231.cpp      # RTC hardware integration
├── audio/
│   ├── AlarmMelodies.cpp
│   ├── AlarmMelodies.generated.inc
│   ├── AlarmMelodyPrefs.cpp
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
│   ├── README.md
│   ├── TelemetryComposer.cpp
│   └── WiFiSync.cpp
├── core/
│   ├── AppSettings.cpp
│   ├── AppState.cpp
│   ├── ClockAlarmService.cpp
│   ├── ModeManager.cpp
│   ├── RamTelemetry.cpp
│   ├── README.md
│   ├── RtcSyncService.cpp
│   ├── RuntimeTelemetry.cpp
│   ├── StatsManager.cpp
│   └── STM32_Data.cpp
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
└── ZEGAR-ESP32.code-workspace

test/
├── test.md               # test directory usage notes
├── logi.txt              # collected serial/log artifacts
├── verify_bt_no_wifi_mqtt.py  # diagnostic script for Bluetooth/MQTT behavior

third_party/
├── third_party.md        # vendor code and archival notes
├── archive/              # archived third-party sources
└── vendor/               # external vendor libraries or patches

lib/                      # optional PlatformIO library dependencies and local libs

# Notes
- The primary division is between portable interfaces (`include/`), implementation (`src/`), and docs/supporting artifacts (`docs/`, `hardware/`, `test/`).
- New source modules should be added under `include/` and `src/` together, with one header in `include/` and matching implementation in `src/`.
- Generated or temporary build artifacts should remain out of source control and under module-specific directories only when necessary.
