# Project Guidelines

## Code Style
- Keep new source and header files lowercase and grouped by the existing domain folders under `src/` and `include/`.
- Mirror `src/<domain>/` with matching public headers in `include/<domain>/`.
- Keep `src/main.cpp` thin; put startup, loop, and shared runtime state in `src/core/app/`.
- Prefer the existing logging macros from `include/core/telemetry/AppLog.h` over ad hoc `Serial.print()` calls.
- Use `include/config/BoardPins.h` for GPIO constants, and guard `Preferences::getXxx()` calls with `isKey()` first.

## Architecture
- `core/` owns bootstrap, loop, and shared runtime glue.
- `comms/` owns WiFi, MQTT, and radio orchestration; `bluetooth/` owns A2DP and I2S; `ui/` owns state and rendering; `sensors/` exposes snapshot-style APIs; `drivers/` stays low-level and stateless.
- Keep radio handoff behavior explicit; do not silently force a fallback mode if the primary radio fails.
- Keep LCD redraws incremental; only mirror the framebuffer when it actually changes.
- For deeper context, see [Project map](../Project-Map.md), [headers overview](../include/headers.md), [docs index](../docs/index.md), and [telemetry docs](../docs/telemetry/telemetry.md).

## Build and Test
- Use the `esp32dev` PlatformIO environment.
- Build with `C:\Users\PC\.platformio\penv\Scripts\pio.exe run -e esp32dev`.
- Upload with `C:\Users\PC\.platformio\penv\Scripts\pio.exe run -t upload -e esp32dev`.
- Monitor serial output with `C:\Users\PC\.platformio\penv\Scripts\pio.exe device monitor -e esp32dev`.
- Keep build output in `.pio/`; do not reintroduce `.pio_build/`.
- Only enable telemetry and RAM diagnostics flags in `platformio.ini` when the task needs them.

## Conventions
- Store alarm melody selections by stable ID, not by list index.
- Never call `WiFi.begin()` with empty credentials.
- When touching I2C or RTC code, preserve the shared bus and fallback recovery patterns.
- Put long-form analysis in `docs/` and link to it instead of repeating it here.
- If you need known project pitfalls, check `TODO` and the repo memory notes before inventing new patterns.