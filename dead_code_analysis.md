# Dead Code Analysis – Executive Summary

This static analysis of the `ZEGAR-ESP32` project found a small set of likely dead or legacy code paths, primarily in the Bluetooth audio subsystem.

- Total likely dead-code findings: 4
- Severity distribution:
  - LEGACY DEAD: 2
  - PROBABLY DEAD: 1
  - PARTIALLY DEAD: 1
- Estimated cleanup impact:
  - Flash size reduction: modest, likely tens of kilobytes if legacy A2DP branches are removed.
  - RAM savings: low, mostly from dead static objects and conditional task configuration.
  - Complexity reduction: moderate, because the Bluetooth/A2DP codebase is large and contains multiple conditional backends.

Assumptions:
- Build environment is Arduino/PlatformIO with `espressif32` and `AudioTools` installed.
- No dynamic execution trace or coverage data was available; analysis is based on source cross-references and compile-time conditions.
- FreeRTOS task and queue usage was inferred from visible creation/consumer code in `src`.

Validation update:
- The later cleanup pass reclassified the Bluetooth and PMserial findings as CONDITIONALLY USED / externally referenced rather than truly dead.
- See [DEAD_CODE_CLEANUP_REPORT.md](DEAD_CODE_CLEANUP_REPORT.md) for the verified status and the confirmed heap fix.

## Dead Code Inventory

| File | Line Range | Element Type | Deadness Level | Confidence | Short Reason |
|------|------------|--------------|----------------|------------|--------------|
| `src/bluetooth/BluetoothA2DPSource.cpp` | 1-1054 | module/class | PROBABLY DEAD | 85% | `BluetoothA2DPSource` is implemented but has no external references in project sources. |
| `include/config/a2dp_config.h` / `src/bluetooth/BluetoothA2DPOutput.cpp` | 7-9 / 8-196 | branch/module | LEGACY DEAD | 75% | Legacy I2S backend guarded by `A2DP_LEGACY_I2S_SUPPORT` and `AudioTools` backend is available. |
| `src/bluetooth/BluetoothA2DPOutput.cpp` | 8-196 | class methods | PARTIALLY DEAD | 70% | `BluetoothA2DPOutputLegacy` methods compile only under legacy I2S support and are likely inactive on current build. |
| `include/sensors/PMserial.h` | 81-94 | constructor/feature | PROBABLY DEAD | 60% | ESP32 `SerialPM(PMS sensor, uint8_t rx, uint8_t tx)` constructor is marked `TODO: WIP!!!` and may be incomplete. |

## Detailed Findings

### [MODULE] BluetoothA2DPSource
- Location: `src/bluetooth/BluetoothA2DPSource.cpp:1`
- Deadness Level: PROBABLY DEAD
- Confidence: 85%
- Why it's dead:
  - The class is defined and compiled, but project-wide grep shows no references outside `BluetoothA2DPSource.cpp` and its header.
  - No call sites to `BluetoothA2DPSource()` or `new BluetoothA2DPSource` were found in `ZEGAR-ESP32/src`.
- Call graph analysis:
  - Orphan module: only self-references and callback wrappers in the same file.
  - No root call from `AppBoot`, `AppLoop`, or Bluetooth initialization code.
- Data flow impact:
  - If truly unused, it contributes only build size and does not change runtime state.
- Risk if removed:
  - Low, if the project does not intend to support A2DP source mode.
  - Medium if external code or future build variants expect source-mode objects.
- Recommendation:
  - Remove the module or isolate it behind an explicit build flag.
  - If source mode is required in future, reintroduce it in a dedicated optional component.

### [BRANCH] Legacy A2DP I2S backend
- Location: `include/config/a2dp_config.h:7-9`, `src/bluetooth/BluetoothA2DPOutput.cpp:8-196`
- Deadness Level: LEGACY DEAD
- Confidence: 75%
- Why it's dead:
  - `A2DP_LEGACY_I2S_SUPPORT` is enabled only for `ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)`.
  - The current PlatformIO `platformio.ini` uses Arduino on `espressif32` and includes `arduino-audio-tools`.
  - The active Bluetooth sink path in `AudioBT.cpp` prefers `A2DP_I2S_AUDIOTOOLS` when `AudioTools.h` is present.
- Call graph analysis:
  - Legacy output backend is a compile-time conditional branch.
  - No active runtime path in modern builds when `AudioTools` is available.
- Data flow impact:
  - This path sets up I2S/DMA, but if inactive it only wastes flash.
- Risk if removed:
  - Low if IDF>=5 and `AudioTools` is always present.
  - High if the project targets older ESP-IDF versions or legacy hardware without `AudioTools`.
- Recommendation:
  - Move legacy I2S backend behind an explicit `USE_LEGACY_A2DP_I2S` flag.
  - Remove support for `A2DP_LEGACY_I2S_SUPPORT` if the current target is locked to modern IDF.

### [CLASS] BluetoothA2DPOutputLegacy methods
- Location: `src/bluetooth/BluetoothA2DPOutput.cpp:8-196`
- Deadness Level: PARTIALLY DEAD
- Confidence: 70%
- Why it's dead:
  - All methods are protected by `#if A2DP_LEGACY_I2S_SUPPORT`.
  - The methods are large and only relevant when the legacy backend is selected.
- Call graph analysis:
  - Methods are reachable only through `BluetoothA2DPOutput` when legacy support is enabled.
  - In modern builds this is effectively a dead subtree.
- Data flow impact:
  - No effect if legacy support is disabled at compile time.
- Risk if removed:
  - Low if legacy support is truly unnecessary.
  - Medium if the code is needed for fallback or older boards.
- Recommendation:
  - Extract legacy backend into a separate translation unit or library.
  - Keep the main Bluetooth audio path small and focused on `AudioTools`.

### [CONSTRUCTOR] Incomplete PMserial ESP32 constructor
- Location: `include/sensors/PMserial.h:81-94`
- Deadness Level: PROBABLY DEAD
- Confidence: 60%
- Why it's dead:
  - The constructor is marked with `//TODO: WIP!!!`.
  - The code path sets `hwSerial = serModeManual` but is guarded by `#ifdef HAS_SW_SERIAL` and `#elif defined(ESP32)`.
- Call graph analysis:
  - It is not clear from the repo whether `SerialPM(PMS sensor, uint8_t rx, uint8_t tx)` is actually instantiated for ESP32.
  - If the project uses only `PMS5003Sensor` wrapper and hardware Serial1, this constructor is a maintenance liability.
- Data flow impact:
  - Minimal at runtime unless manual Serial port mode is used.
- Risk if removed:
  - Low to medium, depending on whether ESP32 serial PM sensor support is needed.
- Recommendation:
  - Replace the unfinished constructor with a stable ESP32 serial-port implementation or remove it until explicitly required.

## Call Graph Gaps

- Orphaned module: `src/bluetooth/BluetoothA2DPSource.cpp` has no calls from any other project file.
- Conditional branch gap: `BluetoothA2DPOutputLegacy` is only reachable when legacy I2S support is enabled.
- The main app entrypoints (`src/main.cpp`, `src/core/app/AppBoot.cpp`, `src/core/app/AppLoop.cpp`) cover UI, sensors, comms, and Bluetooth, with no obvious missing subsystem.

## Unused Tasks and RTOS Artifacts

- No obvious unused RTOS tasks were found in the main app flow.
- Confirmed active task/queue usage in:
  - `src/input/Encoder.cpp` (`encoderTask`, `s_eventQueue`)
  - `src/drivers/I2C_bus_shared.cpp` (`gI2cWorkerTask`, `gI2cRequestQueue`)
  - `src/comms/WiFiSync.cpp` (`wifiInitTask`)
  - `src/comms/MQTTSync.cpp` (`mqtt_task_handle`)
  - `src/bluetooth/BluetoothA2DPSinkQueued.cpp` (`bt_i2s_task_handler`, ringbuffer)
- `BluetoothA2DPSinkQueued` task usage is conditionally active with BT audio and appears intended.

## Dead DMA / Peripheral Paths

- Legacy I2S/DMA path in `src/bluetooth/BluetoothA2DPOutput.cpp` is the only strong peripheral dead path.
- It configures I2S, DMA buffer counts, and pin config only under `A2DP_LEGACY_I2S_SUPPORT`.
- If the project is built with `AudioTools` support, the legacy DMA path is inactive and can be pruned.

## Zombie Code

- No strong evidence of running-but-useless code in the main app loop.
- The likely zombie candidates are:
  - `BluetoothA2DPSource` module: present but unreferenced.
  - `PMserial.h` ESP32 manual constructor path: present but incomplete.

## Refactoring Recommendations

1. Remove or isolate `BluetoothA2DPSource` if source-mode audio is not required.
2. Separate legacy A2DP I2S support into a clearly named compatibility module and guard it with an explicit build flag.
3. Clean up `include/sensors/PMserial.h` ESP32 constructor or replace it with a stable implementation.
4. Keep the main `AudioBT` sink path small; prefer `AudioTools` branch when present.
5. Review `include/bluetooth/BluetoothA2DP.h` and headers for broad inclusions that pull unused source-mode declarations into every compilation unit.

## Risk Assessment

- Removing `BluetoothA2DPSource` is likely safe for the current app but should be verified by searching for future source-mode usage.
- Removing legacy I2S backend is safe only if the target environment is modern and `AudioTools` is guaranteed.
- The greatest uncertainty comes from build variants and external library behavior, not from the static code spelled out in this repository.

## Cleanup Impact

- Estimated binary reduction: 5-20 KiB by removing unreferenced Bluetooth source and legacy I2S code.
- Estimated RAM savings: minimal, mostly stack/task configuration overhead and a few static objects.
- Complexity reduction: moderate, because legacy Bluetooth audio support is a large conditional subsystem.

---

> Note: This analysis is based on static source review and does not use runtime coverage. If you want to verify removals safely, compile with the current PlatformIO environment and run smoke tests for Bluetooth audio, PMS, ENS160, and WiFi/MQTT subsystems.
