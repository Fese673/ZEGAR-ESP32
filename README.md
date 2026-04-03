# ZEGAR-ESP32

Embedded firmware project for an ESP32-based clock platform.

## Repository Layout
- `src/` - firmware source code grouped by domain
- `include/` - public headers grouped by domain
- `hardware/` - PCB, schematics, and hardware reference material
- `third_party/` - external/vendor code and historical snapshots
- `docs/reports/` - project reports and technical writeups
- `scripts/` - build-time helper scripts

## Build
- Environment: `esp32dev`
- Command: `C:\Users\PC\.platformio\penv\Scripts\platformio.exe run --environment esp32dev`

## Notes
- Keep generated files under their dedicated module folders.
- Prefer lowercase folder names for cross-platform consistency.
