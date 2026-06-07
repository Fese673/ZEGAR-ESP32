#include <Arduino.h>
#include <string.h>

#include "core/events/EventBus.h"
#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#endif

Event EventBus::s_queue[kEventQueueSize];
volatile uint8_t EventBus::s_queueHead = 0;
volatile uint8_t EventBus::s_queueTail = 0;
EventBus::Subscription EventBus::s_subs[kMaxSubscriptions];
uint8_t EventBus::s_subCount = 0;
EventBus::TimerEntry EventBus::s_timers[kMaxTimers];
uint8_t EventBus::s_timerCount = 0;
bool EventBus::s_initialized = false;

#ifdef ARDUINO_ARCH_ESP32
static portMUX_TYPE s_queueMux = portMUX_INITIALIZER_UNLOCKED;
#endif

void EventBus::init() {
  memset(s_queue, 0, sizeof(s_queue));
  s_queueHead = 0;
  s_queueTail = 0;
  s_subCount = 0;
  s_timerCount = 0;
  s_initialized = true;
}

bool EventBus::post(uint8_t eventId, int intValue) {
  if (!s_initialized || eventId == EV_NONE || eventId >= EV_COUNT) {
    return false;
  }

#ifdef ARDUINO_ARCH_ESP32
  portENTER_CRITICAL(&s_queueMux);
#endif

  const uint8_t next = (s_queueTail + 1) % kEventQueueSize;
  if (next == s_queueHead) {
#ifdef ARDUINO_ARCH_ESP32
    portEXIT_CRITICAL(&s_queueMux);
#endif
    return false;
  }

  s_queue[s_queueTail].id = eventId;
  s_queue[s_queueTail].intValue = intValue;
  s_queueTail = next;

#ifdef ARDUINO_ARCH_ESP32
  portEXIT_CRITICAL(&s_queueMux);
#endif
  return true;
}

bool EventBus::process() {
  if (!s_initialized) {
    return false;
  }

  const uint32_t now = millis();

  for (uint8_t i = 0; i < s_timerCount; ++i) {
    if (!s_timers[i].active) {
      continue;
    }
    if (now - s_timers[i].lastFireMs >= s_timers[i].periodMs) {
      if (now - s_timers[i].lastFireMs >= s_timers[i].periodMs * 2) {
        s_timers[i].lastFireMs = now;
      } else {
        s_timers[i].lastFireMs += s_timers[i].periodMs;
      }
      if (s_timers[i].oneshot) {
        s_timers[i].active = false;
      }
      post(s_timers[i].eventId);
    }
  }

  bool processed = false;
  for (;;) {
#ifdef ARDUINO_ARCH_ESP32
    portENTER_CRITICAL(&s_queueMux);
#endif
    const bool empty = (s_queueHead == s_queueTail);
    if (empty) {
#ifdef ARDUINO_ARCH_ESP32
      portEXIT_CRITICAL(&s_queueMux);
#endif
      break;
    }
    const Event ev = s_queue[s_queueHead];
    s_queueHead = (s_queueHead + 1) % kEventQueueSize;
#ifdef ARDUINO_ARCH_ESP32
    portEXIT_CRITICAL(&s_queueMux);
#endif

    for (uint8_t i = 0; i < s_subCount; ++i) {
      if (s_subs[i].eventId == ev.id || s_subs[i].eventId == EV_NONE) {
        s_subs[i].handler(ev);
      }
    }
    processed = true;
  }

  return processed;
}

void EventBus::subscribe(uint8_t eventId, Handler handler) {
  if (!s_initialized || s_subCount >= kMaxSubscriptions || handler == nullptr) {
    return;
  }

  for (uint8_t i = 0; i < s_subCount; ++i) {
    if (s_subs[i].eventId == eventId && s_subs[i].handler == handler) {
      return;
    }
  }

  s_subs[s_subCount].eventId = eventId;
  s_subs[s_subCount].handler = handler;
  ++s_subCount;
}

void EventBus::addTimer(uint8_t eventId, uint32_t periodMs, bool oneshot, uint32_t offsetMs) {
  if (!s_initialized || s_timerCount >= kMaxTimers) {
    return;
  }

  for (uint8_t i = 0; i < s_timerCount; ++i) {
    if (s_timers[i].active && s_timers[i].eventId == eventId && s_timers[i].periodMs == periodMs && s_timers[i].oneshot == oneshot) {
      return;
    }
  }

  s_timers[s_timerCount].eventId = eventId;
  s_timers[s_timerCount].periodMs = periodMs;
  // If offsetMs is 0, fire after one periodMs.
  // If offsetMs > 0, fire after offsetMs.
  s_timers[s_timerCount].lastFireMs = millis() + offsetMs - (offsetMs == 0 ? 0 : periodMs);
  s_timers[s_timerCount].active = true;
  s_timers[s_timerCount].oneshot = oneshot;
  ++s_timerCount;
}
