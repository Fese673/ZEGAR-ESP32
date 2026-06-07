#include "Encoder.h"

#include <atomic>

#include "Task_Config.h"
#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#endif

#include "AppLog.h"
#include "RuntimeTelemetry.h"

namespace {

constexpr char TAG[] = "ENCODER";

}  // namespace

// ============================================================================
// KONFIGURACJA PINÓW
// ============================================================================
static uint8_t s_clkPin = 255;
static uint8_t s_dtPin  = 255;
static uint8_t s_swPin  = 255;

// ============================================================================
// ROTACJA - ZMIENNE DLA GRAY-CODE STATE MACHINE
// ============================================================================
static uint8_t s_encoderState     = 0;    // Obecny stan pinów (2 bity: CLK|DT)
static uint8_t s_lastEncoderState = 0b11; // Poprzedni stan (0b11 = HIGH|HIGH)
static uint8_t s_sequenceStep     = 0;    // Krok w sekwencji (0-3)
static int8_t  s_sequenceDirection = 0;   // 1=CW, -1=CCW, 0=idle
static unsigned long s_sequenceTimeout = 0; // Timeout dla sekwencji

// Sekwencje Gray-code (pełna rotacja = 4 kroki)
static const uint8_t GRAY_SEQUENCE_CW[4]  = {0b01, 0b00, 0b10, 0b11};
static const uint8_t GRAY_SEQUENCE_CCW[4] = {0b10, 0b00, 0b01, 0b11};

// Stałe czasowe dla rotacji
static const unsigned long SEQUENCE_TIMEOUT_MS = 100; // Reset sekwencji po 100ms

// ============================================================================
// PRZYCISK - ZMIENNE STANU
// ============================================================================
static int  s_lastSWRaw          = HIGH;
static std::atomic<unsigned long> s_buttonPressStart{0};
static bool s_buttonWasLongPress = false;
static unsigned long s_lastButtonAction  = 0;
static unsigned long s_longPressCooldown = 0; // Ochrona przed powtarzalnością

// Konfiguracja czasów (modyfikowalne przez encoder_begin)
static unsigned long s_longPressMs = 1000;
static unsigned long s_debounceMs  = 200;

#ifdef ARDUINO_ARCH_ESP32
static QueueHandle_t s_eventQueue = nullptr;
static TaskHandle_t s_encoderTaskHandle = nullptr;
static constexpr uint8_t ENCODER_QUEUE_LEN = 64;
static constexpr uint32_t ENCODER_TASK_DELAY_MS = 1;
#endif

// Stałe czasowe dla przycisku
static const unsigned long LONG_PRESS_HOLD_TIME      = 500;  // ms - czas trzymania dla ochrony
static const unsigned long POST_LONG_PRESS_COOLDOWN  = 1000; // ms - blokada po długim wciśnięciu

static EncoderEvent encoderSampleOnce();

#ifdef ARDUINO_ARCH_ESP32
static void encoderTask(void* /*param*/) {
  for (;;) {
    const EncoderEvent evt = encoderSampleOnce();
    if (evt != ENC_NONE && s_eventQueue != nullptr) {
      if (xQueueSendToBack(s_eventQueue, &evt, 0) != pdTRUE) {
        TELEMETRY_INC(encoder_drops);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(ENCODER_TASK_DELAY_MS));
  }
}
#endif

// ============================================================================
// INICJALIZACJA ENKODERA
// ============================================================================
void encoder_begin(uint8_t clkPin, uint8_t dtPin, uint8_t swPin,
                   unsigned long longPressMs, unsigned long debounceMs) {
  // Zapisz konfigurację pinów
  s_clkPin     = clkPin;
  s_dtPin      = dtPin;
  s_swPin      = swPin;
  s_longPressMs = longPressMs;
  s_debounceMs  = debounceMs;

  // Konfiguruj piny z wewnętrznym pull-up
  pinMode(s_clkPin, INPUT_PULLUP);
  pinMode(s_dtPin,  INPUT_PULLUP);
  pinMode(s_swPin,  INPUT_PULLUP);

  // Inicjalizuj stan rotacji (Gray-code)
  const uint8_t clk = digitalRead(s_clkPin) ? 1 : 0;
  const uint8_t dt  = digitalRead(s_dtPin)  ? 1 : 0;
  s_encoderState     = (clk << 1) | dt;
  s_lastEncoderState = s_encoderState;
  s_sequenceStep     = 0;
  s_sequenceDirection = 0;
  s_sequenceTimeout  = 0;

  // Inicjalizuj stan przycisku
  s_lastSWRaw         = digitalRead(s_swPin);
    s_buttonPressStart.store(0, std::memory_order_relaxed);
  s_buttonWasLongPress = false;
  s_lastButtonAction  = 0;
  s_longPressCooldown = 0;

#ifdef ARDUINO_ARCH_ESP32
  if (s_eventQueue == nullptr) {
    s_eventQueue = xQueueCreate(ENCODER_QUEUE_LEN, sizeof(EncoderEvent));
  }

  if (s_encoderTaskHandle == nullptr && s_eventQueue != nullptr) {
    xTaskCreatePinnedToCore(
        encoderTask,
        "encoderTask",
        TaskConfig::EncoderTask::kStackBytes,
        nullptr,
        TaskConfig::EncoderTask::kPriority,
        &s_encoderTaskHandle,
        TaskConfig::EncoderTask::kCore);
  }
#endif
}

// ============================================================================
// PRZYWRÓCENIE PINÓW ENKODERA (GPIO 25/26) - BEZPIECZEŃSTWO
// ============================================================================
// Funkcja bezpieczeństwa przywracająca INPUT_PULLUP na pinach enkodera.
// Konflikt I2S/Encoder ROZWIĄZANY - I2S teraz używa GPIO 33/32 zamiast 25/26.
void encoder_reinit_pins() {
  if (s_clkPin != 255) {
#ifdef ARDUINO_ARCH_ESP32
    if (s_encoderTaskHandle != nullptr) {
      vTaskSuspend(s_encoderTaskHandle);
    }
#endif

    pinMode(s_clkPin, INPUT_PULLUP);
    pinMode(s_dtPin,  INPUT_PULLUP);
    pinMode(s_swPin,  INPUT_PULLUP);

    // Odczytaj aktualny stan po przywróceniu pull-upów
    const uint8_t clk = digitalRead(s_clkPin) ? 1 : 0;
    const uint8_t dt  = digitalRead(s_dtPin)  ? 1 : 0;
    s_encoderState     = (clk << 1) | dt;
    s_lastEncoderState = s_encoderState;
    s_sequenceStep     = 0;
    s_sequenceDirection = 0;

#ifdef ARDUINO_ARCH_ESP32
    if (s_encoderTaskHandle != nullptr) {
      vTaskResume(s_encoderTaskHandle);
    }
#endif

    LOG_I(TAG, "Pins restored mode=INPUT_PULLUP");
  }
}

#ifdef ARDUINO_ARCH_ESP32
TaskHandle_t encoder_getTaskHandle() {
  return s_encoderTaskHandle;
}
#endif

// ============================================================================
// OBSŁUGA ROTACJI - GRAY-CODE STATE MACHINE
// ============================================================================
static EncoderEvent rotationCheck() {
  const unsigned long now = millis();

  // 1. Odczyt aktualnego stanu pinów (2 bity: CLK|DT)
  const uint8_t clk      = digitalRead(s_clkPin) ? 1 : 0;  // Bit 1
  const uint8_t dt       = digitalRead(s_dtPin)  ? 1 : 0;  // Bit 0
  const uint8_t newState = (clk << 1) | dt;                // Połącz do 2 bitów

  // 2. Sprawdź czy jest zmiana stanu
  if (newState == s_lastEncoderState) {
    return ENC_NONE;  // Bez zmian
  }

  // 3. WALIDACJA: Odrzuć skok dwóch bitów naraz (odbicie styków!)
  const uint8_t diff = s_lastEncoderState ^ newState;  // XOR
  if (diff == 0b11) {
    return ENC_NONE;  // ODRZUĆ - to odbicie!
  }

  // 4. Zapamiętaj nowy stan
  s_lastEncoderState = newState;
  s_encoderState     = newState;

  // 5. TIMEOUT: Jeśli sekwencja trwa zbyt długo, zresetuj (szum/problem)
  if (s_sequenceStep > 0 && (now - s_sequenceTimeout) > SEQUENCE_TIMEOUT_MS) {
    s_sequenceStep      = 0;
    s_sequenceDirection = 0;
  }

  // 6. STATE MACHINE - Walidacja pełnej sekwencji Gray-code

  // 6a. POCZĄTEK nowej sekwencji
  if (s_sequenceStep == 0) {
    // Zaakceptuj początek sekwencji CW (11→01)
    if (newState == GRAY_SEQUENCE_CW[0]) {
      s_sequenceDirection = 1;  // CW
      s_sequenceStep      = 1;
      s_sequenceTimeout   = now;
      return ENC_NONE;
    }
    // Zaakceptuj początek sekwencji CCW (11→10)
    if (newState == GRAY_SEQUENCE_CCW[0]) {
      s_sequenceDirection = -1;  // CCW
      s_sequenceStep      = 1;
      s_sequenceTimeout   = now;
      return ENC_NONE;
    }
    return ENC_NONE;
  }

  // 6b. NASTĘPNY KROK w sekwencji CW
  if (s_sequenceDirection == 1) {
    if (newState == GRAY_SEQUENCE_CW[s_sequenceStep]) {
      s_sequenceStep++;
      if (s_sequenceStep >= 4) {  // Sekwencja UKOŃCZONA!
        s_sequenceStep      = 0;
        s_sequenceDirection = 0;
        return ENC_RIGHT;  // Pełny krok clockwise
      }
      s_sequenceTimeout = now;
      return ENC_NONE;
    }
    // Reset jeśli powrót do stanu spoczynkowego
    if (newState == 0b11) {
      s_sequenceStep      = 0;
      s_sequenceDirection = 0;
    }
    return ENC_NONE;
  }

  // 6c. NASTĘPNY KROK w sekwencji CCW
  if (s_sequenceDirection == -1) {
    if (newState == GRAY_SEQUENCE_CCW[s_sequenceStep]) {
      s_sequenceStep++;
      if (s_sequenceStep >= 4) {  // Sekwencja UKOŃCZONA!
        s_sequenceStep      = 0;
        s_sequenceDirection = 0;
        return ENC_LEFT;  // Pełny krok counter-clockwise
      }
      s_sequenceTimeout = now;
      return ENC_NONE;
    }
    // Reset jeśli powrót do stanu spoczynkowego
    if (newState == 0b11) {
      s_sequenceStep      = 0;
      s_sequenceDirection = 0;
    }
    return ENC_NONE;
  }

  // 7. Fallback - reset przy stanie spoczynkowym
  if (newState == 0b11) {
    s_sequenceStep      = 0;
    s_sequenceDirection = 0;
  }

  return ENC_NONE;
}

// ============================================================================
// OBSŁUGA PRZYCISKU - Z OCHRONĄ PRZED POWTARZALNOŚCIĄ
// ============================================================================
static EncoderEvent buttonCheck(const unsigned long now) {
  const int swRaw = digitalRead(s_swPin);

  // Początek wciśnięcia (HIGH → LOW)
  if (s_lastSWRaw == HIGH && swRaw == LOW) {
    s_buttonPressStart.store(now, std::memory_order_relaxed);
    s_buttonWasLongPress = false;
  }

  // Detekcja długiego kliknięcia (przycisk wciąż wciśnięty)
  if (swRaw == LOW && !s_buttonWasLongPress && s_buttonPressStart.load(std::memory_order_relaxed) != 0) {
    const bool longPressReached = (now - s_buttonPressStart.load(std::memory_order_relaxed)) >= s_longPressMs;
    const bool debounceOk       = (now - s_lastButtonAction) >= s_debounceMs;

    if (longPressReached && debounceOk) {
      s_buttonWasLongPress = true;
      s_lastButtonAction   = now;
      s_longPressCooldown  = now;  // Zapamiętaj moment ENC_LONG
      s_lastSWRaw = swRaw;
      return ENC_LONG;  // Zwróć event TYLKO RAZ
    }
  }

  // Zwolnienie przycisku (LOW → HIGH)
  if (s_lastSWRaw == LOW && swRaw == HIGH) {
    // 1) Ignoruj klik jeśli jesteśmy w cooldownie po długim przycisku
    if (s_longPressCooldown != 0 &&
        (now - s_longPressCooldown) < POST_LONG_PRESS_COOLDOWN) {
      s_lastSWRaw = swRaw;
      return ENC_NONE;
    }

    // 2) Normalna obsługa krótkiego kliknięcia
    const bool wasShortPress = !s_buttonWasLongPress;
    const bool debounceOk    = (now - s_lastButtonAction) >= s_debounceMs;

    if (wasShortPress && debounceOk) {
      s_lastButtonAction = now;
      s_lastSWRaw = swRaw;
      return ENC_CLICK;
    }

    // 3) Reset stanu po długim przyciskaniu (jeśli minął hold time)
    if (s_buttonWasLongPress && (now - s_longPressCooldown) > LONG_PRESS_HOLD_TIME) {
      s_buttonWasLongPress = false;
    }
  }

  // Wyzeruj cooldown po upływie czasu (porządek w zmiennych)
  if (s_longPressCooldown != 0 &&
      (now - s_longPressCooldown) > (POST_LONG_PRESS_COOLDOWN + 50)) {
    s_longPressCooldown = 0;
  }

  s_lastSWRaw = swRaw;
  return ENC_NONE;
}

// ============================================================================
// GŁÓWNA FUNKCJA UPDATE - WYWOŁYWAĆ W LOOP()
// ============================================================================
static EncoderEvent encoderSampleOnce() {
  const unsigned long now = millis();

  // Najpierw sprawdź rotację (Gray-code validacja)
  const EncoderEvent rotationEvent = rotationCheck();
  if (rotationEvent != ENC_NONE) {
    return rotationEvent;
  }

  // Potem sprawdź przycisk
  const EncoderEvent buttonEvent = buttonCheck(now);
  if (buttonEvent != ENC_NONE) {
    return buttonEvent;
  }

  return ENC_NONE;
}

EncoderEvent encoder_update() {
#ifdef ARDUINO_ARCH_ESP32
  if (s_eventQueue != nullptr) {
    EncoderEvent evt = ENC_NONE;
    if (xQueueReceive(s_eventQueue, &evt, 0) == pdTRUE) {
      return evt;
    }
    return ENC_NONE;
  }
#endif

  return encoderSampleOnce();
}

unsigned long encoder_button_hold_ms() {
  if (s_swPin == 255) {
    return 0;
  }

  const unsigned long pressStart = s_buttonPressStart.load(std::memory_order_relaxed);
  if (digitalRead(s_swPin) != LOW || pressStart == 0) {
    return 0;
  }

  return millis() - pressStart;
}
