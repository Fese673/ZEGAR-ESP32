// Central configuration for temperature compensation
#pragma once

namespace TempConfig {
// Global temperature offset applied once (in degrees Celsius).
// Set to -3.0 to compensate sensors by -3.0 C.
constexpr float TEMPERATURE_OFFSET_C = -3.0f;
} // namespace TempConfig
