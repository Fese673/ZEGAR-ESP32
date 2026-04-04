#pragma once

#include <Arduino.h>

namespace BoardPins {

// --- I2C (BMP280, ENS160/AHT21, RTC i EEPROM) ---
constexpr uint8_t kI2cSda = 21;
constexpr uint8_t kI2cScl = 22;
constexpr uint32_t kI2cClockHz = 400000UL;

// --- PMS5003 UART ---
constexpr uint8_t kPms5003Rx = 34;
constexpr uint8_t kPms5003Tx = 13;

// --- Wyświetlacz 7-segmentowy  ---
constexpr uint8_t kSevenSegData = 23;
constexpr uint8_t kSevenSegClock = 18;
constexpr uint8_t kSevenSegLatch = 5;

// --- Enkoder obrotowy ---
constexpr uint8_t kEncoderClk = 25;
constexpr uint8_t kEncoderDt = 26;
constexpr uint8_t kEncoderSw = 27;

// --- Buzzer ---
constexpr uint8_t kBuzzer = 19;

// --- STM32 UART (BPM/SpO2) ---
constexpr uint8_t kStm32UartRx = 16;
constexpr uint8_t kStm32UartTx = 17;

// --- Bluetooth I2S (AudioBT) ---
constexpr uint8_t kBtI2sBclk = 33;
constexpr uint8_t kBtI2sWs = 32;
constexpr uint8_t kBtI2sData = 14;

}  // namespace BoardPins
