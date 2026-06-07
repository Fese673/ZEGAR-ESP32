#include "EQFilter.h"
EQFilter s_bassFilter;
EQFilter s_midFilter;
EQFilter s_trebleFilter;

void updateEQFilters(uint8_t bass, uint8_t mid, uint8_t treble) {
    // Map slider values (0-100) to decibels (-12 dB to +12 dB)
    float dbBass = (static_cast<float>(bass) - 50.0f) * (12.0f / 50.0f);
    float dbMid = (static_cast<float>(mid) - 50.0f) * (12.0f / 50.0f);
    float dbTreble = (static_cast<float>(treble) - 50.0f) * (12.0f / 50.0f);

    // Peaking EQ:
    // Bass filter: center 100 Hz, Q = 0.7
    s_bassFilter.updatePeaking(100.0f, 44100.0f, dbBass, 0.7f);
    // Mid filter: center 1000 Hz, Q = 0.7
    s_midFilter.updatePeaking(1000.0f, 44100.0f, dbMid, 0.7f);
    // Treble filter: center 10000 Hz, Q = 0.7
    s_trebleFilter.updatePeaking(10000.0f, 44100.0f, dbTreble, 0.7f);
}
