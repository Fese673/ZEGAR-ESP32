# MQTT Integration for ESP32 - Setup Guide

## ✅ Implementation Complete

### What's Been Done:

1. **PubSubClient & ArduinoJson** - Added to `platformio.ini`
2. **MQTTSync.h & MQTTSync.cpp** - Complete MQTT module with:
   - Secure TLS connection to HiveMQ Cloud
   - Runs on **Core 1** (second CPU core) - non-blocking
   - Automatic reconnection logic
   - JSON payload publishing
   - Static test data (no sensor dependency yet)

3. **Integration in main.cpp** - MQTT task auto-starts in `setup()`

---

## 🔧 Configuration

**HiveMQ Cloud Credentials** (in `MQTTSync.h`):
- **Broker:** `984611e746574f4e8a109c011da25400.s1.eu.hivemq.cloud`
- **Port:** `8883` (TLS required)
- **Username:** `ESP32_CLOCK`
- **Password:** `F43GJA1sdW`
- **Topic:** `sensors/device1/data`

**Publish Interval:** 5 seconds (configurable)

---

## 📊 Data Format (JSON)

Published every 5 seconds:
```json
{
  "t": 23.5,      // Temperature in °C (float)
  "h": 65,        // Humidity in % (int)
  "p": 1013,      // Pressure in hPa (int)
  "ts": 1234567   // Timestamp (millis)
}
```

Currently uses:
- `dhtTemperature` (from DHT sensor)
- `dhtHumidity` (from DHT sensor)
- `1013` (static pressure - can be replaced with BMP sensor)

---

## 🚀 How It Works

### Startup Sequence:
```
setup():
  ├─ WiFiSync::begin()
  ├─ MQTTSync::begin()     ← Configure MQTT client
  └─ MQTTSync::startCore1Task()  ← Create FreeRTOS task on Core 1

loop():
  ├─ Every 5 seconds:
  │  └─ MQTTSync::publishSensorData(temp, humidity, pressure)
  └─ mqtt_task (runs independently on Core 1)
```

### Core Distribution:
- **Core 0:** WiFi, Bluetooth, Main application logic
- **Core 1:** MQTT client (blocking operations safe here)

---

## 🧪 Testing

### 1. Check Serial Output
Look for these messages:
```
[MQTT] Initializing MQTT client...
[MQTT] Client configured
[MQTT] Broker: 984611e746574f4e8a109c011da25400.s1.eu.hivemq.cloud
[MQTT] Port: 8883
[main] MQTT Task scheduled for Core 1
[MQTT] Task started on Core 1
[MQTT] Connected to HiveMQ Cloud!
[MQTT] Published: {"t":23.5,"h":65,"p":1013,"ts":12345}
```

3. **Monitorowanie HiveMQ Cloud Dashboard**
1. Log in: https://console.hivemq.cloud/
2. Go to **Clients** tab
3. Look for `ESP32_CLOCK` client
4. Go to **Logs** tab
5. Filter topic: `sensors/device1/data`
6. Should see messages arriving every 5 seconds

### 3. Local MQTT Monitor (Optional)
Use `mosquitto_sub` if you have local MQTT tools:
```bash
mosquitto_sub -h 984611e746574f4e8a109c011da25400.s1.eu.hivemq.cloud \
              -p 8883 \
              --cafile ca.crt \
              -u SDdfs32sSD \
              -P F43GJA1sdW \
              -t "sensors/#" \
              -v
```

---

## 📝 API Reference

### Main Functions:

```cpp
// Initialize MQTT (call in setup())
MQTTSync::begin(ssid, password);

// Start MQTT task on Core 1
MQTTSync::startCore1Task();

// Publish sensor data
MQTTSync::publishSensorData(float temp, int humidity, int pressure);

// Publish custom JSON
MQTTSync::publishRawJSON(const char* jsonString);

// Check connection status
bool connected = MQTTSync::isConnected();

// Get last publish time
unsigned long lastTime = MQTTSync::getLastPublishTime();

// Change publish interval (default 5000ms)
MQTTSync::setPublishInterval(10000); // 10 seconds
```

---

## 🔐 Security Notes

- ✅ TLS/SSL encryption enabled (port 8883)
- ✅ CA certificate embedded in code
- ✅ Username/password authentication
- ⚠️ **TODO:** Consider storing credentials in encrypted NVS storage instead of hardcoded values

---

## 🐛 Troubleshooting

### MQTT not connecting?
1. **Check WiFi:** Ensure `WiFiSync::begin()` completes first
2. **Check credentials:** Verify in `MQTTSync.h`
3. **Check firewall:** Port 8883 must be open outbound
4. **Check certificate:** Ensure CA cert is valid

### Messages not publishing?
1. Check `MQTTSync::isConnected()` returns `true`
2. Check serial output for publish errors
3. Verify topic name matches `MQTT_TOPIC`

### Memory issues?
- Stack size: 8192 bytes (32KB)
- Buffer size: 512 bytes for JSON
- Increase if needed: `stackSize` param in `xTaskCreatePinnedToCore()`

---

## ✨ Next Steps

1. **Replace static pressure:** Connect BMP280 pressure sensor
2. **Add configuration menu:** Allow user to change topic, interval, broker
3. **Store credentials:** Use ESP32 NVS for secure storage
4. **Logging dashboard:** Create Python backend to log and visualize data
5. **Subscribe to commands:** Implement remote control via MQTT

---

## 📚 References

- PubSubClient: https://github.com/knolleary/pubsubclient
- ArduinoJson: https://arduinojson.org/
- HiveMQ Cloud: https://www.hivemq.cloud/
- ESP32 FreeRTOS: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos.html

---

**Status:** ✅ Ready for testing
**Last Updated:** 2026-01-27
