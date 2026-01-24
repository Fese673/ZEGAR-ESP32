#pragma once

void audioBT_init();        // uruchamia Bluetooth Audio
void audioBT_deinit();      // wyłącza BT i zwalnia zasoby
bool audioBT_isConnected(); // sprawdza, czy telefon jest połączony