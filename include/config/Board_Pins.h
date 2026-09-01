#pragma once

#include <Arduino.h>
namespace BoardPins {

// --- I2C (BMP280, ENS160/AHT21, RTC i EEPROM) ---
constexpr uint8_t kI2cSda = 21; // SDA
constexpr uint8_t kI2cScl = 22; // SCL
constexpr uint32_t kI2cClockHz = 400000UL;

// --- PMS5003 UART ---
constexpr uint8_t kPms5003Rx = 34; // PMS_RX
constexpr uint8_t kPms5003Tx = 13; // PMS_TX

// --- Guition UART ---
constexpr uint8_t kGuitionUartRx = 35; // GUT_RX
constexpr uint8_t kGuitionUartTx = 19; // GUT_TX

// --- Wyświetlacz 7-segmentowy  ---
constexpr uint8_t kSevenSegData = 23; // DATA_PIN
constexpr uint8_t kSevenSegClock = 18; // CLOCK_PIN
constexpr uint8_t kSevenSegLatch = 5; // LATCH_PIN

// --- Enkoder obrotowy ---
constexpr uint8_t kEncoderClk = 25; // ENC_CLK 
constexpr uint8_t kEncoderDt = 26; // ENC_DT
constexpr uint8_t kEncoderSw = 27; // ENC_SW

// --- Buzzer ---
constexpr uint8_t kBuzzer = 2; // BUZZER_PIN

// --- Temporary touch test on GPIO4 ---
constexpr uint8_t kTouchTestPad = 4; // TOUCH_PAD

// --- STM32 UART (BPM/SpO2) ---
// STM32 PA9  (TX)          -> ESP32 GPIO16 (RX)
// STM32 PA10 (RX, pull-up) -> ESP32 GPIO17 (TX)
constexpr uint8_t kStm32UartRx = 16; // STM32_RX
constexpr uint8_t kStm32UartTx = 17; // STM32_TX

// --- Bluetooth I2S -> zewnetrzny DAC / wzmacniacz audio ---
// Mapowanie jest opisane z perspektywy ESP32:
// GPIO33 (BCLK) -> DAC BCK / SCK
// GPIO32 (WS / LRCK) -> DAC LCK / LRCK
// GPIO14 (DOUT) -> DAC DIN
// W tej konfiguracji nie uzywamy osobnego MCLK.
constexpr uint8_t kBtI2sBclk = 33; // I2S_BCLK
constexpr uint8_t kBtI2sWs = 32; // I2S_WS
constexpr uint8_t kBtI2sData = 14; // I2S_DATA

}  // namespace BoardPins
