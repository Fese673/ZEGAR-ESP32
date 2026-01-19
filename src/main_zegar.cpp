#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <time.h>
#include "STM32_Data.h"  // Nasza bibloteczka
#include "LCDMirror.h"   // Okablowanie LCD troche wiecej porządku w main 

// ========== Deklaracje funkcji (dla PlatformIO) ==========

// --- UI / LCD ---
void drawHome();
void drawMenu();
void drawSetTime();
void drawAlarm();
void drawStoper();
void drawDebugSTM32();  
void printTime(bool edit);
void printVal(int v, bool sel);

// --- Obsługa wejścia ---
void handleEncoder();
void handleButton();
void onClick();
void onLongPress();  
void adjustTime(int dir);

// --- Logika zegara ---
void tickClock();
void syncTimeFromWiFi();
uint8_t swapNibbles(uint8_t v);
void slowShiftOut(uint8_t v);

// ---Nie-wiem---
void initSevenSeg();
void updateSevenSeg(); //debug

// ---- ENUMERACJE (stany aplikacji) ----

enum AppState {
  STATE_HOME,
  STATE_MENU,
  STATE_SET_TIME,
  STATE_STOPER,
  STATE_ALARM,
  STATE_DEBUG_STM32
};

enum EditState {
  EDIT_HOURS,
  EDIT_MINUTES,
  EDIT_SECONDS,
  EDIT_DONE
};

// ---- KONFIGURACJA SPRZĘTU (PIN + STAŁE) ----

// --- 7-SEG (74HC595) ---
#define DATA_PIN   23
#define CLOCK_PIN  18
#define LATCH_PIN   5

// --- Buzzer ---
#define BUZZER_PIN 19
int melodyFreq[] = { 1000, 1400, 1000, 1600 };
const int melodyLen = 4;

// --- Encoder ---
#define ENC_CLK 25
#define ENC_DT  26
#define ENC_SW  27
int lastCLK;

// --- UART / Komunikacja ---
#define UART_BAUD 115200 
HardwareSerial &uart = Serial2; 

// --- WiFi / NTP ---
const char* WIFI_SSID = "IPhone";
const char* WIFI_PASS = "12345678";
const char* NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET = 3600;
const int DST_OFFSET = 3600;

// --- LCD ---
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

// ========== ZMIENNE GLOBALNE (pogrupowane funkcjonalnie) ==========

// --- Stany aplikacji ---
AppState appState = STATE_HOME;
EditState editState = EDIT_HOURS;
int menuIndex = 0;

// --- Budzik ---
int alarmHour = 7, alarmMinute = 0;
bool alarmEnabled = false;
bool alarmRinging = false;
unsigned long alarmStartTime = 0;
unsigned long lastMelodyStep = 0;
int melodyStep = 0;

// --- Przycisk (debouncing + długie przytrzymanie) ---
unsigned long buttonPressStart = 0;
unsigned long lastButtonAction = 0;  
bool buttonWasLongPress = false;     
#define LONG_PRESS_TIME 1000         
#define DEBOUNCE_TIME 200      

// --- MENU ---
const char* menuItems[] = {
  "Ustaw czas",
  "Stoper",
  "Budzik",
  "Czas z WiFi",
  "Debug STM32",
  "Wyjscie"
};
const int menuCount = 6;

// --- STM32 DANE (UART) ---
unsigned long lastSTM32Update = 0;
unsigned long lastSTM32DataReceived = 0;    
int displayedBPM = 0;
int displayedSPO2 = 0;
bool stm32Connected = false;

// --- STOPER ---
bool stoperRunning = false;
unsigned long stoperStart = 0, stoperElapsed = 0;
unsigned long lastStoperDraw = 0;

// --- Czas (HH:MM:SS) ---
int hours = 12, minutes = 0, seconds = 0;
unsigned long lastTick = 0;

// ---- IMPLEMENTACJA FUNKCJI - 7-SEGMENT DISPLAY ----
uint8_t swapNibbles(uint8_t v) { return (v << 4) | (v >> 4); }

static void pulse(int pin) {
  digitalWrite(pin, HIGH);
  delayMicroseconds(5);
  digitalWrite(pin, LOW);
  delayMicroseconds(5);
}

void slowShiftOut(uint8_t v) {
  for (int i = 7; i >= 0; i--) {
    digitalWrite(DATA_PIN, (v >> i) & 1);
    delayMicroseconds(5);
    pulse(CLOCK_PIN);
  }
}

void initSevenSeg() {
  pinMode(DATA_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(LATCH_PIN, OUTPUT);

  digitalWrite(DATA_PIN, LOW);
  digitalWrite(CLOCK_PIN, LOW);
  digitalWrite(LATCH_PIN, LOW);

  delay(50);

  digitalWrite(LATCH_PIN, LOW);
  slowShiftOut(0);
  slowShiftOut(0);
  slowShiftOut(0);
  digitalWrite(LATCH_PIN, HIGH);
}

void updateSevenSeg() {
  uint8_t HH = ((hours / 10) << 4) | (hours % 10);
  uint8_t MM = ((minutes / 10) << 4) | (minutes % 10);
  uint8_t SS = ((seconds / 10) << 4) | (seconds % 10);

  digitalWrite(LATCH_PIN, LOW);
  slowShiftOut(swapNibbles(SS));
  slowShiftOut(swapNibbles(MM));
  slowShiftOut(swapNibbles(HH));
  digitalWrite(LATCH_PIN, HIGH);
}

void updateSevenSegStoper(int mins, int secs, int centisec) {
  // Format: MM:SS:CS (minuty:sekundy:centisekundy)
  uint8_t MM = ((mins / 10) << 4) | (mins % 10);
  uint8_t SS = ((secs / 10) << 4) | (secs % 10);
  uint8_t CS = ((centisec / 10) << 4) | (centisec % 10);

  digitalWrite(LATCH_PIN, LOW);
  slowShiftOut(swapNibbles(CS));  // centisekundy
  slowShiftOut(swapNibbles(SS));  // sekundy
  slowShiftOut(swapNibbles(MM));  // minuty
  digitalWrite(LATCH_PIN, HIGH);
}

// ========== IMPLEMENTACJA FUNKCJI - UI / LCD ==========

void drawHome() {
  LCD_CLEAR();
  LCD_SET(4, 1);
  printTime(false);

  if (alarmEnabled) {
    LCD_SET(0, 1);
    LCD_WRITE(byte(0));
  }

  LCD_SET(2, 3);
  LCD_PRINT("Klik -> MENU");

  LCD_DUMP();
}


void drawMenu() {
  LCD_CLEAR();
  int first = (menuIndex / 4) * 4;
  for (int i = 0; i < 4; i++) {
    int item = first + i;
    if (item >= menuCount) break;
    LCD_SET(0, i);
    LCD_PRINT(item == menuIndex ? ">" : " ");
    LCD_PRINT(menuItems[item]);
  }
  LCD_DUMP();
}


void drawSetTime() {
  LCD_CLEAR();
  LCD_SET(2, 1);
  printTime(true);
  LCD_SET(2, 3);
  LCD_PRINT("Klik -> dalej");
  LCD_DUMP();
}

void drawAlarm() {
  LCD_CLEAR();
  LCD_SET(3, 0);
  LCD_PRINT("USTAW BUDZIK");
  LCD_SET(4, 2);

  if (editState == EDIT_HOURS) LCD_PRINT("[");
  if (alarmHour < 10) LCD_PRINT("0");
  LCD_PRINT(alarmHour);
  if (editState == EDIT_HOURS) LCD_PRINT("]");
  LCD_PRINT(":");
  if (editState == EDIT_MINUTES) LCD_PRINT("[");
  if (alarmMinute < 10) LCD_PRINT("0");
  LCD_PRINT(alarmMinute);
  if (editState == EDIT_MINUTES) LCD_PRINT("]");

  LCD_DUMP();
}


void drawStoper() {
  unsigned long t = stoperElapsed;
  if (stoperRunning) t += millis() - stoperStart;

  int cs = (t / 10) % 100;
  int s  = (t / 1000) % 60;
  int m  = (t / 60000) % 100;  // max 99 minut

  LCD_SET(4, 2);
  if (m < 10) LCD_PRINT("0");
  LCD_PRINT(m); LCD_PRINT(":");
  if (s < 10) LCD_PRINT("0");
  LCD_PRINT(s); LCD_PRINT(".");
  if (cs < 10) LCD_PRINT("0");
  LCD_PRINT(cs);
  
  updateSevenSegStoper(m, s, cs);  // 7 segmenty

  LCD_DUMP();
}


void drawDebugSTM32() {
  LCD_CLEAR();
  LCD_SET(2, 0);
  LCD_PRINT("DEBUG STM32");
  
  LCD_SET(0, 1);
  LCD_PRINT("BPM: ");
  LCD_PRINT(displayedBPM);
  
  LCD_SET(0, 2);
  LCD_PRINT("SPO2: ");
  LCD_PRINT(displayedSPO2);
  LCD_PRINT("%");
  
  LCD_SET(0, 3);
  if (stm32Connected) {
    LCD_PRINT("Status: OK");
  } else {
    LCD_PRINT("Status: OFFLINE");
  }
  
  LCD_DUMP();
}


void printTime(bool edit) {
  printVal(hours, edit && editState == EDIT_HOURS);
  LCD_PRINT(":");
  printVal(minutes, edit && editState == EDIT_MINUTES);
  LCD_PRINT(":");
  printVal(seconds, edit && editState == EDIT_SECONDS);
}


void printVal(int v, bool sel) {
  if (sel) LCD_PRINT("[");
  if (v < 10) LCD_PRINT("0");
  LCD_PRINT(v);
  if (sel) LCD_PRINT("]");
}

// ========== IMPLEMENTACJA FUNKCJI - OBSŁUGA WEJŚCIA ==========

// ---  ---
void handleEncoder() {
  int clk = digitalRead(ENC_CLK);
  if (clk != lastCLK && clk == LOW) {
    int dir = (digitalRead(ENC_DT) != clk) ? 1 : -1;

    if (appState == STATE_MENU) {
      menuIndex = constrain(menuIndex + dir, 0, menuCount - 1);
      drawMenu();
    }
    else if (appState == STATE_SET_TIME) {
      adjustTime(dir);
    }
    else if (appState == STATE_ALARM) {
      if (editState == EDIT_HOURS)
        alarmHour = (alarmHour + dir + 24) % 24;
      else
        alarmMinute = (alarmMinute + dir + 60) % 60;
      drawAlarm();
    }
  }
  lastCLK = clk;
}

// ---  ---
void handleButton() {
  static bool last = true;
  bool now = digitalRead(ENC_SW);
  unsigned long currentTime = millis();
  

  if (last && !now) {
    buttonPressStart = currentTime;
    buttonWasLongPress = false;
  }
  
  if (!now && !buttonWasLongPress) {
    if (currentTime - buttonPressStart >= LONG_PRESS_TIME) {
      buttonWasLongPress = true;
      if (currentTime - lastButtonAction >= DEBOUNCE_TIME) {
        lastButtonAction = currentTime;
        onLongPress();
      }
    }
  }

  if (!last && now && !buttonWasLongPress) {
    if (currentTime - lastButtonAction >= DEBOUNCE_TIME) {
      lastButtonAction = currentTime;
      onClick();
    }
  }
  
  last = now;
}

// --- SET TIME ---
void adjustTime(int dir) {
  if (editState == EDIT_HOURS)
    hours = (hours + dir + 24) % 24;
  else if (editState == EDIT_MINUTES)
    minutes = (minutes + dir + 60) % 60;
  else if (editState == EDIT_SECONDS)
    seconds = (seconds + dir + 60) % 60;

  drawSetTime();
  updateSevenSeg();
}

// --- DŁUGIE PRZYTRZYMANIE ---
void onLongPress() {
  if (appState == STATE_STOPER) {
    // Wyjście ze stopera do menu
    appState = STATE_MENU;
    updateSevenSeg();  // Przywróć normalny zegar na 7-seg
    drawMenu();
  }
  else if (appState == STATE_DEBUG_STM32) {
    // Wyjście z debug do menu
    appState = STATE_MENU;
    updateSevenSeg();
    drawMenu();
  }
  // Tutaj możemy dodac więcej stanów które obsługują długie przytrzymanie 
}

// ---  ---
void onClick() {
  if (appState == STATE_HOME) {
    appState = STATE_MENU;
    drawMenu();
  }
  else if (appState == STATE_MENU) {
    if (menuIndex == 0) {
      appState = STATE_SET_TIME;
      editState = EDIT_HOURS;
      drawSetTime();
    }
    else if (menuIndex == 1) {
      appState = STATE_STOPER;
      stoperRunning = false;
      stoperElapsed = 0;
      LCD_CLEAR();
      drawStoper();
    }
    else if (menuIndex == 2) {
      appState = STATE_ALARM;
      editState = EDIT_HOURS;
      drawAlarm();
    }
    else if (menuIndex == 3) {
      syncTimeFromWiFi();
      updateSevenSeg();
      drawMenu();  // FIX 
    }
    else if (menuIndex == 4) {                  
      appState = STATE_DEBUG_STM32;
      lastSTM32Update = 0;
      updateSevenSeg();
      drawDebugSTM32();
    }
    else {
      appState = STATE_HOME;
      drawHome();
    }
  }

// --- Stoper ---
  else if (appState == STATE_STOPER) {
    if (!stoperRunning) {
      // START / WZNÓW
      stoperRunning = true;
      stoperStart = millis();
    } else {
      // STOP / PAUZA
      stoperRunning = false;
      stoperElapsed += millis() - stoperStart;
    }
    drawStoper();
  }
   else if (appState == STATE_DEBUG_STM32) {    
    appState = STATE_HOME;
    updateSevenSeg();
    drawHome();
  }
  else if (appState == STATE_SET_TIME) {
    editState = (EditState)(editState + 1);
    if (editState == EDIT_DONE) {
      lastTick = millis();
      appState = STATE_HOME;
      updateSevenSeg();
      drawHome();
    } else drawSetTime();
  }
  else if (appState == STATE_ALARM) {
    editState = (EditState)(editState + 1);
    if (editState > EDIT_MINUTES) {
      alarmEnabled = true;
      appState = STATE_HOME;
      updateSevenSeg();
      drawHome();
    } else drawAlarm();
  }
}

// ========== IMPLEMENTACJA FUNKCJI - LOGIKA ZEGARA ==========

// --- ZEGAR + BUDZIK ---
void tickClock() {
  if (appState == STATE_SET_TIME) return;

  if (millis() - lastTick >= 1000) {
    lastTick += 1000;
    seconds++;
    if (seconds >= 60) {
      seconds = 0;
      minutes++;
      if (minutes >= 60) {
        minutes = 0;
        hours = (hours + 1) % 24;
      }
    }
    updateSevenSeg();
    if (appState == STATE_HOME) drawHome();
  }

  if (alarmEnabled && !alarmRinging &&
      hours == alarmHour && minutes == alarmMinute && seconds == 0) {
    alarmRinging = true;
    alarmStartTime = millis();
    lastMelodyStep = 0;
  }
}

// --- WIFI ---
void syncTimeFromWiFi() {
  LCD_CLEAR();
  LCD_SET(0, 1);
  LCD_PRINT("Laczenie z WiFi");
  LCD_DUMP();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(500);
    LCD_PRINT(".");
    tries++;
    LCD_DUMP();
  }

  if (WiFi.status() != WL_CONNECTED) {
    LCD_CLEAR();
    LCD_SET(0, 1);
    LCD_PRINT("Blad WiFi");
    LCD_DUMP();
    delay(2000);
    return;
  }

  configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    hours = timeinfo.tm_hour;
    minutes = timeinfo.tm_min;
    seconds = timeinfo.tm_sec;
    lastTick = millis();
  }

  LCD_CLEAR();
  LCD_SET(0, 1);
  LCD_PRINT("Czas ustawiony");
  LCD_DUMP();
  delay(1500);
}

// ========== IMPLEMENTACJA FUNKCJI - ALARM / BUZZER ==========

void playAlarmMelody() {
  if (millis() - lastMelodyStep >= 300) {
    lastMelodyStep = millis();
    tone(BUZZER_PIN, melodyFreq[melodyStep]);
    melodyStep = (melodyStep + 1) % melodyLen;
  }
}

// ================= SETUP =================

void setup() {
 Serial.begin(UART_BAUD);
  delay(800);

#if UART_LCD_MIRROR
  lcdMirror.begin();
#endif

  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.createChar(0, alarmIcon);

  // --- Encoder setup ---
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  lastCLK = digitalRead(ENC_CLK);

  // --- Buzzer setup ---
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);
  
  // --- 7-Segment setup ---
  initSevenSeg();

  // --- STM32 setup ---
  STM32data_begin(uart, 115200, 16, 17);

  drawHome();
}

// ================= LOOP =================

void loop() {
  handleEncoder();
  handleButton();
  tickClock();

  // --- STM32 Debug State ---
  if (appState == STATE_DEBUG_STM32 && millis() - lastSTM32Update >= 500) {
    lastSTM32Update = millis();
    
    STM32data_update();
    
    if (stmDataUpdated) {
      stmDataUpdated = false;
      displayedBPM = bpmNumber;
      displayedSPO2 = spo2Number;
      stm32Connected = true;
      lastSTM32DataReceived = millis();     //  ZAPISZ CZAS OSTATNICH DANYCH
    } else if (millis() - lastSTM32DataReceived > 3000) {
      // Jeśli od ostatniego pakietu minęło więcej niż 3 sekundy wykonaj poprostu zerowanie hi hi
      stm32Connected = false;
      displayedBPM = 0;
      displayedSPO2 = 0;
    }
    
    drawDebugSTM32();
  }

  // --- Alarm Ringing ---
  if (alarmRinging) {
    playAlarmMelody();
    if (millis() - alarmStartTime >= 5000) {
      noTone(BUZZER_PIN);
      alarmRinging = false;
      alarmEnabled = false;
      melodyStep = 0;
    }
  }

  // --- Stopwatch Drawing ---
  if (appState == STATE_STOPER && millis() - lastStoperDraw >= 100) {
    lastStoperDraw = millis();
    drawStoper();
  }
}









  







