#pragma once

#include <Arduino.h>

bool audioBT_init();        // uruchamia Bluetooth Audio, zwraca false przy awarii startu
void audioBT_deinit();      // wyłącza BT i zwalnia zasoby
bool audioBT_isConnected(); // sprawdza, czy telefon jest połączony
TaskHandle_t audioBT_getAppTaskHandle(); // nullptr, gdy BT audio nie jest aktywne
TaskHandle_t audioBT_getI2STaskHandle(); // nullptr, gdy BT audio nie jest aktywne
bool audioBT_play();
bool audioBT_pause();
bool audioBT_previous();
bool audioBT_next();
bool audioBT_volumeDown();
bool audioBT_volumeUp();