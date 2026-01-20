#include "Encoder.h"

// Piny enkodera
static uint8_t s_clkPin = 255;
static uint8_t s_dtPin = 255;
static uint8_t s_swPin = 255;

// Zapamiętane stany dla detekcji zmian
static int s_lastCLK = HIGH;
static int s_lastSWRaw = HIGH;

// Czasomierze i flagi dla przycisku
static unsigned long s_buttonPressStart = 0;
static bool s_buttonWasLongPress = false;
static unsigned long s_lastButtonAction = 0;

// Konfiguracja czasów
static unsigned long s_longPressMs = 1000;
static unsigned long s_debounceMs = 50;

void encoder_begin(uint8_t clkPin, uint8_t dtPin, uint8_t swPin,
                   unsigned long longPressMs, unsigned long debounceMs) {
  s_clkPin = clkPin;
  s_dtPin = dtPin;
  s_swPin = swPin;
  s_longPressMs = longPressMs;
  s_debounceMs = debounceMs;

  pinMode(s_clkPin, INPUT_PULLUP);
  pinMode(s_dtPin, INPUT_PULLUP);
  pinMode(s_swPin, INPUT_PULLUP);

  s_lastCLK = digitalRead(s_clkPin);
  s_lastSWRaw = digitalRead(s_swPin);
  s_buttonPressStart = 0;
  s_buttonWasLongPress = false;
  s_lastButtonAction = 0;
}

EncoderEvent encoder_update() {
  unsigned long now = millis();

  // Detekcja obrotu - zbocze opadające CLK
  int clk = digitalRead(s_clkPin);
  if (clk != s_lastCLK && clk == LOW) {
    int dt = digitalRead(s_dtPin);
    int dir = (dt != clk) ? 1 : -1;
    s_lastCLK = clk;
    return (dir == 1) ? ENC_RIGHT : ENC_LEFT;
  }
  s_lastCLK = clk;

  // Detekcja przycisku
  int swRaw = digitalRead(s_swPin);

  // Początek wciśnięcia (HIGH → LOW)
  if (s_lastSWRaw == HIGH && swRaw == LOW) {
    s_buttonPressStart = now;
    s_buttonWasLongPress = false;
  }

  // Długie kliknięcie
  if (swRaw == LOW && !s_buttonWasLongPress && s_buttonPressStart != 0) {
    if (now - s_buttonPressStart >= s_longPressMs &&
        now - s_lastButtonAction >= s_debounceMs) {
      s_buttonWasLongPress = true;
      s_lastButtonAction = now;
      s_lastSWRaw = swRaw;
      return ENC_LONG;
    }
  }

  // Krótkie kliknięcie - zwolnienie (LOW → HIGH)
  if (s_lastSWRaw == LOW && swRaw == HIGH) {
    if (!s_buttonWasLongPress && now - s_lastButtonAction >= s_debounceMs) {
      s_lastButtonAction = now;
      s_lastSWRaw = swRaw;
      return ENC_CLICK;
    }
  }

  s_lastSWRaw = swRaw;
  return ENC_NONE;
}