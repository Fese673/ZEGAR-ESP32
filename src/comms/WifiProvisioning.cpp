#include "WifiProvisioning.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <atomic>
#include <string.h>

#include "AppLog.h"
#include "ModeManager.h"
#include "Task_Config.h"
#include "WiFiSync.h"

namespace WifiProvisioning {
namespace {

constexpr char TAG[] = "WIFIPROV";

// Test hasla: od WiFi.begin do WL_CONNECTED+GOT_IP. 15 s jak w WiFiSync.
constexpr unsigned long kTestTimeoutMs = 15000UL;
// Bufor roboczy skanu: zbieramy z zapasem, potem sortujemy i tniemy do 12.
constexpr uint8_t kScratchNetworks = 24;
// Powody rozlaczenia z drivera (wifi_err_reason_t) — mapowanie na kody UI.
constexpr uint16_t kReasonWrongPassword = 15;   // 4WAY_HANDSHAKE_TIMEOUT
constexpr uint16_t kReasonNoApFound = 201;      // NO_AP_FOUND
constexpr uint16_t kReasonAuthFail = 202;       // AUTH_FAIL
constexpr uint16_t kReasonHandshakeTimeout = 204;
constexpr uint16_t kReasonConnFail = 205;       // CONNECTION_FAIL

enum TaskState : uint8_t { kIdle = 0, kScanning = 1, kTesting = 2 };

// Stan miedzy loopTask (zlecenia/poll) a taskiem roboczym — atomics + gotowe
// bufory zapisywane PRZED ustawieniem flagi (release/acquire).
std::atomic<uint8_t> s_taskState{kIdle};
std::atomic<bool> s_scanReady{false};
std::atomic<uint8_t> s_scanStatus{kResError};
std::atomic<uint8_t> s_scanCount{0};
std::atomic<bool> s_testReady{false};
std::atomic<uint8_t> s_testCode{kResError};
std::atomic<uint16_t> s_testReason{0};

Network s_networks[kMaxNetworks];
Network s_scratch[kScratchNetworks];
char s_testSsid[kSsidLenMax + 1] = {0};
char s_testPass[kPassLenMax + 1] = {0};
char s_savedSsid[kSsidLenMax + 1] = {0};
portMUX_TYPE s_savedMux = portMUX_INITIALIZER_UNLOCKED;

bool startTask(TaskFunction_t fn, const char* name) {
  if (xTaskCreatePinnedToCore(fn, name,
                              TaskConfig::WifiProvisionTask::kStackBytes, nullptr,
                              TaskConfig::WifiProvisionTask::kPriority, nullptr,
                              TaskConfig::WifiProvisionTask::kCore) != pdPASS) {
    LOG_W(TAG, "task create failed name=%s", name);
    s_taskState.store(kIdle, std::memory_order_release);
    return false;
  }
  return true;
}

// Insertion sort malejaco po RSSI (lista <= 24 elementow).
void sortByRssiDesc(Network* arr, uint8_t count) {
  for (uint8_t i = 1; i < count; ++i) {
    const Network key = arr[i];
    int16_t j = static_cast<int16_t>(i) - 1;
    while (j >= 0 && arr[j].rssi < key.rssi) {
      arr[j + 1] = arr[j];
      --j;
    }
    arr[j + 1] = key;
  }
}

void scanTask(void*) {
  uint8_t status = kResOk;
  uint8_t count = 0;

  if (!ModeManager::isWifiOn()) {
    status = kResWifiOff;
  } else {
    const int found = WiFi.scanNetworks(false, false); // sync, ~2-4 s, bez ukrytych
    if (found < 0) {
      status = kResError;
    } else {
      for (int i = 0; i < found; ++i) {
        const String ssidStr = WiFi.SSID(i);
        if (ssidStr.length() == 0 || ssidStr.length() > kSsidLenMax) {
          continue; // pusta nazwa = siec ukryta — swiadomie pomijana
        }
        const int8_t rssi = static_cast<int8_t>(WiFi.RSSI(i));
        const uint8_t isOpen = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? 1U : 0U;

        // Dedupe: ten sam SSID (wiele AP) -> zostaje najsilniejszy sygnal.
        bool merged = false;
        for (uint8_t n = 0; n < count; ++n) {
          if (strcmp(s_scratch[n].ssid, ssidStr.c_str()) == 0) {
            if (rssi > s_scratch[n].rssi) {
              s_scratch[n].rssi = rssi;
            }
            merged = true;
            break;
          }
        }
        if (merged || count >= kScratchNetworks) {
          continue;
        }

        strncpy(s_scratch[count].ssid, ssidStr.c_str(), kSsidLenMax);
        s_scratch[count].ssid[kSsidLenMax] = '\0';
        s_scratch[count].rssi = rssi;
        s_scratch[count].open = isOpen;
        ++count;
      }

      sortByRssiDesc(s_scratch, count);
      if (count > kMaxNetworks) {
        count = kMaxNetworks;
      }
      memcpy(s_networks, s_scratch, sizeof(Network) * count);
    }
    WiFi.scanDelete(); // zwolnij pamiec wynikow skanu (ochrona przed wyciekiem)
  }

  s_scanCount.store(count, std::memory_order_release);
  s_scanStatus.store(status, std::memory_order_release);
  s_scanReady.store(true, std::memory_order_release);
  s_taskState.store(kIdle, std::memory_order_release);
  vTaskDelete(nullptr);
}

uint8_t mapReasonToCode(uint16_t reason) {
  switch (reason) {
    case kReasonWrongPassword:
    case kReasonAuthFail:
    case kReasonHandshakeTimeout:
    case kReasonConnFail:
      return kResBadAuth;
    case kReasonNoApFound:
      return kResNoAp;
    default:
      return kResTimeout;
  }
}

bool saveCredentialsToNvs(const char* ssid, const char* pass) {
  Preferences prefs;
  if (!prefs.begin("zegar", false)) {
    LOG_E(TAG, "NVS open failed");
    return false;
  }
  const size_t wSsid = prefs.putString("wifiSsid", ssid);
  const size_t wPass = prefs.putString("wifiPass", pass);
  prefs.end();

  // putString("") zwraca 0 (dlugosc pustego stringa) — sieci otwarte maja
  // puste haslo, wiec zero jest POPRAWNE gdy haslo jest puste.
  const bool passOk = (pass[0] == '\0') || (wPass > 0);
  if (wSsid == 0 || !passOk) {
    LOG_E(TAG, "NVS write failed ssid=%u pass=%u", (unsigned)wSsid, (unsigned)wPass);
    return false;
  }

  portENTER_CRITICAL(&s_savedMux);
  strncpy(s_savedSsid, ssid, kSsidLenMax);
  s_savedSsid[kSsidLenMax] = '\0';
  portEXIT_CRITICAL(&s_savedMux);
  LOG_I(TAG, "Credentials saved ssid_len=%u", (unsigned)strlen(s_savedSsid));
  return true;
}

void testTask(void*) {
  uint8_t code = kResError;
  uint16_t reason = 0;
  bool paused = false;
  bool adopted = false;

  if (!ModeManager::isWifiOn()) {
    // Tryb BT: WiFi stacku nie ma — nie ruszamy niczego, nie pauzujemy.
    code = kResWifiOff;
  } else if (!WiFiSync::pauseForProvisioning()) {
    // Automat WiFi nie zatrzymal sie bezpiecznie (wlasnie trwa inicjalizacja
    // drivera) — lepiej odmowic niz ryzykowac uszkodzony driver.
    code = kResBusy;
  } else {
    paused = true;

    // Te same ustawienia co wifiInitTask: bez auto-reconnectu i persistent,
    // wymuszony DNS 1.1.1.1 (adoptowane lacze ma dzialac jak normalne).
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, IPAddress(1, 1, 1, 1),
                IPAddress(1, 0, 0, 1));

    WiFi.disconnect(true, false);
    vTaskDelay(pdMS_TO_TICKS(250)); // chwila na czysty disconnect przed testem

    WiFi.begin(s_testSsid, s_testPass);

    const unsigned long startMs = millis();
    bool connected = false;
    while ((millis() - startMs) < kTestTimeoutMs) {
      if (WiFi.status() == WL_CONNECTED &&
          static_cast<uint32_t>(WiFi.localIP()) != 0U) {
        connected = true;
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(200));
    }
    reason = WiFiSync::lastDisconnectReason();

    if (connected) {
      if (saveCredentialsToNvs(s_testSsid, s_testPass)) {
        code = kResOk;
      } else {
        // Lacze dziala, ale po restarcie wrocilyby stare dane — mow wprost.
        code = kResNvsFail;
      }
      WiFiSync::adoptProvisionedCredentials(s_testSsid, s_testPass);
      adopted = true;
    } else {
      code = mapReasonToCode(reason);
      WiFi.disconnect(true, false);
      LOG_W(TAG, "Provision test failed reason=%u code=%u", (unsigned)reason,
            (unsigned)code);
    }
  }

  if (paused && !adopted) {
    WiFiSync::restoreAfterProvisioning();
  }

  s_testReason.store(reason, std::memory_order_release);
  s_testCode.store(code, std::memory_order_release);
  s_testReady.store(true, std::memory_order_release);
  s_taskState.store(kIdle, std::memory_order_release);
  vTaskDelete(nullptr);
}

}  // namespace

void begin() {
  Preferences prefs;
  if (prefs.begin("zegar", true)) {
    const String saved = prefs.getString("wifiSsid", "");
    prefs.end();
    strncpy(s_savedSsid, saved.c_str(), kSsidLenMax);
    s_savedSsid[kSsidLenMax] = '\0';
  }
  LOG_I(TAG, "begin: saved ssid_len=%u", (unsigned)strlen(s_savedSsid));
}

bool requestScan() {
  // Wynik poprzedniego skanu jeszcze nie polecial do Guitiona? Odrzuc —
  // nadpisanie flagi zjadaloby tamta odpowiedz (panel czekalby na timeout).
  if (s_scanReady.load(std::memory_order_acquire) ||
      s_testReady.load(std::memory_order_acquire)) {
    return false;
  }
  uint8_t expected = kIdle;
  if (!s_taskState.compare_exchange_strong(expected, kScanning,
                                           std::memory_order_acq_rel)) {
    return false;
  }
  s_scanReady.store(false, std::memory_order_release);
  return startTask(scanTask, "wifiScan");
}

bool requestTest(const char* ssid, const char* pass) {
  if (ssid == nullptr || ssid[0] == '\0') {
    return false;
  }
  const size_t ssidLen = strnlen(ssid, kSsidLenMax + 1);
  if (ssidLen == 0 || ssidLen > kSsidLenMax) {
    return false;
  }
  const size_t passLen = (pass != nullptr) ? strnlen(pass, kPassLenMax + 1) : 0;
  if (passLen > kPassLenMax) {
    return false;
  }

  // Wynik poprzedniego testu jeszcze nie polecial? Odrzuc — inaczej nowe
  // SSID nadpisaloby bufor, a pump wyslalby stary wynik z nowa nazwa.
  if (s_testReady.load(std::memory_order_acquire) ||
      s_scanReady.load(std::memory_order_acquire)) {
    return false;
  }

  // Kolejnosc KLUCZOWA: najpierw CAS (czy wolno), potem kopiowanie buforow.
  // Odwrotnie = wyscig z dzialajacym testem (nadpisane dane trafialyby do NVS).
  uint8_t expected = kIdle;
  if (!s_taskState.compare_exchange_strong(expected, kTesting,
                                           std::memory_order_acq_rel)) {
    return false;
  }

  // Bufory zapisane PO przejeciu wladzy, PRZED startem taska.
  memcpy(s_testSsid, ssid, ssidLen);
  s_testSsid[ssidLen] = '\0';
  if (passLen > 0 && pass != nullptr) {
    memcpy(s_testPass, pass, passLen);
  }
  s_testPass[passLen] = '\0';

  s_testReady.store(false, std::memory_order_release);
  return startTask(testTask, "wifiTest");
}

bool requestInfo(uint8_t& wifiOn, uint8_t& btMode, uint8_t& connected,
                 char* ssidOut, uint8_t ssidOutSize) {
  wifiOn = ModeManager::isWifiOn() ? 1U : 0U;
  btMode = ModeManager::isBtOn() ? 1U : 0U;
  connected = (WiFi.status() == WL_CONNECTED) ? 1U : 0U;
  if (ssidOut != nullptr && ssidOutSize > 0U) {
    portENTER_CRITICAL(&s_savedMux);
    strncpy(ssidOut, s_savedSsid, ssidOutSize - 1U);
    ssidOut[ssidOutSize - 1U] = '\0';
    portEXIT_CRITICAL(&s_savedMux);
  }
  return true;
}

Event poll() {
  if (s_scanReady.exchange(false, std::memory_order_acq_rel)) {
    return kEventScanReady;
  }
  if (s_testReady.exchange(false, std::memory_order_acq_rel)) {
    return kEventTestReady;
  }
  return kEventNone;
}

uint8_t scanStatus() {
  return s_scanStatus.load(std::memory_order_acquire);
}

uint8_t scanCount() {
  return s_scanCount.load(std::memory_order_acquire);
}

const Network* scanNetworks() {
  return s_networks;
}

uint8_t testCode() {
  return s_testCode.load(std::memory_order_acquire);
}

uint16_t testReason() {
  return s_testReason.load(std::memory_order_acquire);
}

const char* testSsid() {
  return s_testSsid;
}

const char* savedSsid() {
  return s_savedSsid;
}

}  // namespace WifiProvisioning
