#!/usr/bin/env python3
"""
MQTT to Firebase Realtime Database Bridge
Parses compact array payload from ESP32 and stores in Firebase.

Payload format: [t, h, p, ts, [F_pm1,F_pm25,F_pm10], [A_pm1,A_pm25,A_pm10], [n0.3,n0.5,1.0,2.5,5.0,10.0]]
"""
import os
import json
import ssl
import time
from datetime import datetime
from dotenv import load_dotenv
import paho.mqtt.client as mqtt
import firebase_admin
from firebase_admin import credentials, db as rtdb  # ← REALTIME DATABASE!

# Załaduj zmienne z .env
load_dotenv()

# Konfiguracja HiveMQ
HIVEMQ_HOST = os.getenv('HIVEMQ_HOST')
HIVEMQ_PORT = int(os.getenv('HIVEMQ_PORT', 8883))
HIVEMQ_USERNAME = os.getenv('HIVEMQ_USERNAME')
HIVEMQ_PASSWORD = os.getenv('HIVEMQ_PASSWORD')

# Konfiguracja Firebase
FIREBASE_CREDS = os.getenv('FIREBASE_CREDS')
FIREBASE_DB_URL = os.getenv('FIREBASE_DATABASE_URL')

# Topici do subskrypcji
MQTT_TOPICS = [
    ("sensors/#", 0),
    ("devices/#", 0)
]

# Inicjalizacja Firebase Realtime Database
try:
    cred = credentials.Certificate(FIREBASE_CREDS)
    firebase_admin.initialize_app(cred, {
        'databaseURL': FIREBASE_DB_URL
    })
    print(f"[INFO] Firebase Realtime Database initialized: {FIREBASE_DB_URL}")
except Exception as e:
    print(f"[FATAL] Firebase initialization failed: {e}")
    exit(1)

# Funkcja do parsowania payload - obsługuje zarówno stary format JSON, jak i nowy format tablicowy
def parse_payload(payload_str):
    """
    Parse both old JSON format and new compact array format.
    
    Old format (JSON): {"t": 23.5, "h": 65, "p": 1013, "ts": 12345, ...}
    New format (array): [t, h, p, ts, [F_pm1,F_pm25,F_pm10], [A_pm1,A_pm25,A_pm10], [n0.3,n0.5,1.0,2.5,5.0,10.0]]
    
    Returns dict with standardized keys: t, h, p, ts, F (CF1), A (ATM), particles
    """
    try:
        data = json.loads(payload_str)
        
        # Nowy format - tablica
        if isinstance(data, list):
            if len(data) < 4:
                print(f"[WARN] Array format detected but too short (len={len(data)}), skipping")
                return {}
            
            print(f"[PARSE] Detected array format, length: {len(data)}")
            
            result = {
                "t": float(data[0]) if data[0] is not None else None,
                "h": int(data[1]) if data[1] is not None else None,
                "p": int(data[2]) if data[2] is not None else None,
                "ts": int(data[3]) if data[3] is not None else None,
            }
            
            # CF=1 (Factory calibration)
            if len(data) > 4 and isinstance(data[4], list) and len(data[4]) >= 3:
                result["F"] = {
                    "pm1": int(data[4][0]),
                    "pm25": int(data[4][1]),
                    "pm10": int(data[4][2])
                }
                print(f"[PARSE] CF=1 (F): pm1={data[4][0]}, pm25={data[4][1]}, pm10={data[4][2]}")
            
            # ATM (Atmospheric)
            if len(data) > 5 and isinstance(data[5], list) and len(data[5]) >= 3:
                result["A"] = {
                    "pm1": int(data[5][0]),
                    "pm25": int(data[5][1]),
                    "pm10": int(data[5][2])
                }
                print(f"[PARSE] ATM (A): pm1={data[5][0]}, pm25={data[5][1]}, pm10={data[5][2]}")
            
            # Particle counts (#/100cm3)
            if len(data) > 6 and isinstance(data[6], list) and len(data[6]) >= 6:
                result["particles"] = {
                    "0p3": int(data[6][0]),
                    "0p5": int(data[6][1]),
                    "1p0": int(data[6][2]),
                    "2p5": int(data[6][3]),
                    "5p0": int(data[6][4]),
                    "10p0": int(data[6][5])
                }
                print(f"[PARSE] Particles: 0.3μm={data[6][0]}, 0.5μm={data[6][1]}, 1.0μm={data[6][2]}, 2.5μm={data[6][3]}, 5.0μm={data[6][4]}, 10.0μm={data[6][5]}")
            
            return result
        
        # Stary format - JSON with keys (dict)
        elif isinstance(data, dict):
            print(f"[PARSE] Detected JSON object format")
            
            # Ekstrakcja wartości (elastyczne nazwy pól)
            temp = data.get('t') or data.get('temperature') or data.get('temp')
            hum = data.get('h') or data.get('humidity') or data.get('hum')
            press = data.get('p') or data.get('pressure') or data.get('press')
            
            result = {
                "t": float(temp) if temp is not None else None,
                "h": int(hum) if hum is not None else None,
                "p": int(press) if press is not None else None,
                "ts": int(data.get('ts', int(time.time()))) if data.get('ts') is not None else int(time.time())
            }
            
            # PMS data from JSON
            if 'F' in data and isinstance(data['F'], dict):
                result["F"] = data['F']
            if 'A' in data and isinstance(data['A'], dict):
                result["A"] = data['A']
            if 'particles' in data and isinstance(data['particles'], dict):
                result["particles"] = data['particles']
            
            return result
        
        else:
            print(f"[WARN] Unknown format type: {type(data)}")
            return {}
    
    except json.JSONDecodeError:
        print(f"[WARN] Not JSON, treating as text: {payload_str}")
        return {"value": payload_str}

# Funkcja callback - połączenie z HiveMQ
def on_connect(client, userdata, flags, reason_code, properties=None):
    if reason_code == 0:
        print(f"[SUCCESS] Connected to HiveMQ: {HIVEMQ_HOST}")
        for topic, qos in MQTT_TOPICS:
            client.subscribe(topic, qos)
            print(f"[SUBSCRIBE] Topic: {topic} (QoS {qos})")
    else:
        print(f"[ERROR] Connection failed with code {reason_code}")
        if reason_code == 4:
            print("[ERROR] Check HIVEMQ_USERNAME and HIVEMQ_PASSWORD in .env")
        elif reason_code == 5:
            print("[ERROR] Not authorized")

# Funkcja callback - odbiór wiadomości
def on_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode('utf-8')
    
    print(f"[RECEIVED] Topic: {topic} | Payload length: {len(payload)} bytes")
    
    try:
        # Parsuj payload (obsługuje zarówno JSON jak i array format)
        data = parse_payload(payload)
        print(f"[PARSED] Data: {data}")
        
        # Ensure data is always a dict
        if not isinstance(data, dict):
            print(f"[ERROR] parse_payload did not return dict, got {type(data)}")
            return
        
        # Jeśli brakuje ts, użyj obecnego czasu
        if data.get("ts") is None:
            data["ts"] = int(time.time())
        
        # Przygotuj dane do Firebase
        latest = {}
        
        # Podstawowe sensory (DHT/BME)
        if data.get("t") is not None:
            latest["t"] = data["t"]
        if data.get("h") is not None:
            latest["h"] = data["h"]
        if data.get("p") is not None:
            latest["p"] = data["p"]
        
        latest["ts"] = data["ts"]
        
        # Dodaj PMS5003 dane jeśli dostępne
        if "F" in data:
            latest["F"] = data["F"]
        if "A" in data:
            latest["A"] = data["A"]
        if "particles" in data:
            latest["particles"] = data["particles"]
        
        # Jeśli mamy przynajmniej jedną wartość
        if any(k in latest for k in ['t', 'h', 'p', 'F', 'A', 'particles']):
            # 1. Zaktualizuj latest (NADPISZ)
            latest_ref = rtdb.reference('devices/device1/latest')
            latest_ref.set(latest)
            print(f"[FIREBASE] Updated devices/device1/latest")
            
            # 2. Dodaj do history (PUSH)
            history_ref = rtdb.reference('devices/device1/history')
            new_ref = history_ref.push(latest)
            print(f"[FIREBASE] Pushed to history: {new_ref.key}")
        else:
            print(f"[SKIP] No sensor data found in payload")
        
        # Opcjonalnie: backup RAW do mqtt_data (dla debugowania)
        try:
            raw_backup = {
                'topic': topic,
                'timestamp': datetime.now().isoformat(),
                'source': 'hivemq',
                'ts': data.get("ts", int(time.time())),
                'payload_bytes': len(payload)
            }
            # Dodaj parsowane dane do backupu
            if isinstance(data, dict):
                raw_backup['parsed'] = {k: v for k, v in data.items() if k in ['t', 'h', 'p', 'F', 'A', 'particles']}
            
            rtdb.reference('mqtt_data').push(raw_backup)
        except Exception as backup_err:
            print(f"[WARN] Backup to mqtt_data failed: {backup_err}")
        
    except Exception as e:
        print(f"[ERROR] Failed to process message: {e}")
        import traceback
        traceback.print_exc()

# Funkcja callback - disconnect
def on_disconnect(client, userdata, disconnect_flags, reason_code, properties=None):
    if reason_code != 0:
        print(f"[WARNING] Unexpected disconnect. Code: {reason_code}")
        print("[INFO] Attempting to reconnect...")

# Funkcja callback - subscribe confirmation
def on_subscribe(client, userdata, mid, reason_code_list, properties=None):
    print(f"[INFO] Subscription confirmed (message ID: {mid})")

# Inicjalizacja MQTT client (MQTT 5.0 z nową wersją API)
client = mqtt.Client(
    callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
    client_id="raspberry-pi-bridge", 
    protocol=mqtt.MQTTv5
)
client.username_pw_set(HIVEMQ_USERNAME, HIVEMQ_PASSWORD)
client.tls_set(cert_reqs=ssl.CERT_REQUIRED, tls_version=ssl.PROTOCOL_TLSv1_2)

# Przypisz callbacki
client.on_connect = on_connect
client.on_message = on_message
client.on_disconnect = on_disconnect
client.on_subscribe = on_subscribe

# Połącz z HiveMQ
print(f"[INFO] Connecting to HiveMQ: {HIVEMQ_HOST}:{HIVEMQ_PORT}")
print(f"[INFO] Username: {HIVEMQ_USERNAME}")
print("[INFO] Listening for both array and JSON payload formats...")

try:
    client.connect(HIVEMQ_HOST, HIVEMQ_PORT, keepalive=60)
    print("[INFO] Bridge started. Press Ctrl+C to stop.")
    client.loop_forever()
    
except KeyboardInterrupt:
    print("\n[INFO] Shutting down bridge...")
    client.disconnect()
    print("[INFO] Disconnected from HiveMQ")
    
except Exception as e:
    print(f"[ERROR] Connection error: {e}")
    import traceback
    traceback.print_exc()
