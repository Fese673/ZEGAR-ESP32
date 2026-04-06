#include "SafeCracker.h"

#include <cstring>

#include "AppRuntime.h"
#include "LCDMirror.h"

namespace SafeCracker {
namespace {

constexpr uint8_t kDialMax = 100;
constexpr uint8_t kStageCount = 3;
constexpr unsigned long kRunningFrameMs = 110UL;
constexpr unsigned long kTerminalFrameMs = 80UL;
constexpr unsigned long kLockFlashMs = 220UL;
constexpr unsigned long kSuccessHoldMs = 1200UL;
constexpr unsigned long kFailureHoldMs = 850UL;
constexpr unsigned long kBlinkMs = 180UL;

enum class Mode : uint8_t {
  Idle,
  Running,
  SuccessHold,
  FailureHold,
};

struct State {
  Mode mode = Mode::Idle;
  uint8_t targets[kStageCount] = {0, 0, 0};
  bool revealed[kStageCount] = {false, false, false};
  uint8_t currentValue = 0;
  uint8_t stage = 0;
  uint8_t scanOffset = 0;
  bool blinkVisible = true;
  int8_t lastRotateDir = 0;
  unsigned long lastFrameMs = 0;
  unsigned long lastBlinkMs = 0;
  unsigned long lastBuzzMs = 0;
  unsigned long lockFlashUntilMs = 0;
  unsigned long terminalUntilMs = 0;
};

State s_state;

static void padLine(char* out, const char* text) {
  memset(out, ' ', LCD_COLS);
  out[LCD_COLS] = '\0';

  if (text == nullptr) {
    return;
  }

  const size_t len = strlen(text);
  const size_t copyLen = (len > LCD_COLS) ? LCD_COLS : len;
  memcpy(out, text, copyLen);
}

static void writeLine(uint8_t row, const char* text) {
  char line[LCD_COLS + 1];
  padLine(line, text);
  LCD_SET(0, row);
  LCD_PRINT(line);
}

static uint8_t wrapDial(int value) {
  while (value < 0) {
    value += kDialMax;
  }
  while (value >= static_cast<int>(kDialMax)) {
    value -= kDialMax;
  }
  return static_cast<uint8_t>(value);
}

static uint8_t circularDistance(uint8_t lhs, uint8_t rhs) {
  const uint8_t raw = (lhs > rhs) ? (lhs - rhs) : (rhs - lhs);
  return (raw > 50U) ? static_cast<uint8_t>(kDialMax - raw) : raw;
}

static uint8_t activeTarget() {
  const uint8_t index = (s_state.stage >= kStageCount) ? (kStageCount - 1) : s_state.stage;
  return s_state.targets[index];
}

static void generateCombination() {
  const uint8_t first = static_cast<uint8_t>(random(10, 90));
  const bool secondHigh = first < 50;
  const bool thirdHigh = !secondHigh;

  s_state.targets[0] = first;
  s_state.targets[1] = static_cast<uint8_t>(secondHigh ? random(55, 95) : random(5, 45));
  s_state.targets[2] = static_cast<uint8_t>(thirdHigh ? random(55, 95) : random(5, 45));
}

static void startFailure(unsigned long nowMs) {
  s_state.mode = Mode::FailureHold;
  s_state.terminalUntilMs = nowMs + kFailureHoldMs;
  noTone(BUZZER_PIN);
  tone(BUZZER_PIN, 110, 240);
  s_state.lastFrameMs = 0;
}

static void startSuccess(unsigned long nowMs) {
  s_state.mode = Mode::SuccessHold;
  s_state.terminalUntilMs = nowMs + kSuccessHoldMs;
  noTone(BUZZER_PIN);
  tone(BUZZER_PIN, 1760, 110);
  s_state.lastFrameMs = 0;
}

static void updateBuzzer(unsigned long nowMs, uint8_t distance) {
  if (distance == 0) {
    noTone(BUZZER_PIN);
    return;
  }

  unsigned long gapMs = 320UL;
  uint16_t freq = 360U;

  if (distance <= 3U) {
    gapMs = 70UL;
    freq = 1800U;
  } else if (distance <= 8U) {
    gapMs = 100UL;
    freq = 1300U;
  } else if (distance <= 20U) {
    gapMs = 150UL;
    freq = 900U;
  } else if (distance <= 35U) {
    gapMs = 220UL;
    freq = 620U;
  }

  if (nowMs - s_state.lastBuzzMs >= gapMs) {
    tone(BUZZER_PIN, freq, 35);
    s_state.lastBuzzMs = nowMs;
  }
}

static void buildHeader(char* out) {
  memset(out, ' ', LCD_COLS);
  out[LCD_COLS] = '\0';
  out[0] = '[';
  out[LCD_COLS - 1] = ']';

  const char title[] = "SAFE V.2.1";
  constexpr uint8_t kTitleLen = sizeof(title) - 1;
  constexpr uint8_t kTitleStart = 5;
  memcpy(out + kTitleStart, title, kTitleLen);

  out[1] = '=';
  out[2] = '=';
  out[15] = '=';
  out[16] = '=';

  const uint8_t scanPos = static_cast<uint8_t>(1 + (s_state.scanOffset % 18U));
  out[scanPos] = '#';
}

static void buildTargetLine(char* out) {
  char slot0[3] = {0};
  char slot1[3] = {0};
  char slot2[3] = {0};

  for (uint8_t i = 0; i < kStageCount; ++i) {
    char* slot = (i == 0) ? slot0 : (i == 1) ? slot1 : slot2;
    if (s_state.revealed[i]) {
      snprintf(slot, 3, "%02u", static_cast<unsigned>(s_state.targets[i]));
    } else if (i == s_state.stage && !s_state.blinkVisible) {
      slot[0] = ' ';
      slot[1] = ' ';
      slot[2] = '\0';
    } else {
      slot[0] = '?';
      slot[1] = '?';
      slot[2] = '\0';
    }
  }

  snprintf(out,
           LCD_COLS + 1,
           "Target:  %s  %s  %s",
           slot0,
           slot1,
           slot2);
}

static void buildCurrentLine(char* out) {
  snprintf(out,
           LCD_COLS + 1,
           "Current: > %02u <",
           static_cast<unsigned>(s_state.currentValue));
}

static void buildStatusLine(char* out, unsigned long nowMs) {
  const char* status = nullptr;

  if (s_state.mode == Mode::FailureHold) {
    status = "SYSTEM BREACH";
  } else if (s_state.mode == Mode::SuccessHold || s_state.stage >= kStageCount) {
    status = "LOCKED!";
  } else if (nowMs < s_state.lockFlashUntilMs) {
    status = "LOCKED!";
  } else if (s_state.currentValue == activeTarget()) {
    status = "LOCKED!";
  } else if (s_state.currentValue < activeTarget()) {
    status = "TOO LOW!";
  } else {
    status = "TOO HIGH!";
  }

  snprintf(out, LCD_COLS + 1, "Status:  %s", status);
}

static void buildRunningScreen() {
  char line[LCD_COLS + 1];

  buildHeader(line);
  writeLine(0, line);

  buildTargetLine(line);
  writeLine(1, line);

  buildCurrentLine(line);
  writeLine(2, line);

  buildStatusLine(line, millis());
  writeLine(3, line);

  LCD_DUMP();
}

static void buildFailureScreen() {
  const char noiseChars[] = "!#@*/.:?";
  char line[LCD_COLS + 1];

  writeLine(0, "!! SYSTEM BREACH !!");

  for (uint8_t row = 1; row <= 2; ++row) {
    for (uint8_t col = 0; col < LCD_COLS; ++col) {
      line[col] = noiseChars[random(0, static_cast<int>(sizeof(noiseChars) - 1))];
    }
    line[LCD_COLS] = '\0';
    writeLine(row, line);
  }

  writeLine(3, "!! ACCESS DENIED !!");
  LCD_DUMP();
}

static void enterRunningFrame() {
  s_state.scanOffset = static_cast<uint8_t>((s_state.scanOffset + 1U) % 18U);
  s_state.blinkVisible = !s_state.blinkVisible;
}

}  // namespace

void begin() {
  randomSeed(static_cast<uint32_t>(micros()));
  s_state = State{};
  generateCombination();
  s_state.currentValue = static_cast<uint8_t>(random(0, 100));
  if (s_state.currentValue == s_state.targets[0]) {
    s_state.currentValue = wrapDial(static_cast<int>(s_state.currentValue) + 17);
  }
  s_state.mode = Mode::Running;
  s_state.lastFrameMs = 0;
  s_state.lastBlinkMs = millis();
  s_state.lastBuzzMs = 0;
  s_state.lockFlashUntilMs = 0;
  s_state.terminalUntilMs = 0;
  noTone(BUZZER_PIN);
  draw();
}

void stop() {
  s_state.mode = Mode::Idle;
  s_state.terminalUntilMs = 0;
  s_state.lockFlashUntilMs = 0;
  noTone(BUZZER_PIN);
}

void handleEvent(EncoderEvent event) {
  if (s_state.mode != Mode::Running) {
    return;
  }

  const unsigned long nowMs = millis();

  if (event == ENC_LEFT || event == ENC_RIGHT) {
    const int dir = (event == ENC_RIGHT) ? 1 : -1;
    s_state.currentValue = wrapDial(static_cast<int>(s_state.currentValue) + dir);
    s_state.lastRotateDir = static_cast<int8_t>(dir);
    s_state.lockFlashUntilMs = 0;
    s_state.lastFrameMs = 0;
    draw();
    return;
  }

  if (event != ENC_CLICK) {
    return;
  }

  if (s_state.currentValue == activeTarget()) {
    s_state.revealed[s_state.stage] = true;
    s_state.lockFlashUntilMs = nowMs + kLockFlashMs;
    s_state.stage = static_cast<uint8_t>(s_state.stage + 1U);
    s_state.lastFrameMs = 0;
    tone(BUZZER_PIN, 1760, 90);

    if (s_state.stage >= kStageCount) {
      startSuccess(nowMs);
    }

    draw();
    return;
  }

  if (s_state.stage >= (kStageCount - 1U)) {
    startFailure(nowMs);
    draw();
    return;
  }

  tone(BUZZER_PIN, 220, 90);
  draw();
}

bool service(unsigned long nowMs) {
  if (s_state.mode == Mode::Idle) {
    return false;
  }

  if (s_state.mode == Mode::Running) {
    const uint8_t distance = circularDistance(s_state.currentValue, activeTarget());
    updateBuzzer(nowMs, distance);

    if (nowMs - s_state.lastBlinkMs >= kBlinkMs) {
      s_state.lastBlinkMs = nowMs;
      enterRunningFrame();
    }

    if (nowMs - s_state.lastFrameMs >= kRunningFrameMs) {
      s_state.lastFrameMs = nowMs;
      draw();
    }

    return false;
  }

  if (s_state.mode == Mode::SuccessHold && nowMs - s_state.lastBlinkMs >= kBlinkMs) {
    s_state.lastBlinkMs = nowMs;
    enterRunningFrame();
  }

  if (nowMs - s_state.lastFrameMs >= kTerminalFrameMs) {
    s_state.lastFrameMs = nowMs;
    draw();
  }

  if (nowMs >= s_state.terminalUntilMs) {
    return true;
  }

  return false;
}

void draw() {
  if (s_state.mode == Mode::FailureHold) {
    buildFailureScreen();
    return;
  }

  if (s_state.mode == Mode::SuccessHold) {
    buildRunningScreen();
    return;
  }

  buildRunningScreen();
}

}  // namespace SafeCracker
