#pragma once

#include <stddef.h>

#include <freertos/FreeRTOS.h>

namespace TaskConfig {

// Core 0 is reserved for the latency-sensitive system band.
constexpr BaseType_t CORE_SYSTEM = 0;

// Core 1 is the application band for UI, sensors, and input.
constexpr BaseType_t CORE_APP = 1;

namespace BtAppTask {

// BtAppT stays on Core 0 so the A2DP control path can preempt Core 1 work.
constexpr BaseType_t kCore = CORE_SYSTEM;

// BtAppT must stay below BtI2STask but above I2C/WiFi so state handling does not lag.
constexpr UBaseType_t kPriority = 19;

// BtAppT needs enough room for A2DP state handling; shrinking this risks stack overflow.
constexpr size_t kStackBytes = 4096;

// Event queue depth for BtAppT; reducing this turns bursts of BT events into drops.
constexpr int kEventQueueSize = 32;

}  // namespace BtAppTask

namespace BtI2STask {

// The audio writer stays on Core 0 so encoder/I2C load on Core 1 cannot starve it.
constexpr BaseType_t kCore = CORE_SYSTEM;

// Audio must be the highest user task; lowering this first shows up as underruns.
constexpr UBaseType_t kPriority = 24;

// BtI2STask owns the realtime audio bridge; smaller stacks can break the output path.
constexpr size_t kStackBytes = 3072;

}  // namespace BtI2STask

namespace EncoderTask {

// Encoder stays on Core 1 so it can outpace I2C and keep Gray-code edges fresh.
constexpr BaseType_t kCore = CORE_APP;

// Encoder must outrun I2C; lowering this first shows up as missed steps.
constexpr UBaseType_t kPriority = 20;

// Encoder polling and queue handoff need a stable stack margin; shrinking this risks overflow.
constexpr size_t kStackBytes = 2048;

}  // namespace EncoderTask

namespace I2cWorkerTask {

// I2C stays on Core 1 so it cannot interfere with the Core 0 audio band.
constexpr BaseType_t kCore = CORE_APP;

// I2C is a service task; lowering this below the encoder prevents input starvation.
constexpr UBaseType_t kPriority = 12;

// The shared I2C worker needs headroom for queue handling and retries.
constexpr size_t kStackBytes = 3072;

}  // namespace I2cWorkerTask

namespace WifiInitTask {

// WiFi init stays on Core 1 because it is a setup service, not a realtime path.
constexpr BaseType_t kCore = CORE_APP;

// WiFi init is intentionally low so it cannot delay input or I2C progress.
constexpr UBaseType_t kPriority = 5;

// WiFi init uses a larger stack because the driver bring-up path is heavy.
constexpr size_t kStackBytes = 8192;

}  // namespace WifiInitTask

static_assert(BtAppTask::kCore == CORE_SYSTEM, "BtAppTask must stay on Core 0");
static_assert(BtI2STask::kCore == CORE_SYSTEM, "BtI2STask must stay on Core 0");
static_assert(EncoderTask::kCore == CORE_APP, "EncoderTask must stay on Core 1");
static_assert(I2cWorkerTask::kCore == CORE_APP, "I2cWorkerTask must stay on Core 1");
static_assert(WifiInitTask::kCore == CORE_APP, "WifiInitTask must stay on Core 1");

static_assert(BtI2STask::kPriority > EncoderTask::kPriority,
              "Audio must stay above the encoder priority band");
static_assert(EncoderTask::kPriority > BtAppTask::kPriority,
              "Encoder must stay above the BT app task");
static_assert(BtAppTask::kPriority > I2cWorkerTask::kPriority,
              "BT app must stay above I2C worker");
static_assert(I2cWorkerTask::kPriority > WifiInitTask::kPriority,
              "I2C worker must stay above WiFi init");
static_assert(BtAppTask::kPriority < configMAX_PRIORITIES,
              "BtAppTask priority must fit the FreeRTOS priority range");
static_assert(EncoderTask::kPriority < configMAX_PRIORITIES,
              "EncoderTask priority must fit the FreeRTOS priority range");
static_assert(I2cWorkerTask::kPriority < configMAX_PRIORITIES,
              "I2cWorkerTask priority must fit the FreeRTOS priority range");
static_assert(WifiInitTask::kPriority < configMAX_PRIORITIES,
              "WifiInitTask priority must fit the FreeRTOS priority range");
static_assert(BtI2STask::kPriority < configMAX_PRIORITIES,
              "BtI2STask priority must fit the FreeRTOS priority range");

}  // namespace TaskConfig