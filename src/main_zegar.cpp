#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <time.h>

// ---- Deklaracje funkcji (dla PlatformIO) ----
void drawHome();
void drawMenu();
void drawSetTime();
void drawAlarm();
void drawStoper();

void printTime(bool edit);
void printVal(int v, bool sel);

void adjustTime(int dir);
void handleEncoder();
void handleButton();
void onClick();

void tickClock();
void syncTimeFromWiFi();

uint8_t swapNibbles(uint8_t v);
void slowShiftOut(uint8_t v);
void initSevenSeg();

void updateSevenSeg(); //debug




// ================= LCD =================
LiquidCrystal_I2C lcd(0x27, 20, 4);

// ikona budzika
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

// ================= ENCODER =================
#define ENC_CLK 25
#define ENC_DT  26
#define ENC_SW  27
int lastCLK;

// ================= 7-SEG (74HC595) =================
#define DATA_PIN   23
#define CLOCK_PIN  18
#define LATCH_PIN   5

// ================= BUZZER =================
#define BUZZER_PIN 19

// ================= WIFI / NTP =================
const char* WIFI_SSID = "IPhone";
const char* WIFI_PASS = "12345678";
const char* NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET = 3600;
const int DST_OFFSET = 3600;

// ================= CZAS =================
int hours = 12, minutes = 0, seconds = 0;
unsigned long lastTick = 0;

// ================= STOPER =================
bool stoperRunning = false;
unsigned long stoperStart = 0, stoperElapsed = 0;
unsigned long lastStoperDraw = 0;

// ================= BUDZIK =================
int alarmHour = 7, alarmMinute = 0;
bool alarmEnabled = false;
bool alarmRinging = false;
unsigned long alarmStartTime = 0;
unsigned long lastMelodyStep = 0;
int melodyStep = 0;

// ================= STANY =================
enum AppState {
  STATE_HOME,
  STATE_MENU,
  STATE_SET_TIME,
  STATE_STOPER,
  STATE_ALARM
};
AppState appState = STATE_HOME;

enum EditState {
  EDIT_HOURS,
  EDIT_MINUTES,
  EDIT_SECONDS,
  EDIT_DONE
};
EditState editState = EDIT_HOURS;

// ================= MENU =================
const char* menuItems[] = {
  "Ustaw czas",
  "Stoper",
  "Budzik",
  "Czas z WiFi",
  "Wyjscie"
};
const int menuCount = 5;
int menuIndex = 0;


// ================= UART LCD MIRROR (AUTO) =================
#define UART_LCD_MIRROR 1  // #define UART_LCD_MIRROR 1 → mirror włączony (bufor + zrzuty na UART).  | #define UART_LCD_MIRROR 0 → mirror wyłączony, wrappery działają jak zwykły LCD (bez UART).

#define UART_BAUD 115200

#if UART_LCD_MIRROR
class LcdMirror20x4 : public Print {
public:
  void begin() { clear(); }

  void clear() {
    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 20; c++) buf[r][c] = ' ';
      buf[r][20] = '\0';
    }
    x = 0; y = 0;
  }

  void setCursor(uint8_t col, uint8_t row) {
    x = col; y = row;
  }

  size_t write(uint8_t ch) override {
    if (y < 4 && x < 20) {
      char out = (ch >= 32) ? (char)ch : '?'; // znaki sterujące -> '?'
      if (ch == 0) out = '*';                 // custom char 0 -> '*'
      buf[y][x] = out;
    }
    if (x < 20) x++;
    return 1;
  }

  void dumpUART() {
    Serial.println();
    Serial.println("+--------------------+");
    for (int r = 0; r < 4; r++) {
      Serial.print("|");
      Serial.write((const uint8_t*)buf[r], 20);
      Serial.println("|");
    }
    Serial.println("+--------------------+");
  }

private:
  char buf[4][21];
  uint8_t x = 0, y = 0;
};

LcdMirror20x4 lcdMirror;


// Wrappery: LCD + mirror w jednym miejscu
inline void LCD_CLEAR() { lcd.clear(); lcdMirror.clear(); }
inline void LCD_SET(uint8_t c, uint8_t r) { lcd.setCursor(c, r); lcdMirror.setCursor(c, r); }
template<typename T> inline void LCD_PRINT(const T& v) { lcd.print(v); lcdMirror.print(v); }
inline void LCD_WRITE(uint8_t b) { lcd.write(b); lcdMirror.write(b); }
inline void LCD_DUMP() { lcdMirror.dumpUART(); }

#else
inline void LCD_CLEAR() { lcd.clear(); }
inline void LCD_SET(uint8_t c, uint8_t r) { lcd.setCursor(c, r); }
template<typename T> inline void LCD_PRINT(const T& v) { lcd.print(v); }
inline void LCD_WRITE(uint8_t b) { lcd.write(b); }
inline void LCD_DUMP() {}
#endif

// ================= 7-SEG LOW LEVEL (MUSI BYĆ ZDEFINIOWANE) =================
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


// ================= 7-SEG UPDATE =================
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

// ================= MELODYJKA =================
int melodyFreq[] = { 1000, 1400, 1000, 1600 };
const int melodyLen = 4;

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

//  dbgInit(); //debug
  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.createChar(0, alarmIcon);

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  lastCLK = digitalRead(ENC_CLK);

  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  initSevenSeg();
  drawHome();
}

// ================= LOOP =================
void loop() {
//  dbgLoop(); // debug
  handleEncoder();
  handleButton();
  tickClock();

  if (alarmRinging) {
    playAlarmMelody();
    if (millis() - alarmStartTime >= 5000) {
      noTone(BUZZER_PIN);
      alarmRinging = false;
      alarmEnabled = false;
      melodyStep = 0;
    }
  }

  if (appState == STATE_STOPER && millis() - lastStoperDraw >= 100) {
    lastStoperDraw = millis();
    drawStoper();
  }
}

// ================= ZEGAR + BUDZIK =================
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

// ================= WIFI =================
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

// ================= LCD =================
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
  int m  = (t / 60000);

  LCD_SET(4, 2);
  if (m < 10) LCD_PRINT("0");
  LCD_PRINT(m); LCD_PRINT(":");
  if (s < 10) LCD_PRINT("0");
  LCD_PRINT(s); LCD_PRINT(".");
  if (cs < 10) LCD_PRINT("0");
  LCD_PRINT(cs);

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

// ================= SET TIME =================
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

// ================= ENCODER =================
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

// ================= BUTTON =================
void handleButton() {
  static bool last = true;
  bool now = digitalRead(ENC_SW);
  if (last && !now) {
    onClick();
    delay(200);
  }
  last = now;
}

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
      drawHome();
    }
    else {
      appState = STATE_HOME;
      drawHome();
    }
  }

   // >>> DODAJEMY TO: obsługa kliknięcia w trybie STOPER <<<
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
  
  else if (appState == STATE_SET_TIME) {
    editState = (EditState)(editState + 1);
    if (editState == EDIT_DONE) {
      lastTick = millis();
      appState = STATE_HOME;
      drawHome();
    } else drawSetTime();
  }
  else if (appState == STATE_ALARM) {
    editState = (EditState)(editState + 1);
    if (editState > EDIT_MINUTES) {
      alarmEnabled = true;
      appState = STATE_HOME;
      drawHome();
    } else drawAlarm();
  }
}
