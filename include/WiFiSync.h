#ifndef WIFISYNC_H
#define WIFISYNC_H

#include <Arduino.h>
#include <time.h>

// NOTE: This module intentionally has no LCD/UI dependencies.

namespace WiFiSync {

enum class SyncState : uint8_t {
  Idle,
  WifiConnecting,
  TimeSyncing,
  Backoff,
};

enum class SyncError : uint8_t {
  None,
  MissingCredentials,
  Wifi,
  Ntp,
};

void begin(const char* ssid, const char* pass,
           const char* ntp_server = "pool.ntp.org");

// Request background time sync; WiFi connection will be started automatically
// if WiFi mode is enabled and WiFi is not connected.
void requestTimeSync();

// Backwards-compatible helper: requests WiFi connect + time sync immediately.
void startSync();
void stop();
void update();
bool isBusy();

SyncState getState();
SyncError getLastError();

void setOnStart(void (*cb)());
void setOnDone(void (*cb)());

// przekazanie referencji do globalnych zmiennych czasu (opcjonalne, dla kompatybilności)
void setTimeRefs(int &hoursRef, int &minutesRef, int &secondsRef, unsigned long &lastTickRef);

// Czas ostatniej skutecznej synchronizacji z NTP (millis z momentu ustawienia)
unsigned long getLastNtpSyncTime();

// Zwraca true, jeśli NTP zostało zsynchronizowane choć raz od restartu
bool hasNtpSynced();

} // namespace WiFiSync

#endif // WIFISYNC_H
