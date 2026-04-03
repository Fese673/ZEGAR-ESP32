#include "MQTTSync.h"
#include "WiFiSync.h"
#include <NetworkClientSecure.h>
#include <WiFi.h>

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

// ============================================================================
// Static Variables
// ============================================================================
static NetworkClientSecure wifiClientSecure;
static PubSubClient mqttClient;
static unsigned long lastPublishTime = 0;
static unsigned long publishInterval = 5000; // 5 seconds
static bool mqttConnected = false;
static TaskHandle_t mqtt_task_handle = NULL;
static Config s_config;

// ============================================================================
// Forward declarations
// ============================================================================
static void mqtt_callback(char* topic, byte* payload, unsigned int length);
static void mqtt_reconnect();
static void mqtt_task(void *parameter);
static void applyConfigToClient();

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
}

void configure(const Config& config) {
    s_config = config;
    applyConfigToClient();
}

Config currentConfig() {
    return s_config;
}

// ============================================================================
// MQTT Reconnection Logic
// ============================================================================
static void mqtt_reconnect() {
    // Check WiFi status
    int wifi_status = WiFi.status();
    if (wifi_status != WL_CONNECTED) {
        Serial.print("[MQTT] WiFi not connected. Status: ");
        Serial.println(wifi_status);
        Serial.print("[MQTT] Current IP: ");
        Serial.println(WiFi.localIP());
        return;
    }

    Serial.print("[MQTT] WiFi connected. Attempting MQTT connection to ");
    Serial.print(s_config.brokerAddress);
    Serial.println("...");

    // Try to connect
    if (mqttClient.connect(s_config.clientId.c_str(),
                           s_config.username.c_str(),
                           s_config.password.c_str())) {
        Serial.println("[MQTT] ✅ Connected to HiveMQ Cloud!");
        mqttConnected = true;
        // Trigger immediate NTP sync when MQTT becomes connected
        Serial.println("[MQTT] Requesting background NTP sync via WiFiSync::requestTimeSync()");
        WiFiSync::requestTimeSync();
        
        // Publish-only mode: do not subscribe to any topics
        // mqttClient.subscribe("sensors/device1/cmd");
        
    } else {
        int rc = mqttClient.state();
        Serial.print("[MQTT] ❌ Connection failed, rc=");
        Serial.print(rc);
        Serial.print(" (");
        switch(rc) {
            case -4: Serial.print("MQTT_CONNECTION_TIMEOUT"); break;
            case -3: Serial.print("MQTT_CONNECTION_LOST"); break;
            case -2: Serial.print("MQTT_CONNECT_FAILED - TCP connection failed"); break;
            case -1: Serial.print("MQTT_DISCONNECTED"); break;
            case 0: Serial.print("MQTT_CONNECTED"); break;
            case 1: Serial.print("MQTT_CONNECT_BAD_PROTOCOL"); break;
            case 2: Serial.print("MQTT_CONNECT_BAD_CLIENT_ID"); break;
            case 3: Serial.print("MQTT_CONNECT_UNAVAILABLE"); break;
            case 4: Serial.print("MQTT_CONNECT_BAD_CREDENTIALS"); break;
            case 5: Serial.print("MQTT_CONNECT_UNAUTHORIZED"); break;
            default: Serial.print("UNKNOWN"); break;
        }
        Serial.println(")");
        mqttConnected = false;
    }
}

// ============================================================================
// MQTT Task (runs on Core 1)
// ============================================================================
static void mqtt_task(void *parameter) {
    Serial.println("[MQTT] Task started on Core 1");
    
    // Small delay to let WiFi stabilize
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    unsigned long lastReconnectAttempt = 0;
    
    while (1) {
        // Try to reconnect if not connected (but not every loop - max every 5 seconds)
        if (!mqttClient.connected()) {
            if (millis() - lastReconnectAttempt >= 5000) {
                lastReconnectAttempt = millis();
                mqtt_reconnect();
            }
        }
        
        // This handles incoming messages and keeps connection alive
        if (mqttClient.connected()) {
            mqttClient.loop();
        }
        
        // Small delay to prevent watchdog issues
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
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

    // Set up MQTT client with secure WiFi client
    applyConfigToClient();

    // Configure buffer sizes for JSON payload (increase to support particle arrays)
    mqttClient.setBufferSize(1024);
    
    Serial.println("[MQTT] Client configured");
    Serial.print("[MQTT] Broker: ");
    Serial.println(s_config.brokerAddress);
    Serial.print("[MQTT] Port: ");
    Serial.println(s_config.brokerPort);
}

void startCore1Task() {
    if (mqtt_task_handle == NULL) {
        xTaskCreatePinnedToCore(
            mqtt_task,                    // Function to implement the task
            "mqtt_task",                  // Name of the task
            8192,                         // Stack size in words (32KB)
            NULL,                         // Task input parameter
            2,                            // Priority of the task (higher = more important)
            &mqtt_task_handle,            // Task handle
            1                             // Core ID (0 or 1)
        );
        Serial.println("[MQTT] Task created on Core 1");
    }
}

void stopCore1Task() {
    if (mqtt_task_handle != NULL) {
        // Disconnect MQTT first
        mqttClient.disconnect();
        mqttConnected = false;
        
        // Delete the task
        vTaskDelete(mqtt_task_handle);
        mqtt_task_handle = NULL;
        
        Serial.println("[MQTT] Task stopped and deleted");
    }
}

void publishSensorData(float temp, int humidity, int pressure,
                       uint8_t aqi, uint16_t tvoc, uint16_t eco2) {
    // NOTE: scheduling is handled by the caller (`main.cpp`).
    // Do not duplicate rate-limiting here to avoid missing every-other publish.
    // Check if connected
    if (!mqttClient.connected()) {
        Serial.println("[MQTT] Not connected, cannot publish");
        return;
    }

    // Compact JSON payload as an array to save bytes:
    // [ t, h, p, aqi, tvoc, eco2, ts, [A_pm1,A_pm25,A_pm10], [n0.3,n0.5,1.0,2.5,5.0,10.0] ]
    StaticJsonDocument<512> doc;
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

    // Debug: log particle counts before serialization
    Serial.print("[MQTT] Particles: ");
    Serial.print(pms5003_particleCount_0_3); Serial.print(",");
    Serial.print(pms5003_particleCount_0_5); Serial.print(",");
    Serial.print(pms5003_particleCount_1_0); Serial.print(",");
    Serial.print(pms5003_particleCount_2_5); Serial.print(",");
    Serial.print(pms5003_particleCount_5_0); Serial.print(",");
    Serial.println(pms5003_particleCount_10_0);

    // Serialize to string
    char buffer[1024];
    size_t n = serializeJson(doc, buffer, sizeof(buffer));
    Serial.print("[MQTT] Payload size: "); Serial.println(n);

    // Publish
    if (mqttClient.publish(s_config.topic.c_str(), buffer)) {
        Serial.print("[MQTT] Published: ");
        Serial.println(buffer);
        lastPublishTime = millis();
    } else {
        Serial.println("[MQTT] Publish failed!");
    }
}

void publishRawJSON(const char* jsonString) {
    if (!mqttClient.connected()) {
        Serial.println("[MQTT] Not connected, cannot publish");
        return;
    }
    
    if (mqttClient.publish(s_config.topic.c_str(), jsonString)) {
        Serial.print("[MQTT] Published: ");
        Serial.println(jsonString);
        lastPublishTime = millis();
    } else {
        Serial.println("[MQTT] Publish failed!");
    }
}

bool isConnected() {
    return mqttClient.connected();
}

void update() {
    // Trigger publish from main loop (optional)
    // The mqtt_task handles the connection automatically
}

unsigned long getLastPublishTime() {
    return lastPublishTime;
}

void setPublishInterval(unsigned long intervalMs) {
    publishInterval = intervalMs;
}

} // namespace MQTTSync
