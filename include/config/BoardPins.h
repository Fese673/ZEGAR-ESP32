#pragma once

#include <Arduino.h>

namespace BoardPins {

constexpr uint8_t kI2cSda = 21;
constexpr uint8_t kI2cScl = 22;
constexpr uint32_t kI2cClockHz = 400000UL;

constexpr uint8_t kSevenSegData = 23;
constexpr uint8_t kSevenSegClock = 18;
constexpr uint8_t kSevenSegLatch = 5;

constexpr uint8_t kEncoderClk = 25;
constexpr uint8_t kEncoderDt = 26;
constexpr uint8_t kEncoderSw = 27;

constexpr uint8_t kBuzzer = 19;

constexpr uint8_t kStm32UartRx = 16;
constexpr uint8_t kStm32UartTx = 17;

constexpr uint8_t kBtI2sBclk = 33;
constexpr uint8_t kBtI2sWs = 32;
constexpr uint8_t kBtI2sData = 14;

}  // namespace BoardPins
