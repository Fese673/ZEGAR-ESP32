#ifndef MQTTSYNC_H
#define MQTTSYNC_H

#include <Arduino.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "SecretsConfig.h"

// HiveMQ Cloud CA Certificate (required for TLS)
extern const char* g_mqtt_ca_cert;

/*
  MQTT Sync Module - runs on Core 1 (second core)

  Broker credentials and topics are provided through `configure()` and can be
  loaded from Preferences with build-time placeholders as fallback.
  
  API:
    begin(ssid, password)
    publishSensorData(temp, humidity, pressure)  -> call this from main loop
    publishRawJSON(json_string)
    isConnected()
    update()  -> call from main loop (handles reconnection)
*/

namespace MQTTSync {

struct Config {
  String brokerAddress = PROJECT_MQTT_BROKER;
  uint16_t brokerPort = PROJECT_MQTT_PORT;
  String username = PROJECT_MQTT_USERNAME;
  String password = PROJECT_MQTT_PASSWORD;
  String topic = PROJECT_MQTT_TOPIC;
  String clientId = PROJECT_MQTT_CLIENT_ID;
};

void configure(const Config& config);
Config currentConfig();

// ============================================================================
// Public API
// ============================================================================

/**
 * Initialize MQTT connection
 * Must be called before startCore1Task()
 */
void begin(const char* ssid, const char* password);

/**
 * Start MQTT task on Core 1
 * Call this from setup() after begin()
 */
void startCore1Task();

/**
 * Stop MQTT task (disconnect and kill task)
 */
void stopCore1Task();

/**
 * Publish sensor data (static test values for now)
 * Call this from main loop
 *
 * Added:
 *   aqi  - ENS160 air quality index
 *   tvoc - ENS160 TVOC in ppb
 *   eco2 - ENS160 eCO2 in ppm
 */
void publishSensorData(float temp = 23.5f, int humidity = 65, int pressure = 1013,
                       uint8_t aqi = 0, uint16_t tvoc = 0, uint16_t eco2 = 0);

/**
 * Publish raw JSON string
 */
void publishRawJSON(const char* jsonString);

/**
 * Check if MQTT is connected
 */
bool isConnected();

/**
 * Soft reconnect (call from main loop periodically)
 */
void update();

/**
 * Get last publish timestamp
 */
unsigned long getLastPublishTime();

/**
 * Set publish interval (default 5000ms)
 */
void setPublishInterval(unsigned long intervalMs);

/**
 * Get MQTT background task handle for telemetry.
 */
TaskHandle_t getTaskHandle();

} // namespace MQTTSync

#endif // MQTTSYNC_H
