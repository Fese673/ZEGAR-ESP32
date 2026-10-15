#pragma once

#include <Preferences.h>
namespace AlarmMelodyPrefs {

int loadIndex(Preferences& prefs);
// Zwraca true tylko gdy OBA klucze zapisaly sie poprawnie (retry po stronie
// wolajacego zamiast cichej utraty wyboru melodii).
bool saveSelection(Preferences& prefs, int melodyIndex);

}  // namespace AlarmMelodyPrefs
