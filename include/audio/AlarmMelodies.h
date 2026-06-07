#pragma once

#include <Arduino.h>
namespace AlarmMelodies {

extern const uint8_t kCount;

struct SongTrack {
	const char* id;
	const char* name;
	const uint16_t* notes;
	const int16_t* divs;
	uint16_t length;
	uint16_t tempoBaseMs;
};

const char* id(uint8_t index);
const char* name(uint8_t index);
int indexOfId(const char* melodyId);
void start(uint8_t index, uint8_t buzzerPin);
void service(uint8_t buzzerPin, unsigned long nowMs);
void stop(uint8_t buzzerPin);

}  // namespace AlarmMelodies