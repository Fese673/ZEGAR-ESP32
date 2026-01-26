#ifndef RADIOMODESWITCH_H
#define RADIOMODESWITCH_H

#include <stdint.h>

/**
 * RadioModeSwitch.h
 * 
 * Biblioteka do obsługi przełączania WiFi ↔ Bluetooth z „przyjemnym resetem"
 * 
 * Koncepcja:
 * - Jedna flaga tymczasowa (RTC memory) do przechowywania następnego trybu
 * - Flaga znika po wyłączeniu zasilania (power-on = zawsze WiFi)
 * - Reset jest świadomy, zaplanowany i komunikowany
 * - Po restarcie: sprawdź flagę i uruchom odpowiedni tryb
 */

enum RadioModeSwitchState {
  RADIO_STATE_WIFI,        // Aktualnie WiFi
  RADIO_STATE_BT,          // Aktualnie Bluetooth
  RADIO_STATE_TRANSITIONING // Przejście (przed resetem)
};

enum RadioModeSwitchNextMode {
  RADIO_NEXT_WIFI,    // Następny tryb: WiFi
  RADIO_NEXT_BT,      // Następny tryb: Bluetooth
  RADIO_NEXT_NONE     // Brak zaplanowanego przejścia (neutralne)
};

namespace RadioModeSwitch {

  // ========================================================================
  // INICJALIZACJA I ZARZĄDZANIE STANEM
  // ========================================================================

  /**
   * Inicjalizacja biblioteki
   * - Odczytaj flagę z RTC memory
   * - Określ bieżący stan
   * - Wyczyść flagę jeśli potrzeba
   */
  void begin();

  /**
   * Update - powinno być wywoływane z loop()
   * - Obsługuje opóźnioną inicjalizację WiFi/BT po starcie
   * - Czeka aż system będzie w pełni gotowy
   */
  void update();

  /**
   * Zwraca aktualny stan trybu radia
   */
  RadioModeSwitchState getCurrentState();

  /**
   * Zwraca zaplanowany tryb (ten co ma być po restarcie)
   */
  RadioModeSwitchNextMode getNextMode();

  /**
   * Czy aktualnie się inicjalizuje WiFi/BT?
   * Zwraca true jeśli system jest w trakcie opóźnionej inicjalizacji
   * UŻYWANE: Aby zablokować menu pod czas inicjalizacji
   */
  bool isInitializing();

  // ========================================================================
  // ŻĄDANIE PRZEŁĄCZENIA (z reset loop)
  // ========================================================================

  /**
   * Żądaj przełączenia na WiFi + reset
   * - Ustaw flagę w RTC
   * - Wyświetl komunikat na LCD
   * - Wykonaj reset
   */
  void requestModeSwitch_WiFi();

  /**
   * Żądaj przełączenia na Bluetooth + reset
   * - Ustaw flagę w RTC
   * - Wyświetl komunikat na LCD
   * - Wykonaj reset
   */
  void requestModeSwitch_BT();

  /**
   * Anuluj przełączenie (czyści flagę)
   */
  void cancelModeSwitch();

  // ========================================================================
  // INICJALIZACJA TRYBU PO STARCIE
  // ========================================================================

  /**
   * Sprawdź flagę i zainicjuj odpowiedni tryb
   * - Jeśli flaga = BT, uruchom Bluetooth
   * - W przeciwnym razie uruchom WiFi
   * - Wyczyść flagę po inicjalizacji
   */
  void initializeStartMode();

  /**
   * Zwraca czy startup jest w WiFi trybie (domyślnie)
   */
  bool isDefaultStartupWiFi();

  // ========================================================================
  // POBIERANIE CZASU Z RTC (do przywrócenia po soft reset)
  // ========================================================================

  /**
   * Pobierz godzinę zapisaną w RTC
   */
  uint8_t getRTCHours();

  /**
   * Pobierz minutę zapisaną w RTC
   */
  uint8_t getRTCMinutes();

  /**
   * Pobierz sekundę zapisaną w RTC
   */
  uint8_t getRTCSeconds();

  /**
   * Wyczyść czas z RTC (after restoration)
   */
  void clearRTCTime();

  // ========================================================================
  // DIAGNOSTYKA
  // ========================================================================

  /**
   * Wydrukuj info o stanie do Serial
   */
  void printDiagnostics();

} // namespace RadioModeSwitch

#endif // RADIOMODESWITCH_H
