#include "touch_buzzer_test.h"

#include <Arduino.h>

namespace TouchBuzzerTest {
namespace {

constexpr uint8_t kCalibrationSamples = 16;
constexpr uint16_t kMinValidBaseline = 20U;
constexpr uint16_t kMinPressMargin = 3U;
constexpr uint16_t kMinReleaseMargin = 2U;
constexpr uint16_t kBaselineTrackGuard = 8U;
constexpr uint8_t kPressedSamplesRequired = 2;
constexpr uint8_t kReleasedSamplesRequired = 3;
constexpr uint16_t kToneHz = 1760U;

uint8_t s_touchPad = 255;
uint8_t s_buzzerPin = 255;
bool s_enabled = false;
bool s_ready = false;
uint16_t s_baseline = 0;
uint16_t s_pressThreshold = 0;
uint16_t s_releaseThreshold = 0;
bool s_touchActive = false;
uint8_t s_pressedSamples = 0;
uint8_t s_releasedSamples = 0;
bool s_toneActive = false;

void resetDetectionState() {
  s_touchActive = false;
  s_pressedSamples = 0;
  s_releasedSamples = 0;
}

uint16_t computePressThreshold(uint16_t baseline) {
  uint16_t margin = baseline / 20U;
  if (margin < kMinPressMargin) {
    margin = kMinPressMargin;
  }

  if (baseline > margin) {
    return static_cast<uint16_t>(baseline - margin);
  }

  return static_cast<uint16_t>(baseline / 2U);
}

uint16_t computeReleaseThreshold(uint16_t baseline, uint16_t pressThreshold) {
  uint16_t margin = baseline / 40U;
  if (margin < kMinReleaseMargin) {
    margin = kMinReleaseMargin;
  }

  uint16_t releaseThreshold = static_cast<uint16_t>(pressThreshold + margin);
  if (baseline > 1U && releaseThreshold >= baseline) {
    releaseThreshold = static_cast<uint16_t>(baseline - 1U);
  }

  return releaseThreshold;
}

uint16_t sampleBaseline(uint8_t touchPad) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < kCalibrationSamples; ++i) {
    sum += touchRead(touchPad);
  }

  uint16_t baseline = static_cast<uint16_t>(sum / kCalibrationSamples);
  if (baseline == 0U) {
    baseline = 1U;
  }

  return baseline;
}

void startTone() {
  tone(s_buzzerPin, kToneHz);
  s_toneActive = true;
}

void stopTone() {
  if (!s_toneActive) {
    digitalWrite(s_buzzerPin, LOW);
    return;
  }

  noTone(s_buzzerPin);
  s_toneActive = false;
  digitalWrite(s_buzzerPin, LOW);
}

bool calibrateTouchPad() {
  pinMode(s_touchPad, INPUT);
  s_baseline = sampleBaseline(s_touchPad);
  if (s_baseline < kMinValidBaseline) {
    s_ready = false;
    return false;
  }

  s_pressThreshold = computePressThreshold(s_baseline);
  s_releaseThreshold = computeReleaseThreshold(s_baseline, s_pressThreshold);
  resetDetectionState();
  s_ready = true;
  return true;
}

}  // namespace

void begin(uint8_t touchPad, uint8_t buzzerPin) {
  s_touchPad = touchPad;
  s_buzzerPin = buzzerPin;
  s_enabled = false;
  s_ready = false;
  s_toneActive = false;
  resetDetectionState();
  stopTone();
}

void setEnabled(bool enabled) {
  if (!enabled) {
    s_enabled = false;
    resetDetectionState();
    stopTone();
    return;
  }

  if (s_enabled) {
    return;
  }

  if (!s_ready) {
    s_enabled = calibrateTouchPad();
    if (!s_enabled) {
      stopTone();
    }
    return;
  }

  s_enabled = true;
  resetDetectionState();
  stopTone();
}

bool isEnabled() {
  return s_enabled;
}

void service() {
  if (!s_enabled || !s_ready) {
    return;
  }

  const uint16_t raw = touchRead(s_touchPad);
  const bool isPressed = raw <= s_pressThreshold;
  const bool isReleased = raw >= s_releaseThreshold;

  if (!s_touchActive) {
    s_pressedSamples = isPressed ? static_cast<uint8_t>(s_pressedSamples + 1U) : 0U;
    if (s_pressedSamples >= kPressedSamplesRequired) {
      s_touchActive = true;
      s_releasedSamples = 0;
      startTone();
    }
  } else {
    s_releasedSamples = isReleased ? static_cast<uint8_t>(s_releasedSamples + 1U) : 0U;
    if (s_releasedSamples >= kReleasedSamplesRequired) {
      s_touchActive = false;
      s_pressedSamples = 0;
      stopTone();
    }
  }

  const uint16_t trackThreshold = (s_baseline <= static_cast<uint16_t>(0xFFFFU - kBaselineTrackGuard))
                                     ? static_cast<uint16_t>(s_baseline + kBaselineTrackGuard)
                                     : 0xFFFFU;
  const bool shouldTrackBaseline = !s_touchActive && raw >= trackThreshold;

  if (shouldTrackBaseline) {
    if (raw > s_baseline) {
      s_baseline = static_cast<uint16_t>((static_cast<uint32_t>(s_baseline) * 15U + raw) / 16U);
    }

    if (s_baseline == 0U) {
      s_baseline = 1U;
    }

    s_pressThreshold = computePressThreshold(s_baseline);
    s_releaseThreshold = computeReleaseThreshold(s_baseline, s_pressThreshold);
  }
}

}  // namespace TouchBuzzerTest