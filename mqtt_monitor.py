#!/usr/bin/env python3
"""
ESP32 MQTT Monitor - Python script for Raspberry Pi
Subscribes to ESP32 sensor data and logs it
"""

import paho.mqtt.client as mqtt
import json
from datetime import datetime
import ssl

# ============================================================================
# Configuration
# ============================================================================
BROKER = "984611e746574f4e8a109c011da25400.s1.eu.hivemq.cloud"
PORT = 8883
USERNAME = "ESP32_CLOCK"
PASSWORD = "SDdfs32sSD"
TOPIC = "sensors/device1/data"
CLIENT_ID = "ESP32_CLOCK"

# ============================================================================
# Callbacks
# ============================================================================
def on_connect(client, userdata, flags, rc):
    """Callback when client connects to broker"""
    if rc == 0:
        print(f"[{datetime.now().strftime('%H:%M:%S')}] ✅ Connected to MQTT Broker")
        client.subscribe(TOPIC)
        print(f"[{datetime.now().strftime('%H:%M:%S')}] 📡 Subscribed to: {TOPIC}")
    else:
        print(f"❌ Connection failed with code {rc}")

def on_message(client, userdata, msg):
    """Callback when message is received"""
    try:
        payload = json.loads(msg.payload.decode())
        timestamp = datetime.now().strftime('%H:%M:%S')
        
        temp = payload.get('t', 'N/A')
        humidity = payload.get('h', 'N/A')
        pressure = payload.get('p', 'N/A')
        device_ts = payload.get('ts', 'N/A')
        
        print(f"[{timestamp}] 📊 ESP32 Data:")
        print(f"  ├─ Temperature: {temp}°C")
        print(f"  ├─ Humidity:    {humidity}%")
        print(f"  ├─ Pressure:    {pressure}hPa")
        print(f"  └─ Device TS:   {device_ts}ms")
        
    except json.JSONDecodeError:
        print(f"❌ Invalid JSON: {msg.payload}")
    except Exception as e:
        print(f"❌ Error: {e}")

def on_disconnect(client, userdata, rc):
    """Callback when client disconnects"""
    if rc != 0:
        print(f"❌ Unexpected disconnection (code {rc})")
    else:
        print("⏹️  Disconnected from broker")

# ============================================================================
# Main
# ============================================================================
if __name__ == "__main__":
    client = mqtt.Client(client_id=CLIENT_ID)
    
    # Set credentials
    client.username_pw_set(USERNAME, PASSWORD)
    
    # Set callbacks
    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = on_disconnect
    
    # Configure TLS
    client.tls_set(ca_certs=None, certfile=None, keyfile=None, cert_reqs=ssl.CERT_REQUIRED, tls_version=ssl.PROTOCOL_TLSv1_2, ciphers=None)
    client.tls_insecure_set(False)
    
    try:
        print(f"🔗 Connecting to {BROKER}:{PORT}...")
        client.connect(BROKER, PORT, keepalive=60)
        
        # Block until interrupt
        client.loop_forever()
        
    except KeyboardInterrupt:
        print("\n\n⏹️  Stopping...")
        client.disconnect()
    except Exception as e:
        print(f"❌ Fatal error: {e}")
