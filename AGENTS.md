# AGENTS.md

## Build & Run

```powershell
build_zegar.py
```


No CI, no unit tests, no linter. Validation is build + serial monitor.

## Architecture

Entrypoint: `src/main.cpp` → `AppBoot::runSetup()` → `AppLoop::runLoop()`

Keep `src/main.cpp` thin; startup, loop, and shared runtime state live in `src/core/app/`.

Bootstrap order (must not be rearranged):
1. `initCoreHardware()` — Serial, RTC, heap baseline
2. Guition UART begin
3. `initPersistenceAndConfig()` — Preferences, network config load
4. `initUiAndInput()` — LCD, I2C bus, encoder, boot intro
5. `initSensors()` — PMS5003, ENS160/AHT21, BMP280
6. `initComms()` — WiFi, MQTT, NetworkOrchestrator
7. `finalizeStartup()` — radio mode selection

Domain structure: `src/<domain>/` ↔ `include/<domain>/`. New files MUST be lowercase in an existing domain folder.

Domain ownership: `core/` (bootstrap/loop/glue), `comms/` (WiFi/MQTT/radio), `bluetooth/` (A2DP+I2S), `ui/` (state+rendering), `sensors/` (snapshot-style APIs), `drivers/` (low-level, stateless), `audio/` (alarms), `input/` (encoder), `display/` (LCD), `games/`.

Secrets injected via `build_flags` or NVS — placeholder stubs in `include/config/SecretsConfig.h`, never commit real credentials.

## EventBus (App-level loop scheduling)

In `core/events/`. Timer-driven events dispatched from `AppLoop::runLoop()`:
- `EV_UI_OVERLAY` 10ms — home overlay rotation, redraw
- `EV_SENSOR_READ` 200ms — poll sensors, `NetworkOrchestrator::update()`
- `EV_CLOCK_TICK` 1000ms — clock, alarm, meteo sync
- `EV_UI_REFRESH` 1000ms — screen-specific redraws
- `EV_DIAGNOSTICS` 2000ms — heap/status log, runtime telemetry print
- `EV_MQTT_PUBLISH` 5000ms — publish sensor data
- `EV_BT_CONN_CHECK` 60000ms — BT connection status log

## Task Priorities (include/config/TaskConfig.h)

```
Core 0: BtI2STask(24) > BtAppT(19)
Core 1: encoderTask(20) > i2cWorkerTask(12) > wifiInitTask(5) > meteoSyncTask(4)
```

Static asserts enforce ordering at compile time. Lowering BtI2STask causes audio underruns; lowering encoderTask causes missed steps.

## Critical Build Config

- Partition: `board_build.partitions = huge_app.csv` (BT + WiFi require it)
- Pre-build script: `scripts/generate_alarm_melodies.py` (generates `AlarmMelodies.generated.inc`)
- Include paths are explicit per-domain `-I` flags in `platformio.ini`
- Build output stays in `.pio/`

Key build flags:
- `-DCONFIG_BT_ENABLED=1` — enables Bluetooth stack
- `-DENABLE_RUNTIME_TELEMETRY=1` — event counter logging
- `-DTEST_RAM=1` — heap snapshot telemetry (add during RAM debugging)
- `-DCORE_DEBUG_LEVEL=3` — verbose IDF logging
- `-DUART_LCD_MIRROR=1` — LCD output on UART serial
- `-DBOOT_LCD_CLEAR_TELEMETRY=1` — boot LCD clear diagnostics

Only enable telemetry/RAM flags when the task needs them.

## Conventions

**Logging**: Use `LOG_I(tag, fmt, ...)` / `LOG_W` / `LOG_E` macros from `include/core/telemetry/AppLog.h`. Never call `Serial.print()` directly — especially from audio tasks. Audio path (BtI2STask) must be completely log-free; use `RuntimeTelemetry` atomic counters instead.

**I2C shared bus**: Always lock via `I2cShared::lock(timeoutMs)` before Wire operations, then unlock. Never call Wire methods directly without the lock.

**BT audio-tools quirk**: The `arduino-audio-tools` library may re-init the BT controller. Workaround: define `extern "C" bool btInUse() { return true; }` in a source file (see `src/bluetooth/AudioBT.cpp`).

**GPIO**: Use `include/config/BoardPins.h` for all GPIO constants.

**Preferences**: Always guard reads with `s_prefs.isKey(key)`. Write via `s_prefs.putXxx(key, value)`.

**Radio handoff**: `NetworkOrchestrator::quiesceForModeSwitch()` stops MQTT, disables both radios, then enables the target. Never silently force a fallback.

**Display**: Keep LCD redraws incremental — only mirror the framebuffer when it actually changes.

**Alarm melodies**: Store selections by stable ID, not by list index.

**Safety**: Never call `WiFi.begin()` with empty credentials.

**Documentation**: Put long-form analysis in `docs/`, not in `AGENTS.md` or `README`.

## ERROR_ARCHIVE (Bug Tracking)

Folder `ERROR_ARCHIVE/` at project root. After fixing a bug, append a new entry to the **top** of `ERROR_ARCHIVE/ERROR_LOG.md` using the template from `ERROR_ARCHIVE/ERROR_ARCHIVE_GUIDE.md`.

Format: condensed, technical, entries separated by `---`. Newest always on top.

## Useful References

- `Project-Map.md` — full repo map
- `include/config/TaskConfig.h` — task priority source of truth
- `ERROR_ARCHIVE/ERROR_LOG.md` — archived bug history
- `ERROR_ARCHIVE/ERROR_ARCHIVE_GUIDE.md` — bug archive rules and template
- `docs/reports/task-map.md` — priority rationale
- `README` — per-domain overview
- `TODO` — known issues
