#include "AlarmMelodyPrefs.h"

#include "AlarmMelodies.h"

namespace AlarmMelodyPrefs {

int loadIndex(Preferences& prefs) {
  const String savedMelodyId = prefs.getString("alarmMelodyId", "");
  if (savedMelodyId.length() > 0) {
    const int loadedIndex = AlarmMelodies::indexOfId(savedMelodyId.c_str());
    if (loadedIndex >= 0 && loadedIndex < AlarmMelodies::kCount) {
      return loadedIndex;
    }
  }

  int loadedIndex = (int)prefs.getUShort("alarmMelody", 0);
  if (loadedIndex < 0) {
    loadedIndex = 0;
  }
  if (loadedIndex >= AlarmMelodies::kCount) {
    loadedIndex = AlarmMelodies::kCount - 1;
  }

  return loadedIndex;
}

void saveSelection(Preferences& prefs, int melodyIndex) {
  if (melodyIndex < 0) {
    melodyIndex = 0;
  }
  if (melodyIndex >= AlarmMelodies::kCount) {
    melodyIndex = AlarmMelodies::kCount - 1;
  }

  prefs.putString("alarmMelodyId", AlarmMelodies::id((uint8_t)melodyIndex));
  prefs.putUShort("alarmMelody", (uint16_t)melodyIndex);
}

}  // namespace AlarmMelodyPrefs
