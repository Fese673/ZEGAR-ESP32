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
const char* audioBT_getTitle();
const char* audioBT_getArtist();
bool audioBT_isPlaying();
void audioBT_copyMetadata(char* title, size_t titleSize, char* artist, size_t artistSize);
void audioBT_setVolume(uint8_t vol);
void audioBT_setEQ(uint8_t bass, uint8_t mid, uint8_t treble);
void audioBT_serviceDeferred();