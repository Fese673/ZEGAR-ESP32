#include "AlarmMelodies.h"

#include <cstring>
namespace AlarmMelodies {

struct StepState {
  uint8_t songIndex = 0;
  uint8_t noteIndex = 0;
  bool active = false;
  unsigned long nextStepMs = 0;
};

#include "AlarmMelodies.generated.inc"

static StepState s_state;

static const SongTrack& currentTrack() {
  return kTracks[s_state.songIndex % kCount];
}

static void scheduleNextStep(uint8_t buzzerPin, unsigned long scheduledMs) {
  const SongTrack& track = currentTrack();
  if (track.length == 0) {
    noTone(buzzerPin);
    s_state.nextStepMs = scheduledMs + 100;
    return;
  }

  const uint8_t idx = s_state.noteIndex % track.length;
  const uint16_t freq = track.notes[idx];
  const int16_t div = track.divs[idx];

  unsigned long noteDurationMs = 0;
  if (div > 0) {
    noteDurationMs = ((unsigned long)track.tempoBaseMs * 4UL) / (unsigned long)div;
  } else if (div < 0) {
    noteDurationMs = ((unsigned long)track.tempoBaseMs * 4UL) / (unsigned long)(-div);
    noteDurationMs += noteDurationMs / 2UL;
  }

  if (noteDurationMs == 0) {
    noteDurationMs = 1;
  }

  const unsigned long toneMs = (noteDurationMs * 9UL) / 10UL;

  if (freq == 0) {
    noTone(buzzerPin);
  } else {
    tone(buzzerPin, freq, toneMs);
  }

  s_state.nextStepMs = scheduledMs + noteDurationMs;
  s_state.noteIndex = (s_state.noteIndex + 1) % track.length;
}

const char* name(uint8_t index) {
  return kTracks[index % kCount].name;
}

const char* id(uint8_t index) {
  return kTracks[index % kCount].id;
}

int indexOfId(const char* melodyId) {
  if (melodyId == nullptr || melodyId[0] == '\0') {
    return -1;
  }

  for (uint8_t i = 0; i < kCount; ++i) {
    if (std::strcmp(kTracks[i].id, melodyId) == 0) {
      return (int)i;
    }
  }

  return -1;
}

void start(uint8_t index, uint8_t buzzerPin) {
  s_state.songIndex = index % kCount;
  s_state.noteIndex = 0;
  s_state.active = true;
  s_state.nextStepMs = 0;
  noTone(buzzerPin);
}

void service(uint8_t buzzerPin, unsigned long nowMs) {
  if (!s_state.active) {
    return;
  }

  if (s_state.nextStepMs == 0) {
    scheduleNextStep(buzzerPin, nowMs);
    return;
  }

  constexpr uint8_t kMaxCatchUpSteps = 6;
  uint8_t catchUpSteps = 0;
  while (s_state.active && (long)(nowMs - s_state.nextStepMs) >= 0 && catchUpSteps < kMaxCatchUpSteps) {
    scheduleNextStep(buzzerPin, s_state.nextStepMs);
    ++catchUpSteps;
  }
}

void stop(uint8_t buzzerPin) {
  s_state.active = false;
  s_state.nextStepMs = 0;
  s_state.noteIndex = 0;
  noTone(buzzerPin);
}

}  // namespace AlarmMelodies
