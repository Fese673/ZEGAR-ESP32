#pragma once

// Build-time placeholders for sensitive connectivity settings.
// Real values should be injected via PlatformIO build_flags or persisted in NVS.

#ifndef PROJECT_WIFI_SSID
#define PROJECT_WIFI_SSID "Orange_Swiatlowod_98E2"
#endif

#ifndef PROJECT_WIFI_PASS
#define PROJECT_WIFI_PASS "x1Z6P(~8pry<St."
#endif

#ifndef PROJECT_NTP_SERVER
#define PROJECT_NTP_SERVER "pool.ntp.org"
#endif

#ifndef PROJECT_MQTT_BROKER
#define PROJECT_MQTT_BROKER "984611e746574f4e8a109c011da25400.s1.eu.hivemq.cloud"
#endif

#ifndef PROJECT_MQTT_PORT
#define PROJECT_MQTT_PORT 8883
#endif

#ifndef PROJECT_MQTT_USERNAME
#define PROJECT_MQTT_USERNAME "ESP32_CLOCK"
#endif

#ifndef PROJECT_MQTT_PASSWORD
#define PROJECT_MQTT_PASSWORD "SDdfs32sSD"
#endif

#ifndef PROJECT_MQTT_TOPIC
#define PROJECT_MQTT_TOPIC "sensors/device1/data"
#endif

#ifndef PROJECT_MQTT_CLIENT_ID
#define PROJECT_MQTT_CLIENT_ID "ESP32_CLOCK"
#endif
