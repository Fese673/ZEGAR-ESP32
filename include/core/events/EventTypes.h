#pragma once
#include <stdint.h>

enum EventId : uint8_t {
  EV_NONE = 0,
  EV_ENCODER_CLICK,
  EV_ENCODER_LONG,
  EV_ENCODER_LEFT,
  EV_ENCODER_RIGHT,
  EV_CLOCK_TICK,
  EV_SENSOR_READ,
  EV_UI_OVERLAY,
  EV_UI_REFRESH,
  EV_DIAGNOSTICS,
  EV_MQTT_PUBLISH,
  EV_APP_STATE_CHANGED,
  EV_STATS_REDRAW,
  EV_BT_CONN_CHECK,
  EV_BOOT_LOGGING,
  EV_COUNT
};

struct Event {
  uint8_t id;
  int intValue;
};
