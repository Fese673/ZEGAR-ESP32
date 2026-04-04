/*
 * WiFiSync.h
 * Zarządzanie Wi-Fi, SNTP i stanem synchronizacji czasu.
 */
#pragma once

#include <Arduino.h>

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

// Setup / lifecycle
void begin(const char* ssid, const char* pass, const char* ntpServer = "pool.ntp.org");
void update();
void stop();
bool isBusy();

// Requests
void requestTimeSync();
void startSync();

// State / diagnostics
SyncState getState();
SyncError getLastError();
TaskHandle_t getInitTaskHandle();

// Callbacks
void setOnStart(void (*cb)());
void setOnDone(void (*cb)());

// Configuration
void setPeriodicSyncIntervalMinutes(uint16_t minutes);
uint16_t getPeriodicSyncIntervalMinutes();

// Optional time references for the system clock.
void setTimeRefs(int &hoursRef, int &minutesRef, int &secondsRef, unsigned long &lastTickRef);

// NTP status
unsigned long getLastNtpSyncTime();
bool hasNtpSynced();

}  // namespace WiFiSync
