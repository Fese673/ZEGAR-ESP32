#ifndef MQTTSYNC_H
#define MQTTSYNC_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// HiveMQ Cloud CA Certificate (required for TLS)
extern const char* g_mqtt_ca_cert;

/*
  MQTT Sync Module - runs on Core 1 (second core)
  
  Configuration for HiveMQ Cloud:
  - Broker: 984611e746574f4e8a109c011da25400.s1.eu.hivemq.cloud
  - Port: 8883 (TLS required)
  - Username: SDdfs32sSD
  - Password: F43GJA1sdW
  
  Publish Topic: sensors/device1/data
  Payload: {"t": 23.5, "h": 65, "p": 1013}
  
  API:
    begin(ssid, password)
    publishSensorData(temp, humidity, pressure)  -> call this from main loop
    publishRawJSON(json_string)
    isConnected()
    update()  -> call from main loop (handles reconnection)
*/

namespace MQTTSync {

// ============================================================================
// Configuration
// ============================================================================
#define MQTT_BROKER_ADDRESS "984611e746574f4e8a109c011da25400.s1.eu.hivemq.cloud"
#define MQTT_BROKER_PORT 8883
#define MQTT_USERNAME "ESP32_CLOCK"
#define MQTT_PASSWORD "SDdfs32sSD"
#define MQTT_TOPIC "sensors/device1/data"
#define MQTT_CLIENT_ID "ESP32_CLOCK"

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
 */
void publishSensorData(float temp = 23.5f, int humidity = 65, int pressure = 1013);

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

} // namespace MQTTSync

#endif // MQTTSYNC_H
