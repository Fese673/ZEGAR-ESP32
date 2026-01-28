#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <time.h>
#include <Esp.h>

#include "STM32_Data.h"
#include "LCDMirror.h"
#include "UI_Controller.h"
#include "Encoder.h"
#include "AppState.h"
#include "UI_Draw.h"
#include "WiFiSync.h"
#include "MQTTSync.h"
#include "StatsManager.h" 
#include "AudioBT.h" 
#include "ModeManager.h"
#include "RadioModeSwitch.h"
#include <DHT.h>

// ============================================================================
// STAŁE CZASOWE (zamiast magic numbers)
// ============================================================================
constexpr unsigned long CLOCK_TICK_MS       = 1000; // tykanie zegara co 1s
constexpr unsigned long MELODY_STEP_MS      = 300;  // krok melodii alarmu
constexpr unsigned long ALARM_DURATION_MS   = 5000; // jak długo gra alarm
constexpr unsigned long STM32_UPDATE_MS     = 500;  // odświeżanie danych STM32
constexpr unsigned long STM32_TIMEOUT_MS    = 3000; // timeout połączenia STM32
constexpr unsigned long STOPER_DRAW_MS      = 100;  // odświeżanie stopera
constexpr unsigned long WIFI_RETRY_DELAY_MS = 500;  // próba połączenia WiFi
constexpr int           WIFI_MAX_RETRIES    = 20;   // max prób połączenia
constexpr unsigned long MSG_DISPLAY_MS      = 1500; // wyświetlanie komunikatów
constexpr unsigned long SETUP_DELAY_MS      = 800;  // opóźnienie w setup()

// ============================================================================
// DEKLARACJE FUNKCJI (dla PlatformIO)
// ============================================================================


// --- Logika zegara ---
void tickClock();
void playAlarmMelody();

// --- Wrapper dla kompatybilności ---
void syncTimeFromWiFi() {
  // Uruchom sync w trybie Wi-Fi, wymuszając wyłączenie BT.
  ModeManager::wifiOn();
}

// --- zasoby systemu ---
void drawSystemResources();

// ============================================================================
// KONFIGURACJA SPRZĘTU (PINY + STAŁE)
// ============================================================================

// --- 7-SEG (74HC595) - piny zdefiniowane w UI_Draw.h jako makra ---
// DATA_PIN = 23, CLOCK_PIN = 18, LATCH_PIN = 5

// --- Buzzer ---
constexpr uint8_t BUZZER_PIN = 19;
const int melodyFreq[] = {1000, 1400, 1000, 1600};
constexpr int MELODY_LEN = 4;

// --- Encoder (piny dla encoder_begin) ---
constexpr uint8_t ENC_CLK = 25;
constexpr uint8_t ENC_DT  = 26;
constexpr uint8_t ENC_SW  = 27;

// --- UART / Komunikacja ---
constexpr long UART_BAUD = 115200;
HardwareSerial& uart = Serial2;

// --- WiFi / NTP ---
const char* const WIFI_SSID  = "IPhone";
const char* const WIFI_PASS  = "12345678";
const char* const NTP_SERVER = "pool.ntp.org";
constexpr long GMT_OFFSET    = 3600;
constexpr int  DST_OFFSET    = 3600;

// --- LCD Custom Character ---
byte alarmIcon[8] = {
  B00100,
  B01110,
  B01110,
  B11111,
  B11111,
  B00100,
  B00000,
  B00000
};

// --- DHT Sensor ---
// --- DHT11 ---
#define DHT_PIN 4
#define DHT_TYPE DHT11

// --- DHT11 ---
void updateDHT();
void drawTemperature();
void drawHumidity();
void showTemperature7Seg();
void showHumidity7Seg();


// ============================================================================
// ZMIENNE GLOBALNE (pogrupowane funkcjonalnie)
// ============================================================================

// --- AppState (EXTERN z AppState.cpp) ---
// extern AppState appState;   (zdefiniowane w AppState.cpp)
// extern EditState editState; (zdefiniowane w AppState.cpp)

// --- Menu Główne ---
int menuIndex = 0;
const char* menuItems[] = {
  "Ustaw czas",
  "Stoper",
  "Budzik",
  "Czas z WiFi",
  "Statystyki",
  "Debug STM32",
  "Temperatura",
  "Wilgotnosc",
  "Wyjscie",
  "Radio: Toggle"
};
constexpr int MENU_COUNT = 10;
int menuCount = MENU_COUNT;

// Diagnostyka pamięci
static uint32_t heapBaseline = 0;

// --- Menu Statystyk ---
int statsMenuIndex = 0;
const char* statsMenuItems[] = {
  "Kliki",
  "Kroki",
  "Temp min/max",
  "Wilg min/max",
  "Zasoby",
  "Wyjscie"
};
constexpr int STATS_MENU_COUNT = 6;
int statsMenuCount = STATS_MENU_COUNT;

// --- Budzik ---
int  alarmHour       = 7;
int  alarmMinute     = 0;
bool alarmEnabled    = false;
bool alarmRinging    = false;
unsigned long alarmStartTime  = 0;
unsigned long lastMelodyStep  = 0;
int  melodyStep      = 0;

// --- STM32 DANE (UART) ---
unsigned long lastSTM32Update       = 0;
unsigned long lastSTM32DataReceived = 0;
int  displayedBPM    = 0;
int  displayedSPO2   = 0;
bool stm32Connected  = false;

// --- Stoper ---
bool stoperRunning         = false;
unsigned long stoperStart  = 0;
unsigned long stoperElapsed = 0;
unsigned long lastStoperDraw = 0;

// --- Czas (HH:MM:SS) ---
int hours   = 12;
int minutes = 0;
int seconds = 0;
unsigned long lastTick = 0;

// --- Flaga do przywrócenia czasu z RTC (po soft reset) ---
static bool timeRestored = false;

// --- MQTT Mode Control ---
static bool mqtt_initialized = false;
static RadioModeSwitchState last_radio_mode = RADIO_STATE_WIFI;

// --- DHT Sensor ---
DHT dht(DHT_PIN, DHT_TYPE);
bool dhtScreenDirty = true;
int  savedHours     = 0;
int  savedMinutes   = 0;
int  savedSeconds   = 0;
bool timeSaved      = false;

// Odczyty DHT
float dhtTemperature = 0.0f;
float dhtHumidity    = 0.0f;

// Stabilizacja odczytów
bool dhtReady = false;
unsigned long dhtLastRead = 0;
constexpr unsigned long DHT_READ_INTERVAL_MS = 2000;


// ============================================================================
// IMPLEMENTACJA FUNKCJI - LOGIKA ZEGARA
// ============================================================================

void tickClock() {
  if (appState == STATE_SET_TIME) return;

  if (millis() - lastTick >= CLOCK_TICK_MS) {
    lastTick += CLOCK_TICK_MS;
    seconds++;

    if (seconds >= 60) {
      seconds = 0;
      minutes++;
      if (minutes >= 60) {
        minutes = 0;
        hours = (hours + 1) % 24;
      }
    }

    if (appState != STATE_STOPER) {
      updateSevenSeg();
    }
    if (appState == STATE_HOME) {
      drawHome();
    }
  }

  // Sprawdź czy należy uruchomić alarm
  const bool alarmShouldTrigger = alarmEnabled && !alarmRinging &&
                                   hours == alarmHour &&
                                   minutes == alarmMinute &&
                                   seconds == 0;
  if (alarmShouldTrigger) {
    alarmRinging   = true;
    alarmStartTime = millis();
    lastMelodyStep = 0;
  }
}


// ============================================================================
// IMPLEMENTACJA FUNKCJI - ALARM / BUZZER
// ============================================================================

void playAlarmMelody() {
  if (millis() - lastMelodyStep >= MELODY_STEP_MS) {
    lastMelodyStep = millis();
    tone(BUZZER_PIN, melodyFreq[melodyStep]);
    melodyStep = (melodyStep + 1) % MELODY_LEN;
  }
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  Serial.begin(UART_BAUD);
  delay(SETUP_DELAY_MS);

  heapBaseline = ESP.getFreeHeap();
  Serial.printf("[diag] baseline_heap=%u\n", heapBaseline);
  ModeManager::logDiag("boot");

  // --- DHT Sensor Init ---
  dht.begin();

  // ===== Menedżer trybów (Wi-Fi/BT) - inicjalizuj WCZEŚNIE =====
  ModeManager::begin(&appState);

  // ===== RadioModeSwitch - inicjalizacja przełączania trybu =====
  // BĘDZIE NA KONIEC setup() po LCD i UI init!
  RadioModeSwitch::begin();

#if UART_LCD_MIRROR
  lcdMirror.begin();
#endif

  // I2C initialization with explicit pins: SDA=21, SCL=22 (GPIO22 now free from I2S after fix)
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.createChar(0, alarmIcon);

  // --- Encoder init ---
  encoder_begin(ENC_CLK, ENC_DT, ENC_SW);

  // --- Buzzer setup ---
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  // --- 7-Segment setup ---
  initSevenSeg();

  // --- Inicjalizacja statystyk ---
  statsManager.begin();

  // --- STM32 setup ---
  STM32data_begin(uart, UART_BAUD, 16, 17);

  // ===== UI CONTROLLER =====
  UI_Callbacks callbacks;
  callbacks.drawHome            = drawHome;
  callbacks.drawMenu            = drawMenu;
  callbacks.drawSetTime         = drawSetTime;
  callbacks.drawAlarm           = drawAlarm;
  callbacks.drawStoper          = drawStoper;
  callbacks.drawDebugSTM32      = drawDebugSTM32;
  callbacks.updateSevenSeg      = updateSevenSeg;
  callbacks.updateSevenSegStoper = updateSevenSegStoper;
  callbacks.drawStats           = drawStats;           // Callbacki do UI statystyk
  callbacks.drawSystemResources = drawSystemResources; // Zasoby systemu (RAM/FLASH)

  ui_begin(callbacks);
  drawHome();

  // ===== WiFi / NTP Sync =====
  WiFiSync::setTimeRefs(hours, minutes, seconds, lastTick);        // referencje do zmiennych czasu
  WiFiSync::setAppStatePtr(&appState);                             // wskaźnik do appState
  WiFiSync::setOnDone([]() { drawHome(); });                       // callback po zakończeniu sync
  WiFiSync::begin(WIFI_SSID, WIFI_PASS, NTP_SERVER, GMT_OFFSET, DST_OFFSET);

  // ===== MQTT Sync (Core 1) - initialized only in WiFi mode =====
  // MQTTSync will be initialized later in RadioModeSwitch::update() when WiFi mode is confirmed
  Serial.println("[main] MQTT will start when WiFi mode is active");

  // ===== RadioModeSwitch inicjalizuje się, ale faktyczna inicjalizacja WiFi/BT =====
  // będzie opóźniona w loop() aby zagwarantować że LCD jest w pełni gotowe
  
  // Synchronizuj radioMode ze stanem RadioModeSwitch na starcie
  if (RadioModeSwitch::getCurrentState() == RADIO_STATE_BT) {
    radioMode = BT_ONLY;
  } else {
    radioMode = WIFI_ONLY;
  }

  ModeManager::logDiag("after-setup-radio-ready");
}

// ============================================================================
// LOOP
// ============================================================================

void loop() {
  // --- Obs\u0142uga enkodera ---
  const EncoderEvent evt = encoder_update();
  if (evt != ENC_NONE) {
    // === REJESTRACJA STATYSTYK ===
    if (evt == ENC_CLICK || evt == ENC_LONG) {
      statsManager.registerClick();
    } else if (evt == ENC_LEFT) {
      statsManager.registerStepLeft();
    } else if (evt == ENC_RIGHT) {
      statsManager.registerStepRight();
    }

    // Przekaż event do UI (jeśli WiFi nie jest zajęte ORAZ RadioModeSwitch się nie inicjalizuje)
    if (!WiFiSync::isBusy() && !RadioModeSwitch::isInitializing()) {
      ui_handleEvent(evt);
    }
  }

  // --- Aktualizacja statystyk (zapis do NVS jeśli potrzeba) ---
  statsManager.update();

  // --- Tykanie zegara ---
  tickClock();

  // --- WiFi sync update ---
  WiFiSync::update();

  // --- RadioModeSwitch update (opóźniona inicjalizacja WiFi/BT po starcie) ---
  RadioModeSwitch::update();

  // --- MQTT Control (start/stop based on WiFi mode) ---
  RadioModeSwitchState current_radio_mode = RadioModeSwitch::getCurrentState();
  
  if (current_radio_mode == RADIO_STATE_WIFI && !mqtt_initialized) {
    // Start MQTT when switching to WiFi mode
    Serial.println("[main] Activating MQTT for WiFi mode");
    MQTTSync::begin(WIFI_SSID, WIFI_PASS);
    MQTTSync::startCore1Task();
    mqtt_initialized = true;
    last_radio_mode = RADIO_STATE_WIFI;
  } 
  else if (current_radio_mode == RADIO_STATE_BT && mqtt_initialized) {
    // Stop MQTT when switching to BT mode
    Serial.println("[main] Deactivating MQTT for BT mode");
    MQTTSync::stopCore1Task();
    mqtt_initialized = false;
    last_radio_mode = RADIO_STATE_BT;
  }

  // --- MQTT Update (publish sensor data only if initialized) ---
  if (mqtt_initialized) {
    static unsigned long lastMQTTPublish = 0;
    if (millis() - lastMQTTPublish >= 5000) {
      lastMQTTPublish = millis();
      MQTTSync::publishSensorData(dhtTemperature, (int)dhtHumidity, 1013);
    }
  }

  // --- Synchronizuj radioMode ze stanem RadioModeSwitch ---
  // Ważne: to zapewnia, że menu zawsze pokazuje prawidłowy stan
  if (RadioModeSwitch::getCurrentState() == RADIO_STATE_BT) {
    radioMode = BT_ONLY;
  } else {
    radioMode = WIFI_ONLY;
  }

  // --- Przywrócenie czasu z RTC (jeśli był soft reset z przełączeniem trybu) ---
  if (!timeRestored && !RadioModeSwitch::isInitializing()) {
    uint8_t rtc_hours = RadioModeSwitch::getRTCHours();
    uint8_t rtc_minutes = RadioModeSwitch::getRTCMinutes();
    uint8_t rtc_seconds = RadioModeSwitch::getRTCSeconds();
    
    // Sprawdź czy czas był zapisany (non-zero wartości)
    if (rtc_hours > 0 || rtc_minutes > 0 || rtc_seconds > 0) {
      hours = rtc_hours;
      minutes = rtc_minutes;
      seconds = rtc_seconds;
      lastTick = millis();  // Zresetuj tick timer
      
      Serial.printf("[main] Przywrócono czas z RTC: %02d:%02d:%02d\n", hours, minutes, seconds);
      updateSevenSeg();
      drawHome();
      
      // Wyczyść RTC czas (one-time restoration)
      RadioModeSwitch::clearRTCTime();
    }
    
    timeRestored = true;
  }

  // --- STM32 Debug State ---
  if (appState == STATE_DEBUG_STM32 &&
      millis() - lastSTM32Update >= STM32_UPDATE_MS) {
    lastSTM32Update = millis();
    STM32data_update();

    if (stmDataUpdated) {
      stmDataUpdated        = false;
      displayedBPM          = bpmNumber;
      displayedSPO2         = spo2Number;
      stm32Connected        = true;
      lastSTM32DataReceived = millis();
    } else if (millis() - lastSTM32DataReceived > STM32_TIMEOUT_MS) {
      stm32Connected = false;
      displayedBPM   = 0;
      displayedSPO2  = 0;
    }

    drawDebugSTM32();
  }

  // --- Alarm Ringing ---
  if (alarmRinging) {
    playAlarmMelody();
    if (millis() - alarmStartTime >= ALARM_DURATION_MS) {
      noTone(BUZZER_PIN);
      alarmRinging = false;
      alarmEnabled = false;
      melodyStep   = 0;
    }
  }

  // --- Stopwatch Drawing ---
  if (appState == STATE_STOPER &&
      millis() - lastStoperDraw >= STOPER_DRAW_MS) {
    lastStoperDraw = millis();
    drawStoper();
  }

  // --- Diagnostyka BT (co 60s) ---
  static unsigned long lastBtCheck = 0;
  if (millis() - lastBtCheck > 60000) {
    lastBtCheck = millis();
    Serial.print("BT Connected: ");
    Serial.println(audioBT_isConnected() ? "TAK" : "NIE");
  }

  // --- DHT Sensor Update ---
  updateDHT();

  // --- Ekran temperatury/wilgotności ---
  if (appState == STATE_TEMPERATURE) {
    drawTemperature();
    showTemperature7Seg();
  }

  if (appState == STATE_HUMIDITY) {
    drawHumidity();
    showHumidity7Seg();
  }
}

// ============================================================================
// FUNKCJE DHT (TEMPERATURA/WILGOTNOŚĆ)
// ============================================================================

void updateDHT() {
  if (millis() - dhtLastRead < DHT_READ_INTERVAL_MS) return;
  dhtLastRead = millis();

  const float t = dht.readTemperature();
  const float h = dht.readHumidity();

  if (!isnan(t) && !isnan(h)) {
    dhtTemperature = t;
    dhtHumidity    = h;
    statsManager.updateTemperature(t);
    statsManager.updateHumidity(h);

    if (!dhtReady) {
      dhtScreenDirty = true;
    }
    dhtReady = true;
  }
}
