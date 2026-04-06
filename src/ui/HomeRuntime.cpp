#include "HomeRuntime.h"

#include "UI_Draw.h"

namespace HomeRuntime {
namespace {

constexpr unsigned long kHomeRedrawMinIntervalMs = 1000UL;

enum class HomeUiProfile : uint8_t {
  Minimal = 0,
  Balanced = 1,
  Extreme = 2,
};

enum class HomeOverlayPage : uint8_t {
  Time = 0,
  Indoor = 1,
  Outdoor = 2,
  Extreme = 3,
  Systems = 4,
  ExtremeAlgos = 5,
};

DrawCallbacks s_drawCallbacks{};

bool s_homeRedrawDirty = true;
unsigned long s_lastHomeRedrawMs = 0;

HomeUiProfile s_homeUiProfile = HomeUiProfile::Minimal;
HomeOverlayPage s_homeOverlay = HomeOverlayPage::Time;
uint8_t s_homeOverlayIndex = 0;
unsigned long s_homeOverlaySinceMs = 0;
unsigned long s_homeOverlaySwitchMs = 7000UL;

static uint8_t homeOverlayCountForProfile(HomeUiProfile profile) {
  switch (profile) {
    case HomeUiProfile::Minimal:
      return 2;
    case HomeUiProfile::Balanced:
      return 3;
    case HomeUiProfile::Extreme:
      return 6;
  }
  return 2;
}

static HomeOverlayPage homeOverlayPageFor(HomeUiProfile profile, uint8_t index) {
  switch (profile) {
    case HomeUiProfile::Minimal:
      return (index % 2 == 0) ? HomeOverlayPage::Time : HomeOverlayPage::Outdoor;
    case HomeUiProfile::Balanced:
      switch (index % 3) {
        case 0:
          return HomeOverlayPage::Time;
        case 1:
          return HomeOverlayPage::Indoor;
        default:
          return HomeOverlayPage::Outdoor;
      }
    case HomeUiProfile::Extreme:
      switch (index % 6) {
        case 0:
          return HomeOverlayPage::Time;
        case 1:
          return HomeOverlayPage::Indoor;
        case 2:
          return HomeOverlayPage::Outdoor;
        case 3:
          return HomeOverlayPage::Extreme;
        case 4:
          return HomeOverlayPage::Systems;
        default:
          return HomeOverlayPage::ExtremeAlgos;
      }
  }
  return HomeOverlayPage::Time;
}

static void syncHomeOverlayToProfile(bool resetTimer) {
  const uint8_t overlayCount = homeOverlayCountForProfile(s_homeUiProfile);
  if (overlayCount == 0) {
    s_homeOverlayIndex = 0;
    s_homeOverlay = HomeOverlayPage::Time;
    s_homeOverlaySinceMs = 0;
    markHomeDirty();
    return;
  }

  if (s_homeOverlayIndex >= overlayCount) {
    s_homeOverlayIndex = 0;
  }

  s_homeOverlay = homeOverlayPageFor(s_homeUiProfile, s_homeOverlayIndex);
  if (resetTimer) {
    s_homeOverlaySinceMs = millis();
  }

  markHomeDirty();
}

}  // namespace

void begin(const DrawCallbacks& callbacks, uint8_t initialProfileIndex, uint8_t overlaySwitchSeconds) {
  s_drawCallbacks = callbacks;
  s_homeRedrawDirty = true;
  s_lastHomeRedrawMs = 0;
  s_homeOverlayIndex = 0;
  s_homeOverlaySinceMs = 0;
  setOverlayIntervalSeconds(overlaySwitchSeconds);
  setProfile(initialProfileIndex);
}

void setProfile(uint8_t profileIndex) {
  if (profileIndex > static_cast<uint8_t>(HomeUiProfile::Extreme)) {
    profileIndex = 0;
  }

  s_homeUiProfile = static_cast<HomeUiProfile>(profileIndex);
  syncHomeOverlayToProfile(true);
  requestUiFullRedraw();
}

uint8_t getProfile() {
  return static_cast<uint8_t>(s_homeUiProfile);
}

void setOverlayIntervalSeconds(uint8_t seconds) {
  if (seconds < 1) {
    seconds = 1;
  }
  if (seconds > 10) {
    seconds = 10;
  }
  s_homeOverlaySwitchMs = (unsigned long)seconds * 1000UL;
}

uint8_t getOverlayIntervalSeconds() {
  return static_cast<uint8_t>(s_homeOverlaySwitchMs / 1000UL);
}

void markHomeDirty() {
  s_homeRedrawDirty = true;
}

void serviceRedraw(AppState appState) {
  if (appState != STATE_HOME) {
    return;
  }
  if (!s_homeRedrawDirty) {
    return;
  }

  const unsigned long nowMs = millis();
  if (s_lastHomeRedrawMs != 0 && (nowMs - s_lastHomeRedrawMs) < kHomeRedrawMinIntervalMs) {
    return;
  }

  s_lastHomeRedrawMs = nowMs;
  s_homeRedrawDirty = false;

  switch (s_homeOverlay) {
    case HomeOverlayPage::Indoor:
      if (s_drawCallbacks.drawIndoorWeather) {
        s_drawCallbacks.drawIndoorWeather();
      }
      break;
    case HomeOverlayPage::Outdoor:
      if (s_drawCallbacks.drawOutdoorAir) {
        s_drawCallbacks.drawOutdoorAir();
      }
      break;
    case HomeOverlayPage::Extreme:
      if (s_drawCallbacks.drawExtremeEnvironment) {
        s_drawCallbacks.drawExtremeEnvironment();
      }
      break;
    case HomeOverlayPage::Systems:
      if (s_drawCallbacks.drawSystemResources) {
        s_drawCallbacks.drawSystemResources();
      }
      break;
    case HomeOverlayPage::ExtremeAlgos:
      if (s_drawCallbacks.drawExtremeAlgorithms) {
        s_drawCallbacks.drawExtremeAlgorithms();
      }
      break;
    case HomeOverlayPage::Time:
    default:
      if (s_drawCallbacks.drawHome) {
        s_drawCallbacks.drawHome();
      }
      break;
  }
}

void serviceOverlayRotation(AppState appState) {
  if (appState != STATE_HOME) {
    return;
  }

  const unsigned long nowMs = millis();
  const uint8_t overlayCount = homeOverlayCountForProfile(s_homeUiProfile);
  if (overlayCount == 0) {
    return;
  }

  if (s_homeOverlaySinceMs == 0) {
    s_homeOverlayIndex = 0;
    s_homeOverlay = homeOverlayPageFor(s_homeUiProfile, s_homeOverlayIndex);
    s_homeOverlaySinceMs = nowMs;
    requestUiFullRedraw();
    markHomeDirty();
    return;
  }

  if (nowMs - s_homeOverlaySinceMs >= s_homeOverlaySwitchMs) {
    s_homeOverlaySinceMs = nowMs;
    s_homeOverlayIndex = (uint8_t)((s_homeOverlayIndex + 1) % overlayCount);
    s_homeOverlay = homeOverlayPageFor(s_homeUiProfile, s_homeOverlayIndex);
    requestUiFullRedraw();
    markHomeDirty();
  }
}

void handleHomeEntryIfStateChanged(AppState appState) {
  static AppState lastState = STATE_HOME;
  if (appState == lastState) {
    return;
  }

  if (appState == STATE_HOME) {
    s_homeOverlayIndex = 0;
    syncHomeOverlayToProfile(true);
    requestUiFullRedraw();
  }

  lastState = appState;
}

}  // namespace HomeRuntime
