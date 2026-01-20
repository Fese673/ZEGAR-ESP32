#include "Encoder.h"

// Piny enkodera
static uint8_t s_clkPin = 255;
static uint8_t s_dtPin = 255;
static uint8_t s_swPin = 255;

// ========== ROTACJA - ZMIENNE ORYGINALNE ==========
static int s_lastCLK = HIGH;

// ========== ROTACJA - NOWE ZMIENNE DLA GRAY-CODE ==========
// State machine dla validacji sekwencji
static uint8_t s_encoderState = 0;          // Obecny stan pinów (2 bity: CLK|DT)
static uint8_t s_lastEncoderState = 3;      // Poprzedni stan (3 = 0b11 = HIGH|HIGH)
static uint8_t s_sequenceStep = 0;          // Krok w sekwencji (0-3)
static int s_sequenceDirection = 0;         // 1=CW, -1=CCW, 0=idle
static unsigned long s_sequenceTimeout = 0; // Timeout dla sekwencji

// Sekwencje Gray-code (dokładnie jak w KY040)
static const uint8_t s_signalSequenceCW[4] = {0b01, 0b00, 0b10, 0b11};
static const uint8_t s_signalSequenceCCW[4] = {0b10, 0b00, 0b01, 0b11};

// ========== PRZYCISK - ZMIENNE BEZ ZMIAN ==========
// Zapamiętane stany dla detekcji zmian
static int s_lastSWRaw = HIGH;

// Czasomierze i flagi dla przycisku
static unsigned long s_buttonPressStart = 0;
static bool s_buttonWasLongPress = false;
static unsigned long s_lastButtonAction = 0;

// Konfiguracja czasów
static unsigned long s_longPressMs = 1000;
static unsigned long s_debounceMs = 200;
static unsigned long s_longPressCooldown = 0;  // ← NOWE: ochrona przed powtarzalnością
static const unsigned long LONG_PRESS_HOLD_TIME = 500;  // ms - wydłużone na 500ms dla większej ochrony
static const unsigned long POST_LONG_PRESS_COOLDOWN = 800; // ms - blokada po długim wciśnięciu

// ========== INICJALIZACJA ==========
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
  
  // Inicjalizuj stan rotacji
  uint8_t clk = digitalRead(s_clkPin) ? 1 : 0;
  uint8_t dt = digitalRead(s_dtPin) ? 1 : 0;
  s_encoderState = (clk << 1) | dt;
  s_lastEncoderState = s_encoderState;
  s_sequenceStep = 0;
  s_sequenceDirection = 0;
  s_sequenceTimeout = 0;
  
  s_lastSWRaw = digitalRead(s_swPin);
  s_buttonPressStart = 0;
  s_buttonWasLongPress = false;
  s_lastButtonAction = 0;
  s_longPressCooldown = 0;
}

// ========== OBSŁUGA ROTACJI Z GRAY-CODE (ULEPSZONA) ==========
static EncoderEvent rotationCheck() {
  unsigned long now = millis();
  
  // 1. Odczyt aktualnego stanu pinów (2 bity: CLK|DT)
  uint8_t clk = digitalRead(s_clkPin) ? 1 : 0;  // Bit 1
  uint8_t dt = digitalRead(s_dtPin) ? 1 : 0;     // Bit 0
  uint8_t newState = (clk << 1) | dt;            // Połącz do 2 bitów

  // 2. Czy jest zmiana stanu?
  if (newState == s_lastEncoderState) {
    return ENC_NONE;  // Bez zmian
  }

  // 3. WALIDACJA: Odrzuć skok dwóch bitów naraz (odbicie styków!)
  uint8_t diff = s_lastEncoderState ^ newState;  // XOR
  if (diff == 0x03) {  // 0b11 = dwa bity na raz
    return ENC_NONE;   // ODRZUĆ - to odbicie!
  }

  // 4. Zapamiętaj nowy stan
  s_lastEncoderState = newState;
  s_encoderState = newState;

  // 5. TIMEOUT: Jeśli sekwencja trwa >100ms, zresetuj (szum/problem)
  if (s_sequenceStep > 0 && (now - s_sequenceTimeout) > 100) {
    s_sequenceStep = 0;
    s_sequenceDirection = 0;
  }

  // 6. STATE MACHINE - Walidacja pełnej sekwencji
  
  // 6a. Czy to POCZĄTEK nowej sekwencji?
  if (s_sequenceStep == 0) {
    // Dopuść trochę elastyczności: zaakceptuj też 11→01 i 11→10
    if (newState == s_signalSequenceCW[0] || (s_lastEncoderState == 0b11 && newState == 0b01)) {
      s_sequenceDirection = 1;  // CW
      s_sequenceStep = 1;
      s_sequenceTimeout = now;
      return ENC_NONE;
    }
    if (newState == s_signalSequenceCCW[0] || (s_lastEncoderState == 0b11 && newState == 0b10)) {
      s_sequenceDirection = -1;  // CCW
      s_sequenceStep = 1;
      s_sequenceTimeout = now;
      return ENC_NONE;
    }
    return ENC_NONE;
  }

  // 6b. Czy to NASTĘPNY KROK w sekwencji CW?
  if (s_sequenceDirection == 1) {
    if (newState == s_signalSequenceCW[s_sequenceStep]) {
      s_sequenceStep++;
      if (s_sequenceStep >= 4) {  // Sekwencja UKOŃCZONA!
        s_sequenceStep = 0;
        s_sequenceDirection = 0;
        return ENC_RIGHT;  // ✅ Pełny krok clockwise
      }
      s_sequenceTimeout = now;  // Aktualizuj timeout
      return ENC_NONE;
    }
    // Jeśli nie pasuje, ale wróciło do 11, zresetuj (koniec sekwencji)
    if (newState == 0b11) {
      s_sequenceStep = 0;
      s_sequenceDirection = 0;
      return ENC_NONE;
    }
    // Jeśli wciąż do sekwencji, czekaj
    return ENC_NONE;
  }

  // 6c. Czy to NASTĘPNY KROK w sekwencji CCW?
  if (s_sequenceDirection == -1) {
    if (newState == s_signalSequenceCCW[s_sequenceStep]) {
      s_sequenceStep++;
      if (s_sequenceStep >= 4) {  // Sekwencja UKOŃCZONA!
        s_sequenceStep = 0;
        s_sequenceDirection = 0;
        return ENC_LEFT;  // ✅ Pełny krok counter-clockwise
      }
      s_sequenceTimeout = now;  // Aktualizuj timeout
      return ENC_NONE;
    }
    // Jeśli nie pasuje, ale wróciło do 11, zresetuj
    if (newState == 0b11) {
      s_sequenceStep = 0;
      s_sequenceDirection = 0;
      return ENC_NONE;
    }
    // Jeśli wciąż do sekwencji, czekaj
    return ENC_NONE;
  }

  // 7. Fallback - reset
  if (newState == 0b11) {
    s_sequenceStep = 0;
    s_sequenceDirection = 0;
  }
  
  return ENC_NONE;
}

// ========== OBSŁUGA PRZYCISKU - ZMIANY: DODANO POST_LONG_PRESS_COOLDOWN ==========
static EncoderEvent buttonCheck(unsigned long now) {
  int swRaw = digitalRead(s_swPin);

  // Początek wciśnięcia (HIGH → LOW)
  if (s_lastSWRaw == HIGH && swRaw == LOW) {
    s_buttonPressStart = now;
    s_buttonWasLongPress = false;
  }

  // Długie kliknięcie - zwiększony debounce
  if (swRaw == LOW && !s_buttonWasLongPress && s_buttonPressStart != 0) {
    if (now - s_buttonPressStart >= s_longPressMs &&
        now - s_lastButtonAction >= s_debounceMs) {
      s_buttonWasLongPress = true;
      s_lastButtonAction = now;
      s_longPressCooldown = now;  // ← Zapamiętaj moment ENC_LONG
      s_lastSWRaw = swRaw;
      return ENC_LONG;  // Zwróć event TYLKO RAZ
    }
  }

  // Krótkie kliknięcie - zwolnienie (LOW → HIGH)
  if (s_lastSWRaw == LOW && swRaw == HIGH) {

    // 1) Jeśli JESTEŚMY jeszcze w cooldownie po długim przycisku – ignoruj klik
    if (s_longPressCooldown != 0 &&
        (now - s_longPressCooldown) < POST_LONG_PRESS_COOLDOWN) {
      // tylko zaktualizuj s_lastSWRaw i wyjdź bez eventu
      s_lastSWRaw = swRaw;
      return ENC_NONE;
    }

    // 2) Normalna obsługa zwykłego kliknięcia
    if (!s_buttonWasLongPress && now - s_lastButtonAction >= s_debounceMs) {
      s_lastButtonAction = now;
      s_lastSWRaw = swRaw;
      return ENC_CLICK;
    }

    // Zwolnienie po długim przyciskaniu - resetuj stan (jeśli minął hold time)
    if (s_buttonWasLongPress && now - s_longPressCooldown > LONG_PRESS_HOLD_TIME) {
      s_buttonWasLongPress = false;
    }
  }

  // Jeśli minął czas cooldownu – wyzeruj (porządek w zmiennych)
  if (s_longPressCooldown != 0 &&
      (now - s_longPressCooldown) > (POST_LONG_PRESS_COOLDOWN + 50)) {
    s_longPressCooldown = 0;
  }

  s_lastSWRaw = swRaw;
  return ENC_NONE;
}

// ========== GŁÓWNA FUNKCJA UPDATE ==========
EncoderEvent encoder_update() {
  unsigned long now = millis();

  // Najpierw sprawdź rotację (Gray-code validacja)
  EncoderEvent rotationEvent = rotationCheck();
  if (rotationEvent != ENC_NONE) {
    return rotationEvent;  // Zwróć event rotacji
  }

  // Potem sprawdź przycisk
  EncoderEvent buttonEvent = buttonCheck(now);
  if (buttonEvent != ENC_NONE) {
    return buttonEvent;  // Zwróć event przycisku
  }

  return ENC_NONE;  // Nic się nie działo
}
