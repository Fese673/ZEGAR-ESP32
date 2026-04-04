# Telemetry

This project uses two complementary telemetry layers:

- `RuntimeTelemetry` for cumulative runtime counters such as I2C failures and audio underruns.
- `RamTelemetry` for stage-based RAM snapshots with deltas between checkpoints.

Both layers are intentionally small, production-friendly, and easy to extend without scattering ad hoc `Serial.print()` calls across the codebase.

## Build Flags

Telemetry is controlled at compile time in [platformio.ini](../../platformio.ini):

- `ENABLE_RUNTIME_TELEMETRY=1` enables the runtime counter layer and its Serial output.
- `TEST_RAM=1` enables the RAM snapshot profiler.
- `RAM_TELEMETRY_PRINT_INTERVAL_MS=5000UL` enables an optional periodic RAM heartbeat when `TEST_RAM` is on.
- `TEST_RAM_HEAP_INFO=1` enables verbose `heap_caps_print_heap_info(MALLOC_CAP_DEFAULT)` output on each RAM checkpoint.

Recommended defaults for normal builds:

- `ENABLE_RUNTIME_TELEMETRY=1`
- `TEST_RAM=0`
- `RAM_TELEMETRY_PRINT_INTERVAL_MS=5000UL` or `0UL` depending on whether you want periodic heartbeat output when RAM telemetry is enabled
- `TEST_RAM_HEAP_INFO=0`

## RuntimeTelemetry

### Module Layout

- [include/core/RuntimeTelemetry.h](../../include/core/RuntimeTelemetry.h) contains the public API, macros, and counter schema.
- [src/core/RuntimeTelemetry.cpp](../../src/core/RuntimeTelemetry.cpp) owns the global counters and the Serial formatting.

### Current Counters

The module currently tracks two high-risk areas:

- I2C
  - `i2c_timeouts`
  - `i2c_queue_full`
  - `i2c_errors`
- Audio
  - `audio_underruns`
  - `audio_overflows`
  - `audio_drops`

All counters are cumulative. They are meant for long-running debugging sessions and for comparing behavior before and after refactors.

### Serial Output

When enabled, the firmware prints a compact summary line periodically:

```text
[TEL] i2c timeouts=0 queue_full=0 errors=0 | audio underruns=0 overflows=0 drops=0
```

This line is intentionally short so it can stay usable on the Serial Monitor without flooding the log.

### How It Is Wired

- I2C counters are incremented from the shared bus worker and fallback path.
- Audio counters are incremented from the queue/ringbuffer and I2S write path.
- `src/main.cpp` calls the runtime telemetry service periodically so you see one consistent line instead of scattered ad hoc prints.

### How To Extend It

1. Add a new field to `RuntimeTelemetry::Counters` and `RuntimeTelemetry::Snapshot`.
2. Update `RuntimeTelemetry::snapshot()` and `RuntimeTelemetry::print()`.
3. Insert `TELEMETRY_INC(new_field)` or `TELEMETRY_ADD(new_field, value)` at the event source.
4. Keep the category names stable so dashboards and manual log parsing do not break.

## RamTelemetry

### Module Layout

- [include/core/RamTelemetry.h](../../include/core/RamTelemetry.h) contains the snapshot API and build flags.
- [src/core/RamTelemetry.cpp](../../src/core/RamTelemetry.cpp) owns the delta logic and Serial formatting.

### What It Measures

`RamTelemetry` captures:

- free heap
- total heap
- largest free 8-bit block
- DMA free heap
- minimum free heap

It is not a full heap tracer. The goal is to identify which stage drops RAM or fragments heap, not to record every malloc call.

### Output Contract

Example checkpoint output:

```text
[RAM][CHK] #3 SENSORS_INIT free=164832B (prev -4096, base -8192) largest=121344B (prev -2048, base -2048) ratio=736/1000 frag=264/1000 dma=46016B (prev -1024, base -1024) min=163520B total=327680B stk[mqtt=4096B wifiInit=n/a btI2S=n/a]
```

The line is intentionally compact so it stays readable in the Serial Monitor and can be copied into reports.

The extra `ratio` and `frag` fields are derived from `largestFreeBlock / freeHeap` and are meant to expose heap fragmentation at a glance. The `stk[...]` segment reports FreeRTOS stack high-water marks for the long-lived MQTT task, the WiFi init task, and the BT I2S task. If a task is not running at the moment of the snapshot, it is reported as `n/a`.

### Typical Checkpoints

Use stable tags that describe the stage, not the implementation detail:

- `BOOT`
- `I2C_READY`
- `SENSORS_INIT`
- `UI_READY`
- `NETWORK_INIT`
- `WIFI_ON`
- `WIFI_DRIVER_ON`
- `WIFI_OFF`
- `MQTT_ON`
- `MQTT_CONNECTED`
- `MQTT_OFF`
- `BT_ON`
- `AUDIO_ON`
- `AUDIO_OFF`
- `MODE_SWITCH_DONE`

### How To Extend It

1. Keep the snapshot API in `RamTelemetry` as the single source of truth.
2. Add new fields to `RamTelemetry::Snapshot` only if they are cheap and stable.
3. Add new checkpoint tags at subsystem boundaries, not inside tight loops.
4. If you need deeper heap analysis, enable `TEST_RAM_HEAP_INFO=1` temporarily instead of adding heavyweight tracing to the hot path.
5. Keep task names and task-handle getters stable if you want the stack-watermark output to remain actionable across refactors.

### Rules

- Keep `TEST_RAM` off in normal production builds.
- Use `TEST_RAM_HEAP_INFO=1` only when you need detailed heap dump output.
- Prefer explicit checkpoint tags over free-form logging.
- Do not replace the runtime telemetry counters with RAM snapshots; they solve different problems.

## Recommended Workflow

1. Enable `TEST_RAM=1`.
2. Boot the device and record the baseline.
3. Switch between BT and WiFi modes.
4. Compare `free`, `largest`, and `dma` deltas at each checkpoint.
5. If a stage drops `largest` sharply, narrow the instrumentation around that subsystem.

## Notes

- `RamTelemetry` and `RuntimeTelemetry` both use the Serial monitor, but they answer different questions.
- `RuntimeTelemetry` answers: what is failing repeatedly?
- `RamTelemetry` answers: which stage consumes or fragments RAM?
- If you later need JSON, binary export, or storage to SD/NVS, add a transport layer on top of the snapshot API instead of changing every call site.