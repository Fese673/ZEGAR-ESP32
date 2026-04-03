#pragma once

bool audioBT_init();        // uruchamia Bluetooth Audio, zwraca false przy awarii startu
void audioBT_deinit();      // wyłącza BT i zwalnia zasoby
bool audioBT_isConnected(); // sprawdza, czy telefon jest połączony