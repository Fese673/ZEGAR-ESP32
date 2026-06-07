#pragma once

#include <cstddef>
#include <stdint.h>
namespace EsptoGuition {
namespace Config {

enum Signal : size_t {
  SIGNAL_SETTINGS,
  SIGNAL_WEATHER,
  SIGNAL_PMS,
  SIGNAL_TIME,
  SIGNAL_WIFI,
  SIGNAL_SYSTEM_RESOURCES,
  SIGNAL_COUNT
};

enum class SendPolicy : uint8_t {
  INITIAL_SYNC,
  REACTIVE,
  PERIODIC,
  REACTIVE_OR_PERIODIC
};

struct SignalConfig {
  SendPolicy policy;
  uint32_t periodicIntervalMs;
  uint8_t payloadType;
};

constexpr SignalConfig kSignals[SIGNAL_COUNT] = {
  { SendPolicy::INITIAL_SYNC,         0,    0x04 },
  { SendPolicy::REACTIVE,             0,    0x01 },
  { SendPolicy::REACTIVE,             0,    0x02 },
  { SendPolicy::REACTIVE_OR_PERIODIC, 1000, 0x03 },
  { SendPolicy::REACTIVE,             0,    0x07 },
  { SendPolicy::REACTIVE_OR_PERIODIC, 2000, 0x06 },
};

constexpr uint32_t kKeepaliveIntervalMs = 30000UL;
constexpr uint32_t kSafetyRefreshIntervalMs = 300000UL;

constexpr bool kUseSyntheticPayloads = false;
constexpr unsigned long kSyntheticUpdateIntervalMs = 3000UL;
constexpr float kSyntheticChangePercent = 5.0f;

}  // namespace Config
}  // namespace EsptoGuition