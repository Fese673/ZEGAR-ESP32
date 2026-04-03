// Nagłówek dla komunikacji z STM32 (BPM / SpO2)

#pragma once

#ifndef STM32_DATA_H
#define STM32_DATA_H

#include <Arduino.h>

// Dane od STM32 (aktualizowane przez STM32data_update)
extern int bpmNumber;        // beats per minute
extern int spo2Number;       // SpO2 (%)
extern bool stmDataUpdated;  // ustawiana, gdy pojawią się nowe wartości

// Inicjalizacja połączenia (wywołać raz w setup)
void STM32data_begin(HardwareSerial &serialPort, uint32_t baudRate, int rxPin, int txPin);

// Obsługa odbioru - wywoływać cyklicznie z loop()
void STM32data_update();

#endif // STM32_DATA_H