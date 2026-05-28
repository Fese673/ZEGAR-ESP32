#pragma once

#include <stdint.h>

#include "AppLog.h"

#ifndef ESP_TO_GUTION_LOG_ENABLED
#define ESP_TO_GUTION_LOG_ENABLED 0
#endif

namespace EsptoGuition::MusicLog {

#if ESP_TO_GUTION_LOG_ENABLED

inline const char *commandName(uint8_t command) {
    switch (command) {
        case 0:
            return "play";
        case 1:
            return "pause";
        case 2:
            return "next";
        case 3:
            return "prev";
        default:
            return nullptr;
    }
}

inline void receivedCommand(uint8_t command) {
    const char *name = commandName(command);
    if (name == nullptr) {
        return;
    }

    LOG_I("COM", "(music) cmd=%s", name);
}

inline void receivedVolume(uint8_t volume) {
    LOG_I("COM", "(music) volume=%u", static_cast<unsigned>(volume));
}

inline void receivedEQ(uint8_t bass, uint8_t mid, uint8_t treble) {
    LOG_I("COM", "(music) eq bass=%u mid=%u treble=%u",
          static_cast<unsigned>(bass),
          static_cast<unsigned>(mid),
          static_cast<unsigned>(treble));
}

#else

inline void receivedCommand(uint8_t) {}
inline void receivedVolume(uint8_t) {}
inline void receivedEQ(uint8_t, uint8_t, uint8_t) {}

#endif

}  // namespace EsptoGuition::MusicLog