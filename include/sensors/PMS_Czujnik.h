#pragma once

#include <Arduino.h>

// ============================================================================
// KONFIGURACJA PINÓW PMS5003
// ============================================================================
#define PMS_RX 34
#define PMS_TX 13

// ============================================================================
// KLASA PMS5003 CZUJNIK
// ============================================================================
// Sterownik czujnika pyłu PMS5003 oparty na bibliotece PMserial.
// Realizuje nieblokujący automat stanów:
//   1) Odczyt ATM (atmosferyczny) → pauza 2 s
//   2) Odczyt CF=1 (fabryczny)    → raport + pauza 8 s
// Wyniki trafiają bezpośrednio do zmiennych globalnych zadeklarowanych
// w main.cpp (extern), dzięki czemu menu LCD widzi dane natychmiast.
// ============================================================================

namespace PMS5003Sensor {

  /// Inicjalizacja czujnika — wywołaj raz w setup()
  void begin();

  /// Nieblokujący automat stanów — wywołaj w każdym loop()
  void update();

  /// Czy ostatni odczyt był poprawny (przynajmniej ATM OK)
  bool isOk();

  /// Zwraca timestamp (ms) ostatniej aktualizacji danych z czujnika
  uint32_t getLastUpdateTime();

  /// Wymuś natychmiastowy cykl odczytu (rozpoczyna nowy START_ATM_READ)
  void requestImmediateRead();

  /// Wymuś reset statystyk min/max (wywoływane wewnętrznie w begin(),
  /// ale można też z menu)
  void resetMinMax();

}  // namespace PMS5003Sensor
