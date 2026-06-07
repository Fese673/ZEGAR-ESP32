#include "BootIntroService.h"

#include <cstring>

#include "LCDMirror.h"
#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace BootIntroService {
namespace {

enum class IntroPhase : uint8_t {
  Idle,
  Noise,
  AuthHold,
  FrameBuild,
  Reveal,
  FlashOn,
  FlashOff,
  SuccessReady,
  SuccessRiff,
  Done,
};

struct BootIntroState {
  IntroPhase phase = IntroPhase::Idle;
  unsigned long phaseStartedMs = 0;
  unsigned long lastNoiseMs = 0;
  unsigned long lastRevealMs = 0;
  unsigned long flashStartedMs = 0;
  unsigned long riffStartedMs = 0;
  uint8_t revealIndex = 0;
  uint8_t flashCount = 0;
  uint8_t riffIndex = 0;
  bool backlightOn = true;
};

BootIntroState s_state;
Callbacks s_callbacks{};
uint8_t s_buzzerPin = 0;

size_t boundedTextLength(const char* text, size_t maxLength) {
  if (text == nullptr) {
    return 0;
  }

  size_t length = 0;
  while (length < maxLength && text[length] != '\0') {
    ++length;
  }

  return length;
}

void printPaddedLine(uint8_t row, const char* text) {
  char line[21];
  const size_t length = boundedTextLength(text, 20);
  memset(line, ' ', 20);
  memcpy(line, text, length);
  line[20] = '\0';
  LCD_SET(0, row);
  LCD_PRINT(line);
}

void printCentered(uint8_t row, const char* text) {
  constexpr size_t width = 20;
  const size_t length = boundedTextLength(text, width);
  char line[21];
  memset(line, ' ', width);
  const size_t start = (width - length) / 2;
  memcpy(line + start, text, length);
  line[width] = '\0';
  LCD_SET(0, row);
  LCD_PRINT(line);
}

void printBorderLine(uint8_t row, char leftCorner, char rightCorner) {
  LCD_SET(0, row);
  LCD_PRINT(leftCorner);
  LCD_PRINT(F("=================="));
  LCD_PRINT(rightCorner);
}

void renderNoiseFrame() {
  LCD_CLEAR();
  for (uint8_t row = 0; row < 4; ++row) {
    char line[21];
    for (uint8_t col = 0; col < 20; ++col) {
      static const char noiseChars[] = {'.', ':', '*', '#', '@', ' ', '/'};
      line[col] = noiseChars[random(0, static_cast<int>(sizeof(noiseChars) / sizeof(noiseChars[0])))];
    }
    line[20] = '\0';
    LCD_SET(0, row);
    LCD_PRINT(line);
  }
  LCD_DUMP();
}

void renderAuthFrame() {
  LCD_CLEAR();
  printPaddedLine(0, "[ QUANTUM CORE OS ]");
  printPaddedLine(1, "  AUTH: D.  MELCER  ");
  printPaddedLine(2, "  AUTH: R. WOZNIAK  ");
  printPaddedLine(3, "VERIFYING CREDENTIAL");
  LCD_DUMP();
}

void renderTickingFrameBase() {
  LCD_CLEAR();
  printBorderLine(0, '.', '.');
  LCD_SET(0, 1);
  LCD_PRINT(F("|                  |"));
  LCD_SET(0, 2);
  LCD_PRINT(F("|                  |"));
  printBorderLine(3, '\'', '\'');
  LCD_DUMP();
}

void renderTickingFrameText(uint8_t revealCount) {
  static const char kTicking[] = "TICKING";
  static const char kBomb[] = "BOMB";
  char row1[19];
  char row2[19];
  memset(row1, ' ', 18);
  memset(row2, ' ', 18);
  row1[18] = '\0';
  row2[18] = '\0';

  const uint8_t tickingVisible = revealCount < 7 ? revealCount : 7;
  const uint8_t bombVisible = revealCount > 7 ? static_cast<uint8_t>((revealCount - 7) < 4 ? (revealCount - 7) : 4) : 0;
  const uint8_t tickingStart = (18 - 7) / 2;
  const uint8_t bombStart = (18 - 4) / 2;

  memcpy(row1 + tickingStart, kTicking, tickingVisible);
  memcpy(row2 + bombStart, kBomb, bombVisible);

  printBorderLine(0, '.', '.');
  LCD_SET(0, 1);
  LCD_PRINT(F("|"));
  LCD_PRINT(row1);
  LCD_PRINT(F("|"));
  LCD_SET(0, 2);
  LCD_PRINT(F("|"));
  LCD_PRINT(row2);
  LCD_PRINT(F("|"));
  printBorderLine(3, '\'', '\'');
  LCD_DUMP();
}

void backlightOn() {
  if (s_callbacks.backlightOn != nullptr) {
    s_callbacks.backlightOn();
  }
}

void backlightOff() {
  if (s_callbacks.backlightOff != nullptr) {
    s_callbacks.backlightOff();
  }
}

}  // namespace

void begin(const Callbacks& callbacks, uint8_t buzzerPin) {
  s_callbacks = callbacks;
  s_buzzerPin = buzzerPin;
}



bool isActive() {
  return s_state.phase != IntroPhase::Idle && s_state.phase != IntroPhase::Done;
}

static void introTask(void* param) {
  while (isActive()) {
    const unsigned long nowMs = millis();
    bool continueRunning = true;
    
    switch (s_state.phase) {
    case IntroPhase::Noise:
      if (s_state.lastNoiseMs == 0 || (nowMs - s_state.lastNoiseMs) >= 90UL) {
        renderNoiseFrame();
        s_state.lastNoiseMs = nowMs;
      }
      if ((nowMs - s_state.phaseStartedMs) >= 1800UL) {
        renderAuthFrame();
        tone(s_buzzerPin, 1760, 70);
        s_state.phase = IntroPhase::AuthHold;
        s_state.phaseStartedMs = nowMs;
      }
      break;

    case IntroPhase::AuthHold:
      if ((nowMs - s_state.phaseStartedMs) >= 3000UL) {
        s_state.phase = IntroPhase::FrameBuild;
        s_state.phaseStartedMs = nowMs;
      }
      break;

    case IntroPhase::FrameBuild:
      renderTickingFrameBase();
      tone(s_buzzerPin, 140, 120);
      s_state.revealIndex = 0;
      s_state.phase = IntroPhase::Reveal;
      s_state.phaseStartedMs = nowMs;
      s_state.lastRevealMs = nowMs;
      break;

    case IntroPhase::Reveal:
      if ((nowMs - s_state.lastRevealMs) >= 200UL) {
        ++s_state.revealIndex;
        renderTickingFrameText(s_state.revealIndex);
        tone(s_buzzerPin, static_cast<uint16_t>(145 + (s_state.revealIndex * 14U)), 160);
        s_state.lastRevealMs = nowMs;
      }
      if (s_state.revealIndex >= 11) {
        s_state.phase = IntroPhase::FlashOn;
        s_state.flashStartedMs = nowMs;
        s_state.flashCount = 0;
        s_state.backlightOn = true;
        renderTickingFrameText(11);
        backlightOn();
      }
      break;

    case IntroPhase::FlashOn:
      if (!s_state.backlightOn) {
        backlightOn();
        s_state.backlightOn = true;
      }
      if ((nowMs - s_state.flashStartedMs) >= 110UL) {
        s_state.phase = IntroPhase::FlashOff;
        s_state.flashStartedMs = nowMs;
      }
      break;

    case IntroPhase::FlashOff:
      if (s_state.backlightOn) {
        backlightOff();
        s_state.backlightOn = false;
      }
      if ((nowMs - s_state.flashStartedMs) >= 90UL) {
        ++s_state.flashCount;
        if (s_state.flashCount >= 3) {
          backlightOn();
          s_state.backlightOn = true;
          printBorderLine(0, '.', '.');
          printCentered(1, "SYSTEM READY");
          printPaddedLine(2, "                  ");
          printBorderLine(3, '\'', '\'');
          LCD_DUMP();
          tone(s_buzzerPin, 523, 90);
          s_state.phase = IntroPhase::SuccessReady;
          s_state.phaseStartedMs = nowMs;
        } else {
          s_state.phase = IntroPhase::FlashOn;
          s_state.flashStartedMs = nowMs;
        }
      }
      break;

    case IntroPhase::SuccessReady:
      if ((nowMs - s_state.phaseStartedMs) >= 300UL) {
        s_state.phase = IntroPhase::SuccessRiff;
        s_state.riffStartedMs = nowMs;
        s_state.riffIndex = 0;
      }
      break;

    case IntroPhase::SuccessRiff: {
      static const uint16_t kNotes[] = {523, 659, 784, 1047, 1319};
      static const uint16_t kDurationsMs[] = {120, 100, 95, 85, 180};

      if (s_state.riffIndex == 0) {
        tone(s_buzzerPin, kNotes[0], kDurationsMs[0]);
        s_state.riffStartedMs = nowMs;
        ++s_state.riffIndex;
      } else if (s_state.riffIndex < 5 && (nowMs - s_state.riffStartedMs) >= kDurationsMs[s_state.riffIndex - 1]) {
        tone(s_buzzerPin, kNotes[s_state.riffIndex], kDurationsMs[s_state.riffIndex]);
        s_state.riffStartedMs = nowMs;
        ++s_state.riffIndex;
      } else if (s_state.riffIndex >= 5 && (nowMs - s_state.riffStartedMs) >= 220UL) {
        noTone(s_buzzerPin);
        s_state.phase = IntroPhase::Done;
      }
      break;
    }

    case IntroPhase::Done:
    case IntroPhase::Idle:
    default:
      continueRunning = false;
      break;
    }

    if (!continueRunning) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  vTaskDelete(nullptr);
}

void start() {
  randomSeed(static_cast<uint32_t>(micros()));
  s_state = BootIntroState{};
  s_state.phase = IntroPhase::Noise;
  s_state.phaseStartedMs = millis();
  s_state.backlightOn = true;

  xTaskCreatePinnedToCore(introTask,
                          "bootIntro",
                          4096,
                          nullptr,
                          15, // High priority to avoid UI freezes
                          nullptr,
                          1); // Core 1
}

bool service() {
  return isActive();
}

}  // namespace BootIntroService
