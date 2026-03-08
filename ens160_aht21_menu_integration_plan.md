# ENS160 + AHT21 menu integration report

## 1. Analysis of the existing menu system

- The main menu is defined in `src/main.cpp` through the global `menuItems[]`, `menuIndex`, and `menuCount` variables.
- Encoder-driven state transitions are handled centrally in `src/UI_Controller.cpp` inside `ui_handleEvent()`.
- LCD rendering is split by screen type in `src/UI_Draw.cpp`; the menu itself is rendered by `drawMenu()`, while sensor and stats-related screens are rendered by `drawStats()`.
- The project uses `AppState` values from `include/AppState.h` as the single routing mechanism for screen changes.
- Periodic live redraws for sensor-related views are triggered in `src/main.cpp` from the main loop.

## 2. How the PMS5003 screen works

- `PMS5003` exists as a dedicated top-level menu option in `src/main.cpp`.
- Selecting it in `src/UI_Controller.cpp` switches the app into a dedicated `STATE_PMS5003` state.
- The PMS5003 UI is rendered in `drawStats()` in `src/UI_Draw.cpp`, which contains both the submenu view and the detail screens.
- PMS-specific redraw throttling is based on a dirty flag and the last update timestamp returned by `PMS5003Sensor::getLastUpdateTime()`.
- The long-press navigation path is also centralized in `src/UI_Controller.cpp`, returning from PMS screens back to the PMS menu and then to the main menu.

## 3. Architectural decisions for ENS160 + AHT21

- The new screen follows the same routing model as PMS5003: a dedicated `AppState` value is added and handled by the existing controller and renderer.
- No sensor logic was added. A separate lightweight scaffold module was introduced in `include/ENS160AHT21Screen.h` and `src/ENS160AHT21Screen.cpp`.
- The scaffold exposes `ENS160AHT21Screen::RuntimeData`, which is the future integration point for the already working ENS160 + AHT21 acquisition code.
- A `screenDirty` flag and `lastUpdateMs` field were prepared so future sensor code can request redraws without changing the UI architecture.
- The placeholder layout was compacted to fit the existing 20x4 LCD while still reserving all required fields: `AQI`, `TVOC`, `eCO2`, `Temp`, `Hum`, and `Status`.

## 4. Implemented changes

- Added a new main menu item: `AHT21 + ENS160`.
- Added a new state: `STATE_ENS160_AHT21`.
- Added controller handling so selecting the new menu item opens the dedicated placeholder screen.
- Added long-press navigation so the new screen returns to the main menu consistently with the rest of the UI.
- Added the placeholder LCD screen to `drawStats()`.
- Added a compact runtime-data container for future sensor integration.
- Extended the periodic redraw logic in `src/main.cpp` so the new screen is refreshed through the same UI path used by sensor/stat views.

## 5. Prepared integration points for future sensor code

- `include/ENS160AHT21Screen.h` defines the future handoff structure:
  - `aqi`
  - `tvoc`
  - `eco2`
  - `temperatureC`
  - `humidityPct`
  - `statusText`
  - `lastUpdateMs`
  - `hasSample`
- `src/ENS160AHT21Screen.cpp` provides:
  - `runtimeData`
  - `screenDirty`
  - `resetRuntimeData()`
  - `markScreenDirty()`
- Future ENS160 + AHT21 code can update `runtimeData`, set `hasSample = true`, refresh `statusText`, update `lastUpdateMs`, and call `markScreenDirty()`.
- No MQTT, I2C readout, or sensor `update()` logic was added at this stage.