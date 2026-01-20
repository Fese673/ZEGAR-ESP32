#pragma once
#include <Arduino.h>

// Możliwe zdarzenia enkodera obrotowego
typedef enum {
  ENC_NONE = 0,   // Brak zdarzenia
  ENC_LEFT,       // Obrót w lewo
  ENC_RIGHT,      // Obrót w prawo
  ENC_CLICK,      // Krótkie kliknięcie
  ENC_LONG        // Długie kliknięcie
} EncoderEvent;

// Inicjalizuje enkodera na podanych pinach
// clkPin, dtPin, swPin - piny enkodera
// longPressMs - czas do uznania za długie kliknięcie (domyślnie 1000ms)
// debounceMs - opóźnienie filtrujące szumy (domyślnie 50ms)
void encoder_begin(uint8_t clkPin, uint8_t dtPin, uint8_t swPin,
                   unsigned long longPressMs = 1000,
                   unsigned long debounceMs = 50);

// Aktualizuje enkodera i zwraca aktywne zdarzenie
// Wywoływać w loop() - zwraca ENC_NONE jeśli nic się nie dzieje
EncoderEvent encoder_update();