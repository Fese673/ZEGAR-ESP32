# Project map

The map is organized by functional domains, not by implementation detail. It is intended to be easy to extend as the firmware grows.

platformio.ini            # build config, lib_deps, build_flags
README.md                 # high-level project overview for humans
Project-Map.md            # this file

docs/
├── index.md              # docs index and navigation guidance
├── telemetry/            # runtime and diagnostic telemetry docs
│   └── telemetry.md
├── reports/              # architecture/refactor/analysis reports
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
├── config/              # board pins, secrets, temperature config
│   ├── BoardPins.h
│   ├── SecretsConfig.h
│   └── TemperatureConfig.h
├── core/                # core application interfaces
│   ├── AlarmTypes.h
│   ├── AppState.h
│   ├── ClockAlarmService.h
│   ├── ModeManager.h
│   ├── RamTelemetry.h
│   ├── RtcSyncService.h
│   ├── RuntimeTelemetry.h
│   └── StatsManager.h
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
│   ├── BluetoothA2DPSource.h
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
└── ui/                  # UI and menu headers

scripts/
├── generate_alarm_melodies.py    # build-time melody generation
└── mqtt_firebase_bridge.py       # bridge/utility script for MQTT diagnostics

src/
├── main.cpp              # application entrypoint, setup() + loop()
├── ErriezDS3231.cpp      # RTC hardware integration
├── audio/                # audio subsystem implementation
├── bluetooth/            # Bluetooth service implementation
├── comms/                # WiFi/MQTT and network orchestration implementation
├── core/                 # core application logic and state management
├── display/              # screen rendering and UI output
├── drivers/              # low-level hardware driver implementations
├── input/                # encoder and user input services
├── sensors/              # sensor runtime acquisition services
└── ui/                   # menu, display flow, and user interaction

test/
├── test.md               # test directory usage notes
├── logi.txt              # collected serial/log artifacts

third_party/
├── third_party.md        # vendor code and archival notes
├── archive/              # archived third-party sources
└── vendor/               # external vendor libraries or patches

lib/                      # optional PlatformIO library dependencies and local libs

# Notes
- The primary division is between portable interfaces (`include/`), implementation (`src/`), and docs/supporting artifacts (`docs/`, `hardware/`, `test/`).
- New source modules should be added under `include/` and `src/` together, with one header in `include/` and matching implementation in `src/`.
- Generated or temporary build artifacts should remain out of source control and under module-specific directories only when necessary.
