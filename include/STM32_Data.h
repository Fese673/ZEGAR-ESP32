// To jest plik nagłówkowy

#ifndef STM32_Data_H
#define STM32_Data_H
#pragma once

#include <Arduino.h>

// Publiczne zmienne , dzięki temu main.cpp będzie mógł je używać
extern int bpmNumber;
extern int spo2Number;

extern bool stmDataUpdated; // To jest deklaracja zmiennej . Mówi kompilatorowi że można użyć jej 


// Funkcje do uruchomienia komunikacji z stm32
void STM32data_begin(HardwareSerial &serialPort, uint32_t baudRate, int rxPin, int txPin);

// Funkcja która trzeba wywołać w loop()
// Zajmuje sie ona całym programem w tym odbiorem i podziałem na bpmNumber i spo2Number
void STM32data_update();

#endif