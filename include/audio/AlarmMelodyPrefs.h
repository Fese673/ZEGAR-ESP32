#pragma once

#include <Preferences.h>

namespace AlarmMelodyPrefs {

int loadIndex(Preferences& prefs);
void saveSelection(Preferences& prefs, int melodyIndex);

}  // namespace AlarmMelodyPrefs
