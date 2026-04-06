#include "AppSettings.h"

namespace AppSettings {

State& mutableState() {
  static State s_state;
  return s_state;
}

const State& state() {
  return mutableState();
}

void reset() {
  mutableState() = State{};
}

}  // namespace AppSettings