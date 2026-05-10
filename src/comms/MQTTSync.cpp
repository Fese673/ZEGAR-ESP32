#include "MQTTSync.h"
#include "WiFiSync.h"
#include "AppLog.h"
#include <esp_system.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <atomic>

#include "PMS_Czujnik.h"
#include "RamTelemetry.h"

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

static constexpr const char* TAG = "MQTT";

static constexpr size_t MQTT_JSON_BUFFER_SIZE = 192;
static constexpr size_t MQTT_PACKET_BUFFER_SIZE = 256;
static constexpr size_t MQTT_JSON_DOC_CAPACITY = 384;
static constexpr size_t MQTT_PACKET_MARGIN_BYTES = 8;

// ============================================================================
// Static Variables
// ============================================================================
static WiFiClientSecure wifiClientSecure;
static PubSubClient mqttClient;
static unsigned long lastPublishTime = 0;
static unsigned long publishInterval = 5000; // 5 seconds
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
static unsigned long s_lastTransitionMs = 0;
static unsigned long s_delayMs = 0;
static unsigned long s_connectStartMs = 0;
static uint8_t s_connectFailureCount = 0;
static TaskHandle_t s_connectTask = nullptr;
static std::atomic<bool> s_connectInProgress{false};
static std::atomic<bool> s_connectResult{false};

static constexpr unsigned long MQTT_WIFI_RECOVERY_DELAY_MS = 250;
static constexpr unsigned long MQTT_CONNECT_BUDGET_MS = 4500;
static constexpr unsigned long MQTT_TCP_CONNECT_TIMEOUT_SEC = 3;
static constexpr unsigned long MQTT_TLS_HANDSHAKE_TIMEOUT_SEC = 2;
static constexpr uint16_t MQTT_SOCKET_TIMEOUT_SEC = 1;
static constexpr uint16_t MQTT_KEEPALIVE_SEC = 15;
static constexpr uint32_t MQTT_CONNECT_TASK_STACK_BYTES = 12288;

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
    char payloadText[MQTT_PACKET_BUFFER_SIZE];
    const size_t copyLength = (length < (sizeof(payloadText) - 1)) ? length : (sizeof(payloadText) - 1);
    memcpy(payloadText, payload, copyLength);
    payloadText[copyLength] = '\0';
    LOG_I(TAG, "Message received topic=%s payload=%s payload_len=%u", topic, payloadText, length);
}

static void mqttConnectTask(void*) {
    const bool ok = mqttClient.connect(s_config.clientId.c_str(),
                                        s_config.username.c_str(),
                                        s_config.password.c_str());
    s_connectResult.store(ok);
    s_connectInProgress.store(false);
    vTaskDelete(nullptr);
}

static void applyConfigToClient() {
    mqttClient.setClient(wifiClientSecure);
    mqttClient.setServer(s_config.brokerAddress.c_str(), s_config.brokerPort);
    mqttClient.setCallback(mqtt_callback);
    mqttClient.setKeepAlive(MQTT_KEEPALIVE_SEC);
    mqttClient.setSocketTimeout(MQTT_SOCKET_TIMEOUT_SEC);
}

static void applyConnectionBudget() {
    wifiClientSecure.setTimeout(MQTT_TCP_CONNECT_TIMEOUT_SEC);
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
        LOG_W(TAG, "Empty payload action=queue_skip reason=null_payload");
        return false;
    }

    if (payloadLength >= sizeof(s_pendingPayload)) {
        LOG_W(TAG, "Payload too large reason=staging payload=%u buffer=%u", (unsigned)payloadLength, (unsigned)sizeof(s_pendingPayload));
        return false;
    }

    if (!mqttPayloadFits(payloadLength)) {
        LOG_W(TAG, "Payload too large reason=packet payload=%u topic_len=%u buffer=%u", (unsigned)payloadLength, (unsigned)s_config.topic.length(), (unsigned)MQTT_PACKET_BUFFER_SIZE);
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
        LOG_I(TAG, "Published bytes=%u payload=%s", (unsigned)s_pendingPayloadLength, s_pendingPayload);
        lastPublishTime = millis();
        s_pendingPayloadReady = false;
        s_pendingPayloadLength = 0;
    } else {
        LOG_E(TAG, "Publish failed topic=%s bytes=%u", s_config.topic.c_str(), (unsigned)s_pendingPayloadLength);
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
        LOG_I(TAG, "WiFi not connected state=waiting_for_link");
        s_state = MqttConnectionState::WaitingForWifi;
        s_lastTransitionMs = nowMs;
        s_delayMs = MQTT_WIFI_RECOVERY_DELAY_MS;
        return false;
    }

    LOG_I(TAG, "Connecting broker=%s port=%u", s_config.brokerAddress.c_str(), (unsigned)s_config.brokerPort);

    s_state = MqttConnectionState::Connecting;
    s_connectStartMs = nowMs;
    s_connectInProgress.store(true);
    s_connectResult.store(false);

    BaseType_t created = xTaskCreatePinnedToCore(
        mqttConnectTask,
        "mqttConn",
        MQTT_CONNECT_TASK_STACK_BYTES,
        nullptr,
        1,
        &s_connectTask,
        0
    );

    if (created != pdPASS) {
        LOG_E(TAG, "Failed to create connection task, connecting synchronously");
        s_connectInProgress.store(false);
        s_connectTask = nullptr;

        const bool connected = mqttClient.connect(s_config.clientId.c_str(),
                                                  s_config.username.c_str(),
                                                  s_config.password.c_str());
        const unsigned long elapsedMs = millis() - s_connectStartMs;

        if (connected && elapsedMs <= MQTT_CONNECT_BUDGET_MS) {
            LOG_I(TAG, "Connected elapsed_ms=%lu", elapsedMs);
            s_connectFailureCount = 0;
            s_state = MqttConnectionState::Online;
            WiFiSync::requestTimeSync();
            RAM_CHECKPOINT("MQTT_CONNECTED");
            return true;
        }

        if (connected) {
            LOG_W(TAG, "Connect exceeded budget elapsed_ms=%lu budget_ms=%lu", elapsedMs, MQTT_CONNECT_BUDGET_MS);
            mqttClient.disconnect();
        }

        const int rc = mqttClient.state();
        if (s_connectFailureCount < 255) ++s_connectFailureCount;

        const unsigned long backoffMs = computeBackoffMs(s_connectFailureCount);
        s_lastTransitionMs = nowMs;
        s_delayMs = backoffMs;
        s_state = MqttConnectionState::Backoff;

        LOG_W(TAG, "Connection failed rc=%d elapsed_ms=%lu failures=%u backoff_ms=%lu", rc, elapsedMs, (unsigned)s_connectFailureCount, backoffMs);
        return false;
    }

    return false;
}

// ============================================================================
// Public API Implementation
// ============================================================================

void begin(const char* ssid, const char* password) {
    (void)ssid;
    (void)password;

    LOG_I(TAG, "Initializing MQTT client");
    
    // Configure secure WiFi client with CA certificate
    wifiClientSecure.setCACert(g_mqtt_ca_cert);
    applyConnectionBudget();

    // Set up MQTT client with secure WiFi client
    applyConfigToClient();

    // Right-size the MQTT packet buffer for the current sensor payloads.
    mqttClient.setBufferSize(MQTT_PACKET_BUFFER_SIZE);

    s_serviceStarted = false;
    s_connectFailureCount = 0;
    s_lastTransitionMs = 0;
    s_delayMs = 0;
    s_connectStartMs = 0;
    s_state = (WiFi.status() == WL_CONNECTED)
        ? MqttConnectionState::Idle
        : MqttConnectionState::WaitingForWifi;
    s_pendingPayloadReady = false;
    s_pendingPayloadLength = 0;
    s_pendingPayload[0] = '\0';
    
    LOG_I(TAG, "Client configured broker=%s port=%u", s_config.brokerAddress.c_str(), (unsigned)s_config.brokerPort);
}

void startCore1Task() {
    if (s_serviceStarted) {
        return;
    }

    s_serviceStarted = true;
    s_state = MqttConnectionState::WaitingForWifi;
    s_lastTransitionMs = millis();
    s_delayMs = MQTT_WIFI_RECOVERY_DELAY_MS;
    s_connectStartMs = 0;
    LOG_I(TAG, "Service started scope=main_loop");
    RAM_CHECKPOINT("MQTT_ON");
}

void stopCore1Task() {
    if (!s_serviceStarted && mqtt_task_handle == NULL) {
        return;
    }

    if (s_connectInProgress.load()) {
        unsigned long waitStart = millis();
        while (s_connectInProgress.load() && (millis() - waitStart) < 2000) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (s_connectInProgress.load()) {
            LOG_W(TAG, "Connection task did not finish, proceeding");
        }
        s_connectInProgress.store(false);
        s_connectTask = nullptr;
    }

    s_serviceStarted = false;
    s_state = MqttConnectionState::WaitingForWifi;
    s_lastTransitionMs = 0;
    s_delayMs = 0;
    s_connectStartMs = 0;
    s_pendingPayloadReady = false;
    s_pendingPayloadLength = 0;
    s_pendingPayload[0] = '\0';
    mqttClient.disconnect();
    mqtt_task_handle = NULL;

    LOG_I(TAG, "Service stopped");
    RAM_CHECKPOINT("MQTT_OFF");
}

void publishSensorData(float temp, int humidity, int pressure,
                       uint8_t aqi, uint16_t tvoc, uint16_t eco2) {
    if (!s_serviceStarted) {
        LOG_W(TAG, "Service not active action=publish_sensor_data");
        return;
    }

    // NOTE: scheduling is handled by the caller (`main.cpp`).
    // Do not duplicate rate-limiting here to avoid missing every-other publish.
    // Compact JSON payload as an array to save bytes:
    // [ t, h, p, aqi, tvoc, eco2, ts, [A_pm1,A_pm25,A_pm10], [n0.3,n0.5,1.0,2.5,5.0,10.0] ]
    StaticJsonDocument<MQTT_JSON_DOC_CAPACITY> doc;
    JsonArray root = doc.to<JsonArray>();
    const PMS5003Sensor::MassReadings atmospheric = PMS5003Sensor::getAtmospheric();
    const PMS5003Sensor::ParticleCounts particleCounts = PMS5003Sensor::getParticleCounts();
    char tempBuffer[16];
    snprintf(tempBuffer, sizeof(tempBuffer), "%.2f", temp);
    root.add(serialized(tempBuffer));
    root.add(humidity);
    root.add(pressure);
    root.add(aqi);
    root.add(tvoc);
    root.add(eco2);
    root.add(millis());

    LOG_I(TAG, "ENS160 values aqi=%u tvoc=%u eco2=%u", (unsigned)aqi, (unsigned)tvoc, (unsigned)eco2);

    JsonArray a = root.createNestedArray();
    a.add(atmospheric.pm01);
    a.add(atmospheric.pm25);
    a.add(atmospheric.pm10);

    JsonArray particleArray = root.createNestedArray();
    particleArray.add(particleCounts.count0p3);
    particleArray.add(particleCounts.count0p5);
    particleArray.add(particleCounts.count1p0);
    particleArray.add(particleCounts.count2p5);
    particleArray.add(particleCounts.count5p0);
    particleArray.add(particleCounts.count10p0);

    if (doc.overflowed()) {
        LOG_E(TAG, "JSON document overflow action=skip_publish");
        return;
    }

    const size_t payloadLength = measureJson(doc);
    if (payloadLength >= MQTT_JSON_BUFFER_SIZE) {
        LOG_W(TAG, "JSON payload exceeds staging buffer payload=%u buffer=%u", (unsigned)payloadLength, (unsigned)MQTT_JSON_BUFFER_SIZE);
        return;
    }
    if (!mqttPayloadFits(payloadLength)) {
        LOG_W(TAG, "Payload too large reason=packet payload=%u topic_len=%u buffer=%u", (unsigned)payloadLength, (unsigned)s_config.topic.length(), (unsigned)MQTT_PACKET_BUFFER_SIZE);
        return;
    }

    // Debug: log particle counts before serialization
    LOG_I(TAG, "Particles p0_3=%u p0_5=%u p1_0=%u p2_5=%u p5_0=%u p10_0=%u",
            (unsigned)particleCounts.count0p3,
            (unsigned)particleCounts.count0p5,
            (unsigned)particleCounts.count1p0,
            (unsigned)particleCounts.count2p5,
            (unsigned)particleCounts.count5p0,
            (unsigned)particleCounts.count10p0);

    // Serialize to string
    char buffer[MQTT_JSON_BUFFER_SIZE];
    size_t n = serializeJson(doc, buffer, sizeof(buffer));
    LOG_I(TAG, "Payload size bytes=%u", (unsigned)n);

    if (!queuePendingPublish(buffer, n)) {
        return;
    }

    if (mqttClient.connected()) {
        flushPendingPublish();
    } else {
        LOG_I(TAG, "Publish queued state=offline");
    }
}

void publishRawJSON(const char* jsonString) {
    if (!s_serviceStarted) {
        LOG_W(TAG, "Service not active action=publish_raw_json");
        return;
    }

    if (jsonString == nullptr) {
        LOG_W(TAG, "Raw JSON is null action=publish_skip");
        return;
    }

    const size_t payloadLength = strlen(jsonString);
    if (!queuePendingPublish(jsonString, payloadLength)) {
        return;
    }

    if (mqttClient.connected()) {
        flushPendingPublish();
    } else {
        LOG_I(TAG, "Publish queued state=offline");
    }
}

bool isConnected() {
    return mqttClient.connected();
}

void update() {
    if (!s_serviceStarted) {
        return;
    }

    const unsigned long now = millis();

    if (WiFi.status() != WL_CONNECTED) {
        if (mqttClient.connected()) {
            mqttClient.disconnect();
        }

        if (s_state != MqttConnectionState::WaitingForWifi) {
            LOG_W(TAG, "WiFi lost state=waiting_for_link_recovery");
        }

        s_state = MqttConnectionState::WaitingForWifi;
        s_lastTransitionMs = now;
        s_delayMs = MQTT_WIFI_RECOVERY_DELAY_MS;
        return;
    }

    if (s_state == MqttConnectionState::WaitingForWifi || s_state == MqttConnectionState::Backoff) {
        if (now - s_lastTransitionMs < s_delayMs) {
            return;
        }

        s_state = MqttConnectionState::Idle;
    }

    if (s_state == MqttConnectionState::Connecting) {
        if (s_connectInProgress.load()) {
            return;
        }
        s_connectTask = nullptr;

        const unsigned long elapsedMs = millis() - s_connectStartMs;
        const bool connected = s_connectResult.load();

        if (connected && elapsedMs <= MQTT_CONNECT_BUDGET_MS) {
            LOG_I(TAG, "Connected elapsed_ms=%lu", elapsedMs);
            s_connectFailureCount = 0;
            s_state = MqttConnectionState::Online;
            WiFiSync::requestTimeSync();
            RAM_CHECKPOINT("MQTT_CONNECTED");
            return;
        }

        if (connected) {
            LOG_W(TAG, "Connect exceeded budget elapsed_ms=%lu budget_ms=%lu", elapsedMs, MQTT_CONNECT_BUDGET_MS);
            mqttClient.disconnect();
        }

        const int rc = mqttClient.state();
        if (s_connectFailureCount < 255) ++s_connectFailureCount;

        const unsigned long backoffMs = computeBackoffMs(s_connectFailureCount);
        s_lastTransitionMs = now;
        s_delayMs = backoffMs;
        s_state = MqttConnectionState::Backoff;

        LOG_W(TAG, "Connection failed rc=%d elapsed_ms=%lu failures=%u backoff_ms=%lu",
              rc, elapsedMs, (unsigned)s_connectFailureCount, backoffMs);
        return;
    }

    if (s_state == MqttConnectionState::Idle) {
        if (!mqttClient.connected()) {
            mqtt_reconnect(now);
            return;
        }

        s_state = MqttConnectionState::Online;
    }

    if (s_state == MqttConnectionState::Online) {
        flushPendingPublish();
        mqttClient.loop();

        if (!mqttClient.connected()) {
            if (s_connectFailureCount < 255) {
                ++s_connectFailureCount;
            }

            const unsigned long backoffMs = computeBackoffMs(s_connectFailureCount);
            s_lastTransitionMs = now;
            s_delayMs = backoffMs;
            s_state = MqttConnectionState::Backoff;

            LOG_W(TAG, "Connection lost backoff_ms=%lu failures=%u", backoffMs, (unsigned)s_connectFailureCount);
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
