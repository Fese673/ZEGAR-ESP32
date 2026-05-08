#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "core/events/EventTypes.h"

constexpr uint8_t kEventQueueSize = 16;
constexpr uint8_t kMaxSubscriptions = 32;
constexpr uint8_t kMaxTimers = 16;

class EventBus {
public:
  using Handler = void(*)(const Event&);

  static void init();
  static bool post(uint8_t eventId, int intValue = 0);
  static bool process();
  static void subscribe(uint8_t eventId, Handler handler);
  static void addTimer(uint8_t eventId, uint32_t periodMs, bool oneshot = false, uint32_t offsetMs = 0);

private:
  struct Subscription {
    uint8_t eventId;
    Handler handler;
  };

  struct TimerEntry {
    uint8_t eventId;
    uint32_t periodMs;
    uint32_t lastFireMs;
    bool active;
    bool oneshot;
  };

  static Event s_queue[kEventQueueSize];
  static volatile uint8_t s_queueHead;
  static volatile uint8_t s_queueTail;
  static Subscription s_subs[kMaxSubscriptions];
  static uint8_t s_subCount;
  static TimerEntry s_timers[kMaxTimers];
  static uint8_t s_timerCount;
  static bool s_initialized;
};
