# Heap Stability Report – Executive Summary

This review of the `ZEGAR-ESP32` codebase focused on dynamic memory, FreeRTOS task/queue management, DMA heap usage, and fragmentation risk.

Summary:
- Stability posture: generally good, with mostly static or stack-based allocations in the main app flow.
- Highest risk: Bluetooth A2DP internal event message allocation pattern can leak if queue send fails; fixed in code on 2026-04-08.
- Fragmentation risk: moderate in Bluetooth due to repeated `malloc`/`free` and audio metadata allocation.
- Long-term verdict: the system can likely run for days if Bluetooth event queues remain healthy; if Bluetooth bursts cause queue failures, memory may degrade over time.

## Heap Behavior Analysis

The project already contains heap telemetry through `src/core/telemetry/RamTelemetry.cpp`:
- `ESP.getFreeHeap()`
- `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)`
- `heap_caps_get_free_size(MALLOC_CAP_DMA)`
- computed `fragmentationPermille`
- task watermarks for `mqtt`, `wifiInit`, `btApp`, `btI2S`, `encoder`, `i2cWorker`

This is a strong indicator that the authors are tracking heap stability. The code records both checkpoint deltas and heartbeat snapshots, so any long-term drift should be visible in runtime logs.

### Inferred heap behavior

- `freeHeap` is sampled on demand and compared to baseline/previous snapshot.
- `largestFreeBlock` and `fragmentationPermille` are derived properly, which means fragmentation should be detected early.
- `dmaFree` is tracked, but only from `MALLOC_CAP_DMA`, not specific I2S allocations.

## Memory Leak Findings

### [FIXED] Bluetooth event queue allocation on send failure
- Location: `src/bluetooth/BluetoothA2DPSink.cpp:393`
- Severity: CRITICAL LEAK, fixed in code
- Leak Type: missing free on failed queue enqueue
- Why it leaks:
  - `BluetoothA2DPSink::app_work_dispatch()` allocates `msg.param = malloc(param_len)`.
  - If `app_send_msg(&msg)` fails, the function now frees `msg.param` before returning `false`.
- Trigger frequency:
  - During Bluetooth control/event bursts when `app_task_queue` is full or `xQueueSend` times out.
- Estimated impact over time:
  - Potentially severe if Bluetooth is active and queue saturation occurs repeatedly; a few dozen failures could consume kilobytes quickly.
- Fix recommendation:
  - Already applied in code.

```cpp
if ((msg.param = malloc(param_len)) != nullptr) {
  memcpy(msg.param, p_params, param_len);
  if (!app_send_msg(&msg)) {
    free(msg.param);
    return false;
  }
  return true;
}
```

### [FIXED] Bluetooth Source event queue allocation on send failure
- Location: `src/bluetooth/BluetoothA2DPSource.cpp:235`
- Severity: CRITICAL LEAK, fixed in code
- Leak Type: missing cleanup on enqueue failure
- Why it leaks:
  - Same pattern as above in `BluetoothA2DPSource::app_work_dispatch()`.
  - Allocates `msg.param` and now frees it if the queue send fails.
- Trigger frequency:
  - When source-mode Bluetooth event queue is full or unavailable.
- Fix recommendation:
  - Already applied in code.

### [FIXED / HARDENED] AVRCP metadata allocation
- Location: `src/bluetooth/BluetoothA2DPSink.cpp:405`
- Severity: FRAGMENTATION RISK, hardened
- Leak Type: repeated dynamic metadata buffer allocation with explicit failure handling
- Why it is risky:
  - `BluetoothA2DPSink::app_alloc_meta_buffer()` allocates `attr_text` with `malloc()`.
  - The allocated buffer is freed later in `BluetoothA2DPCommon::app_task_handler()` via `free(msg.param)`.
  - If message dispatch fails, the buffer is now freed on the failure path, and allocation failure is handled before dispatch.
- Trigger frequency:
  - Every AVRCP metadata event, which may happen frequently during track changes and remote control events.
- Estimated impact over time:
  - Moderate: repeated allocation/free churn can fragment heap.
- Fix recommendation:
  - Already applied in code; the remaining risk is heap churn from the event volume itself.

### [SAFE STATIC ALLOCATION] MQTT and WiFi static buffers
- Location: `src/comms/MQTTSync.cpp`, `src/comms/WiFiSync.cpp`
- Severity: INFO
- Why it is safe:
  - `s_pendingPayload` is a fixed-size stack/ static buffer.
  - `ssidCopy`, `passCopy`, `ntpServerCopy` are fixed-size static arrays.
  - `NetworkClientSecure` and `PubSubClient` are static objects with no manual heap use in this module.
- Recommendation:
  - Keep this pattern; it is stable and avoids fragmentation.

### [SAFE STATIC ALLOCATION] JSON serialization on stack
- Location: `src/comms/MQTTSync.cpp: publishSensorData`
- Severity: INFO
- Why it is safe:
  - `StaticJsonDocument<MQTT_JSON_DOC_CAPACITY>` is stack-allocated and bounded.
  - Output is serialized into a fixed-size local buffer.
- Recommendation:
  - Maintain current buffer size ceilings and avoid larger dynamic JSON payloads.

### [LOW RISK] Arduino `String` usage
- Locations:
  - `src/core/app/AppBoot.cpp` (WiFi/MQTT/NTP config strings)
  - `src/audio/AlarmMelodyPrefs.cpp` (Preferences load/save)
- Severity: FRAGMENTATION RISK
- Why:
  - `String` can fragment heap, but usage is limited and mostly occurs in init or rare preference operations.
- Recommendation:
  - For long-term stability, replace `String` with fixed-size char buffers or `std::string` only if necessary.
  - Keep `Preferences` string use limited to startup or occasional save/load.

## Fragmentation Risks

### Bluetooth dynamic event payloads
- `BluetoothA2DPCommon::app_work_dispatch()` allocates event payloads programmatically.
- Fragmentation is possible when many small allocations are created and freed from the task queue.
- Even if freed correctly, long-lived audio sessions with many metadata/callback messages can lead to heap churn.

### Audio metadata strings
- `BluetoothA2DPSink::app_alloc_meta_buffer()` allocates metadata text.
- These allocations can fragment if the event queue backs up or if callbacks are delayed.

### Arduino `String` on heap
- `AppBoot` and `AlarmMelodyPrefs` create `String` instances.
- Although limited, the use of `String` in long-running firmware is always a moderate fragmentation risk.

## DMA Memory Analysis

### DMA tracking: good coverage
- `RamTelemetry` tracks `heap_caps_get_free_size(MALLOC_CAP_DMA)`.
- `SystemResourcesService` also reports DMA-capable free memory.
- `BluetoothA2DPSinkQueued` uses `xRingbufferCreate()` for I2S queueing rather than manual DMA allocation.

### DMA allocation risk
- No direct `heap_caps_malloc(MALLOC_CAP_DMA)` usage found in `src/`.
- Audio DMA backend is delegated to the Espressif A2DP / AudioTools libraries.
- Risk is therefore external to this repository, but the project does not explicitly guard I2S buffers with DMA-capable allocations.
- Recommendation:
  - Verify the external audio backend allocates DMA-capable memory.
  - If using manual I2S buffers, prefer `heap_caps_malloc(size, MALLOC_CAP_DMA)`.

## RTOS Memory Usage

### Task stacks
- The project defines explicit stack sizes in `include/config/TaskConfig.h`.
- Stack sizes appear reasonable:
  - `BtAppTask`: 4096 bytes
  - `BtI2STask`: 3072 bytes
  - `EncoderTask`: 2048 bytes
  - `I2cWorkerTask`: 3072 bytes
  - `WifiInitTask`: 4096 bytes
- These sizes are conservative enough for Bluetooth, I2S, and WiFi driver initialization.
- `RamTelemetry` includes stack high-water marks for these tasks.

### Queue/semaphore lifetime
- `BluetoothA2DPCommon::app_task_start_up()` creates the BT app queue.
- `BluetoothA2DPCommon::app_task_shut_down()` deletes the queue and task.
- `BluetoothA2DPSinkQueued::bt_i2s_task_start_up()` creates the I2S ringbuffer and semaphore.
- `BluetoothA2DPSinkQueued::bt_i2s_task_shut_down()` deletes them cleanly.
- `WiFiSync` creates a transient `wifiInitTask` and deletes it on start.
- `src/drivers/I2C_bus_shared.cpp` creates the I2C worker queue and mutex once.

### Potential RTOS risk
- `WiFiSync::startWifiConnectionTask()` forcefully deletes a previous `wifiBeginTaskHandle` before creating a new task.
- This is likely safe but should be audited for task lifetime and possible use-after-delete if the previous task is still active.

## Memory Hotspots

- `src/bluetooth/BluetoothA2DPSink.cpp` and `src/bluetooth/BluetoothA2DPSource.cpp` are the highest-risk modules because they allocate heap for event parameters and metadata.
- `src/comms/MQTTSync.cpp` is a low-risk hotspot, thanks to fixed-size buffers and static objects.
- `src/core/app/AppBoot.cpp` is a minor hotspot for `String` usage, but it is not a runtime leak source.

## Recommendations

1. Fix Bluetooth queue allocation failure cleanup immediately.
   - Add `free(msg.param)` in both `BluetoothA2DPSink::app_work_dispatch()` and `BluetoothA2DPSource::app_work_dispatch()` when `app_send_msg()` fails.
2. Harden Bluetooth metadata allocation.
   - Prefer static or preallocated buffers in `app_alloc_meta_buffer()`.
   - Ensure any allocated metadata is freed on all error/early-exit paths.
3. Avoid `String` in long-running flows.
   - Replace `String` with fixed-size C strings for WiFi/MQTT/NTP config loading and alarms.
4. Keep the existing heap telemetry enabled in production if possible.
   - The telemetry framework is a valuable early-warning system for slow leaks and fragmentation.
5. Validate DMA buffer ownership.
   - Confirm the external audio backend uses DMA-safe memory.
   - If not, add explicit DMA-capable allocations for I2S ringbuffers.

## Long-Term Stability Verdict

The core firmware appears stable for long-term runtime, with the following caveats:
- The Bluetooth subsystem is the most likely source of heap instability.
- If Bluetooth event queue failures occur, memory can leak on repeated failures.
- The rest of the system uses mostly static or stack-managed memory and is unlikely to cause a catastrophic leak.

Final verdict: **probably stable for days/weeks** if Bluetooth is not stressed by repeated queue overflows. If Bluetooth event queue saturation happens, the leak path in `BluetoothA2DPSink.cpp` / `BluetoothA2DPSource.cpp` should be fixed before accepting multi-week uptime.
