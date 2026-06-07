#include "AlarmMelodyPreview.h"

#include "Board_Pins.h"
#include "ClockAlarmService.h"
namespace AlarmMelodyPreview {

void start(uint8_t melodyIndex) {
  ClockAlarmService::startAlarmMelodyDemo(melodyIndex, BoardPins::kBuzzer);
}

void stop() {
  ClockAlarmService::stopAlarmMelodyDemo(BoardPins::kBuzzer);
}

}  // namespace AlarmMelodyPreview