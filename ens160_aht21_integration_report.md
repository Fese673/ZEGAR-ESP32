# ENS160 + AHT21 integration report

## 1. Integration plan

- Reuse the already working non-blocking `AHTxx` implementation from `KOD_do_ens160_i_aht21` as local project files.
- Extract ENS160 runtime logic from the standalone sensor example into a dedicated adapter module instead of importing the old standalone `main.cpp`.
- Initialize the adapter after the main project I2C setup in `src/main.cpp`.
- Run sensor polling from the main firmware loop using a throttled, non-blocking `update()`.
- Push sensor values into `ENS160AHT21Screen::runtimeData` and request UI refresh with `markScreenDirty()`.

## 2. Added and modified files

### Added

- `include/ENS160AHT21Sensor.h`
- `src/ENS160AHT21Sensor.cpp`
- `include/AHTxx.h`
- `src/AHTxx.cpp`
- `ens160_aht21_integration_report.md`

### Modified

- `platformio.ini`
- `src/main.cpp`
- `include/ENS160AHT21Screen.h`
- `src/ENS160AHT21Screen.cpp`
- `src/UI_Draw.cpp`

## 3. How sensor data reaches the UI

- `src/main.cpp` calls `ENS160AHT21Sensor::begin()` during setup and `ENS160AHT21Sensor::update()` in the main loop.
- `src/ENS160AHT21Sensor.cpp` owns the ENS160 and AHT21 runtime objects and performs the actual polling.
- AHT21 is driven by the non-blocking `AHTxx` state machine.
- ENS160 is polled on a timed interval derived from `ENS16X_SYSTEM_TIMING_STANDARD_MEASURE`.
- When fresh values or a relevant status change appear, the adapter updates:
  - `aqi`
  - `tvoc`
  - `eco2`
  - `temperatureC`
  - `humidityPct`
  - `statusText`
  - `lastUpdateMs`
  - `hasSample`
  - `hasGasSample`
  - `hasClimateSample`
- After updating runtime data, the adapter calls `ENS160AHT21Screen::markScreenDirty()`.
- `src/UI_Draw.cpp` reads `ENS160AHT21Screen::runtimeData` and displays gas values independently from temperature and humidity placeholders.

## 4. Problems encountered during integration

- The working ENS160 + AHT21 code was delivered as a standalone project with its own `main.cpp`, so it could not be dropped into the clock project directly.
- The main project already used DHT for temperature and humidity, while the new integrated path uses AHT21 only for the ENS160+AHT21 screen. To avoid unnecessary architectural churn, the existing DHT path was left untouched.
- The placeholder UI originally had only a single `hasSample` flag. It was extended with `hasGasSample` and `hasClimateSample` so the LCD can show partial data correctly during startup and warm-up.
- The build still reports existing Bluetooth-related `CONFIG_BLUEDROID_ENABLED` redefinition warnings; these predate this integration and are not caused by the ENS160/AHT21 changes.

## 5. Notes for further development

- If the project should fully migrate environmental temperature and humidity to AHT21, the current DHT path in `src/main.cpp` can be retired in a later refactor.
- If MQTT should publish ENS160+AHT21 values, the safest next step is to read directly from `ENS160AHT21Screen::runtimeData` or expose a small read-only accessor from the adapter.
- If you want more precise LCD output, `src/UI_Draw.cpp` can be extended to render temperature and humidity with one decimal place using formatted buffers.