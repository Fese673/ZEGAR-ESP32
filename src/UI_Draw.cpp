#include "UI_Draw.h"
#include "LCDMirror.h"
#include "StatsManager.h"
#include "WiFiSync.h"
#include <LiquidCrystal_I2C.h>
#include <Esp.h>
#include "PMS_Czujnik.h"
#include "ENS160AHT21Screen.h"

extern LiquidCrystal_I2C lcd;

#if UART_LCD_MIRROR
extern LcdMirror20x4 lcdMirror;
#endif

// ============================================================================
// ZMIENNE GLOBALNE (extern)
// ============================================================================

// --- Menu ---
extern int menuIndex;
extern const char* menuItems[];
extern int menuCount;

// --- Statystyki ---
extern int statsMenuIndex;
extern const char* statsMenuItems[];
extern int statsMenuCount;

// --- Zasoby ---
extern int resourcesMenuIndex;
extern const char* resourcesMenuItems[];
extern int resourcesMenuCount;

// --- PMS5003 ---
extern int pms5003MenuIndex;
extern const char* pms5003MenuItems[];
extern int pms5003MenuCount;
extern int pms5003CF1MenuIndex;
extern const char* pms5003CF1MenuItems[];
extern int pms5003CF1MenuCount;
extern int pms5003ATMMenuIndex;
extern const char* pms5003ATMMenuItems[];
extern int pms5003ATMMenuCount;
extern int pms5003ParticlesMenuIndex;
extern const char* pms5003ParticlesMenuItems[];
extern int pms5003ParticlesMenuCount;
extern int ens160MenuIndex;
extern const char* ens160MenuItems[];
extern int ens160MenuCount;

// --- Dane PMS5003 ---
extern uint16_t pms5003_PM1_0_CF1;
extern uint16_t pms5003_PM2_5_CF1;
extern uint16_t pms5003_PM10_CF1;
extern uint16_t pms5003_PM1_0_ATM;
extern uint16_t pms5003_PM2_5_ATM;
extern uint16_t pms5003_PM10_ATM;
extern uint16_t pms5003_PM1_0_CF1_MIN;
extern uint16_t pms5003_PM1_0_CF1_MAX;
extern uint16_t pms5003_PM2_5_CF1_MIN;
extern uint16_t pms5003_PM2_5_CF1_MAX;
extern uint16_t pms5003_PM10_CF1_MIN;
extern uint16_t pms5003_PM10_CF1_MAX;
extern uint16_t pms5003_PM1_0_ATM_MIN;
extern uint16_t pms5003_PM1_0_ATM_MAX;
extern uint16_t pms5003_PM2_5_ATM_MIN;
extern uint16_t pms5003_PM2_5_ATM_MAX;
extern uint16_t pms5003_PM10_ATM_MIN;
extern uint16_t pms5003_PM10_ATM_MAX;
extern uint16_t pms5003_particleCount_0_3;
extern uint16_t pms5003_particleCount_0_5;
extern uint16_t pms5003_particleCount_1_0;
extern uint16_t pms5003_particleCount_2_5;
extern uint16_t pms5003_particleCount_5_0;
extern uint16_t pms5003_particleCount_10_0;
extern uint16_t pms5003_particleCount_0_3_MIN;
extern uint16_t pms5003_particleCount_0_3_MAX;
extern uint16_t pms5003_particleCount_0_5_MIN;
extern uint16_t pms5003_particleCount_0_5_MAX;
extern uint16_t pms5003_particleCount_1_0_MIN;
extern uint16_t pms5003_particleCount_1_0_MAX;
extern uint16_t pms5003_particleCount_2_5_MIN;
extern uint16_t pms5003_particleCount_2_5_MAX;
extern uint16_t pms5003_particleCount_5_0_MIN;
extern uint16_t pms5003_particleCount_5_0_MAX;
extern uint16_t pms5003_particleCount_10_0_MIN;
extern uint16_t pms5003_particleCount_10_0_MAX;
extern uint16_t pms5003_errorCount_current;
extern uint16_t pms5003_errorCount_total;
extern uint16_t pms5003_bytesReceived;
extern uint32_t pms5003_lastFrameTime;
extern uint32_t pms5003_latency_ms;

// --- Stan aplikacji ---
extern enum AppState appState;
extern enum EditState editState;
extern RadioMode radioMode;

// --- Budzik ---
extern int  alarmHour;
extern int  alarmMinute;
extern bool alarmEnabled;

// --- Stoper ---
extern bool stoperRunning;
extern unsigned long stoperStart;
extern unsigned long stoperElapsed;

// --- Czas ---
extern int hours;
extern int minutes;
extern int seconds;

// --- STM32 ---
extern int  displayedBPM;
extern int  displayedSPO2;
extern bool stm32Connected;

// --- CPU Load ---
extern uint8_t cpuLoadPercent;
extern uint8_t cpuCore0Percent;
extern uint8_t cpuCore1Percent;

// --- System Resources ---
extern uint32_t ramFreeBytes;
extern uint32_t flashFreeBytes;

// --- DHT / Climate (z main.cpp) ---
extern float dhtTemperature;
extern float dhtHumidity;
extern bool  dhtReady;

// --- Settings (z main.cpp / UI_Controller.cpp) ---
extern bool pms5003Enabled;
extern int settingsMqttMenuIndex;
extern bool mqttEnabled;

// ============================================================================
// IMPLEMENTACJA FUNKCJI - 7-SEGMENT (74HC595)
// ============================================================================

uint8_t swapNibbles(uint8_t v) {
  return (v << 4) | (v >> 4);
}

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

  // Wyzeruj wyświetlacz
  digitalWrite(LATCH_PIN, LOW);
  slowShiftOut(0);
  slowShiftOut(0);
  slowShiftOut(0);
  digitalWrite(LATCH_PIN, HIGH);
}

void updateSevenSeg() {
  uint8_t HH, MM, SS;

  if (timerRunning) {
    unsigned long nowMs = millis();
    unsigned long elapsed = (nowMs >= timerStartMillis) ? (nowMs - timerStartMillis) : 0;
    long remainingMs = (long)timerDurationMs - (long)elapsed;
    if (remainingMs < 0) remainingMs = 0;
    int rh = (int)(remainingMs / 3600000L);
    int rm = (int)((remainingMs % 3600000L) / 60000L);
    int rs = (int)((remainingMs % 60000L) / 1000L);
    HH = ((rh / 10) << 4) | (rh % 10);
    MM = ((rm / 10) << 4) | (rm % 10);
    SS = ((rs / 10) << 4) | (rs % 10);
  } else {
    HH = ((hours / 10) << 4) | (hours % 10);
    MM = ((minutes / 10) << 4) | (minutes % 10);
    SS = ((seconds / 10) << 4) | (seconds % 10);
  }

  digitalWrite(LATCH_PIN, LOW);
  slowShiftOut(swapNibbles(SS));
  slowShiftOut(swapNibbles(MM));
  slowShiftOut(swapNibbles(HH));
  digitalWrite(LATCH_PIN, HIGH);
}

void updateSevenSegStoper(int mins, int secs, int centisec) {
  const uint8_t MM = ((mins / 10) << 4) | (mins % 10);
  const uint8_t SS = ((secs / 10) << 4) | (secs % 10);
  const uint8_t CS = ((centisec / 10) << 4) | (centisec % 10);

  digitalWrite(LATCH_PIN, LOW);
  slowShiftOut(swapNibbles(CS));
  slowShiftOut(swapNibbles(SS));
  slowShiftOut(swapNibbles(MM));
  digitalWrite(LATCH_PIN, HIGH);
}

// ============================================================================
// IMPLEMENTACJA FUNKCJI - UI / LCD
// ============================================================================

// --- Ekran główny ---
static const char* polishMonths[] = {
    "sty", "lut", "mar", "kwi", "maj", "cze",
  "lip", "sie", "wrz", "paz", "lis", "gru"
};

void drawHome() {
  LCD_CLEAR();

  // Wiersz 0: Nagłówek wyśrodkowany
  LCD_SET(3, 0); 
  LCD_PRINT("~ Wejherowo ~");

  if (alarmEnabled) {
    LCD_SET(19, 0); // Prawy górny róg dla aktywnego budzika
    LCD_WRITE(byte(0));  
  }

  // Wiersz 1: Znacznik synchronizacji NTP widoczny przez 1 godzinę (3600000 ms) po synchronizacji
  if (WiFiSync::hasNtpSynced() && (millis() - WiFiSync::getLastNtpSyncTime() <= 3600000UL)) {
    LCD_SET(17, 1);
    LCD_PRINT("(N)");
  }

  // System time fetch dla daty
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  // Wiersz 2: Data, np. "17 marca 2026"
  // tm_year == lata od 1900, więc tm_year > 120 to rok po 2020 (NTP ok)
  if (timeinfo.tm_year > 120) {
    char dateBuf[25];
    int day = timeinfo.tm_mday;
    int monIndex = timeinfo.tm_mon; // 0-11
    int year = timeinfo.tm_year + 1900;
    
    snprintf(dateBuf, sizeof(dateBuf), "%d %s %d", day, polishMonths[monIndex], year);
    
    int len = strlen(dateBuf);
    int pad = (20 - len) / 2;
    if (pad < 0) pad = 0;
    
    LCD_SET(pad, 2);
    LCD_PRINT(dateBuf);
  } else {
    // Brak lub słaba synchronizacja NTP
    LCD_SET(1, 2);
    LCD_PRINT("-- brak daty NTP --");
  }

  // Wiersz 3: Aktywny zsynchronizowany, globalny czas z obramowaniem
  char timeBuf[25];
  snprintf(timeBuf, sizeof(timeBuf), ">> %02d:%02d:%02d <<", hours, minutes, seconds);
  
  int tLen = strlen(timeBuf);
  int tPad = (20 - tLen) / 2;
  
  LCD_SET(tPad, 3);
  LCD_PRINT(timeBuf);

  LCD_DUMP();
}

// ============================================================================
// AIR QUALITY SCREEN (20x4)
// ============================================================================

static void lcdPrintCenteredRow(uint8_t row, const char* text) {
  const int maxCols = 20;
  int len = (int)strlen(text);
  if (len > maxCols) len = maxCols;
  int pad = (maxCols - len) / 2;
  if (pad < 0) pad = 0;

  LCD_SET(0, row);
  for (int i = 0; i < maxCols; ++i) {
    LCD_PRINT(" ");
  }

  LCD_SET((uint8_t)pad, row);
  for (int i = 0; i < len; ++i) {
    LCD_WRITE((uint8_t)text[i]);
  }
}

static const char* airHeaderFor(uint16_t pm25, uint16_t eco2, uint8_t aqi) {
  // Priorytety:
  // - Najpierw stany alarmowe (Poziom 5, potem 4)
  // - Potem najlepsze poziomy (1 -> 2 -> 3)
  //   bo kryteria są zagnieżdżone (IDEALNE ⊂ DOBRE ⊂ SREDNIE).

  // Poziom 5: SMOG / ZLE
  if (pm25 >= 50 || eco2 >= 2000 || aqi == 5) {
    if (pm25 >= 50) return "! UWAGA: SMOG !";
    return "! ZLE POWIETRZE !";
  }

  // Poziom 4: PRZEWIETRZ!
  if ((eco2 >= 1500 || aqi >= 4) && pm25 < 50) {
    return "! PRZEWIETRZ !";
  }

  // Poziom 1: IDEALNE
  if (pm25 < 15 && eco2 < 800 && aqi == 1) {
    return "POWIETRZE: IDEALNE";
  }

  // Poziom 2: DOBRE
  if (pm25 < 25 && eco2 < 1000 && aqi <= 2) {
    return "POWIETRZE: DOBRE";
  }

  // Poziom 3: SREDNIE
  if (pm25 < 50 && eco2 < 1500 && aqi <= 3) {
    return "POWIETRZE: SREDNIE";
  }

  // Jeśli nie wpasowuje się idealnie w powyższe progi (np. brak danych / nietypowa kombinacja)
  // wybierz bezpieczny komunikat.
  return "POWIETRZE: ---";
}

static void padRightTo20(char* line) {
  const int maxCols = 20;
  const int len = (int)strlen(line);
  if (len >= maxCols) {
    line[maxCols] = '\0';
    return;
  }
  for (int i = len; i < maxCols; ++i) line[i] = ' ';
  line[maxCols] = '\0';
}

void drawAirScreen() {
  LCD_CLEAR();

  // Collect values.
  const bool ensGasValid = ENS160AHT21Screen::runtimeData.hasGasSample;
  const bool ensClimateValid = ENS160AHT21Screen::runtimeData.hasClimateSample;
  const uint8_t aqi = ensGasValid ? ENS160AHT21Screen::runtimeData.aqi : 0;
  const uint16_t eco2 = ensGasValid ? ENS160AHT21Screen::runtimeData.eco2 : 0;

  // PM2.5: bierzemy ATM (bardziej „ambient”), a gdy PMS wyłączony, pokażemy kreski.
  const bool pmValid = pms5003Enabled && pms5003_PM2_5_ATM > 0;
  const uint16_t pm25 = pmValid ? pms5003_PM2_5_ATM : 0;

  // Decide header: if we have at least one of PM or ENS gas/climate, attempt header.
  const bool haveAny = pmValid || ensGasValid || ensClimateValid;
  const char* header = "POWIETRZE: BRAK DANYCH";
  if (haveAny) {
    header = airHeaderFor(pm25, eco2, aqi);
  }
  lcdPrintCenteredRow(0, header);

  // Row 1: temperature + humidity.
  {
    char line[21];
    // Prefer ENS/AHT21 climate data if available, otherwise fallback to DHT sensor.
    if (ensClimateValid) {
      const float t = ENS160AHT21Screen::runtimeData.temperatureC;
      const int hum = (int)(ENS160AHT21Screen::runtimeData.humidityPct + 0.5f);
      snprintf(line, sizeof(line), " %5.1f\xDF" "C |  %3d%%    ", t, hum);
    } else if (dhtReady) {
      const int hum = (int)(dhtHumidity + 0.5f);
      snprintf(line, sizeof(line), " %5.1f\xDF" "C |  %3d%%    ", dhtTemperature, hum);
    } else {
      snprintf(line, sizeof(line), "  --.-\xDF" "C |   --%%    ");
    }
    padRightTo20(line);
    LCD_SET(0, 1);
    LCD_PRINT(line);
  }

  // Row 2: PM2.5
  {
    char line[21];
    if (pmValid) {
      snprintf(line, sizeof(line), "  PM2.5: %3u ug/m3  ", (unsigned)pm25);
    } else {
      snprintf(line, sizeof(line), "  PM2.5:  -- ug/m3  ");
    }
    padRightTo20(line);
    LCD_SET(0, 2);
    LCD_PRINT(line);
  }

  // Row 3: AQI + CO2
  {
    char line[21];
    if (ensGasValid) {
      char right[21];
      snprintf(right, sizeof(right), "eCO2:%4u", (unsigned)eco2);
      // Build left part (AQI) then right-justify the right part into remaining space so total is 20 cols.
      int left = snprintf(line, sizeof(line), " AQI:%-2u  |", (unsigned)aqi);
      int rem = 20 - left;
      int rlen = (int)strlen(right);
      int pad = rem - rlen;
      if (pad < 0) pad = 0;
      // append pad spaces then right text
      int pos = left;
      for (int i = 0; i < pad && pos < 20; ++i) line[pos++] = ' ';
      for (int i = 0; i < rlen && pos < 20; ++i) line[pos++] = right[i];
      // fill remaining with spaces (shouldn't be necessary)
      for (; pos < 20; ++pos) line[pos] = ' ';
      line[20] = '\0';
    } else {
      // No gas data — show placeholder but keep alignment
      snprintf(line, sizeof(line), " AQI:--  | eCO2:----");
      padRightTo20(line);
    }
    LCD_SET(0, 3);
    LCD_PRINT(line);
  }

  LCD_DUMP();
}

// --- Ekran menu ---
void drawMenu() {
  LCD_CLEAR();
  const int first = (menuIndex / 4) * 4;

  for (int i = 0; i < 4; i++) {
    const int item = first + i;
    if (item >= menuCount) break;

    LCD_SET(0, i);
    LCD_PRINT(item == menuIndex ? "> " : "  ");

    if (item == 12) {
      if (radioMode == WIFI_ONLY) {
        LCD_PRINT("BLUETOOTH MODE");  // Teraz w WiFi, przełącz na BT
      } else {
        LCD_PRINT("WIFI MODE     ");  // Teraz w BT, przełącz na WiFi
      }
    } else {
      LCD_PRINT(menuItems[item]);
    }
  }
  LCD_DUMP();
}

// --- Ekran ustawiania czasu ---
void drawSetTime() {
  LCD_CLEAR();

  // Centered header
  lcdPrintCenteredRow(0, "USTAW CZAS");

  // Separator
  LCD_SET(0, 1);
  LCD_PRINT(" ------------------ ");

  // Centered time line: "< [HH]:MM:SS >" with brackets around active field
  char tbuf[21];
  char hh[3]; char mm[3]; char ss[3];
  snprintf(hh, sizeof(hh), "%02d", hours);
  snprintf(mm, sizeof(mm), "%02d", minutes);
  snprintf(ss, sizeof(ss), "%02d", seconds);

  if (editState == EDIT_HOURS) {
    snprintf(tbuf, sizeof(tbuf), "< [%s]:%s:%s >", hh, mm, ss);
  } else if (editState == EDIT_MINUTES) {
    snprintf(tbuf, sizeof(tbuf), "< %s:[%s]:%s >", hh, mm, ss);
  } else if (editState == EDIT_SECONDS) {
    snprintf(tbuf, sizeof(tbuf), "< %s:%s:[%s] >", hh, mm, ss);
  } else {
    snprintf(tbuf, sizeof(tbuf), "< %s:%s:%s >", hh, mm, ss);
  }

  lcdPrintCenteredRow(2, tbuf);

  // Bottom row: leave empty
  LCD_SET(0, 3);
  for (int i = 0; i < 20; ++i) LCD_PRINT(" ");
  LCD_DUMP();
}

// --- Ekran budzika ---
void drawAlarm() {
  LCD_CLEAR();

  // Centered header
  lcdPrintCenteredRow(0, "USTAW BUDZIK");

  // Separator
  LCD_SET(0, 1);
  LCD_PRINT(" ------------------ ");

  // Centered time line: "< [HH]:MM >" with brackets around active field
  char abuf[21];
  char hh[3]; char mm[3];
  snprintf(hh, sizeof(hh), "%02d", alarmHour);
  snprintf(mm, sizeof(mm), "%02d", alarmMinute);

  if (editState == EDIT_HOURS) {
    snprintf(abuf, sizeof(abuf), "< [%s]:%s >", hh, mm);
  } else if (editState == EDIT_MINUTES) {
    snprintf(abuf, sizeof(abuf), "< %s:[%s] >", hh, mm);
  } else {
    snprintf(abuf, sizeof(abuf), "< %s:%s >", hh, mm);
  }

  lcdPrintCenteredRow(2, abuf);

  // Bottom row empty for aesthetics
  LCD_SET(0, 3);
  for (int i = 0; i < 20; ++i) LCD_PRINT(" ");

  LCD_DUMP();
}

// --- Ekran Minutnika (Timer) ---
void drawTimer() {
  LCD_CLEAR();

  // Centered header
  lcdPrintCenteredRow(0, "MINUTNIK");

  // Separator line
  LCD_SET(0, 1);
  LCD_PRINT(" ------------------ ");

  // Build centered time line: when editing show brackets around active field,
  // when running show remaining time counting down (HH:MM:SS).
  char tbuf[21];
  if (timerRunning) {
    unsigned long nowMs = millis();
    unsigned long elapsed = (nowMs >= timerStartMillis) ? (nowMs - timerStartMillis) : 0;
    long remainingMs = (long)timerDurationMs - (long)elapsed;
    if (remainingMs < 0) remainingMs = 0;
    int rh = (int)(remainingMs / 3600000L);
    int rm = (int)((remainingMs % 3600000L) / 60000L);
    int rs = (int)((remainingMs % 60000L) / 1000L);
    // Show countdown with arrows as requested: "> HH:MM:SS <"
    snprintf(tbuf, sizeof(tbuf), "> %02d:%02d:%02d <", rh, rm, rs);
  } else {
    char hh[3]; char mm[3]; char ss[3];
    snprintf(hh, sizeof(hh), "%02d", timerSetHours);
    snprintf(mm, sizeof(mm), "%02d", timerSetMinutes);
    snprintf(ss, sizeof(ss), "%02d", timerSetSeconds);

    if (editState == EDIT_HOURS) {
      snprintf(tbuf, sizeof(tbuf), "< [%s]:%s:%s >", hh, mm, ss);
    } else if (editState == EDIT_MINUTES) {
      snprintf(tbuf, sizeof(tbuf), "< %s:[%s]:%s >", hh, mm, ss);
    } else if (editState == EDIT_SECONDS) {
      snprintf(tbuf, sizeof(tbuf), "< %s:%s:[%s] >", hh, mm, ss);
    } else {
      snprintf(tbuf, sizeof(tbuf), "< %s:%s:%s >", hh, mm, ss);
    }
  }

  lcdPrintCenteredRow(2, tbuf);

  // Bottom row empty for aesthetics
  LCD_SET(0, 3);
  for (int i = 0; i < 20; ++i) LCD_PRINT(" ");
  LCD_DUMP();
}

// --- Ekran stopera ---
void drawStoper() {
  LCD_CLEAR();

  unsigned long t = stoperElapsed;
  if (stoperRunning) {
    t += millis() - stoperStart;
  }

  const int cs = (t / 10) % 100;
  const int s  = (t / 1000) % 60;
  const int m  = (t / 60000) % 100;

  // Centered header
  lcdPrintCenteredRow(0, "STOPER");

  // Separator
  LCD_SET(0, 1);
  LCD_PRINT(" ------------------ ");

  // Centered stopwatch time: "> MM:SS.CS <"
  char buf[21];
  // If elapsed >= 1 hour, show hours zone: "> HH:MM:SS.CS <" and update 7-seg to HH:MM:SS
  unsigned long totalMs = t;
  int hh = (int)(totalMs / 3600000UL);
  if (hh > 0) {
    int rm = (int)((totalMs % 3600000UL) / 60000UL);
    int rs = (int)((totalMs % 60000UL) / 1000UL);
    int rcs = (int)((totalMs / 10) % 100);
    snprintf(buf, sizeof(buf), "> %02d:%02d:%02d.%02d <", hh, rm, rs, rcs);
    lcdPrintCenteredRow(2, buf);

    // Bottom empty
    LCD_SET(0, 3);
    for (int i = 0; i < 20; ++i) LCD_PRINT(" ");

    // Update 7-seg to HH:MM:SS (drop centisec on 7-seg)
    uint8_t HHb = ((hh / 10) << 4) | (hh % 10);
    uint8_t MMb = ((rm / 10) << 4) | (rm % 10);
    uint8_t SSb = ((rs / 10) << 4) | (rs % 10);
    digitalWrite(LATCH_PIN, LOW);
    slowShiftOut(swapNibbles(SSb));
    slowShiftOut(swapNibbles(MMb));
    slowShiftOut(swapNibbles(HHb));
    digitalWrite(LATCH_PIN, HIGH);
    LCD_DUMP();
    return;
  }

  // Default (no hours): "> MM:SS.CS <"
  snprintf(buf, sizeof(buf), "> %02d:%02d.%02d <", m, s, cs);
  lcdPrintCenteredRow(2, buf);

  // Bottom empty
  LCD_SET(0, 3);
  for (int i = 0; i < 20; ++i) LCD_PRINT(" ");

  updateSevenSegStoper(m, s, cs);
  LCD_DUMP();
}

// --- Ekran debug STM32 ---
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

// --- Pomocnicza do rysowania czasu ---
void printTime(bool edit) {
  printVal(hours, edit && editState == EDIT_HOURS);
  LCD_PRINT(":");
  printVal(minutes, edit && editState == EDIT_MINUTES);
  LCD_PRINT(":");
  printVal(seconds, edit && editState == EDIT_SECONDS);
}

// --- Pomocnicza do printTime ---
void printVal(int v, bool sel) {
  if (sel) LCD_PRINT("[");
  if (v < 10) LCD_PRINT("0");
  LCD_PRINT(v);
  if (sel) LCD_PRINT("]");
}

// ============================================================================
// STATYSTYKI UI
// ============================================================================

constexpr int STATS_ITEMS_PER_PAGE = 3;

static bool isEns160State(AppState state) {
  return state == STATE_ENS160_AHT21 ||
         state == STATE_ENS160_AHT21_SUMMARY ||
         state == STATE_ENS160_AHT21_GAS_AQI ||
         state == STATE_ENS160_AHT21_GAS_TVOC ||
         state == STATE_ENS160_AHT21_GAS_ECO2 ||
         state == STATE_ENS160_AHT21_CLIMATE_TEMP ||
         state == STATE_ENS160_AHT21_CLIMATE_HUM ||
         state == STATE_ENS160_AHT21_GAS ||
         state == STATE_ENS160_AHT21_CLIMATE ||
         state == STATE_ENS160_AHT21_STATUS;
}

static void printEnsFloatOrDash(bool available, float value, uint8_t width = 4, uint8_t precision = 1) {
  if (!available) {
    LCD_PRINT("--");
    return;
  }

  char buffer[12];
  dtostrf(value, width, precision, buffer);
  LCD_PRINT(buffer);
}

static void printEnsAgeSeconds(uint32_t lastUpdateMs) {
  if (lastUpdateMs == 0) {
    LCD_PRINT("--s");
    return;
  }

  LCD_PRINT((millis() - lastUpdateMs) / 1000UL);
  LCD_PRINT("s");
}

struct Ens160UiHistory {
  bool hasAqi = false;
  bool hasTvoc = false;
  bool hasEco2 = false;
  bool hasTemp = false;
  bool hasHum = false;
  uint8_t minAqi = 0;
  uint8_t maxAqi = 0;
  uint16_t minTvoc = 0;
  uint16_t maxTvoc = 0;
  uint16_t minEco2 = 0;
  uint16_t maxEco2 = 0;
  float minTemp = 0.0f;
  float maxTemp = 0.0f;
  float minHum = 0.0f;
  float maxHum = 0.0f;
  uint32_t lastProcessedUpdateMs = 0;
};

static Ens160UiHistory s_ens160UiHistory;

static void updateEns160UiHistory(const ENS160AHT21Screen::RuntimeData& data) {
  if (data.lastUpdateMs == 0 || data.lastUpdateMs == s_ens160UiHistory.lastProcessedUpdateMs) {
    return;
  }

  s_ens160UiHistory.lastProcessedUpdateMs = data.lastUpdateMs;

  if (data.hasGasSample) {
    if (!s_ens160UiHistory.hasAqi) {
      s_ens160UiHistory.minAqi = data.aqi;
      s_ens160UiHistory.maxAqi = data.aqi;
      s_ens160UiHistory.hasAqi = true;
    } else {
      s_ens160UiHistory.minAqi = min<uint8_t>(s_ens160UiHistory.minAqi, data.aqi);
      s_ens160UiHistory.maxAqi = max<uint8_t>(s_ens160UiHistory.maxAqi, data.aqi);
    }

    if (!s_ens160UiHistory.hasTvoc) {
      s_ens160UiHistory.minTvoc = data.tvoc;
      s_ens160UiHistory.maxTvoc = data.tvoc;
      s_ens160UiHistory.hasTvoc = true;
    } else {
      s_ens160UiHistory.minTvoc = min<uint16_t>(s_ens160UiHistory.minTvoc, data.tvoc);
      s_ens160UiHistory.maxTvoc = max<uint16_t>(s_ens160UiHistory.maxTvoc, data.tvoc);
    }

    if (!s_ens160UiHistory.hasEco2) {
      s_ens160UiHistory.minEco2 = data.eco2;
      s_ens160UiHistory.maxEco2 = data.eco2;
      s_ens160UiHistory.hasEco2 = true;
    } else {
      s_ens160UiHistory.minEco2 = min<uint16_t>(s_ens160UiHistory.minEco2, data.eco2);
      s_ens160UiHistory.maxEco2 = max<uint16_t>(s_ens160UiHistory.maxEco2, data.eco2);
    }
  }

  if (data.hasClimateSample) {
    if (!s_ens160UiHistory.hasTemp) {
      s_ens160UiHistory.minTemp = data.temperatureC;
      s_ens160UiHistory.maxTemp = data.temperatureC;
      s_ens160UiHistory.hasTemp = true;
    } else {
      s_ens160UiHistory.minTemp = min(s_ens160UiHistory.minTemp, data.temperatureC);
      s_ens160UiHistory.maxTemp = max(s_ens160UiHistory.maxTemp, data.temperatureC);
    }

    if (!s_ens160UiHistory.hasHum) {
      s_ens160UiHistory.minHum = data.humidityPct;
      s_ens160UiHistory.maxHum = data.humidityPct;
      s_ens160UiHistory.hasHum = true;
    } else {
      s_ens160UiHistory.minHum = min(s_ens160UiHistory.minHum, data.humidityPct);
      s_ens160UiHistory.maxHum = max(s_ens160UiHistory.maxHum, data.humidityPct);
    }
  }
}

static void printEnsMenuValue(int itemIndex, const ENS160AHT21Screen::RuntimeData& data) {
  switch (itemIndex) {
    case 0:
      LCD_PRINT("AQI: ");
      if (data.hasGasSample) {
        LCD_PRINT(data.aqi);
      } else {
        LCD_PRINT("--");
      }
      break;
    case 1:
      LCD_PRINT("TVOC: ");
      if (data.hasGasSample) {
        LCD_PRINT(data.tvoc);
      } else {
        LCD_PRINT("--");
      }
      break;
    case 2:
      LCD_PRINT("eCO2: ");
      if (data.hasGasSample) {
        LCD_PRINT(data.eco2);
      } else {
        LCD_PRINT("--");
      }
      break;
    case 3:
      LCD_PRINT("Temp: ");
      printEnsFloatOrDash(data.hasClimateSample, data.temperatureC, 4, 1);
      if (data.hasClimateSample) {
        LCD_PRINT(" C");
      }
      break;
    case 4:
      LCD_PRINT("Hum: ");
      printEnsFloatOrDash(data.hasClimateSample, data.humidityPct, 3, 0);
      if (data.hasClimateSample) {
        LCD_PRINT(" %");
      }
      break;
    case 5:
      LCD_PRINT("Status: ");
      for (uint8_t idx = 0; idx < 10 && data.statusText[idx] != '\0'; ++idx) {
        LCD_PRINT(data.statusText[idx]);
      }
      break;
    default:
      break;
  }
}

void drawStats() {
  // Optymalizacja dla ekranów PMS5003: rysuj tylko gdy są nowe dane
  if (appState == STATE_PMS5003 || appState == STATE_PMS5003_CF1 ||
      appState == STATE_PMS5003_CF1_PM1 || appState == STATE_PMS5003_CF1_PM25 || appState == STATE_PMS5003_CF1_PM10 ||
      appState == STATE_PMS5003_ATM || appState == STATE_PMS5003_ATM_PM1 || appState == STATE_PMS5003_ATM_PM25 || appState == STATE_PMS5003_ATM_PM10 ||
      appState == STATE_PMS5003_PARTICLES || appState == STATE_PMS5003_PARTICLES_0_3 || appState == STATE_PMS5003_PARTICLES_0_5 ||
      appState == STATE_PMS5003_PARTICLES_1_0 || appState == STATE_PMS5003_PARTICLES_2_5 || appState == STATE_PMS5003_PARTICLES_5_0 || appState == STATE_PMS5003_PARTICLES_10_0 ||
      appState == STATE_PMS5003_TELEMETRY) {
    static uint32_t lastPmsSeen = 0;
    uint32_t lu = PMS5003Sensor::getLastUpdateTime();
    if (!pmsScreenDirty && lu == lastPmsSeen) return; // brak nowych danych -> nie rysuj
    lastPmsSeen = lu;
    pmsScreenDirty = false;
  }

  if (isEns160State(appState)) {
    static uint32_t lastEns160Seen = 0;
    const uint32_t lu = ENS160AHT21Screen::runtimeData.lastUpdateMs;
    if (!ENS160AHT21Screen::screenDirty && lu == lastEns160Seen) return;
    lastEns160Seen = lu;
    ENS160AHT21Screen::screenDirty = false;
  }

  LCD_CLEAR();
  const AppStats stats = statsManager.getStats();
  updateEns160UiHistory(ENS160AHT21Screen::runtimeData);

  // === 1. MENU STATYSTYK (LISTA Z LICZBAMI) ===
  if (appState == STATE_STATS) {
    LCD_SET(2, 0);
    LCD_PRINT("MENU STATYSTYK");

    const int first = (statsMenuIndex / STATS_ITEMS_PER_PAGE) * STATS_ITEMS_PER_PAGE;

    for (int row = 0; row < STATS_ITEMS_PER_PAGE; row++) {
      const int i = first + row;
      if (i >= statsMenuCount) break;

      LCD_SET(0, row + 1);
      LCD_PRINT(i == statsMenuIndex ? "> " : "  ");

      switch (i) {
        case 0:
          LCD_PRINT("Kliki ");
          LCD_PRINT(stats.totalClicks);
          break;
        case 1:
          LCD_PRINT("Kroki ");
          LCD_PRINT(statsManager.getTotalSteps());
          break;
        case 2:
          LCD_PRINT("Temp min/max");
          break;
        case 3:
          LCD_PRINT("Wilg min/max");
          break;
        case 4:
          LCD_PRINT("Zasoby");
          break;
        case 5:
          LCD_PRINT("Wyjscie");
          break;
      }
    }
  }
  // === 1b. MENU PMS5003 (LISTA Z WYBOREM) ===
  else if (appState == STATE_PMS5003) {
    LCD_SET(2, 0);
    LCD_PRINT("MENU PMS5003");

    const int first = (pms5003MenuIndex / 3) * 3;

    for (int row = 0; row < 3; row++) {
      const int i = first + row;
      if (i >= pms5003MenuCount) break;

      LCD_SET(0, row + 1);
      LCD_PRINT(i == pms5003MenuIndex ? "> " : "  ");
      LCD_PRINT(pms5003MenuItems[i]);
    }
  }
  else if (appState == STATE_ENS160_AHT21) {
    LCD_SET(1, 0);
    LCD_PRINT("AHT21 + ENS160");

    const int first = (ens160MenuIndex / 3) * 3;
    for (int row = 0; row < 3; row++) {
      const int i = first + row;
      if (i >= ens160MenuCount) break;

      LCD_SET(0, row + 1);
      LCD_PRINT(i == ens160MenuIndex ? "> " : "  ");
      printEnsMenuValue(i, ENS160AHT21Screen::runtimeData);
    }
  }
  else if (appState == STATE_ENS160_AHT21_GAS_AQI) {
    const ENS160AHT21Screen::RuntimeData& data = ENS160AHT21Screen::runtimeData;

    LCD_SET(0, 0);
    LCD_PRINT("AQI");
    LCD_SET(0, 1);
    LCD_PRINT("Biezaca: ");
    if (data.hasGasSample) {
      LCD_PRINT(data.aqi);
    } else {
      LCD_PRINT("--");
    }
    LCD_SET(0, 2);
    if (s_ens160UiHistory.hasAqi) {
      LCD_PRINT("Min:");
      LCD_PRINT(s_ens160UiHistory.minAqi);
      LCD_PRINT(" Max:");
      LCD_PRINT(s_ens160UiHistory.maxAqi);
    } else {
      LCD_PRINT("Min:-- Max:--");
    }
    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Powrot");
  }
  else if (appState == STATE_ENS160_AHT21_GAS_TVOC) {
    const ENS160AHT21Screen::RuntimeData& data = ENS160AHT21Screen::runtimeData;

    LCD_SET(0, 0);
    LCD_PRINT("TVOC");
    LCD_SET(0, 1);
    LCD_PRINT("Biezaca: ");
    if (data.hasGasSample) {
      LCD_PRINT(data.tvoc);
      LCD_PRINT(" ppb");
    } else {
      LCD_PRINT("--");
    }
    LCD_SET(0, 2);
    if (s_ens160UiHistory.hasTvoc) {
      LCD_PRINT("Min:");
      LCD_PRINT(s_ens160UiHistory.minTvoc);
      LCD_PRINT(" Max:");
      LCD_PRINT(s_ens160UiHistory.maxTvoc);
    } else {
      LCD_PRINT("Min:-- Max:--");
    }
    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Powrot");
  }
  else if (appState == STATE_ENS160_AHT21_GAS_ECO2) {
    const ENS160AHT21Screen::RuntimeData& data = ENS160AHT21Screen::runtimeData;

    LCD_SET(0, 0);
    LCD_PRINT("eCO2");
    LCD_SET(0, 1);
    LCD_PRINT("Biezaca: ");
    if (data.hasGasSample) {
      LCD_PRINT(data.eco2);
      LCD_PRINT(" ppm");
    } else {
      LCD_PRINT("--");
    }
    LCD_SET(0, 2);
    if (s_ens160UiHistory.hasEco2) {
      LCD_PRINT("Min:");
      LCD_PRINT(s_ens160UiHistory.minEco2);
      LCD_PRINT(" Max:");
      LCD_PRINT(s_ens160UiHistory.maxEco2);
    } else {
      LCD_PRINT("Min:-- Max:--");
    }
    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Powrot");
  }
  else if (appState == STATE_ENS160_AHT21_CLIMATE_TEMP) {
    const ENS160AHT21Screen::RuntimeData& data = ENS160AHT21Screen::runtimeData;

    LCD_SET(0, 0);
    LCD_PRINT("Temperatura");
    LCD_SET(0, 1);
    LCD_PRINT("Biezaca: ");
    printEnsFloatOrDash(data.hasClimateSample, data.temperatureC);
    if (data.hasClimateSample) {
      LCD_PRINT(" C");
    }
    LCD_SET(0, 2);
    if (s_ens160UiHistory.hasTemp) {
      LCD_PRINT("Min:");
      printEnsFloatOrDash(true, s_ens160UiHistory.minTemp, 4, 1);
      LCD_PRINT(" Max:");
      printEnsFloatOrDash(true, s_ens160UiHistory.maxTemp, 4, 1);
    } else {
      LCD_PRINT("Min:-- Max:--");
    }
    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Powrot");
  }
  else if (appState == STATE_ENS160_AHT21_CLIMATE_HUM) {
    const ENS160AHT21Screen::RuntimeData& data = ENS160AHT21Screen::runtimeData;

    LCD_SET(0, 0);
    LCD_PRINT("Wilgotnosc");
    LCD_SET(0, 1);
    LCD_PRINT("Biezaca: ");
    printEnsFloatOrDash(data.hasClimateSample, data.humidityPct);
    if (data.hasClimateSample) {
      LCD_PRINT(" %");
    }
    LCD_SET(0, 2);
    if (s_ens160UiHistory.hasHum) {
      LCD_PRINT("Min:");
      printEnsFloatOrDash(true, s_ens160UiHistory.minHum, 3, 0);
      LCD_PRINT(" Max:");
      printEnsFloatOrDash(true, s_ens160UiHistory.maxHum, 3, 0);
    } else {
      LCD_PRINT("Min:-- Max:--");
    }
    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Powrot");
  }
  else if (appState == STATE_ENS160_AHT21_STATUS) {
    const ENS160AHT21Screen::RuntimeData& data = ENS160AHT21Screen::runtimeData;

    LCD_SET(0, 0);
    LCD_PRINT("Status");
    LCD_SET(0, 1);
    LCD_PRINT("Status: ");
    LCD_PRINT(data.statusText);
    LCD_SET(0, 2);
    LCD_PRINT("Gaz:");
    LCD_PRINT(data.hasGasSample ? "OK" : "--");
    LCD_PRINT(" Klim:");
    LCD_PRINT(data.hasClimateSample ? "OK" : "--");
    LCD_SET(0, 3);
    LCD_PRINT("Ostatnia: ");
    printEnsAgeSeconds(data.lastUpdateMs);
  }
  // === 1c. MENU USTAWIEŃ (Settings) ===
  else if (appState == STATE_SETTINGS) {
    LCD_SET(2, 0);
    LCD_PRINT("USTAWIENIA");

    const int first = (settingsMenuIndex / 3) * 3;

    for (int row = 0; row < 3; row++) {
      const int i = first + row;
      if (i >= settingsMenuCount) break;

      LCD_SET(0, row + 1);
      LCD_PRINT(i == settingsMenuIndex ? "> " : "  ");
      LCD_PRINT(settingsMenuItems[i]);
    }
  }
  // === 1d. USTAWIENIA PMS5003 (włącz/wyłącz) ===
  else if (appState == STATE_SETTINGS_PMS5003) {
    // Centered header
    lcdPrintCenteredRow(0, "PMS5003");

    // Separator
    LCD_SET(0, 1);
    LCD_PRINT(" ------------------ ");

    // Centered state line
    char pbuf[21];
    const char* pstate = settingsPmsMenuIndex == 0 ? "ON" : "OFF";
    snprintf(pbuf, sizeof(pbuf), "SENSOR: <  %s  >", pstate);
    lcdPrintCenteredRow(2, pbuf);

    // Bottom empty
    LCD_SET(0, 3);
    for (int i = 0; i < 20; ++i) LCD_PRINT(" ");
  }
  // === 1e. USTAWIENIA BUZERA (włącz/wyłącz) ===
  else if (appState == STATE_SETTINGS_BUZZER) {
    // Centered header
    lcdPrintCenteredRow(0, "BUZZER");

    // Separator
    LCD_SET(0, 1);
    LCD_PRINT(" ------------------ ");

    // Centered state line
    char bbuf[21];
    const char* bstate = settingsBuzzerMenuIndex == 0 ? "ON" : "OFF";
    snprintf(bbuf, sizeof(bbuf), "STAN:   <  %s  >", bstate);
    lcdPrintCenteredRow(2, bbuf);

    // Bottom empty
    LCD_SET(0, 3);
    for (int i = 0; i < 20; ++i) LCD_PRINT(" ");
  }
  // === 1f. USTAWIENIA MQTT (włącz/wyłącz) ===
  else if (appState == STATE_SETTINGS_MQTT) {
    // Centered header
    lcdPrintCenteredRow(0, "MQTT");

    // Separator
    LCD_SET(0, 1);
    LCD_PRINT(" ------------------ ");

    // Centered broker on/off line
    char buf[21];
    const char* state = settingsMqttMenuIndex == 0 ? "ON" : "OFF";
    snprintf(buf, sizeof(buf), "BROKER: <  %s  >", state);
    lcdPrintCenteredRow(2, buf);

    // Bottom row empty (instructions on selection screen)
    LCD_SET(0, 3);
    for (int i = 0; i < 20; ++i) LCD_PRINT(" ");
  }
  // === 1g. USTAWIENIE: ROTACJA EKRANU (1..10s, enkoder) ===
  else if (appState == STATE_SETTINGS_ROTATION) {
    // Header (centered)
    lcdPrintCenteredRow(0, "ROTACJA EKRANU");

    // Separator: centered dashes
    LCD_SET(0, 1);
    LCD_PRINT(" ------------------ ");

    // Row 2: CZAS with left/right markers, centered
    char valueBuf[32];
    snprintf(valueBuf, sizeof(valueBuf), "CZAS: < %2ds >", settingsRotationSec);
    lcdPrintCenteredRow(2, valueBuf);

    // Row 3: empty (instructions are on the second line already)
    LCD_SET(0, 3);
    for (int i = 0; i < 20; ++i) LCD_PRINT(" ");
  }
  // === 1h. USTAWIENIE: SYNCHRONIZACJA NTP (10..360 min, enkoder) ===
  else if (appState == STATE_SETTINGS_SYNC) {
    // Header
    lcdPrintCenteredRow(0, "SYNCHRONIZACJA");

    // Separator
    LCD_SET(0, 1);
    LCD_PRINT(" ------------------ ");

    // Row 2: minutes value
    char valueBuf[32];
    snprintf(valueBuf, sizeof(valueBuf), "CZAS: < %3dmin >", settingsSyncMinutes);
    lcdPrintCenteredRow(2, valueBuf);

    // Row 3: empty
    LCD_SET(0, 3);
    for (int i = 0; i < 20; ++i) LCD_PRINT(" ");
  }
  // === 2. WIDOK KLIKNIĘĆ ===
  else if (appState == STATE_STATS_CLICKS) {
    LCD_SET(0, 0);
    LCD_PRINT("LICZNIK KLIKNIEC");
    LCD_SET(0, 1);
    LCD_PRINT("Razem: ");
    LCD_PRINT(stats.totalClicks);
    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Powrot");
  }
  // === 3. WIDOK KROKÓW ===
  else if (appState == STATE_STATS_STEPS) {
    LCD_SET(0, 0);
    LCD_PRINT("LICZNIK KROKOW");
    LCD_SET(0, 1);
    LCD_PRINT("L: ");
    LCD_PRINT(stats.stepsLeft);
    LCD_SET(10, 1);
    LCD_PRINT("R: ");
    LCD_PRINT(stats.stepsRight);
    LCD_SET(0, 2);
    LCD_PRINT("Suma: ");
    LCD_PRINT(statsManager.getTotalSteps());
    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Powrot");
  }
  // === 4. WIDOK TEMPERATURY MIN/MAX ===
  else if (appState == STATE_STATS_TEMP) {
    const EnvStats e = statsManager.getEnvStats();
    char buf[10];

    LCD_SET(0, 0);
    LCD_PRINT("TEMPERATURA");

    LCD_SET(0, 1);
    LCD_PRINT("MIN: ");
    dtostrf(e.tempMin, 4, 1, buf);
    LCD_PRINT(buf);

    LCD_SET(0, 2);
    LCD_PRINT("MAX: ");
    dtostrf(e.tempMax, 4, 1, buf);
    LCD_PRINT(buf);

    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Powrot");
  }
  // === 5. WIDOK WILGOTNOŚCI MIN/MAX ===
  else if (appState == STATE_STATS_HUM) {
    const EnvStats e = statsManager.getEnvStats();
    char buf[10];

    LCD_SET(0, 0);
    LCD_PRINT("WILGOTNOSC");

    LCD_SET(0, 1);
    LCD_PRINT("MIN: ");
    dtostrf(e.humMin, 4, 1, buf);
    LCD_PRINT(buf);

    LCD_SET(0, 2);
    LCD_PRINT("MAX: ");
    dtostrf(e.humMax, 4, 1, buf);
    LCD_PRINT(buf);

    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Powrot");
  }
  // === 5b. MENU ZASOBÓW SYSTEMU ===
  else if (appState == STATE_STATS_RESOURCES_MENU) {
    LCD_SET(1, 0);
    LCD_PRINT("ZASOBY SYSTEMU");

    const int first = (resourcesMenuIndex / 3) * 3;

    for (int row = 0; row < 3; row++) {
      const int i = first + row;
      if (i >= resourcesMenuCount) break;

      LCD_SET(0, row + 1);
      LCD_PRINT(i == resourcesMenuIndex ? "> " : "  ");
      LCD_PRINT(resourcesMenuItems[i]);
    }
  }
  // === 5c. WIDOK PAMIĘCI RAM ===
  else if (appState == STATE_STATS_RESOURCES_RAM) {
    char buf[10];
    uint32_t ramMB = ramFreeBytes / (1024 * 1024);
    uint32_t ramKB = (ramFreeBytes % (1024 * 1024)) / 1024;

    LCD_SET(0, 0);
    LCD_PRINT("PAMIEC RAM");

    LCD_SET(0, 1);
    LCD_PRINT("Free: ");
    LCD_PRINT(ramMB);
    LCD_PRINT(".");
    LCD_PRINT(ramKB);
    LCD_PRINT(" MB");

    LCD_SET(0, 2);
    LCD_PRINT("Bytes: ");
    LCD_PRINT(ramFreeBytes);

    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Powrot");
  }
  // === 5d. WIDOK OBCIĄŻENIA CPU ===
  else if (appState == STATE_STATS_RESOURCES_CPU) {
    LCD_SET(0, 0);
    LCD_PRINT("CPU LOAD");

    LCD_SET(0, 1);
    LCD_PRINT("Calkowite: ");
    LCD_PRINT(cpuLoadPercent);
    LCD_PRINT("%");

    LCD_SET(0, 2);
    LCD_PRINT("CORE0: ");
    LCD_PRINT(cpuCore0Percent);
    LCD_PRINT("%");

    LCD_SET(0, 3);
    LCD_PRINT("CORE1: ");
    LCD_PRINT(cpuCore1Percent);
    LCD_PRINT("%");
  }
  // === 5e. WIDOK PAMIĘCI FLASH ===
  else if (appState == STATE_STATS_RESOURCES_FLASH) {
    char buf[10];
    uint32_t flashMB = flashFreeBytes / (1024 * 1024);
    uint32_t flashKB = (flashFreeBytes % (1024 * 1024)) / 1024;

    LCD_SET(0, 0);
    LCD_PRINT("PAMIEC FLASH");

    LCD_SET(0, 1);
    LCD_PRINT("Free: ");
    LCD_PRINT(flashMB);
    LCD_PRINT(".");
    LCD_PRINT(flashKB);
    LCD_PRINT(" MB");

    LCD_SET(0, 2);
    LCD_PRINT("Bytes: ");
    LCD_PRINT(flashFreeBytes);

    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Powrot");
  }
  // === 6. WIDOK PMS5003 TRYB FABRYCZNY CF=1 (BIEŻĄCE DANE Z WYBOREM) ===
  else if (appState == STATE_PMS5003_CF1) {
    LCD_SET(0, 0);
    LCD_PRINT("PMS5003 CF=1");
    
    // Wiersz 1: PM1.0 (z > jeśli wybrany)
    LCD_SET(0, 1);
    LCD_PRINT(pms5003CF1MenuIndex == 0 ? ">" : " ");
    LCD_PRINT(" PM1.0: ");
    LCD_PRINT(pms5003_PM1_0_CF1 > 0 ? pms5003_PM1_0_CF1 : 0);
    LCD_PRINT(" µg/m3");
    
    // Wiersz 2: PM2.5 (z > jeśli wybrany)
    LCD_SET(0, 2);
    LCD_PRINT(pms5003CF1MenuIndex == 1 ? ">" : " ");
    LCD_PRINT(" PM2.5: ");
    LCD_PRINT(pms5003_PM2_5_CF1 > 0 ? pms5003_PM2_5_CF1 : 0);
    LCD_PRINT(" µg/m3");
    
    // Wiersz 3: PM10 (z > jeśli wybrany)
    LCD_SET(0, 3);
    LCD_PRINT(pms5003CF1MenuIndex == 2 ? ">" : " ");
    LCD_PRINT(" PM10:  ");
    LCD_PRINT(pms5003_PM10_CF1 > 0 ? pms5003_PM10_CF1 : 0);
    LCD_PRINT(" µg/m3");
  }
  // === 6a. WIDOK SZCZEGÓŁÓW PM1.0 TRYB CF=1 (MIN/MAX) ===
  else if (appState == STATE_PMS5003_CF1_PM1) {
    LCD_SET(0, 0);
    LCD_PRINT("PM1.0 CF=1");
    
    LCD_SET(0, 1);
    LCD_PRINT("Biezaca: ");
    LCD_PRINT(pms5003_PM1_0_CF1);
    LCD_PRINT(" µg/m3");
    
    LCD_SET(0, 2);
    LCD_PRINT("Min:");
    LCD_PRINT(pms5003_PM1_0_CF1_MIN < 9999 ? pms5003_PM1_0_CF1_MIN : 0);
    LCD_PRINT(" Max:");
    LCD_PRINT(pms5003_PM1_0_CF1_MAX);
    
    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Powrot");
  }
  // === 6b. WIDOK SZCZEGÓŁÓW PM2.5 TRYB CF=1 (MIN/MAX) ===
  else if (appState == STATE_PMS5003_CF1_PM25) {
    LCD_SET(0, 0);
    LCD_PRINT("PM2.5 CF=1");
    
    LCD_SET(0, 1);
    LCD_PRINT("Biezaca: ");
    LCD_PRINT(pms5003_PM2_5_CF1);
    LCD_PRINT(" µg/m3");
    
    LCD_SET(0, 2);
    LCD_PRINT("Min:");
    LCD_PRINT(pms5003_PM2_5_CF1_MIN < 9999 ? pms5003_PM2_5_CF1_MIN : 0);
    LCD_PRINT(" Max:");
    LCD_PRINT(pms5003_PM2_5_CF1_MAX);
    
    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Powrot");
  }
  // === 6c. WIDOK SZCZEGÓŁÓW PM10 TRYB CF=1 (MIN/MAX) ===
  else if (appState == STATE_PMS5003_CF1_PM10) {
    LCD_SET(0, 0);
    LCD_PRINT("PM10 CF=1");
    
    LCD_SET(0, 1);
    LCD_PRINT("Biezaca: ");
    LCD_PRINT(pms5003_PM10_CF1);
    LCD_PRINT(" µg/m3");
    
    LCD_SET(0, 2);
    LCD_PRINT("Min:");
    LCD_PRINT(pms5003_PM10_CF1_MIN < 9999 ? pms5003_PM10_CF1_MIN : 0);
    LCD_PRINT(" Max:");
    LCD_PRINT(pms5003_PM10_CF1_MAX);
    
    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Powrot");
  }
  // === 7. WIDOK PMS5003 TRYB ATMOSFERYCZNY (BIEŻĄCE DANE Z WYBOREM) ===
  else if (appState == STATE_PMS5003_ATM) {
    LCD_SET(0, 0);
    LCD_PRINT("PMS5003 ATM");
    
    // Wiersz 1: PM1.0 (z > jeśli wybrany)
    LCD_SET(0, 1);
    LCD_PRINT(pms5003ATMMenuIndex == 0 ? ">" : " ");
    LCD_PRINT(" PM1.0: ");
    LCD_PRINT(pms5003_PM1_0_ATM > 0 ? pms5003_PM1_0_ATM : 0);
    LCD_PRINT(" µg/m3");
    
    // Wiersz 2: PM2.5 (z > jeśli wybrany)
    LCD_SET(0, 2);
    LCD_PRINT(pms5003ATMMenuIndex == 1 ? ">" : " ");
    LCD_PRINT(" PM2.5: ");
    LCD_PRINT(pms5003_PM2_5_ATM > 0 ? pms5003_PM2_5_ATM : 0);
    LCD_PRINT(" µg/m3");
    
    // Wiersz 3: PM10 (z > jeśli wybrany)
    LCD_SET(0, 3);
    LCD_PRINT(pms5003ATMMenuIndex == 2 ? ">" : " ");
    LCD_PRINT(" PM10:  ");
    LCD_PRINT(pms5003_PM10_ATM > 0 ? pms5003_PM10_ATM : 0);
    LCD_PRINT(" µg/m3");
  }
  // === 7a. WIDOK SZCZEGÓŁÓW PM1.0 TRYB ATM (MIN/MAX) ===
  else if (appState == STATE_PMS5003_ATM_PM1) {
    LCD_SET(0, 0);
    LCD_PRINT("PM1.0 ATM");
    
    LCD_SET(0, 1);
    LCD_PRINT("Biezaca: ");
    LCD_PRINT(pms5003_PM1_0_ATM);
    LCD_PRINT(" µg/m3");
    
    LCD_SET(0, 2);
    LCD_PRINT("Min:");
    LCD_PRINT(pms5003_PM1_0_ATM_MIN < 9999 ? pms5003_PM1_0_ATM_MIN : 0);
    LCD_PRINT(" Max:");
    LCD_PRINT(pms5003_PM1_0_ATM_MAX);
    
    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Powrot");
  }
  // === 7b. WIDOK SZCZEGÓŁÓW PM2.5 TRYB ATM (MIN/MAX) ===
  else if (appState == STATE_PMS5003_ATM_PM25) {
    LCD_SET(0, 0);
    LCD_PRINT("PM2.5 ATM");
    
    LCD_SET(0, 1);
    LCD_PRINT("Biezaca: ");
    LCD_PRINT(pms5003_PM2_5_ATM);
    LCD_PRINT(" µg/m3");
    
    LCD_SET(0, 2);
    LCD_PRINT("Min:");
    LCD_PRINT(pms5003_PM2_5_ATM_MIN < 9999 ? pms5003_PM2_5_ATM_MIN : 0);
    LCD_PRINT(" Max:");
    LCD_PRINT(pms5003_PM2_5_ATM_MAX);
    
    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Powrot");
  }
  // === 7c. WIDOK SZCZEGÓŁÓW PM10 TRYB ATM (MIN/MAX) ===
  else if (appState == STATE_PMS5003_ATM_PM10) {
    LCD_SET(0, 0);
    LCD_PRINT("PM10 ATM");
    
    LCD_SET(0, 1);
    LCD_PRINT("Biezaca: ");
    LCD_PRINT(pms5003_PM10_ATM);
    LCD_PRINT(" µg/m3");
    
    LCD_SET(0, 2);
    LCD_PRINT("Min:");
    LCD_PRINT(pms5003_PM10_ATM_MIN < 9999 ? pms5003_PM10_ATM_MIN : 0);
    LCD_PRINT(" Max:");
    LCD_PRINT(pms5003_PM10_ATM_MAX);
    
    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Powrot");
  }

  // ========== Particles ==========
  else if (appState == STATE_PMS5003_PARTICLES) {
    LCD_SET(0, 0);
    LCD_PRINT("Liczba Czastek");
    
    // Pagination: show 3 items per page
    const int itemsPerPage = 3;
    const int first = (pms5003ParticlesMenuIndex / itemsPerPage) * itemsPerPage;
    
    const char* particleLabels[] = {"0.3um", "0.5um", "1.0um", "2.5um", "5.0um", "10um"};
    uint16_t particleValues[] = {
      pms5003_particleCount_0_3,
      pms5003_particleCount_0_5,
      pms5003_particleCount_1_0,
      pms5003_particleCount_2_5,
      pms5003_particleCount_5_0,
      pms5003_particleCount_10_0
    };
    
    for (int row = 0; row < itemsPerPage; row++) {
      const int i = first + row;
      if (i >= 6) break;
      
      LCD_SET(0, row + 1);
      LCD_PRINT(i == pms5003ParticlesMenuIndex ? ">" : " ");
      LCD_PRINT(" ");
      LCD_PRINT(particleLabels[i]);
      LCD_PRINT(": ");
      LCD_PRINT(particleValues[i]);
    }
  }

  else if (appState == STATE_PMS5003_PARTICLES_0_3) {
    LCD_SET(0, 0);
    LCD_PRINT("0.3um");
    
    LCD_SET(0, 1);
    LCD_PRINT("Biezaca: ");
    LCD_PRINT(pms5003_particleCount_0_3);
    
    LCD_SET(0, 2);
    LCD_PRINT("Min:");
    LCD_PRINT(pms5003_particleCount_0_3_MIN < 9999 ? pms5003_particleCount_0_3_MIN : 0);
    LCD_PRINT(" Max:");
    LCD_PRINT(pms5003_particleCount_0_3_MAX);
    
    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Powrot");
  }

  else if (appState == STATE_PMS5003_PARTICLES_0_5) {
    LCD_SET(0, 0);
    LCD_PRINT("0.5um");
    
    LCD_SET(0, 1);
    LCD_PRINT("Biezaca: ");
    LCD_PRINT(pms5003_particleCount_0_5);
    
    LCD_SET(0, 2);
    LCD_PRINT("Min:");
    LCD_PRINT(pms5003_particleCount_0_5_MIN < 9999 ? pms5003_particleCount_0_5_MIN : 0);
    LCD_PRINT(" Max:");
    LCD_PRINT(pms5003_particleCount_0_5_MAX);
    
    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Powrot");
  }

  else if (appState == STATE_PMS5003_PARTICLES_1_0) {
    LCD_SET(0, 0);
    LCD_PRINT("1.0um");
    
    LCD_SET(0, 1);
    LCD_PRINT("Biezaca: ");
    LCD_PRINT(pms5003_particleCount_1_0);
    
    LCD_SET(0, 2);
    LCD_PRINT("Min:");
    LCD_PRINT(pms5003_particleCount_1_0_MIN < 9999 ? pms5003_particleCount_1_0_MIN : 0);
    LCD_PRINT(" Max:");
    LCD_PRINT(pms5003_particleCount_1_0_MAX);
    
    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Powrot");
  }

  else if (appState == STATE_PMS5003_PARTICLES_2_5) {
    LCD_SET(0, 0);
    LCD_PRINT("2.5um");
    
    LCD_SET(0, 1);
    LCD_PRINT("Biezaca: ");
    LCD_PRINT(pms5003_particleCount_2_5);
    
    LCD_SET(0, 2);
    LCD_PRINT("Min:");
    LCD_PRINT(pms5003_particleCount_2_5_MIN < 9999 ? pms5003_particleCount_2_5_MIN : 0);
    LCD_PRINT(" Max:");
    LCD_PRINT(pms5003_particleCount_2_5_MAX);
    
    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Powrot");
  }

  else if (appState == STATE_PMS5003_PARTICLES_5_0) {
    LCD_SET(0, 0);
    LCD_PRINT("5.0um");
    
    LCD_SET(0, 1);
    LCD_PRINT("Biezaca: ");
    LCD_PRINT(pms5003_particleCount_5_0);
    
    LCD_SET(0, 2);
    LCD_PRINT("Min:");
    LCD_PRINT(pms5003_particleCount_5_0_MIN < 9999 ? pms5003_particleCount_5_0_MIN : 0);
    LCD_PRINT(" Max:");
    LCD_PRINT(pms5003_particleCount_5_0_MAX);
    
    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Powrot");
  }

  else if (appState == STATE_PMS5003_PARTICLES_10_0) {
    LCD_SET(0, 0);
    LCD_PRINT("10um");
    
    LCD_SET(0, 1);
    LCD_PRINT("Biezaca: ");
    LCD_PRINT(pms5003_particleCount_10_0);
    
    LCD_SET(0, 2);
    LCD_PRINT("Min:");
    LCD_PRINT(pms5003_particleCount_10_0_MIN < 9999 ? pms5003_particleCount_10_0_MIN : 0);
    LCD_PRINT(" Max:");
    LCD_PRINT(pms5003_particleCount_10_0_MAX);
    
    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Powrot");
  }

  // ========== Telemetria ==========
  else if (appState == STATE_PMS5003_TELEMETRY) {
    LCD_SET(0, 0);
    LCD_PRINT("Telemetria");
    
    LCD_SET(0, 1);
    LCD_PRINT("Bledy: ");
    LCD_PRINT(pms5003_errorCount_current);
    LCD_PRINT("/");
    LCD_PRINT(pms5003_errorCount_total);
    
    LCD_SET(0, 2);
    LCD_PRINT("Bajty: ");
    if (pms5003_bytesReceived < 10) LCD_PRINT("0");
    LCD_PRINT(pms5003_bytesReceived);
    
    LCD_SET(0, 3);
    LCD_PRINT("Latencja: ");
    if (pms5003_latency_ms < 10) LCD_PRINT("0");
    if (pms5003_latency_ms < 100) LCD_PRINT("0");
    LCD_PRINT(pms5003_latency_ms);
    LCD_PRINT(" ms");
  }

  LCD_DUMP();
}


// ============================================================================
// IMPLEMENTACJA DHT (TEMPERATURA/WILGOTNOŚĆ)
// ============================================================================

void drawTemperature() {
    static float lastTemp = -1000;

    // Jeśli czujnik nie gotowy
    if (!dhtReady) {
        LCD_CLEAR();
        LCD_SET(0, 0);
        LCD_PRINT("Temperatura");
        LCD_SET(0, 1);
        LCD_PRINT("Odczyt...");
        LCD_DUMP();
        return;
    }

    // Optymalizacja: nie rysuj jeśli nic się nie zmieniło
    if (!dhtScreenDirty && dhtTemperature == lastTemp) return;
    
    dhtScreenDirty = false;
    lastTemp = dhtTemperature;

    LCD_CLEAR();
    LCD_SET(0, 0);
    LCD_PRINT("Temperatura");
    
    LCD_SET(0, 1);
    LCD_PRINT(dhtTemperature); 
    // Ręczna obsługa formatowania (float, 1 miejsce po przecinku) nie jest wprost w makrze,
    // ale LCD_PRINT(float) zazwyczaj drukuje 2 miejsca.
    // Jeśli potrzebujesz dokładnie 1 miejsca, możesz użyć lcd.print, ale wtedy mirror nie zadziała dla tej liczby.
    // Najlepiej zostawić domyślne print lub sformatować do String/buffer.
    
    LCD_WRITE(223); // Znak stopnia
    LCD_PRINT("C");

    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Wyjscie");
    
    LCD_DUMP();
}

void drawHumidity() {
    static float lastHum = -1000;

    if (!dhtReady) {
        LCD_CLEAR();
        LCD_SET(0, 0);
        LCD_PRINT("Wilgotnosc");
        LCD_SET(0, 1);
        LCD_PRINT("Odczyt...");
        LCD_DUMP();
        return;
    }

    if (!dhtScreenDirty && dhtHumidity == lastHum) return;
    
    dhtScreenDirty = false;
    lastHum = dhtHumidity;

    LCD_CLEAR();
    LCD_SET(0, 0);
    LCD_PRINT("Wilgotnosc");
    
    LCD_SET(0, 1);
    LCD_PRINT((int)dhtHumidity); // Rzutowanie na int dla ładniejszego wyglądu
    LCD_PRINT(" %");

    LCD_SET(0, 3);
    LCD_PRINT("Dlugi -> Wyjscie");

    LCD_DUMP();
}

// ============================================================================
// OBSŁUGA 7-SEGMENTOWEGO WYŚWIETLACZA DLA DHT
// ============================================================================

void showTemperature7Seg() {
    if (!dhtReady) return;

    // Zapisz aktualny czas tylko raz przy wejściu
    if (!timeSaved) {
        savedHours   = hours;
        savedMinutes = minutes;
        savedSeconds = seconds;
        timeSaved    = true;
    }

    // Formatowanie: np. 24.5°C -> wyświetl jako 00:24:50
    const int tempInt = constrain(static_cast<int>(dhtTemperature), 0, 99);
    const int tempDec = constrain(static_cast<int>((dhtTemperature - tempInt) * 10), 0, 99);

    // Nadpisz zmienne czasu dla wyświetlacza 7-seg
    // UWAGA: Prawdziwy czas przywracamy przy wyjściu (w UI_Controller)
    hours   = 0;
    minutes = tempInt;
    seconds = tempDec * 10;

    updateSevenSeg();
}

void showHumidity7Seg() {
    if (!dhtReady) return;

    if (!timeSaved) {
        savedHours   = hours;
        savedMinutes = minutes;
        savedSeconds = seconds;
        timeSaved    = true;
    }

    const int hum = constrain(static_cast<int>(dhtHumidity), 0, 99);

    hours   = 0;
    minutes = hum;
    seconds = 0;

    updateSevenSeg();
}

// ============================================================================
// ZASOBY SYSTEMU
// ============================================================================

void drawSystemResources() {
    LCD_CLEAR();

    // --- RAM INFO ---
    const uint32_t freeRam  = ESP.getFreeHeap();
    const uint32_t maxBlock = ESP.getMaxAllocHeap();
    (void)maxBlock;  // Nieużywane, ale dostępne do debugowania

    // --- FLASH INFO ---
    const uint32_t usedFlash = ESP.getSketchSize();
    const uint32_t freeFlash = ESP.getFreeSketchSpace();

    char buf[17];

    // Wiersz 0: Nagłówek
    LCD_SET(0, 0);
    LCD_PRINT("ZASOBY SYSTEMU");

    // Wiersz 1: RAM (Wolny)
    LCD_SET(0, 1);
    snprintf(buf, sizeof(buf), "RAM: %lu kB", freeRam / 1024);
    LCD_PRINT(buf);

    // Wiersz 2: Flash (Zajęty kodem)
    LCD_SET(0, 2);
    snprintf(buf, sizeof(buf), "FL Use: %lu kB", usedFlash / 1024);
    LCD_PRINT(buf);

    // Wiersz 3: Flash (Wolny)
    LCD_SET(0, 3);
    snprintf(buf, sizeof(buf), "FL Free: %lu kB", freeFlash / 1024);
    LCD_PRINT(buf);

    LCD_DUMP();
}

// ============================================================================
// EKRAN PRZEJŚCIA TRYBU (WiFi ↔ Bluetooth)
// ============================================================================

void drawModeTransition() {
    LCD_CLEAR();

    LCD_SET(0, 0);
    LCD_PRINT("ZMIANA TRYBU");

    LCD_SET(0, 1);
    LCD_PRINT("                ");

    LCD_SET(0, 2);
    LCD_PRINT("     RESET      ");

    LCD_SET(0, 3);
    LCD_PRINT("                ");

    LCD_DUMP();
}