#include "RadioModeSwitch.h"
#include <Arduino.h>
#include <Esp.h>
#include <esp_attr.h>  // Oficjalne makro RTC_NOINIT_ATTR
#include "ModeManager.h"
#include "StatsManager.h"

// ============================================================================
// RTC MEMORY - FLAGA PRZEJŚCIA (tymczasowa, ginie po power-off)
// ============================================================================
// RTC_NOINIT_ATTR: zmienna w RTC slow memory, przetrwa soft reset (esp_restart)
// ale zginie po power cycle (wyłączeniu zasilania)

// Wartości flagi
#define RTC_FLAG_WIFI 0x1234  // Magiczne słowo do WiFi
#define RTC_FLAG_BT   0x5678  // Magiczne słowo do BT
#define RTC_FLAG_NONE 0x0000  // Neutralne (brak przejścia)

// Struktura RTC state - przechowuje tryb + czas
struct RTC_State {
  uint32_t mode_flag;  // WiFi/BT flaga
  uint8_t hours;       // Godzina (0-23)
  uint8_t minutes;     // Minuta (0-59)
  uint8_t seconds;     // Sekunda (0-59)
};

// POPRAWNE użycie RTC_NOINIT_ATTR - struktura w RTC RAM (0x50000000+)
RTC_NOINIT_ATTR static RTC_State rtc_state;

// External time variables from main.cpp
extern int hours, minutes, seconds;

namespace RadioModeSwitch {

  // Stan wewnętrzny
  static RadioModeSwitchState s_current_state = RADIO_STATE_WIFI;
  static RadioModeSwitchNextMode s_next_mode = RADIO_NEXT_NONE;
  static bool s_initialized = false;
  static bool s_mode_initialized = false;       // Flaga czy tryb (WiFi/BT) był już zainicjalizowany
  static unsigned long s_init_start_time = 0;   // Czas startu systemu - opóźniamy inicjalizację
  static bool s_restartPending = false;
  static unsigned long s_restartAtMs = 0;
  static unsigned long s_next_bt_retry_ms = 0;

  constexpr unsigned long kRestartDelayMs = 100UL;
  constexpr unsigned long kBtRetryIntervalMs = 3000UL;

  static void scheduleRestart() {
    s_restartPending = true;
    s_restartAtMs = millis() + kRestartDelayMs;
  }

  // ========================================================================
  // INICJALIZACJA
  // ========================================================================

  void begin() {
    if (s_initialized) return;

    // Przeczytaj flagę z RTC memory
    // Po power cycle wartość może być losowa - sprawdzamy tylko znane wartości
    uint32_t rtc_val = rtc_state.mode_flag;
    
    Serial.printf("[RadioModeSwitch] Odczytana flaga RTC: 0x%04X | Czas: %02d:%02d:%02d\n", 
                  rtc_val, rtc_state.hours, rtc_state.minutes, rtc_state.seconds);

    if (rtc_val == RTC_FLAG_BT) {
      // Zaplanowany Bluetooth (po soft reset z menu)
      s_next_mode = RADIO_NEXT_BT;
      s_current_state = RADIO_STATE_BT;
      // Wyczyść flagę po odczytaniu - jednorazowe użycie
      rtc_state.mode_flag = RTC_FLAG_NONE;
    } else if (rtc_val == RTC_FLAG_WIFI) {
      // Zaplanowany WiFi (po soft reset z menu)
      s_next_mode = RADIO_NEXT_WIFI;
      s_current_state = RADIO_STATE_WIFI;
      // Wyczyść flagę po odczytaniu
      rtc_state.mode_flag = RTC_FLAG_NONE;
    } else {
      // Nieznana wartość (power cycle lub pierwsza inicjalizacja) -> WiFi domyślnie
      s_next_mode = RADIO_NEXT_WIFI;
      s_current_state = RADIO_STATE_WIFI;
      rtc_state.mode_flag = RTC_FLAG_NONE;
    }

    s_initialized = true;
    s_init_start_time = millis();  // Zanotuj czas startu
    s_restartPending = false;
    s_restartAtMs = 0;
    s_next_bt_retry_ms = 0;

    Serial.println("[RadioModeSwitch] Inicjalizacja zakończona");
    printDiagnostics();
  }

  RadioModeSwitchState getCurrentState() {
    return s_current_state;
  }

  RadioModeSwitchNextMode getNextMode() {
    return s_next_mode;
  }

  bool isInitializing() {
    // Zwróć true jeśli system inicjalizuje WiFi/BT
    // true od kiedy s_mode_initialized == false aż do końca setup() i delay'u
    return s_initialized && !s_mode_initialized;
  }

  // ========================================================================
  // ŻĄDANIE PRZEŁĄCZENIA
  // ========================================================================

  void requestModeSwitch_WiFi() {
    // KROK 1: Zapisz czas
    rtc_state.hours = hours;
    rtc_state.minutes = minutes;
    rtc_state.seconds = seconds;
    
    // KROK 2: Ustaw flagę w RTC memory
    rtc_state.mode_flag = RTC_FLAG_WIFI;
    
    // KROK 3: Log
    Serial.printf("[RadioModeSwitch] Flaga WiFi ustawiona. Czas zapisany: %02d:%02d:%02d, restart...\n",
                  rtc_state.hours, rtc_state.minutes, rtc_state.seconds);
    // Ensure any pending stats are flushed to NVS before restarting
    statsManager.saveStats();
    Serial.flush();

    // KROK 4: Zaplanuj restart bez blokowania
    scheduleRestart();
  }

  void requestModeSwitch_BT() {
    // KROK 1: Zapisz czas
    rtc_state.hours = hours;
    rtc_state.minutes = minutes;
    rtc_state.seconds = seconds;
    
    // KROK 2: Ustaw flagę w RTC memory
    rtc_state.mode_flag = RTC_FLAG_BT;
    
    // KROK 3: Log
    Serial.printf("[RadioModeSwitch] Flaga BT ustawiona. Czas zapisany: %02d:%02d:%02d, restart...\n",
                  rtc_state.hours, rtc_state.minutes, rtc_state.seconds);
    // Ensure any pending stats are flushed to NVS before restarting
    statsManager.saveStats();
    Serial.flush();

    // KROK 4: Zaplanuj restart bez blokowania
    scheduleRestart();
  }

  void cancelModeSwitch() {
    // Wyczyść flagę - następny reset będzie WiFi
    rtc_state.mode_flag = RTC_FLAG_NONE;
    s_next_mode = RADIO_NEXT_WIFI;

    Serial.println("[RadioModeSwitch] Przełączenie anulowane");
  }

  // ========================================================================
  // UPDATE - POWINNO BYĆ WYWOŁYWANE Z loop()
  // ========================================================================
  
  void update() {
    if (s_restartPending) {
      const unsigned long nowMs = millis();
      if ((long)(nowMs - s_restartAtMs) >= 0) {
        esp_restart();
      }
      return;
    }

    // Opóźniona inicjalizacja trybu WiFi/BT
    // Czekamy aż system będzie w pełni gotowy (LCD, UI, itd.)
    const unsigned long INIT_DELAY_MS = 200;   // min delay for LCD readiness
    
    if (!s_mode_initialized && s_initialized && (millis() - s_init_start_time >= INIT_DELAY_MS)) {
      s_mode_initialized = true;
      
      // Teraz faktycznie inicjalizuj WiFi/BT
      if (s_next_mode == RADIO_NEXT_BT) {
        s_current_state = RADIO_STATE_BT;
        s_next_bt_retry_ms = millis() + kBtRetryIntervalMs;
        Serial.println("[RadioModeSwitch] update() - Inicjalizacja Bluetooth (po delay)");
        ModeManager::btOn();
      } else {
        s_current_state = RADIO_STATE_WIFI;
        s_next_bt_retry_ms = 0;
        Serial.println("[RadioModeSwitch] update() - Inicjalizacja WiFi (po delay)");
        ModeManager::wifiOn();
      }
    }

    if (s_mode_initialized && s_current_state == RADIO_STATE_BT && !ModeManager::isBtOn()) {
      const unsigned long nowMs = millis();
      if ((long)(nowMs - s_next_bt_retry_ms) >= 0) {
        Serial.println("[RadioModeSwitch] BT inactive in BT mode, retrying init");
        ModeManager::btOn();
        s_next_bt_retry_ms = nowMs + kBtRetryIntervalMs;
      }
    }
  }

  // ========================================================================
  // INICJALIZACJA TRYBU PO STARCIE
  // ========================================================================

  void initializeStartMode() {
    // Ta funkcja powinna być wywołana w setup() lub bardzo wcześnie w main.cpp

    if (!s_initialized) {
      begin();
    }

    // === LOGIKA: Faktycznie uruchom odpowiedni tryb na podstawie flagi ===
    // *** WAŻNE: NIE wywoływaj ModeManager::wifiOn()/btOn() TUTAJ ***
    // To robi LoadStoreError na adresie 0x3f43c06c gdy callback LCD się jeszcze inicjalizuje!
    // Zamiast tego: ustaw flagę i pozwól callbackom na obsługę

    if (s_next_mode == RADIO_NEXT_BT) {
      // Bluetooth był zaplanowany - inicjalizuj BT
      Serial.println("[RadioModeSwitch] Inicjalizacja: Bluetooth");
      // ModeManager::btOn() - NIE TUTAJ! Będzie w update() po delay
    } else {
      // WiFi (domyślnie)
      Serial.println("[RadioModeSwitch] Inicjalizacja: WiFi (domyślnie)");
      rtc_state.mode_flag = RTC_FLAG_NONE;
      s_next_mode = RADIO_NEXT_NONE;
    }
  }

  bool isDefaultStartupWiFi() {
    return (s_next_mode == RADIO_NEXT_WIFI || s_next_mode == RADIO_NEXT_NONE);
  }

  void forceMode(RadioModeSwitchState state, RadioModeSwitchNextMode nextMode) {
    s_current_state = state;
    s_next_mode = nextMode;
    s_mode_initialized = true;
    s_restartPending = false;
    s_restartAtMs = 0;

    if (state == RADIO_STATE_WIFI) {
      s_next_bt_retry_ms = 0;
      rtc_state.mode_flag = RTC_FLAG_NONE;
    } else if (state == RADIO_STATE_BT) {
      s_next_bt_retry_ms = millis() + kBtRetryIntervalMs;
    }

    Serial.printf("[RadioModeSwitch] Force mode: %s\n",
                  (state == RADIO_STATE_BT) ? "Bluetooth" : "WiFi");
  }

  // ========================================================================
  // DIAGNOSTYKA
  // ========================================================================

  void printDiagnostics() {
    Serial.println("\n=== RadioModeSwitch Diagnostyka ===");
    Serial.print("RTC Flag Value: 0x");
    Serial.println(rtc_state.mode_flag, HEX);
    Serial.printf("RTC Time: %02d:%02d:%02d\n", rtc_state.hours, rtc_state.minutes, rtc_state.seconds);

    Serial.print("Bieżący stan: ");
    switch (s_current_state) {
      case RADIO_STATE_WIFI:
        Serial.println("WiFi");
        break;
      case RADIO_STATE_BT:
        Serial.println("Bluetooth");
        break;
      case RADIO_STATE_TRANSITIONING:
        Serial.println("Przejście (reset)");
        break;
      default:
        Serial.println("NIEZNANY");
    }

    Serial.print("Następny tryb: ");
    switch (s_next_mode) {
      case RADIO_NEXT_WIFI:
        Serial.println("WiFi");
        break;
      case RADIO_NEXT_BT:
        Serial.println("Bluetooth");
        break;
      case RADIO_NEXT_NONE:
        Serial.println("Neutralny (WiFi default)");
        break;
      default:
        Serial.println("NIEZNANY");
    }

    Serial.print("Inicjalizacja: ");
    Serial.println(s_initialized ? "TAK" : "NIE");
    Serial.println("=====================================\n");
  }

  // ========================================================================
  // POBIERANIE CZASU Z RTC (do przywrócenia po soft reset)
  // ========================================================================

  uint8_t getRTCHours() {
    return rtc_state.hours;
  }

  uint8_t getRTCMinutes() {
    return rtc_state.minutes;
  }

  uint8_t getRTCSeconds() {
    return rtc_state.seconds;
  }

  void clearRTCTime() {
    rtc_state.hours = 0;
    rtc_state.minutes = 0;
    rtc_state.seconds = 0;
  }

} // namespace RadioModeSwitch
