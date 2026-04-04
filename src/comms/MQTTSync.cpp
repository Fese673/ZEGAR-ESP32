#include "MQTTSync.h"
#include "WiFiSync.h"
#include <esp_system.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>

#include "RamTelemetry.h"

// Externy PMS5003 - zmienne globalne z `main.cpp`
extern uint16_t pms5003_PM1_0_CF1;
extern uint16_t pms5003_PM2_5_CF1;
extern uint16_t pms5003_PM10_CF1;

extern uint16_t pms5003_PM1_0_ATM;
extern uint16_t pms5003_PM2_5_ATM;
extern uint16_t pms5003_PM10_ATM;

// Particle counts (#/100cm3)
extern uint16_t pms5003_particleCount_0_3;
extern uint16_t pms5003_particleCount_0_5;
extern uint16_t pms5003_particleCount_1_0;
extern uint16_t pms5003_particleCount_2_5;
extern uint16_t pms5003_particleCount_5_0;
extern uint16_t pms5003_particleCount_10_0;

// ============================================================================
// CA Certificate Definition (GLOBAL - outside namespace)
// ============================================================================
extern const char* g_mqtt_ca_cert;

const char* g_mqtt_ca_cert = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)EOF";


namespace MQTTSync {

static constexpr size_t MQTT_JSON_BUFFER_SIZE = 192;
static constexpr size_t MQTT_PACKET_BUFFER_SIZE = 256;
static constexpr size_t MQTT_JSON_DOC_CAPACITY = 384;
static constexpr size_t MQTT_PACKET_MARGIN_BYTES = 8;

// ============================================================================
// Static Variables
// ============================================================================
static NetworkClientSecure wifiClientSecure;
static PubSubClient mqttClient;
static unsigned long lastPublishTime = 0;
static unsigned long publishInterval = 5000; // 5 seconds
static volatile bool mqttConnected = false;
static TaskHandle_t mqtt_task_handle = NULL;
static Config s_config;
static char s_pendingPayload[MQTT_PACKET_BUFFER_SIZE] = {0};
static size_t s_pendingPayloadLength = 0;
static bool s_pendingPayloadReady = false;
static bool s_serviceStarted = false;

enum class MqttConnectionState : uint8_t {
    WaitingForWifi,
    Idle,
    Connecting,
    Online,
    Backoff,
};

static MqttConnectionState s_state = MqttConnectionState::WaitingForWifi;
static unsigned long s_nextStateCheckMs = 0;
static unsigned long s_connectStartMs = 0;
static uint8_t s_connectFailureCount = 0;
static volatile bool s_stopRequested = false;

static constexpr unsigned long MQTT_WIFI_RECOVERY_DELAY_MS = 250;
static constexpr unsigned long MQTT_CONNECT_BUDGET_MS = 4500;
static constexpr unsigned long MQTT_TCP_CONNECT_TIMEOUT_MS = 3000;
static constexpr unsigned long MQTT_TLS_HANDSHAKE_TIMEOUT_SEC = 2;
static constexpr uint16_t MQTT_SOCKET_TIMEOUT_SEC = 3;
static constexpr uint16_t MQTT_KEEPALIVE_SEC = 15;

// ============================================================================
// Forward declarations
// ============================================================================
static void mqtt_callback(char* topic, byte* payload, unsigned int length);
static bool mqtt_reconnect(unsigned long nowMs);
static void applyConfigToClient();
static void applyConnectionBudget();
static unsigned long computeBackoffMs(uint8_t failures);
static bool mqttPayloadFits(size_t payloadLength);
static bool queuePendingPublish(const char* payload, size_t payloadLength);
static void flushPendingPublish();

// ============================================================================
// Callback for incoming MQTT messages
// ============================================================================
static void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    Serial.print("[MQTT] Message received on topic: ");
    Serial.println(topic);
    Serial.print("[MQTT] Payload: ");
    for (int i = 0; i < length; i++) {
        Serial.print((char)payload[i]);
    }
    Serial.println();
}

static void applyConfigToClient() {
    mqttClient.setClient(wifiClientSecure);
    mqttClient.setServer(s_config.brokerAddress.c_str(), s_config.brokerPort);
    mqttClient.setCallback(mqtt_callback);
    mqttClient.setKeepAlive(MQTT_KEEPALIVE_SEC);
    mqttClient.setSocketTimeout(MQTT_SOCKET_TIMEOUT_SEC);
}

static void applyConnectionBudget() {
    wifiClientSecure.setConnectionTimeout(MQTT_TCP_CONNECT_TIMEOUT_MS);
    wifiClientSecure.setHandshakeTimeout(MQTT_TLS_HANDSHAKE_TIMEOUT_SEC);
}

static unsigned long computeBackoffMs(uint8_t failures) {
    if (failures == 0) {
        return 0;
    }

    unsigned long baseMs = 1000UL;
    for (uint8_t i = 1; i < failures; ++i) {
        if (baseMs >= 30000UL) {
            baseMs = 30000UL;
            break;
        }
        baseMs *= 2UL;
    }

    if (baseMs > 30000UL) {
        baseMs = 30000UL;
    }

    const unsigned long jitterWindowMs = baseMs / 4UL;
    const unsigned long jitterMs = (jitterWindowMs > 0)
      ? (esp_random() % (jitterWindowMs + 1UL))
      : 0UL;

    unsigned long totalMs = baseMs + jitterMs;
    if (totalMs > 60000UL) {
        totalMs = 60000UL;
    }

    return totalMs;
}

static bool mqttPayloadFits(size_t payloadLength) {
    const size_t topicLength = s_config.topic.length();
    const size_t requiredBytes = payloadLength + topicLength + MQTT_PACKET_MARGIN_BYTES;
    return requiredBytes <= MQTT_PACKET_BUFFER_SIZE;
}

static bool queuePendingPublish(const char* payload, size_t payloadLength) {
    if (payload == nullptr) {
        Serial.println("[MQTT] Empty payload, cannot queue");
        return false;
    }

    if (payloadLength >= sizeof(s_pendingPayload)) {
        Serial.print("[MQTT] Payload too large for staging buffer (payload=");
        Serial.print(payloadLength);
        Serial.print(", buffer=");
        Serial.print(sizeof(s_pendingPayload));
        Serial.println(")");
        return false;
    }

    if (!mqttPayloadFits(payloadLength)) {
        Serial.print("[MQTT] Payload too large for packet buffer (payload=");
        Serial.print(payloadLength);
        Serial.print(", topic=");
        Serial.print(s_config.topic.length());
        Serial.print(", buffer=");
        Serial.print(MQTT_PACKET_BUFFER_SIZE);
        Serial.println(")");
        return false;
    }

    memcpy(s_pendingPayload, payload, payloadLength);
    s_pendingPayload[payloadLength] = '\0';
    s_pendingPayloadLength = payloadLength;
    s_pendingPayloadReady = true;
    return true;
}

static void flushPendingPublish() {
    if (!s_pendingPayloadReady) {
        return;
    }

    if (!mqttClient.connected()) {
        return;
    }

    if (mqttClient.publish(s_config.topic.c_str(), s_pendingPayload)) {
        Serial.print("[MQTT] Published (");
        Serial.print(s_pendingPayloadLength);
        Serial.print(" B): ");
        Serial.println(s_pendingPayload);
        lastPublishTime = millis();
        s_pendingPayloadReady = false;
        s_pendingPayloadLength = 0;
    } else {
        Serial.println("[MQTT] Publish failed!");
    }
}

void configure(const Config& config) {
    s_config = config;
    applyConfigToClient();
    applyConnectionBudget();
}

Config currentConfig() {
    return s_config;
}

// ============================================================================
// MQTT Reconnection Logic
// ============================================================================
static bool mqtt_reconnect(unsigned long nowMs) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[MQTT] WiFi not connected, waiting for link");
        mqttConnected = false;
        s_state = MqttConnectionState::WaitingForWifi;
        s_nextStateCheckMs = nowMs + MQTT_WIFI_RECOVERY_DELAY_MS;
        return false;
    }

    Serial.print("[MQTT] Connecting to ");
    Serial.print(s_config.brokerAddress);
    Serial.print(":");
    Serial.println(s_config.brokerPort);

    s_state = MqttConnectionState::Connecting;
    s_connectStartMs = nowMs;
    mqttConnected = false;

    const bool connected = mqttClient.connect(s_config.clientId.c_str(),
                                              s_config.username.c_str(),
                                              s_config.password.c_str());
    const unsigned long elapsedMs = millis() - s_connectStartMs;

    if (connected && elapsedMs <= MQTT_CONNECT_BUDGET_MS) {
        Serial.print("[MQTT] Connected in ");
        Serial.print(elapsedMs);
        Serial.println(" ms");
        mqttConnected = true;
        s_connectFailureCount = 0;
        s_state = MqttConnectionState::Online;

        Serial.println("[MQTT] Requesting background NTP sync via WiFiSync::requestTimeSync()");
        WiFiSync::requestTimeSync();
        RAM_CHECKPOINT("MQTT_CONNECTED");
        return true;
    }

    if (connected) {
        Serial.print("[MQTT] Connect exceeded budget (elapsed=");
        Serial.print(elapsedMs);
        Serial.print(" ms, budget=");
        Serial.print(MQTT_CONNECT_BUDGET_MS);
        Serial.println(" ms), disconnecting");
        mqttClient.disconnect();
    }

    const int rc = mqttClient.state();
    if (s_connectFailureCount < 255) {
        ++s_connectFailureCount;
    }

    const unsigned long backoffMs = computeBackoffMs(s_connectFailureCount);
    s_nextStateCheckMs = nowMs + backoffMs;
    s_state = MqttConnectionState::Backoff;
    mqttConnected = false;

    Serial.print("[MQTT] Connection failed, rc=");
    Serial.print(rc);
    Serial.print(", elapsed=");
    Serial.print(elapsedMs);
    Serial.print(" ms, failures=");
    Serial.print(s_connectFailureCount);
    Serial.print(", backoff=");
    Serial.print(backoffMs);
    Serial.println(" ms");
    return false;
}

// ============================================================================
// Public API Implementation
// ============================================================================

void begin(const char* ssid, const char* password) {
    (void)ssid;
    (void)password;

    Serial.println("[MQTT] Initializing MQTT client...");
    
    // Configure secure WiFi client with CA certificate
    wifiClientSecure.setCACert(g_mqtt_ca_cert);
    applyConnectionBudget();

    // Set up MQTT client with secure WiFi client
    applyConfigToClient();

    // Right-size the MQTT packet buffer for the current sensor payloads.
    mqttClient.setBufferSize(MQTT_PACKET_BUFFER_SIZE);

    s_serviceStarted = false;
    s_stopRequested = false;
    s_connectFailureCount = 0;
    s_nextStateCheckMs = 0;
    s_connectStartMs = 0;
    s_state = (WiFi.status() == WL_CONNECTED)
        ? MqttConnectionState::Idle
        : MqttConnectionState::WaitingForWifi;
    mqttConnected = false;
    s_pendingPayloadReady = false;
    s_pendingPayloadLength = 0;
    s_pendingPayload[0] = '\0';
    
    Serial.println("[MQTT] Client configured");
    Serial.print("[MQTT] Broker: ");
    Serial.println(s_config.brokerAddress);
    Serial.print("[MQTT] Port: ");
    Serial.println(s_config.brokerPort);
}

void startCore1Task() {
    if (s_serviceStarted) {
        return;
    }

    s_stopRequested = false;
    s_serviceStarted = true;
    s_state = MqttConnectionState::WaitingForWifi;
    s_nextStateCheckMs = millis() + MQTT_WIFI_RECOVERY_DELAY_MS;
    s_connectStartMs = 0;
    mqttConnected = false;
    Serial.println("[MQTT] Service started on main loop");
    RAM_CHECKPOINT("MQTT_ON");
}

void stopCore1Task() {
    if (!s_serviceStarted && mqtt_task_handle == NULL) {
        return;
    }

    s_stopRequested = true;
    s_serviceStarted = false;
    s_state = MqttConnectionState::WaitingForWifi;
    s_nextStateCheckMs = 0;
    s_connectStartMs = 0;
    s_pendingPayloadReady = false;
    s_pendingPayloadLength = 0;
    s_pendingPayload[0] = '\0';
    mqttClient.disconnect();
    mqttConnected = false;
    mqtt_task_handle = NULL;

    Serial.println("[MQTT] Service stopped");
    RAM_CHECKPOINT("MQTT_OFF");
}

void publishSensorData(float temp, int humidity, int pressure,
                       uint8_t aqi, uint16_t tvoc, uint16_t eco2) {
    if (!s_serviceStarted || s_stopRequested) {
        Serial.println("[MQTT] Service not active, cannot publish");
        return;
    }

    // NOTE: scheduling is handled by the caller (`main.cpp`).
    // Do not duplicate rate-limiting here to avoid missing every-other publish.
    // Compact JSON payload as an array to save bytes:
    // [ t, h, p, aqi, tvoc, eco2, ts, [A_pm1,A_pm25,A_pm10], [n0.3,n0.5,1.0,2.5,5.0,10.0] ]
    StaticJsonDocument<MQTT_JSON_DOC_CAPACITY> doc;
    JsonArray root = doc.to<JsonArray>();
    char tempBuffer[16];
    snprintf(tempBuffer, sizeof(tempBuffer), "%.2f", temp);
    root.add(serialized(tempBuffer));
    root.add(humidity);
    root.add(pressure);
    root.add(aqi);
    root.add(tvoc);
    root.add(eco2);
    root.add(millis());

    Serial.print("[MQTT] ENS160 values: AQI="); Serial.print(aqi);
    Serial.print(" TVOC="); Serial.print(tvoc);
    Serial.print(" eCO2="); Serial.println(eco2);

    JsonArray a = root.createNestedArray();
    a.add(pms5003_PM1_0_ATM);
    a.add(pms5003_PM2_5_ATM);
    a.add(pms5003_PM10_ATM);

    JsonArray particles = root.createNestedArray();
    particles.add(pms5003_particleCount_0_3);
    particles.add(pms5003_particleCount_0_5);
    particles.add(pms5003_particleCount_1_0);
    particles.add(pms5003_particleCount_2_5);
    particles.add(pms5003_particleCount_5_0);
    particles.add(pms5003_particleCount_10_0);

    if (doc.overflowed()) {
        Serial.println("[MQTT] JSON document overflow, skipping publish");
        return;
    }

    const size_t payloadLength = measureJson(doc);
    if (payloadLength >= MQTT_JSON_BUFFER_SIZE) {
        Serial.print("[MQTT] JSON payload exceeds staging buffer (payload=");
        Serial.print(payloadLength);
        Serial.print(", buffer=");
        Serial.print(MQTT_JSON_BUFFER_SIZE);
        Serial.println(")");
        return;
    }
    if (!mqttPayloadFits(payloadLength)) {
        Serial.print("[MQTT] Payload too large for packet buffer (payload=");
        Serial.print(payloadLength);
        Serial.print(", topic=");
        Serial.print(s_config.topic.length());
        Serial.print(", buffer=");
        Serial.print(MQTT_PACKET_BUFFER_SIZE);
        Serial.println(")");
        return;
    }

    // Debug: log particle counts before serialization
    Serial.print("[MQTT] Particles: ");
    Serial.print(pms5003_particleCount_0_3); Serial.print(",");
    Serial.print(pms5003_particleCount_0_5); Serial.print(",");
    Serial.print(pms5003_particleCount_1_0); Serial.print(",");
    Serial.print(pms5003_particleCount_2_5); Serial.print(",");
    Serial.print(pms5003_particleCount_5_0); Serial.print(",");
    Serial.println(pms5003_particleCount_10_0);

    // Serialize to string
    char buffer[MQTT_JSON_BUFFER_SIZE];
    size_t n = serializeJson(doc, buffer, sizeof(buffer));
    Serial.print("[MQTT] Payload size: "); Serial.println(n);

    if (!queuePendingPublish(buffer, n)) {
        return;
    }

    if (mqttClient.connected()) {
        flushPendingPublish();
    } else {
        Serial.println("[MQTT] Not connected yet, queued publish");
    }
}

void publishRawJSON(const char* jsonString) {
    if (!s_serviceStarted || s_stopRequested) {
        Serial.println("[MQTT] Service not active, cannot publish");
        return;
    }

    if (jsonString == nullptr) {
        Serial.println("[MQTT] Raw JSON is null, cannot publish");
        return;
    }

    const size_t payloadLength = strlen(jsonString);
    if (!queuePendingPublish(jsonString, payloadLength)) {
        return;
    }

    if (mqttClient.connected()) {
        flushPendingPublish();
    } else {
        Serial.println("[MQTT] Not connected yet, queued publish");
    }
}

bool isConnected() {
    return mqttClient.connected();
}

void update() {
    if (!s_serviceStarted || s_stopRequested) {
        return;
    }

    const unsigned long now = millis();

    if (WiFi.status() != WL_CONNECTED) {
        if (mqttClient.connected()) {
            mqttClient.disconnect();
        }

        mqttConnected = false;
        if (s_state != MqttConnectionState::WaitingForWifi) {
            Serial.println("[MQTT] WiFi lost, waiting for link recovery");
        }

        s_state = MqttConnectionState::WaitingForWifi;
        s_nextStateCheckMs = now + MQTT_WIFI_RECOVERY_DELAY_MS;
        return;
    }

    if (s_state == MqttConnectionState::WaitingForWifi || s_state == MqttConnectionState::Backoff) {
        if (now < s_nextStateCheckMs) {
            return;
        }

        s_state = MqttConnectionState::Idle;
    }

    if (s_state == MqttConnectionState::Idle) {
        if (!mqttClient.connected()) {
            mqtt_reconnect(now);
            return;
        }

        mqttConnected = true;
        s_state = MqttConnectionState::Online;
    }

    if (s_state == MqttConnectionState::Online) {
        mqttConnected = true;
        flushPendingPublish();
        mqttClient.loop();

        if (!mqttClient.connected()) {
            mqttConnected = false;
            if (s_connectFailureCount < 255) {
                ++s_connectFailureCount;
            }

            const unsigned long backoffMs = computeBackoffMs(s_connectFailureCount);
            s_nextStateCheckMs = now + backoffMs;
            s_state = MqttConnectionState::Backoff;

            Serial.print("[MQTT] Connection lost, retry in ");
            Serial.print(backoffMs);
            Serial.print(" ms (failures=");
            Serial.print(s_connectFailureCount);
            Serial.println(")");
        }
    }
}

unsigned long getLastPublishTime() {
    return lastPublishTime;
}

void setPublishInterval(unsigned long intervalMs) {
    publishInterval = intervalMs;
}

TaskHandle_t getTaskHandle() {
    return mqtt_task_handle;
}

} // namespace MQTTSync
