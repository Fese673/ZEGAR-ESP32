/*
  * STM32_Data.h
  * Nagłówek dla komunikacji z STM32 (BPM / SpO2)
*/


#pragma once

#ifndef STM32_DATA_H
#define STM32_DATA_H

#include <Arduino.h>
#include <SoftwareSerial.h>  // EspSoftwareSerial on ESP32

//--- Dane od STM32 (aktualizowane przez STM32data_update) ---
extern int bpmNumber;        // beats per minute
extern int spo2Number;       // SpO2 (%)
extern bool stmDataUpdated;  // ustawiana, gdy pojawią się nowe wartości
extern int16_t stm32PpgDiff; // ostatnia wartość PPG diff z ramki binarnej

//--- PPG stream aktywny (true jeśli ramki 0xAA odbierane są regularnie) ---
extern bool stm32PpgActive;

//--- Inicjalizacja połączenia (wywołać raz w setup) ---
void STM32data_begin(int rxPin, int txPin, uint32_t baudRate = 115200UL);

//--- Obsługa odbioru - wywoływać cyklicznie z loop() ---
void STM32data_update();

#endif // STM32_DATA_H