#pragma once

#include <stdint.h>

/*
 * WifiProvisioning — zdalna konfiguracja WiFi ZEGARA z panelu na Guitionie.
 *
 * Zasady systemu (V3.0):
 *  - Guition jest TYLKO HMI: nie laczy sie z WiFi, nie odpala SNTP.
 *  - Cala robota sieciowa (skan, test hasla, zapis, przelaczenie) dzieje sie
 *    TU, w krotkozyjacych taskach — petla glowna, UI, audio i most STM32
 *    nigdy nie sa blokowane.
 *  - Test hasla nie wymaga restartu: po sukcesie dane sa zapisywane w NVS
 *    ("zegar": wifiSsid/wifiPass) i NATYCHMIAST adoptowane przez WiFiSync
 *    (swiezy NTP/RTC na nowym lace). Restart to juz tylko awaryjna droga.
 *  - Po nieudanym tescie stare polaczenie wraca automatycznie.
 *
 * Przeplyw (loopTask):
 *   requestScan()/requestTest() -> task w tle -> poll() == kEvent*Ready
 *   -> EsptoGuitionState wysyla ramki 0x11 do Guitiona.
 *
 * Bufory wynikow sa statyczne (stała pamiec, zero fragmentacji miedzy
 * probami). Alokacje przejsciowe robi tylko API Arduino WiFi (String na
 * SSID, tablica skanu) — WiFi.scanDelete() zwalnia je po kazdym skanie.
 */

namespace WifiProvisioning {

constexpr uint8_t kMaxNetworks = 12;   // gorna granica listy (top po RSSI)
constexpr uint8_t kSsidLenMax = 32;
constexpr uint8_t kPassLenMax = 64;

// Kody wyniku (wspolne z Guitionem — lustro w CommunicationState.cpp).
enum ResultCode : uint8_t {
  kResOk = 0,        // polaczono + zapisano (albo: skan OK)
  kResBadAuth = 1,   // zle haslo / odrzucona autoryzacja
  kResNoAp = 2,      // nie znaleziono sieci
  kResTimeout = 3,   // siec nie odpowiedziala w czasie
  kResBusy = 4,      // inne zadanie w toku
  kResWifiOff = 5,   // tryb Bluetooth (WiFi wylaczone)
  kResNvsFail = 6,   // polaczono, ale zapis do NVS sie nie udal
  kResError = 7,     // blad wewnetrzny / drivera
  kResInvalid = 8,   // zle dane wejsciowe (dlugosci SSID/hasla)
};

enum Event : uint8_t {
  kEventNone = 0,
  kEventScanReady = 1,
  kEventTestReady = 2,
};

struct Network {
  char ssid[kSsidLenMax + 1];
  int8_t rssi;
  uint8_t open; // 1 = siec otwarta (bez hasla)
};

// Inicjalizacja: wczytuje zapisany SSID z NVS do cache (info dla panelu).
void begin();

// Zlecenia z loopTask. false = odrzucone (busy/invalid) — wywolujacy
// powinien wtedy odeslac wynik kResBusy/kResInvalid.
bool requestScan();
bool requestTest(const char* ssid, const char* pass);
// ssidOut: kopia zapisanego SSID pod blokada (bez wyscigu z testem w tle).
bool requestInfo(uint8_t& wifiOn, uint8_t& btMode, uint8_t& connected,
                 char* ssidOut, uint8_t ssidOutSize);

// Puls z Esptogution::update() (loopTask) — jedno zdarzenie na raz.
Event poll();

// Po poll() == kEventScanReady (wazne do nastepnego poll):
uint8_t scanStatus();          // ResultCode
uint8_t scanCount();
const Network* scanNetworks();

// Po poll() == kEventTestReady:
uint8_t testCode();            // ResultCode
uint16_t testReason();         // surowy powod rozlaczenia drivera (diagnostyka)
const char* testSsid();

// Cache zapisanego SSID (z NVS, aktualizowany po udanym zapisie).
const char* savedSsid();

}  // namespace WifiProvisioning
