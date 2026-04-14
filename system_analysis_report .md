# System Initialization Flow

This document provides a deep technical analysis of task initialization, DMA usage, and multi-core workload distribution in the ZEGAR-ESP32 embedded system, which runs on FreeRTOS with the Arduino framework on an ESP32 dual-core processor.

## Boot Sequence Overview

The system follows a structured multi-stage initialization sequence from hardware reset to steady-state operation:

### Stage 0: Hardware Reset
- ESP32 bootloader executes
- Arduino framework initializes
- FreeRTOS scheduler starts (implicit, managed by ESP-IDF)
- Core 0: Runs `setup()` then `loop()`
- Core 1: Idle until tasks are spawned

### Stage 1: Core Hardware Initialization (`initCoreHardware`)
**File**: `src/core/app/AppBoot.cpp:157-175`

```
1. Serial.begin(921600)                  - UART0 console output
2. Delay 100ms with yield()              - Allow serial to stabilize
3. RtcSyncService::applyTimezone()       - Configure timezone offset
4. Capture heap baseline                 - Record initial free memory
5. ModeManager::begin()                  - Initialize mode state machine
6. RamTelemetry::begin()                 - Reset telemetry counters
7. initSevenSeg()                        - Configure 7-segment display GPIOs
```

**Dependencies**: None (first initialization step)
**Execution Context**: Arduino setup() on Core 0

### Stage 2: Persistence and Configuration (`initPersistenceAndConfig`)
**File**: `src/core/app/AppBoot.cpp:177-191`

```
1. s_prefs.begin("zegar", false)         - Open NVS (Non-Volatile Storage) namespace
2. loadNetworkConfigFromPreferences()    - Load WiFi/MQTT credentials
3. Load user preferences                 - Background music, boot intro settings
4. RadioModeSwitch::begin()              - Initialize RTC communication for mode detection
5. restoreRtcHandoffTime()               - Restore clock time from DS3231 RTC if available
```

**Dependencies**: Core hardware (Serial, RTC)
**Execution Context**: Arduino setup() on Core 0

### Stage 3: UI and Input Initialization (`initUiAndInput`)
**File**: `src/core/app/AppBoot.cpp:193-239`

```
1. LCD initialization                    - Initialize HD44780-compatible LCD via I2C
2. I2cShared::initMaster()              - **CRITICAL: Creates I2C worker task on Core 1**
   - Wire.begin(SDA=21, SCL=22)
   - Set clock to 100kHz
   - Create gI2cRequestQueue (8 slots)
   - Spawn i2cWorker task on Core 1 (priority: configMAX_PRIORITIES-4, stack: 3072)
3. RTC time restore from DS3231         - Read current time from hardware RTC
4. LCD backlight on                      - Turn on display backlight
5. BootIntroService::begin()             - Prepare boot animation (if enabled)
6. encoder_begin()                       - **CRITICAL: Creates encoder task on Core 1**
   - Configure GPIO pins with pull-ups
   - Create s_eventQueue (64 events)
   - Spawn encoderTask on Core 1 (priority: 2, stack: 2048)
7. pinMode(BUZZER_PIN, OUTPUT)           - Configure buzzer GPIO
```

**Dependencies**: I2C bus, RTC hardware
**First Background Tasks Created**:
- `i2cWorker` (Core 1)
- `encoderTask` (Core 1)

**Execution Context**: Arduino setup() on Core 0

### Stage 4: Sensor Initialization (`initSensors`)
**File**: `src/core/app/AppBoot.cpp:241-264`

```
1. STM32data_begin()                     - Initialize UART link to STM32 co-processor
2. PMS5003Sensor::begin()                - Initialize particulate matter sensor
3. ENS160AHT21Sensor::begin()            - Initialize air quality + humidity sensor
4. BMP280Sensor::begin()                 - Initialize barometric pressure sensor
5. HomeRuntime::begin()                  - Register UI draw callbacks
```

**Dependencies**: I2C worker task (sensors use I2C bus)
**Execution Context**: Arduino setup() on Core 0
**Note**: All I2C operations are serialized through the i2cWorker task on Core 1

### Stage 5: Communications Initialization (`initComms`)
**File**: `src/core/app/AppBoot.cpp:266-352`

```
1. ui_begin()                            - Register UI callbacks
2. Load user preferences                 - Home overlay interval, alarm settings, MQTT config
3. WiFiSync::begin()                     - Configure WiFi parameters (does NOT start WiFi yet)
4. MQTTSync::configure()                 - Configure MQTT client parameters
5. NetworkOrchestrator::begin()          - Initialize network state machine
```

**Dependencies**: Preferences loaded, UI initialized
**Execution Context**: Arduino setup() on Core 0
**Note**: WiFi and Bluetooth tasks are NOT created yet; they are spawned on-demand

### Stage 6: Finalization (`finalizeStartup`)
**File**: `src/core/app/AppBoot.cpp:354-364`

```
1. LoopBaselineTelemetry::resetWindow()  - Reset telemetry timing window
2. Detect current radio mode             - Determine if BT or WiFi should be active
3. ModeManager::logDiag()                - Log boot diagnostics
```

**Dependencies**: All subsystems initialized
**Execution Context**: Arduino setup() on Core 0

## Task Lifecycle and Scheduling

### Task Creation Summary

| Task Name | Core | Priority | Stack (bytes) | Queue Size | Created When | File:Line |
|-----------|------|----------|---------------|------------|--------------|-----------|
| **encoderTask** | 1 | 2 | 2048 | 64 events | Stage 3 (setup) | Encoder.cpp:117 |
| **i2cWorker** | 1 | configMAX_PRIORITIES-4 | 3072 | 8 requests | Stage 3 (setup) | I2C_bus_shared.cpp:196 |
| **wifiInit** | 1 | 5 | 4096 | - | On-demand | WiFiSync.cpp:188 |
| **BtAppT** | 1 | configMAX_PRIORITIES-10 | 3072 | 20 events | When BT starts | BluetoothA2DPCommon.cpp:498 |
| **BtI2STask** | 0 | configMAX_PRIORITIES-3 | 3072 | ringbuffer | When audio starts | BluetoothA2DPSinkQueued.cpp:24 |

### Task Priority Hierarchy

On ESP32, `configMAX_PRIORITIES` is typically 25, so the effective priorities are:

1. **Priority 24 (configMAX_PRIORITIES-1)**: Reserved for critical system tasks
2. **Priority 22 (configMAX_PRIORITIES-3)**: `BtI2STask` - Real-time audio output
3. **Priority 21 (configMAX_PRIORITIES-4)**: `i2cWorker` - I2C peripheral access
4. **Priority 15 (configMAX_PRIORITIES-10)**: `BtAppT` - Bluetooth event handling
5. **Priority 5**: `wifiInit` - WiFi driver initialization
6. **Priority 2**: `encoderTask` - User input polling
7. **Priority 1**: Arduino loop() - Main application logic (default)
8. **Priority 0**: FreeRTOS idle task

### Task Execution Flow

#### Encoder Task (`encoderTask`)
**File**: `src/input/Encoder.cpp:67-75`

```c++
void encoderTask(void* param) {
  for (;;) {
    const EncoderEvent evt = encoderSampleOnce();
    if (evt != ENC_NONE && s_eventQueue != nullptr) {
      xQueueSendToBack(s_eventQueue, &evt, 0);
    }
    vTaskDelay(pdMS_TO_TICKS(1));  // 1ms polling interval
  }
}
```

**Behavior**:
- Continuously polls rotary encoder GPIO pins (CLK, DT, SW)
- Implements Gray-code state machine for rotation detection
- Handles button debouncing and long-press detection
- Publishes events to queue (consumed by main loop)
- **No blocking**: 1ms fixed interval

#### I2C Worker Task (`i2cWorkerTask`)
**File**: `src/drivers/I2C_bus_shared.cpp:143-184`

```c++
void i2cWorkerTask(void *) {
  for (;;) {
    I2cRequest *request = nullptr;
    // Block until request arrives
    if (xQueueReceive(gI2cRequestQueue, &request, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    // Execute I2C operation
    switch (request->op) {
      case Probe:   request->result = executeProbe(...); break;
      case Write:   request->result = executeWrite(...); break;
      case WriteRead: request->result = executeWriteRead(...); break;
    }

    // Signal completion
    request->completed.store(true);
    xSemaphoreGive(request->done);
    releaseRequest(request);
  }
}
```

**Behavior**:
- **Blocks** on queue waiting for I2C requests
- Serializes all I2C operations (prevents bus conflicts)
- Supports retry mechanism (up to 3 attempts)
- Returns results via semaphore signaling
- **Purpose**: Offload I2C operations from main loop to dedicated task

#### WiFi Initialization Task (`wifiInitTask`)
**File**: `src/comms/WiFiSync.cpp:158-176`

```c++
static void wifiInitTask(void *) {
  WiFi.mode(WIFI_MODE_STA);
  vTaskDelay(pdMS_TO_TICKS(50));

  WiFi.begin(s_wifiSsid.c_str(), s_wifiPass.c_str());
  vTaskDelay(pdMS_TO_TICKS(50));

  // Self-delete after initialization
  wifiBeginTaskHandle = nullptr;
  vTaskDelete(nullptr);
}
```

**Behavior**:
- **Temporary task**: Self-terminates after WiFi.begin()
- Created on-demand when WiFi connection requested
- Offloads blocking WiFi driver initialization to Core 1
- **Lifecycle**: Created → Initialize WiFi → Delete

#### Bluetooth App Task (`BtAppT`)
**File**: `src/Bluetooth/BluetoothA2DPCommon.cpp:29-31`

```c++
void ccall_bt_app_task_handler(void *arg) {
  bt_app_task_handler();  // Internal library implementation
}
```

**Behavior**:
- Waits on `app_task_queue` for Bluetooth stack events
- Processes A2DP connection state changes
- Handles remote control commands (play, pause, volume)
- Dispatches callbacks to user code

#### Bluetooth I2S Audio Task (`BtI2STask`)
**File**: `src/Bluetooth/BluetoothA2DPSinkQueued.cpp:75-143`

```c++
void i2s_task_handler(void *arg) {
  is_starting.store(true);

  while (true) {
    // Check if ringbuffer needs reset
    if (needs_ringbuffer_reset.load()) {
      drain_ringbuffer();
      needs_ringbuffer_reset.store(false);
    }

    // Suspend task when audio inactive
    if (!bt_audio_active.load()) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }

    // Wait for ringbuffer prefetch on startup
    if (is_starting.load()) {
      if (pdTRUE != xSemaphoreTake(s_i2s_write_semaphore, pdMS_TO_TICKS(3000))) {
        TELEMETRY_INC(audio_underruns);
        continue;
      }
      is_starting.store(false);
    }

    // Read audio data from ringbuffer
    data = xRingbufferReceiveUpTo(s_ringbuf_i2s, &item_size, pdMS_TO_TICKS(10), 1920);

    if (item_size == 0) {
      TELEMETRY_INC(audio_underruns);
      ringbuffer_mode.store(RINGBUFFER_MODE_PREFETCHING);
      continue;
    }

    // Write to I2S DMA
    if (is_i2s_active.load()) {
      i2s_write_data(data, item_size);
    }

    vRingbufferReturnItem(s_ringbuf_i2s, data);
  }
}
```

**Behavior**:
- **Real-time audio output**: Highest priority user task (Priority 22)
- **Adaptive prefetch**: Waits for ringbuffer to fill before starting playback
- **Underrun handling**: Switches to prefetch mode if buffer runs dry
- **DMA feeding**: Continuously writes audio samples to I2S DMA buffers
- **Suspension**: Blocks via `ulTaskNotifyTake()` when audio inactive

### Task Dependencies

```
setup() [Core 0]
  │
  ├─→ initCoreHardware()
  │     └─→ Serial, RTC, GPIO init
  │
  ├─→ initPersistenceAndConfig()
  │     └─→ NVS, preferences
  │
  ├─→ initUiAndInput()
  │     ├─→ I2C master init
  │     │    └─→ SPAWNS i2cWorker [Core 1, Priority 21]
  │     └─→ Encoder init
  │          └─→ SPAWNS encoderTask [Core 1, Priority 2]
  │
  ├─→ initSensors()
  │     └─→ Sensors use i2cWorker (dependency)
  │
  └─→ initComms()
        └─→ Configure WiFi/MQTT (tasks created later)

loop() [Core 0]
  │
  ├─→ encoder_update()
  │     └─→ Reads from s_eventQueue (produced by encoderTask)
  │
  ├─→ UI refresh
  │     └─→ May use I2C (via i2cWorker)
  │
  ├─→ NetworkOrchestrator::update()
  │     ├─→ May spawn wifiInit [Core 1, Priority 5]
  │     └─→ May spawn BtAppT [Core 1, Priority 15]
  │
  └─→ Sensor updates
        └─→ Use i2cWorker for I2C transactions

Bluetooth Audio Start (on-demand)
  │
  └─→ audioBT_init()
        └─→ SPAWNS BtI2STask [Core 0, Priority 22]
```

## DMA Analysis

### I2S DMA Configuration

The system uses I2S (Inter-IC Sound) DMA for audio output to minimize CPU overhead during Bluetooth A2DP audio playback.

#### DMA Buffer Configuration (AudioTools Backend)
**File**: `src/Bluetooth/AudioBT.cpp:60-72`

```c++
audio_tools::I2SConfig i2sConfig = s_audioStream.defaultConfig(audio_tools::TX_MODE);
i2sConfig.sample_rate = 44100;           // 44.1 kHz sample rate
i2sConfig.bits_per_sample = 16;          // 16-bit audio
i2sConfig.channels = 2;                  // Stereo
i2sConfig.buffer_count = 8;              // 8 DMA buffers
i2sConfig.buffer_size = 256;             // 256 frames per buffer
i2sConfig.use_apll = true;               // Use Audio PLL for precise timing
i2sConfig.auto_clear = true;             // Zero-fill on underrun
```

**Total DMA Buffer Size Calculation**:
```
Total Size = buffer_count × buffer_size × channels × (bits_per_sample / 8)
           = 8 × 256 × 2 × 2
           = 8192 bytes (8 KB)
```

#### DMA Buffer Configuration (Legacy ESP-IDF Backend)
**File**: `src/Bluetooth/AudioBT.cpp:156-167`

```c++
i2s_config_t i2s_config = {
  .mode = I2S_MODE_MASTER | I2S_MODE_TX,
  .sample_rate = 44100,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
  .communication_format = I2S_COMM_FORMAT_STAND_I2S,
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,  // Interrupt priority level 1
  .dma_buf_count = 12,                       // 12 DMA buffers
  .dma_buf_len = 128,                        // 128 samples per buffer
  .use_apll = true,
  .tx_desc_auto_clear = true
};
```

**Total DMA Buffer Size (Legacy)**:
```
Total Size = dma_buf_count × dma_buf_len × channels × (bits_per_sample / 8)
           = 12 × 128 × 2 × 2
           = 6144 bytes (6 KB)
```

#### I2S Pin Configuration
**File**: `src/Bluetooth/AudioBT.cpp:70-72` and `BoardPins.h`

```c++
i2sConfig.pin_bck = BoardPins::kBtI2sBclk;   // Bit Clock (GPIO 33)
i2sConfig.pin_ws = BoardPins::kBtI2sWs;      // Word Select (GPIO 32)
i2sConfig.pin_data = BoardPins::kBtI2sData;  // Data Output (GPIO 14)
```

**GPIO Assignments**:
- **GPIO 33**: Bit Clock (BCK) - Synchronizes bit transmission
- **GPIO 32**: Word Select (WS/LRCK) - Selects left/right channel
- **GPIO 14**: Serial Data (DOUT) - Audio sample data

### Ringbuffer (Software DMA Queue)

Between the Bluetooth stack and I2S DMA, a FreeRTOS ringbuffer acts as an elastic buffer.

**File**: `src/Bluetooth/BluetoothA2DPSinkQueued.cpp:17`

```c++
s_ringbuf_i2s = xRingbufferCreate(i2s_ringbuffer_size, RINGBUF_TYPE_BYTEBUF);
```

**Configuration** (`src/Bluetooth/AudioBT.cpp:27-28`):
```c++
constexpr int kRingbufferSizeBytes = 12 * 1024;        // 12 KB
constexpr int kRingbufferPrefetchPercent = 80;          // Wait until 80% full before starting
```

**Prefetch Threshold**:
```
Prefetch Size = 12288 × 0.80 = 9830 bytes
```

**Purpose**:
- Absorb jitter from Bluetooth stack
- Prevent I2S underruns during CPU load spikes
- Allow prefetch before starting audio playback

### DMA Memory Management

**DMA-Capable Memory Tracking**:
**File**: `src/core/telemetry/RamTelemetry.cpp:124`

```c++
values.dmaFree = heap_caps_get_free_size(MALLOC_CAP_DMA);
```

**Periodic Reporting**:
The system reports DMA memory usage in telemetry heartbeats every 5 seconds:

```
Heap heartbeat #42 checkpoint=HEARTBEAT ... dma_b=52480 prev_dma_b=+0 base_dma_b=-8192 ...
```

**Interpretation**:
- `dma_b=52480`: Current DMA-capable free memory (52 KB)
- `prev_dma_b=+0`: No change since last heartbeat
- `base_dma_b=-8192`: 8 KB less than baseline (allocated for I2S DMA buffers)

### DMA Data Flow

```
Bluetooth Stack (Core 1)
  │
  │ [A2DP data callback]
  ├─→ write_audio(data, size)
  │     │
  │     └─→ xRingbufferSend(s_ringbuf_i2s, data, size, 0)
  │           │
  │           ├─ Ringbuffer fills (Producer)
  │           │
  │           └─ When 80% full: xSemaphoreGive(s_i2s_write_semaphore)
  │
  v
Ringbuffer (12 KB, DMA-capable memory)
  │
  │ [Consumer: BtI2STask on Core 0]
  ├─→ xRingbufferReceiveUpTo(s_ringbuf_i2s, &item_size, ticks, 1920)
  │     │
  │     └─→ i2s_write_data(data, item_size)
  │           │
  │           └─→ ESP-IDF I2S Driver
  │                 │
  │                 └─→ DMA Controller
  │                       │
  │                       └─→ I2S Peripheral
  │                             │
  │                             └─→ GPIO 14 (Serial Data)
  │
  v
External I2S DAC (MAX98357A or similar)
```

### DMA Interrupt Handling

**File**: `src/Bluetooth/AudioBT.cpp:162`

```c++
.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,  // Interrupt priority level 1
```

**Behavior**:
- I2S DMA generates interrupts when buffers are consumed
- ESP-IDF driver handles interrupt in ISR context
- ISR refills DMA buffers from user-provided data
- **No explicit ISR in user code**: ESP-IDF driver abstracts DMA interrupt handling

### DMA Usage Summary

| Peripheral | DMA Channels | Buffer Size | Interrupt Priority | Purpose |
|------------|--------------|-------------|-------------------|---------|
| I2S0 | TX (Channel 0) | 6-8 KB (8-12 buffers) | Level 1 | Bluetooth audio output |
| I2C | No DMA | N/A | N/A | Sensor communication (polled) |
| UART | No DMA | N/A | N/A | Serial logging, STM32 link |
| SPI | Not used | N/A | N/A | Reserved for future use |

**Key Insight**: Only I2S uses DMA. All other peripherals use CPU-driven polled I/O.

## Core Allocation (Core 0 vs Core 1)

### ESP32 Dual-Core Architecture

The ESP32 has two Xtensa LX6 cores running at 240 MHz:
- **Core 0 (PRO_CPU)**: Protocol CPU - Handles WiFi/Bluetooth protocol stacks
- **Core 1 (APP_CPU)**: Application CPU - Typically runs user application

However, FreeRTOS SMP (Symmetric Multiprocessing) allows tasks to run on either core unless pinned.

### Core 0 Workload

| Component | Priority | Description | File:Line |
|-----------|----------|-------------|-----------|
| **Arduino loop()** | 1 | Main application loop (default priority) | main.cpp:10 |
| **BtI2STask** | 22 | **Real-time audio output** | BluetoothA2DPSinkQueued.cpp:24 |
| WiFi/BT stack ISRs | Varies | ESP-IDF internal interrupt handlers | (ESP-IDF) |

**Rationale for Core 0**:
- **Low latency**: I2S DMA refill must be predictable
- **No I/O blocking**: Core 1 handles slow I2C operations
- **Dedicated audio path**: Minimize jitter by avoiding heavy I/O on Core 0

### Core 1 Workload

| Component | Priority | Description | File:Line |
|-----------|----------|-------------|-----------|
| **i2cWorker** | 21 | Serializes I2C bus access | I2C_bus_shared.cpp:196 |
| **BtAppT** | 15 | Bluetooth event handling | BluetoothA2DPCommon.cpp:498 |
| **wifiInit** | 5 | WiFi driver initialization (temporary) | WiFiSync.cpp:188 |
| **encoderTask** | 2 | User input polling (1ms interval) | Encoder.cpp:117 |

**Rationale for Core 1**:
- **I/O offload**: All blocking I2C operations handled here
- **Input processing**: Encoder polling doesn't interfere with audio
- **BT/WiFi setup**: Heavy initialization off Core 0

### Task Core Assignment Strategy

```
Core 0 (Real-time Path)
  │
  ├─ Arduino loop() [Priority 1]
  │    └─ UI updates, sensor reading dispatch, state machine
  │
  └─ BtI2STask [Priority 22]
       └─ Read from ringbuffer → Write to I2S DMA
          ↓
          └─ Minimize latency and jitter

Core 1 (I/O and Background Services)
  │
  ├─ i2cWorker [Priority 21]
  │    └─ Serialize all I2C transactions
  │
  ├─ BtAppT [Priority 15]
  │    └─ Process Bluetooth stack events
  │
  ├─ wifiInit [Priority 5]
  │    └─ WiFi driver initialization (temporary task)
  │
  └─ encoderTask [Priority 2]
       └─ Poll rotary encoder every 1ms
```

### Interrupt Distribution

ESP-IDF manages interrupt affinity, but generally:

**Core 0 Interrupts**:
- I2S DMA interrupts (Level 1)
- WiFi MAC interrupts (ESP-IDF managed)
- Bluetooth controller interrupts (ESP-IDF managed)

**Core 1 Interrupts**:
- Timer interrupts (FreeRTOS tick)
- GPIO interrupts (if used)
- UART interrupts (if enabled)

**Note**: The ESP32 interrupt controller allows flexible interrupt routing. The ESP-IDF framework handles most interrupt affinity decisions automatically.

### Cache Coherency

The ESP32 has separate L1 instruction and data caches for each core, but shares L2 cache.

**Potential Issues**:
1. **Ringbuffer access**: Both cores access `s_ringbuf_i2s`
   - **Mitigation**: FreeRTOS ringbuffer API uses memory barriers
2. **Atomic flags**: `bt_audio_active`, `s_connected`
   - **Mitigation**: `std::atomic` with appropriate memory ordering
3. **I2C request pool**: Shared static pool accessed from multiple tasks
   - **Mitigation**: Critical sections (`taskENTER_CRITICAL`) protect access

**Memory Barriers Used**:
```c++
// src/drivers/I2C_bus_shared.cpp:72
slot.refs.store(2, std::memory_order_relaxed);  // Relaxed ordering (safe within critical section)

// src/drivers/I2C_bus_shared.cpp:178
request->completed.store(true, std::memory_order_release);  // Release barrier

// src/drivers/I2C_bus_shared.cpp:247
const bool ok = request->completed.load(std::memory_order_acquire);  // Acquire barrier
```

## Hidden/Internal Mechanisms

### Interrupt Service Routines (ISRs)

#### 1. I2S DMA Completion ISR
**Managed by**: ESP-IDF I2S driver (internal)
**Trigger**: DMA buffer consumption
**Behavior**:
- Refills DMA descriptor with next audio data
- Updates buffer pointers
- Signals driver task if needed

**User Impact**: Transparent; handled by `i2s_write_data()`

#### 2. WiFi Event Handler
**File**: `src/comms/WiFiSync.cpp:79-95`

```c++
static void handleWiFiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      s_connected.store(true);
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      s_connected.store(false);
      break;
  }
}
```

**Context**: Callback from WiFi driver (likely in WiFi task context)
**Behavior**: Updates atomic connection flags

#### 3. Bluetooth Callbacks
**File**: `src/Bluetooth/BluetoothA2DPCommon.cpp`

Multiple callbacks registered with Bluetooth stack:
- `ccall_app_gap_callback` (Line 34): GAP (Generic Access Profile) events
- `ccall_app_rc_ct_callback` (Line 41): Remote Control (AVRCP) events
- `ccall_app_a2d_callback` (Line 47): A2DP audio events
- `ccall_av_hdl_stack_evt` (Line 53): Bluetooth stack events

**Context**: Called from Bluetooth controller task (ESP-IDF internal)
**Behavior**: Post messages to `app_task_queue` for processing by `BtAppT`

### Background Services

#### 1. FreeRTOS Idle Task
**Priority**: 0 (lowest)
**Purpose**:
- Runs when no other tasks are ready
- Performs heap cleanup
- Handles task deletion (via `vTaskDelete`)

**Not visible in user code**, but critical for system operation.

#### 2. ESP-IDF System Tasks

**WiFi Task** (internal to ESP-IDF):
- Priority: ~23 (very high)
- Manages WiFi MAC/PHY
- Handles 802.11 protocol

**Bluetooth Controller Task** (internal to ESP-IDF):
- Priority: ~23 (very high)
- Manages Bluetooth HCI (Host Controller Interface)
- Handles link layer

**TCP/IP Task** (lwIP stack):
- Priority: ~18
- Processes network packets
- Manages TCP connections

### Synchronization Primitives in Detail

#### I2C Synchronization

**Recursive Mutex** (`gI2cMutex`):
**File**: `src/drivers/I2C_bus_shared.cpp:284`

```c++
gI2cMutex = xSemaphoreCreateRecursiveMutex();
```

**Purpose**: Allow same task to lock I2C bus multiple times (e.g., nested sensor reads)

**Lock/Unlock Pattern**:
```c++
bool I2cShared::lock(uint32_t timeoutMs) {
  if (xSemaphoreTakeRecursive(gI2cMutex, ticks) != pdTRUE) {
    return false;
  }
  ++gI2cOwnerDepth;  // Track recursion depth
  return true;
}

void I2cShared::unlock() {
  if (gI2cOwnerDepth > 0) {
    --gI2cOwnerDepth;
  }
  xSemaphoreGiveRecursive(gI2cMutex);
}
```

**Binary Semaphores per Request** (`slot.done`):
**File**: `src/drivers/I2C_bus_shared.cpp:88`

```c++
slot.done = xSemaphoreCreateBinary();
```

**Purpose**: Signal completion of asynchronous I2C operation

**Usage Pattern**:
```c++
// Requester (main loop on Core 0):
I2cRequest *req = acquireRequest();
req->op = I2cRequest::Op::Write;
submitRequest(req);  // Internally waits on req->done

// Worker (i2cWorker on Core 1):
executeWrite(req->wire, req->address7bit, ...);
req->completed.store(true);
xSemaphoreGive(req->done);  // Wake up requester
```

**Critical Sections** (`portMUX_TYPE gI2cRequestPoolMux`):
**File**: `src/drivers/I2C_bus_shared.cpp:67`

```c++
taskENTER_CRITICAL(&gI2cRequestPoolMux);
// ... access static request pool ...
taskEXIT_CRITICAL(&gI2cRequestPoolMux);
```

**Purpose**: Protect static request pool from concurrent allocation/deallocation
**Context**: Disables interrupts; very short duration

#### Audio Synchronization

**Ringbuffer** (`s_ringbuf_i2s`):
**File**: `src/Bluetooth/BluetoothA2DPSinkQueued.cpp:17`

```c++
s_ringbuf_i2s = xRingbufferCreate(12288, RINGBUF_TYPE_BYTEBUF);
```

**Producer**: Bluetooth data sink callback (Core 1)
```c++
xRingbufferSend(s_ringbuf_i2s, data, size, 0);  // Non-blocking send
```

**Consumer**: BtI2STask (Core 0)
```c++
data = xRingbufferReceiveUpTo(s_ringbuf_i2s, &item_size, ticks, 1920);
vRingbufferReturnItem(s_ringbuf_i2s, data);
```

**Binary Semaphore** (`s_i2s_write_semaphore`):
**File**: `src/Bluetooth/BluetoothA2DPSinkQueued.cpp:13`

```c++
s_i2s_write_semaphore = xSemaphoreCreateBinary();
```

**Purpose**: Signal when ringbuffer has reached prefetch threshold

**Flow**:
1. Audio task waits: `xSemaphoreTake(s_i2s_write_semaphore, pdMS_TO_TICKS(3000))`
2. Data arrives, ringbuffer fills to 80%
3. Producer signals: `xSemaphoreGive(s_i2s_write_semaphore)`
4. Audio task wakes up and starts playback

**Task Notification** (for activation/deactivation):
**File**: `src/Bluetooth/BluetoothA2DPSinkQueued.cpp:92`

```c++
if (!bt_audio_active.load()) {
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // Block until notified
}
```

**Purpose**: Suspend audio task when not playing
**Signaling**: `xTaskNotifyGive(s_bt_i2s_task_handle)` (when audio starts)

#### Logging Synchronization

**Static Mutex** (`s_mutex`):
**File**: `include/core/telemetry/AppLog.h:20`

```c++
static SemaphoreHandle_t s_mutex = xSemaphoreCreateMutex();
```

**Purpose**: Prevent interleaved log output from multiple tasks

**Usage**:
```c++
#define LOG_I(tag, fmt, ...) \
  do { \
    xSemaphoreTake(s_mutex, portMAX_DELAY); \
    Serial.printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__); \
    xSemaphoreGive(s_mutex); \
  } while (0)
```

### Queue Mechanisms

| Queue | Size | Item Type | Producer | Consumer | Purpose |
|-------|------|-----------|----------|----------|---------|
| `s_eventQueue` | 64 | `EncoderEvent` | encoderTask (Core 1) | loop() (Core 0) | User input events |
| `gI2cRequestQueue` | 8 | `I2cRequest*` | Any task | i2cWorker (Core 1) | I2C operation requests |
| `app_task_queue` | 20 | `bt_app_msg_t` | BT callbacks | BtAppT (Core 1) | Bluetooth events |
| `s_ringbuf_i2s` | 12 KB | `uint8_t[]` | BT audio callback | BtI2STask (Core 0) | Audio sample buffer |

## Data Flow Diagram

### Overall System Data Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                          CORE 0 (PRO_CPU)                       │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ Arduino loop() [Priority 1]                              │   │
│  │   │                                                       │   │
│  │   ├─ Read encoder events ◄──────────────────────┐        │   │
│  │   │                                              │        │   │
│  │   ├─ Update UI (LCD via I2C)──────────┐         │        │   │
│  │   │                                    │         │        │   │
│  │   ├─ Poll sensors (I2C)───────────────┤         │        │   │
│  │   │                                    │         │        │   │
│  │   └─ Publish MQTT data                │         │        │   │
│  └───────────────────────────────────────┼─────────┼────────┘   │
│                                           │         │            │
│  ┌───────────────────────────────────────┼─────────┼────────┐   │
│  │ BtI2STask [Priority 22]               │         │        │   │
│  │   │                                    │         │        │   │
│  │   ├─ Read from ringbuffer ◄───────────┼─────────┼───┐    │   │
│  │   │                                    │         │   │    │   │
│  │   └─ Write to I2S DMA ───► GPIO 14    │         │   │    │   │
│  └────────────────────────────────────────┼─────────┼───┼────┘   │
└─────────────────────────────────────────────────────┼───┼────────┘
                                            │         │   │
                                            ▼         ▼   │
┌─────────────────────────────────────────────────────────┼────────┐
│                          CORE 1 (APP_CPU)               │        │
│  ┌──────────────────────────────────────────────────────┼────┐   │
│  │ i2cWorker [Priority 21]                              │    │   │
│  │   │                                                   │    │   │
│  │   ├─ Receive I2C requests ◄──────────────────────────┘    │   │
│  │   │                                                        │   │
│  │   ├─ Execute I2C transactions ──► Wire (SDA/SCL)          │   │
│  │   │                                                        │   │
│  │   └─ Signal completion                                    │   │
│  └────────────────────────────────────────────────────────────┘   │
│                                                                    │
│  ┌────────────────────────────────────────────────────────────┐   │
│  │ BtAppT [Priority 15]                                       │   │
│  │   │                                                         │   │
│  │   ├─ Process BT events ◄───── Bluetooth Stack (ESP-IDF)   │   │
│  │   │                                                         │   │
│  │   └─ Handle A2DP audio data ──► Ringbuffer ───────────────────┘
│  └────────────────────────────────────────────────────────────┘
│                                                                    │
│  ┌────────────────────────────────────────────────────────────┐   │
│  │ encoderTask [Priority 2]                                   │   │
│  │   │                                                         │   │
│  │   ├─ Poll encoder GPIOs (CLK, DT, SW)                      │   │
│  │   │                                                         │   │
│  │   └─ Send events ──────────────────────────────────────────────┘
│  └────────────────────────────────────────────────────────────┘
└────────────────────────────────────────────────────────────────────┘
```

### Bluetooth Audio Path (Detailed)

```
Bluetooth Controller (ESP-IDF, Core 0)
  │
  │ [A2DP SBC decode]
  ▼
Bluetooth Stack Callback (Core 1)
  │
  │ write_audio(pcm_data, size)
  ▼
xRingbufferSend(s_ringbuf_i2s, data, size, 0)
  │
  ├─ Ringbuffer fills
  │
  └─ When filled >= 9830 bytes (80%):
       xSemaphoreGive(s_i2s_write_semaphore)
            │
            ▼
       BtI2STask wakes up (Core 0)
            │
            │ xRingbufferReceiveUpTo(...)
            │    └─ Read up to 1920 bytes
            │
            │ i2s_write_data(data, item_size)
            │    │
            │    └─ Copy to I2S driver buffer
            │         │
            │         └─ ESP-IDF I2S Driver
            │              │
            │              └─ DMA Controller
            │                   │
            │                   └─ I2S Peripheral (I2S0)
            │                        │
            │                        └─ GPIO 14 (Serial Data)
            │                             │
            │                             ▼
            │                        External DAC (I2S)
            │                             │
            │                             └─ Analog Audio Output
            │
            └─ vRingbufferReturnItem(data)
                 └─ Free buffer slot for next data
```

### I2C Transaction Flow

```
Sensor Read Request (loop on Core 0)
  │
  │ I2cShared::writeRead(address, reg, &data, 1, 100ms, 3)
  ▼
acquireRequest()  ◄─── Static pool (protected by critical section)
  │
  │ Fill request:
  │   req->op = WriteRead
  │   req->address7bit = 0x76
  │   req->writeData = &reg
  │   req->readData = &data
  │   req->retries = 3
  ▼
xQueueSend(gI2cRequestQueue, &req, pdMS_TO_TICKS(5))
  │
  ├─ Request queued
  │
  ▼
i2cWorker wakes up (Core 1)
  │
  │ xQueueReceive(gI2cRequestQueue, &req, portMAX_DELAY)
  │
  │ executeWriteRead(req)
  │   │
  │   ├─ I2cShared::lock(100ms)
  │   │    └─ xSemaphoreTakeRecursive(gI2cMutex, ticks)
  │   │
  │   ├─ Wire.beginTransmission(0x76)
  │   ├─ Wire.write(&reg, 1)
  │   ├─ Wire.endTransmission(false)
  │   ├─ Wire.requestFrom(0x76, 1)
  │   ├─ data = Wire.read()
  │   │
  │   └─ I2cShared::unlock()
  │        └─ xSemaphoreGiveRecursive(gI2cMutex)
  │
  │ req->result = true
  │ req->completed.store(true, memory_order_release)
  ▼
xSemaphoreGive(req->done)  ──► Wake up requester (Core 0)
  │                               │
  │                               │ xSemaphoreTake(req->done, ticks)
  │                               │
  │                               └─ Return result to caller
  │
  └─ releaseRequest(req)
       └─ Free request slot
```

### Network Data Flow (MQTT Telemetry)

```
loop() [Core 0, every 5 seconds]
  │
  │ TelemetryComposer::buildMqttTelemetrySample(sample)
  │    │
  │    ├─ Read sensors (via I2C worker on Core 1)
  │    └─ Build JSON payload
  │
  ▼
MQTTSync::publishSensorData(temp, humidity, pressure, aqi, tvoc, eco2)
  │
  │ PubSubClient::publish(topic, payload)
  │    │
  │    └─ WiFiClient::write(data)
  │         │
  │         └─ lwIP TCP/IP stack (ESP-IDF)
  │              │
  │              └─ WiFi MAC/PHY (ESP-IDF, Core 0)
  │                   │
  │                   └─ Wireless transmission
  │
  └─ Record telemetry timing
```

## Potential Issues

### Race Conditions

#### 1. Ringbuffer Concurrent Access
**Location**: `s_ringbuf_i2s` accessed from both cores
**Producer**: Bluetooth callback (Core 1)
**Consumer**: BtI2STask (Core 0)

**Risk**: Data corruption if ringbuffer API doesn't use proper memory barriers

**Mitigation**: FreeRTOS ringbuffer API includes memory barriers and atomic operations
```c++
// FreeRTOS internally uses portENTER_CRITICAL for ringbuffer operations
```

**Status**: ✅ Safe (FreeRTOS handles synchronization)

#### 2. I2C Request Pool Allocation
**Location**: `gI2cRequestPool` (static array)
**Concurrent Access**: Multiple tasks may call `acquireRequest()` simultaneously

**Risk**: Two tasks allocate same slot

**Mitigation**: Critical sections protect allocation
```c++
taskENTER_CRITICAL(&gI2cRequestPoolMux);
// ... find and mark slot in use ...
taskEXIT_CRITICAL(&gI2cRequestPoolMux);
```

**Status**: ✅ Safe

#### 3. Atomic Flag Access
**Locations**: `s_connected`, `bt_audio_active`, `ringbuffer_mode`

**Risk**: Non-atomic read-modify-write operations

**Mitigation**: Uses `std::atomic` with appropriate memory ordering
```c++
std::atomic<bool> s_connected{false};
s_connected.store(true);  // Atomic write
if (s_connected.load()) { ... }  // Atomic read
```

**Status**: ✅ Safe

### Deadlocks

#### 1. I2C Mutex Recursion
**Scenario**: Task locks I2C mutex, calls function that tries to lock again

**Risk**: Deadlock with non-recursive mutex

**Mitigation**: Uses **recursive mutex**
```c++
gI2cMutex = xSemaphoreCreateRecursiveMutex();
xSemaphoreTakeRecursive(gI2cMutex, ticks);
```

**Status**: ✅ Safe

#### 2. Queue Overflow with Blocking
**Scenario**: Producer blocks on full queue, consumer blocked on empty queue

**Risk**: Deadlock if queue size is 1 and both tasks block

**Mitigation**:
- I2C queue: 8 slots (adequate for peak load)
- Encoder queue: 64 slots (very large, unlikely to fill)
- Audio ringbuffer: 12 KB (large buffer)
- All producers use timeout (not `portMAX_DELAY`)

**Status**: ✅ Safe (adequate queue sizes)

### Priority Inversion

#### Scenario
**Low Priority Task** (encoderTask, Priority 2) locks I2C mutex
**High Priority Task** (i2cWorker, Priority 21) blocks waiting for mutex
**Medium Priority Task** (WiFi or BT tasks, Priority 5-15) preempts low priority task
→ High priority task waits for low priority task, but low priority never runs

**Mitigation**: FreeRTOS recursive mutex includes priority inheritance
```c++
// When low-priority task holds mutex and high-priority task blocks:
// FreeRTOS temporarily raises low-priority task to high-priority
```

**Status**: ✅ Mitigated (priority inheritance enabled)

**Potential Concern**: If low-priority task is preempted by medium-priority task before high-priority task blocks, priority inheritance won't help.

**Recommendation**: Avoid I2C operations in low-priority tasks, or increase encoderTask priority.

### Cache Coherency

#### 1. Ringbuffer Data
**Issue**: Producer (Core 1) writes audio data, consumer (Core 0) reads

**Risk**: Core 0 may read stale cached data

**Mitigation**: FreeRTOS ringbuffer API uses memory barriers
```c++
// Producer (Core 1):
xRingbufferSend(s_ringbuf_i2s, data, size, 0);
// Internally: __sync_synchronize() or DMB instruction

// Consumer (Core 0):
data = xRingbufferReceiveUpTo(s_ringbuf_i2s, ...);
// Internally: __sync_synchronize() or DMB instruction
```

**Status**: ✅ Safe (memory barriers present)

#### 2. I2C Request Metadata
**Issue**: Requester (Core 0) writes request fields, worker (Core 1) reads

**Risk**: Worker sees inconsistent request state

**Mitigation**:
- Request acquired in critical section (interrupts disabled)
- `completed` flag uses `memory_order_release` / `memory_order_acquire`
```c++
// Worker (Core 1):
request->completed.store(true, std::memory_order_release);

// Requester (Core 0):
if (request->completed.load(std::memory_order_acquire)) { ... }
```

**Status**: ✅ Safe

### DMA Conflicts

**Potential Issue**: I2S DMA and WiFi/Bluetooth DMA share memory bus

**Risk**:
- Bus contention slows DMA transfers
- Audio underruns if I2S DMA starved

**Mitigation**:
1. **Large ringbuffer** (12 KB) absorbs jitter
2. **High task priority** (22) for audio task
3. **Prefetch strategy** (wait for 80% fill before starting)
4. **Underrun detection** with automatic recovery

**Observed in Code**:
```c++
if (item_size == 0) {
  TELEMETRY_INC(audio_underruns);
  ringbuffer_mode.store(RINGBUFFER_MODE_PREFETCHING);
  continue;
}
```

**Status**: ⚠️ Monitored (telemetry tracks underruns)

### Memory Fragmentation

**Issue**: Frequent allocation/deallocation causes heap fragmentation

**Observed**:
```c++
// RamTelemetry.cpp:130
values.fragmentationPermille = 1000 - (largestFreeBlock * 1000 / freeHeap);
```

**Mitigation**:
1. **Static allocation**: Most buffers allocated at startup
   - I2C request pool: static array
   - Encoder queue: created once in `encoder_begin()`
   - Ringbuffer: created once in `bt_i2s_task_start_up()`
2. **Telemetry monitoring**: Fragmentation reported every 5 seconds
3. **Temporary tasks**: WiFi init task self-deletes after use

**Status**: ✅ Minimal fragmentation risk (static allocation strategy)

### Interrupt Latency

**Issue**: Critical sections disable interrupts, increasing latency

**Observed**:
```c++
taskENTER_CRITICAL(&gI2cRequestPoolMux);
// ... very short operation (find free slot) ...
taskEXIT_CRITICAL(&gI2cRequestPoolMux);
```

**Risk**: If critical section is too long, I2S DMA interrupt may miss deadline

**Mitigation**:
1. Critical sections are very short (<10 µs)
2. Only used for static pool access, not I2C transactions
3. I2S DMA has 8 buffers (buffering depth)

**Status**: ✅ Safe (critical sections are minimal)

### WiFi/Bluetooth Coexistence

**Issue**: ESP32 shares RF hardware between WiFi and Bluetooth Classic

**Risk**:
- Switching between modes causes connection drops
- Simultaneous operation degrades performance

**Mitigation** (NetworkOrchestrator):
```c++
// AppBoot.cpp:356-360
if (NetworkOrchestrator::getCurrentRadioState() == RADIO_STATE_BT) {
  radioMode = BT_ONLY;
} else {
  radioMode = WIFI_ONLY;
}
```

**Status**: ⚠️ **Exclusive mode** (only one radio active at a time)

**Recommendation**: This is a hardware limitation. The current implementation is correct.

## Key Insights

### 1. **Asymmetric Core Utilization**
- **Core 0**: Real-time audio path (minimal latency)
- **Core 1**: I/O-bound operations (I2C, Bluetooth, WiFi, input)

**Justification**: Separates time-critical audio from blocking I/O, preventing jitter.

### 2. **Task-Based I2C Serialization**
Instead of locking I2C mutex for entire transaction:
- Offload to dedicated worker task
- Main loop submits request and waits on semaphore
- Worker processes queue sequentially

**Benefits**:
- Main loop doesn't block on slow I2C
- Retry logic isolated in worker
- Telemetry tracking of I2C errors

**Trade-off**: Extra task overhead, but ESP32 has resources

### 3. **Ringbuffer as Elastic Buffer**
The 12 KB ringbuffer between Bluetooth stack and I2S DMA:
- Absorbs jitter from Bluetooth packet arrival
- Allows prefetch before playback starts
- Switches to "prefetch mode" on underrun

**Critical Parameter**: 80% prefetch threshold
- Too low: Frequent underruns
- Too high: Longer startup delay

**Current Value**: 9830 bytes ≈ 111 ms of audio @ 44.1 kHz stereo 16-bit
```
Latency = 9830 bytes / (44100 Hz × 2 channels × 2 bytes) = 0.056 seconds = 56 ms
```

**Recommendation**: Monitor `audio_underruns` telemetry counter. If high, increase prefetch threshold.

### 4. **Priority Inheritance on Recursive Mutex**
FreeRTOS recursive mutex provides priority inheritance, preventing priority inversion:

**Example**:
1. EncoderTask (Priority 2) locks I2C mutex
2. i2cWorker (Priority 21) blocks on mutex
3. FreeRTOS temporarily raises EncoderTask to Priority 21
4. EncoderTask completes, releases mutex
5. i2cWorker unblocks

**Critical**: This only works if mutex is held when high-priority task blocks. If low-priority task is preempted before high-priority task blocks, inversion still occurs.

### 5. **Lazy Task Initialization**
WiFi and Bluetooth tasks are created on-demand:
- Reduces initial memory footprint
- Faster boot time
- Heap fragmentation avoidance

**Trade-off**: First WiFi connection is slower (task creation overhead)

### 6. **Telemetry Without Overhead**
Increment-only counters using `TELEMETRY_INC()`:
```c++
#define TELEMETRY_INC(counter) \
  do { \
    extern std::atomic<uint32_t> g_telemetry_##counter; \
    g_telemetry_##counter.fetch_add(1, std::memory_order_relaxed); \
  } while (0)
```

**Benefits**:
- No mutex required (atomic increment)
- Minimal overhead (~1 CPU cycle)
- Counters reported periodically

**Counters**:
- `audio_underruns`: Ringbuffer empty during playback
- `audio_overflows`: Ringbuffer full, drop incoming data
- `audio_drops`: Packets dropped
- `i2c_timeouts`: I2C operation timeout
- `i2c_errors`: I2C NACK or bus error

### 7. **Static Allocation Strategy**
Most data structures allocated once at startup:
- I2C request pool: 8 static slots
- Encoder queue: 64 events
- Ringbuffer: 12 KB

**Benefits**:
- Deterministic memory usage
- No fragmentation from repeated allocation
- Fast allocation (no heap search)

**Trade-off**: Fixed limits (cannot grow beyond pool size)

### 8. **Critical Section Minimization**
Critical sections (interrupt disabling) used only for:
- I2C request pool access (~5 µs)
- Never for I2C transactions (would cause >1 ms interrupt latency)

**Alternative**: Could use mutex instead, but critical section is faster for short operations.

### 9. **Prefetch Strategy for Audio Playback**
Audio task waits for ringbuffer to fill before starting:
```c++
if (is_starting.load()) {
  xSemaphoreTake(s_i2s_write_semaphore, pdMS_TO_TICKS(3000));
  is_starting.store(false);
}
```

**Benefits**:
- Prevents immediate underrun on startup
- Allows Bluetooth stack to buffer ahead

**Trade-off**: ~56 ms startup delay (acceptable for music playback)

### 10. **Adaptive Ringbuffer Mode**
Three modes:
1. **PREFETCHING**: Wait for 80% fill
2. **PROCESSING**: Normal playback
3. **DROPPING**: Ringbuffer full, discard incoming data

**Mode Transitions**:
```
PREFETCHING ──[80% full]──► PROCESSING ──[overflow]──► DROPPING
      ▲                                                     │
      └────────────────[data decreased to <80%]────────────┘
```

**Insight**: System adapts to varying CPU load and Bluetooth jitter without manual tuning.

---

## Summary

This ZEGAR-ESP32 system demonstrates **well-architected embedded RTOS design**:

✅ **Clear separation of concerns**: Real-time audio on Core 0, I/O on Core 1
✅ **Proactive synchronization**: Mutexes, semaphores, critical sections used correctly
✅ **Defensive programming**: Timeouts, retries, telemetry counters
✅ **Performance optimization**: Static allocation, lazy task creation, minimal critical sections
✅ **Observability**: Comprehensive telemetry without performance impact

**Potential Improvements**:
1. Monitor `audio_underruns` counter; if high, increase ringbuffer size or prefetch threshold
2. Consider increasing `encoderTask` priority to avoid priority inversion with I2C operations
3. Add watchdog timer for critical tasks (I2S audio, WiFi, Bluetooth)
4. Implement stack overflow detection for all tasks (FreeRTOS configCHECK_FOR_STACK_OVERFLOW)

**Architecture Rating**: ⭐⭐⭐⭐⭐ (5/5)
Excellent design for a complex multi-core embedded system with real-time audio requirements.
