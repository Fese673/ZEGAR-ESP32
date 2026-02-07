#ifndef WIFISYNC_H
#define WIFISYNC_H

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "LCDMirror.h"
#include "AppState.h"

namespace WiFiSync {

/*
  API:
    begin(ssid, pass, ntp, gmt, dst)
    startSync()
    update()     -> call from loop()
    isBusy()
    setOnStart(cb)
    setOnDone(cb)
    setTimeRefs(hours, minutes, seconds, lastTick)
    setAppStatePtr(&appState)
*/

void begin(const char* ssid, const char* pass,
           const char* ntp_server = "pool.ntp.org",
           long gmt_offset = 3600, int dst_offset = 3600);

void startSync();
void stop();
void update();
bool isBusy();

void setOnStart(void (*cb)());
void setOnDone(void (*cb)());

// Automatyczne wywoływanie synchronizacji po uzyskaniu adresu IP
void setAutoSyncOnConnect(bool enable);

// przekazanie referencji do globalnych zmiennych czasu (opcjonalne, dla kompatybilności)
void setTimeRefs(int &hoursRef, int &minutesRef, int &secondsRef, unsigned long &lastTickRef);

// opcjonalnie: wskaźnik do appState (jeśli chcesz, by biblioteka zmieniała stan UI)
void setAppStatePtr(AppState *appStatePtr);

} // namespace WiFiSync

#endif // WIFISYNC_H
