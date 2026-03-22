#include "UI_Draw.h"
#include "LCDMirror.h"
#include "StatsManager.h"
#include "WiFiSync.h"
#include "ModeManager.h"
#include <LiquidCrystal_I2C.h>
#include <Esp.h>
#include "PMS_Czujnik.h"
#include "ENS160AHT21Screen.h"
#include "BMP280Sensor.h"
#include "LCDIcons.h"

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
extern int bmp280MenuIndex;
extern const char* bmp280MenuItems[];
extern int bmp280MenuCount;

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
extern bool alarmRinging;

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
extern int settingsRotationSec;
extern int settingsUiScreenIndex;
extern int settingsUiScreenCount;
extern const char* settingsUiScreenItems[];

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
// STAŁE I FUNKCJE POMOCNICZE
// ============================================================================

constexpr uint8_t SCREEN_WIDTH = 20;
constexpr uint8_t SCREEN_HEIGHT = 4;
constexpr const char* ALIGN_CENTER = " ------------------ ";

static void clearRow(uint8_t row) {
  LCD_CLEAR_ROW(row);
}

static void lcdPrintCentered(uint8_t row, const char* text) {
  int len = (int)strlen(text);
  if (len > SCREEN_WIDTH) len = SCREEN_WIDTH;
  int pad = (SCREEN_WIDTH - len) / 2;

  clearRow(row);
  LCD_SET((uint8_t)pad, row);
  for (int i = 0; i < len; ++i) {
    LCD_WRITE((uint8_t)text[i]);
  }
}

static void lcdPrintCentered(uint8_t row, const __FlashStringHelper* text) {
  int len = (int)strlen_P((const char*)text);
  if (len > SCREEN_WIDTH) len = SCREEN_WIDTH;
  int pad = (SCREEN_WIDTH - len) / 2;

  clearRow(row);
  LCD_SET((uint8_t)pad, row);
  LCD_PRINT(text);
}

static void lcdPrintCenteredWithAlarmIcon(uint8_t row, const char* text, bool showIcon) {
  const int textLen = (int)strlen(text);
  const int totalLen = textLen + (showIcon ? 1 : 0);
  const int pad = (SCREEN_WIDTH - min(totalLen, (int)SCREEN_WIDTH)) / 2;

  clearRow(row);
  LCD_SET((uint8_t)pad, row);
  if (showIcon) {
    LCD_WRITE(byte(LCDIcons::AlarmSlot));
  }
  LCD_PRINT(text);
}

static void lcdPrintCenteredWithIcons(uint8_t row, const char* text, const uint8_t* icons, uint8_t iconCount) {
  const int textLen = (int)strlen(text);
  const int totalLen = textLen + (int)iconCount;
  const int pad = (SCREEN_WIDTH - min(totalLen, (int)SCREEN_WIDTH)) / 2;

  clearRow(row);
  LCD_SET((uint8_t)pad, row);
  for (uint8_t i = 0; i < iconCount; ++i) {
    LCD_WRITE((uint8_t)icons[i]);
  }
  LCD_PRINT(text);
}

static bool isAnyAlarmArmed() {
  if (alarmEnabled || alarmRinging) {
    return true;
  }

  for (int i = 0; i < alarmsCount; ++i) {
    if (alarms[i].enabled) {
      return true;
    }
  }

  return false;
}

// --- Ekran główny ---
static const char* const polishMonths[] PROGMEM = {
    "STY", "LUT", "MAR", "KWI", "MAJ", "CZE",
  "LIP", "SIE", "WRZ", "PAZ", "LIS", "GRU"
};

void drawHome() {
  LCD_CLEAR();
  LCDIcons::loadPalette(lcd, LCDIcons::Palette::Home);

  // Header
  char titleBuf[21];
  snprintf(titleBuf, sizeof(titleBuf), "WEJHEROWO");
  const bool ntpFresh = WiFiSync::hasNtpSynced() && (millis() - WiFiSync::getLastNtpSyncTime() <= 3600000UL);
  const bool alarmArmed = isAnyAlarmArmed();
  uint8_t icons[2];
  uint8_t iconCount = 0;
  if (ntpFresh) {
    icons[iconCount++] = LCDIcons::NtpSlot;
  }
  if (alarmArmed) {
    icons[iconCount++] = LCDIcons::BellSlot;
  }

  if (iconCount > 0) {
    lcdPrintCenteredWithIcons(0, titleBuf, icons, iconCount);
  } else {
    lcdPrintCentered(0, titleBuf);
  }

  // Date (centered)
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  char dateBuf[21];
  snprintf(dateBuf, sizeof(dateBuf), "%02d %s %04d", timeinfo.tm_mday, polishMonths[timeinfo.tm_mon], 1900 + timeinfo.tm_year);
  lcdPrintCentered(1, dateBuf);

  // Row 2: centered time with symmetric arrows
  char timeLine[21];
  snprintf(timeLine, sizeof(timeLine), ">> %02d:%02d:%02d <<", hours, minutes, seconds);
  lcdPrintCentered(2, timeLine);

  // Row 3: indoor summary from BMP280 temperature/pressure + AHT21 humidity.
  char lineBuf[21];
  const bool bmpValid = BMP280Screen::runtimeData.hasSample;
  const bool ahtValid = ENS160AHT21Screen::runtimeData.hasClimateSample;

  if (bmpValid && ahtValid) {
    const float tempC = BMP280Screen::runtimeData.temperatureC;
    const int humidity = (int)(ENS160AHT21Screen::runtimeData.humidityPct + 0.5f);
    const float pressure = BMP280Screen::runtimeData.pressureHpa;
    snprintf(lineBuf, sizeof(lineBuf), "IN:%4.1fC %2d%% %4.0fhPa", tempC, humidity, pressure);
  } else if (bmpValid) {
    snprintf(lineBuf, sizeof(lineBuf), "IN:%4.1fC --%% %4.0fhPa", BMP280Screen::runtimeData.temperatureC, BMP280Screen::runtimeData.pressureHpa);
  } else if (ahtValid) {
    const int humidity = (int)(ENS160AHT21Screen::runtimeData.humidityPct + 0.5f);
    snprintf(lineBuf, sizeof(lineBuf), "IN: --.-C %2d%% ----hPa", humidity);
  } else {
    snprintf(lineBuf, sizeof(lineBuf), "IN: --.-C --%% ----hPa");
  }
  lcdPrintCentered(3, lineBuf);

  LCD_DUMP();
}

// ============================================================================
// AIR QUALITY SCREEN (20x4)
// ============================================================================

static const __FlashStringHelper* airHeaderFor(uint16_t pm25, uint16_t eco2, uint8_t aqi) {
  // Priorytety:
  // - Najpierw stany alarmowe (Poziom 5, potem 4)
  // - Potem najlepsze poziomy (1 -> 2 -> 3)
  //   bo kryteria są zagnieżdżone (IDEALNE ⊂ DOBRE ⊂ SREDNIE).

  // Poziom 5: SMOG / ZLE
  if (pm25 >= 50 || eco2 >= 2000 || aqi == 5) {
    if (pm25 >= 50) return F("! UWAGA: SMOG !");
    return F("! ZLE POWIETRZE !");
  }

  // Poziom 4: PRZEWIETRZ!
  if ((eco2 >= 1500 || aqi >= 4) && pm25 < 50) {
    return F("! PRZEWIETRZ !");
  }

  // Poziom 1: IDEALNE
  if (pm25 < 15 && eco2 < 800 && aqi == 1) {
    return F("POWIETRZE: IDEALNE");
  }

  // Poziom 2: DOBRE
  if (pm25 < 25 && eco2 < 1000 && aqi <= 2) {
    return F("POWIETRZE: DOBRE");
  }

  // Poziom 3: SREDNIE
  if (pm25 < 50 && eco2 < 1500 && aqi <= 3) {
    return F("POWIETRZE: SREDNIE");
  }

  // Jeśli nie wpasowuje się idealnie w powyższe progi
  return F("POWIETRZE: ---");
}

static void padRightTo20(char* line) {
  const int len = (int)strlen(line);
  if (len >= SCREEN_WIDTH) {
    line[SCREEN_WIDTH] = '\0';
    return;
  }
  for (int i = len; i < SCREEN_WIDTH; ++i) line[i] = ' ';
  line[SCREEN_WIDTH] = '\0';
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
  const __FlashStringHelper* header = F("POWIETRZE: BRAK DANYCH");
  if (haveAny) {
    header = airHeaderFor(pm25, eco2, aqi);
  }
  lcdPrintCentered(0, header);

  // Row 1: temperature + humidity.
  {
    char line[21];
    // Temperature comes from BMP280; humidity stays with AHT21, with DHT as a fallback only if AHT is unavailable.
    if (BMP280Screen::runtimeData.hasTemperature && ensClimateValid) {
      const int hum = (int)(ENS160AHT21Screen::runtimeData.humidityPct + 0.5f);
      snprintf(line, sizeof(line), " %5.1f\xDF" "C |  %3d%%    ", BMP280Screen::runtimeData.temperatureC, hum);
    } else if (BMP280Screen::runtimeData.hasTemperature && dhtReady) {
      const int hum = (int)(dhtHumidity + 0.5f);
      snprintf(line, sizeof(line), " %5.1f\xDF" "C |  %3d%%    ", BMP280Screen::runtimeData.temperatureC, hum);
    } else if (BMP280Screen::runtimeData.hasTemperature) {
      snprintf(line, sizeof(line), " %5.1f\xDF" "C |   --%%    ", BMP280Screen::runtimeData.temperatureC);
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
      int rem = SCREEN_WIDTH - left;
      int rlen = (int)strlen(right);
      int pad = rem - rlen;
      if (pad < 0) pad = 0;
      // append pad spaces then right text
      int pos = left;
      for (int i = 0; i < pad && pos < SCREEN_WIDTH; ++i) line[pos++] = ' ';
      for (int i = 0; i < rlen && pos < SCREEN_WIDTH; ++i) line[pos++] = right[i];
      for (; pos < SCREEN_WIDTH; ++pos) line[pos] = ' ';
      line[SCREEN_WIDTH] = '\0';
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

void drawIndoorWeatherScreen() {
  LCD_CLEAR();

  lcdPrintCentered(0, F("WNETRZE"));

  char line[21];
  const bool ensClimateValid = ENS160AHT21Screen::runtimeData.hasClimateSample;
  const uint8_t phase = (uint8_t)((millis() / 3500UL) % 4UL);

  if (ensClimateValid) {
    const float temp = ENS160AHT21Screen::runtimeData.temperatureC;
    const int hum = (int)(ENS160AHT21Screen::runtimeData.humidityPct + 0.5f);
    snprintf(line, sizeof(line), " T:%5.1f C H:%3d%% ", temp, hum);
  } else if (dhtReady) {
    const int hum = (int)(dhtHumidity + 0.5f);
    snprintf(line, sizeof(line), " T:%5.1f C H:%3d%% ", dhtTemperature, hum);
  } else {
    snprintf(line, sizeof(line), " T:  --.- C H: --%% ");
  }
  padRightTo20(line);
  LCD_SET(0, 1);
  LCD_PRINT(line);

  if (ensClimateValid) {
    snprintf(line, sizeof(line), " ENS:%s", ENS160AHT21Screen::runtimeData.statusText);
  } else if (dhtReady) {
    snprintf(line, sizeof(line), " DHT:READY");
  } else {
    snprintf(line, sizeof(line), " DHT:WAITING");
  }
  padRightTo20(line);
  LCD_SET(0, 2);
  LCD_PRINT(line);

  switch (phase) {
    case 0:
      snprintf(line, sizeof(line), " PULS: < o   >");
      break;
    case 1:
      snprintf(line, sizeof(line), " PULS: <  o  >");
      break;
    case 2:
      snprintf(line, sizeof(line), " PULS: <   o >");
      break;
    default:
      snprintf(line, sizeof(line), " PULS: <    o>");
      break;
  }
  padRightTo20(line);
  LCD_SET(0, 3);
  LCD_PRINT(line);

  LCD_DUMP();
}

void drawExtremeEnvironmentScreen() {
  LCD_CLEAR();

  lcdPrintCentered(0, F("EXTREME DASH"));

  char line[21];
  const bool ensGasValid = ENS160AHT21Screen::runtimeData.hasGasSample;
  const bool ensClimateValid = ENS160AHT21Screen::runtimeData.hasClimateSample;
  const bool bmpValid = BMP280Screen::runtimeData.hasSample;
  const bool pmValid = pms5003Enabled && PMS5003Sensor::getLastUpdateTime() != 0;

  if (ensGasValid) {
    snprintf(line, sizeof(line), "AQI:%u TVOC:%u", (unsigned)ENS160AHT21Screen::runtimeData.aqi, (unsigned)ENS160AHT21Screen::runtimeData.tvoc);
  } else {
    snprintf(line, sizeof(line), "AQI:-- TVOC:--");
  }
  padRightTo20(line);
  LCD_SET(0, 1);
  LCD_PRINT(line);

  if (bmpValid) {
    snprintf(line, sizeof(line), "CO2:%u PRS:%4.1f", (unsigned)(ensGasValid ? ENS160AHT21Screen::runtimeData.eco2 : 0), BMP280Screen::runtimeData.pressureHpa);
  } else if (ensGasValid) {
    snprintf(line, sizeof(line), "CO2:%u PRS:--.-", (unsigned)ENS160AHT21Screen::runtimeData.eco2);
  } else {
    snprintf(line, sizeof(line), "CO2:-- PRS:--.-");
  }
  padRightTo20(line);
  LCD_SET(0, 2);
  LCD_PRINT(line);

  if (pmValid) {
    snprintf(line, sizeof(line), "PM2.5:%u W:%s B:%s", (unsigned)pms5003_PM2_5_ATM, ModeManager::isWifiOn() ? "ON" : "OFF", ModeManager::isBtOn() ? "ON" : "OFF");
  } else {
    snprintf(line, sizeof(line), "PM2.5:-- W:%s B:%s", ModeManager::isWifiOn() ? "ON" : "OFF", ModeManager::isBtOn() ? "ON" : "OFF");
  }
  padRightTo20(line);
  LCD_SET(0, 3);
  LCD_PRINT(line);

  LCD_DUMP();
}

// --- Ekran menu ---
void drawMenu() {
  LCD_CLEAR();
  const int first = (menuIndex / SCREEN_HEIGHT) * SCREEN_HEIGHT;

  for (int i = 0; i < SCREEN_HEIGHT; i++) {
    const int item = first + i;
    if (item >= menuCount) break;

    LCD_SET(0, i);
    LCD_PRINT(item == menuIndex ? F("> ") : F("  "));

    if (item == 12) {
      if (radioMode == WIFI_ONLY) {
        LCD_PRINT(F("BLUETOOTH MODE"));
      } else {
        LCD_PRINT(F("WIFI MODE     "));
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

  lcdPrintCentered(0, F("USTAW CZAS"));
  lcdPrintCentered(1, F(ALIGN_CENTER));

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

  lcdPrintCentered(2, tbuf);
  clearRow(3);
  LCD_DUMP();
}

// --- Ekran budzika ---
void drawAlarm() {
  LCD_CLEAR();

  lcdPrintCentered(0, F("USTAW BUDZIK"));
  lcdPrintCentered(1, F(ALIGN_CENTER));

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

  lcdPrintCentered(2, abuf);
  clearRow(3);

  LCD_DUMP();
}

// --- Ekran Minutnika (Timer) ---
void drawTimer() {
  LCD_CLEAR();

  lcdPrintCentered(0, F("MINUTNIK"));
  lcdPrintCentered(1, F(ALIGN_CENTER));

  // Row 2: editable/set time line with left marker.
  char tbuf[21];
  if (timerRunning) {
    unsigned long nowMs = millis();
    unsigned long elapsed = (nowMs >= timerStartMillis) ? (nowMs - timerStartMillis) : 0;
    long remainingMs = (long)timerDurationMs - (long)elapsed;
    if (remainingMs < 0) remainingMs = 0;
    int rh = (int)(remainingMs / 3600000L);
    int rm = (int)((remainingMs % 3600000L) / 60000L);
    int rs = (int)((remainingMs % 60000L) / 1000L);
    snprintf(tbuf, sizeof(tbuf), "CZAS: %02d:%02d:%02d", rh, rm, rs);
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
      snprintf(tbuf, sizeof(tbuf), "CZAS: %s:%s:%s", hh, mm, ss);
    }
  }

  LCD_SET(0, 2);
  if (!timerRunning && editState == EDIT_DONE && timerUiCursor == 0) {
    LCD_PRINT(F("> "));
  } else {
    LCD_PRINT(F("  "));
  }
  LCD_PRINT(tbuf);
  int used = 2 + (int)strlen(tbuf);
  for (int i = used; i < SCREEN_WIDTH; ++i) LCD_PRINT(F(" "));

  // Row 3: quick presets.
  LCD_SET(0, 3);
  if (!timerRunning && editState == EDIT_DONE && timerUiCursor == 1) {
    LCD_PRINT(F("> "));
  } else {
    LCD_PRINT(F("  "));
  }

  const char* p0 = (timerPresetIndex == 0) ? ">[2m]<"  : "[2m]";
  const char* p1 = (timerPresetIndex == 1) ? ">[15m]<" : "[15m]";
  const char* p2 = (timerPresetIndex == 2) ? ">[45m]<" : "[45m]";
  char pbuf[32];
  snprintf(pbuf, sizeof(pbuf), "%s %s %s", p0, p1, p2);
  LCD_PRINT(pbuf);
  int pUsed = 2 + (int)strlen(pbuf);
  for (int i = pUsed; i < SCREEN_WIDTH; ++i) LCD_PRINT(F(" "));
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

  lcdPrintCentered(0, F("STOPER"));
  lcdPrintCentered(1, F(ALIGN_CENTER));

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
    lcdPrintCentered(2, buf);
    clearRow(3);

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
  lcdPrintCentered(2, buf);
  clearRow(3);

  updateSevenSegStoper(m, s, cs);
  LCD_DUMP();
}

// --- Ekran debug STM32 ---
void drawDebugSTM32() {
  LCD_CLEAR();
  LCD_SET(2, 0);
  LCD_PRINT(F("DEBUG STM32"));
  LCD_SET(0, 1);
  LCD_PRINT(F("BPM: "));
  LCD_PRINT(displayedBPM);
  LCD_SET(0, 2);
  LCD_PRINT(F("SPO2: "));
  LCD_PRINT(displayedSPO2);
  LCD_PRINT(F("%"));
  LCD_SET(0, 3);
  if (stm32Connected) {
    LCD_PRINT(F("Status: OK"));
  } else {
    LCD_PRINT(F("Status: OFFLINE"));
  }
  LCD_DUMP();
}

// --- Pomocnicza do rysowania czasu ---
void printTime(bool edit) {
  printVal(hours, edit && editState == EDIT_HOURS);
  LCD_PRINT(F(":"));
  printVal(minutes, edit && editState == EDIT_MINUTES);
  LCD_PRINT(F(":"));
  printVal(seconds, edit && editState == EDIT_SECONDS);
}

// --- Pomocnicza do printTime ---
void printVal(int v, bool sel) {
  if (sel) LCD_PRINT(F("["));
  if (v < 10) LCD_PRINT(F("0"));
  LCD_PRINT(v);
  if (sel) LCD_PRINT(F("]"));
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

static bool isBmp280State(AppState state) {
  return state == STATE_BMP280 ||
         state == STATE_BMP280_TEMP ||
         state == STATE_BMP280_PRESSURE ||
         state == STATE_BMP280_STATUS ||
         state == STATE_BMP280_ALTITUDE;
}

static void printEnsFloatOrDash(bool available, float value, uint8_t width = 4, uint8_t precision = 1) {
  if (!available) {
    LCD_PRINT(F("--"));
    return;
  }

  char buffer[12];
  dtostrf(value, width, precision, buffer);
  LCD_PRINT(buffer);
}

static void printEnsAgeSeconds(uint32_t lastUpdateMs) {
  if (lastUpdateMs == 0) {
    LCD_PRINT(F("--s"));
    return;
  }

  LCD_PRINT((millis() - lastUpdateMs) / 1000UL);
  LCD_PRINT(F("s"));
}

static void printBmp280FloatOrDash(bool available, float value, uint8_t width = 5, uint8_t precision = 1) {
  if (!available) {
    LCD_PRINT(F("--"));
    return;
  }

  char buffer[12];
  dtostrf(value, width, precision, buffer);
  LCD_PRINT(buffer);
}

static void printBmp280AgeSeconds(uint32_t lastSampleMs) {
  if (lastSampleMs == 0) {
    LCD_PRINT(F("--s"));
    return;
  }

  LCD_PRINT((millis() - lastSampleMs) / 1000UL);
  LCD_PRINT(F("s"));
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

struct Bmp280UiHistory {
  bool hasTemp = false;
  bool hasPressure = false;
  bool hasAltitude = false;
  float minTemp = 0.0f;
  float maxTemp = 0.0f;
  float minPressure = 0.0f;
  float maxPressure = 0.0f;
  float minAltitude = 0.0f;
  float maxAltitude = 0.0f;
  uint32_t lastProcessedRevision = 0;
};

static Bmp280UiHistory s_bmp280UiHistory;

static void updateBmp280UiHistory(const BMP280Screen::RuntimeData& data);
static void printBmp280MenuValue(int itemIndex, const BMP280Screen::RuntimeData& data);

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
      LCD_PRINT(F("AQI: "));
      if (data.hasGasSample) {
        LCD_PRINT(data.aqi);
      } else {
        LCD_PRINT(F("--"));
      }
      break;
    case 1:
      LCD_PRINT(F("TVOC: "));
      if (data.hasGasSample) {
        LCD_PRINT(data.tvoc);
      } else {
        LCD_PRINT(F("--"));
      }
      break;
    case 2:
      LCD_PRINT(F("eCO2: "));
      if (data.hasGasSample) {
        LCD_PRINT(data.eco2);
      } else {
        LCD_PRINT(F("--"));
      }
      break;
    case 3:
      LCD_PRINT(F("Temp: "));
      printEnsFloatOrDash(data.hasClimateSample, data.temperatureC, 4, 1);
      if (data.hasClimateSample) {
        LCD_PRINT(F(" C"));
      }
      break;
    case 4:
      LCD_PRINT(F("Hum: "));
      printEnsFloatOrDash(data.hasClimateSample, data.humidityPct, 3, 0);
      if (data.hasClimateSample) {
        LCD_PRINT(F(" %"));
      }
      break;
    case 5:
      LCD_PRINT(F("Status: "));
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

  if (isBmp280State(appState)) {
    static uint32_t lastBmp280Seen = 0;
    const uint32_t revision = BMP280Screen::runtimeData.sampleRevision;
    if (!BMP280Screen::screenDirty && revision == lastBmp280Seen) return;
    lastBmp280Seen = revision;
    BMP280Screen::screenDirty = false;
  }

  LCD_CLEAR();
  const AppStats stats = statsManager.getStats();
  updateEns160UiHistory(ENS160AHT21Screen::runtimeData);
  updateBmp280UiHistory(BMP280Screen::runtimeData);

  switch (appState) {
  // === 1. MENU STATYSTYK (LISTA Z LICZBAMI) ===
  case STATE_STATS: {
    LCD_SET(2, 0);
    LCD_PRINT(F("MENU STATYSTYK"));

    const int first = (statsMenuIndex / STATS_ITEMS_PER_PAGE) * STATS_ITEMS_PER_PAGE;

    for (int row = 0; row < STATS_ITEMS_PER_PAGE; row++) {
      const int i = first + row;
      if (i >= statsMenuCount) break;

      LCD_SET(0, row + 1);
      LCD_PRINT(i == statsMenuIndex ? F("> ") : F("  "));

      switch (i) {
        case 0:
          LCD_PRINT(F("Kliki "));
          LCD_PRINT(stats.totalClicks);
          break;
        case 1:
          LCD_PRINT(F("Kroki "));
          LCD_PRINT(statsManager.getTotalSteps());
          break;
        case 2:
          LCD_PRINT(F("Temp min/max"));
          break;
        case 3:
          LCD_PRINT(F("Wilg min/max"));
          break;
        case 4:
          LCD_PRINT(F("Zasoby"));
          break;
        case 5:
          LCD_PRINT(F("Wyjscie"));
          break;
      }
    }
    break;
  }
  // === 1b. MENU PMS5003 (LISTA Z WYBOREM) ===
  case STATE_PMS5003: {
    LCD_SET(2, 0);
    LCD_PRINT(F("MENU PMS5003"));

    const int first = (pms5003MenuIndex / 3) * 3;

    for (int row = 0; row < 3; row++) {
      const int i = first + row;
      if (i >= pms5003MenuCount) break;

      LCD_SET(0, row + 1);
      LCD_PRINT(i == pms5003MenuIndex ? F("> ") : F("  "));
      LCD_PRINT(pms5003MenuItems[i]);
    }
    break;
  }
  case STATE_ENS160_AHT21: {
    LCD_SET(1, 0);
    LCD_PRINT(F("AHT21 + ENS160"));

    const int first = (ens160MenuIndex / 3) * 3;
    for (int row = 0; row < 3; row++) {
      const int i = first + row;
      if (i >= ens160MenuCount) break;

      LCD_SET(0, row + 1);
      LCD_PRINT(i == ens160MenuIndex ? F("> ") : F("  "));
      printEnsMenuValue(i, ENS160AHT21Screen::runtimeData);
    }
    break;
  }
  case STATE_ENS160_AHT21_GAS_AQI: {
    const ENS160AHT21Screen::RuntimeData& data = ENS160AHT21Screen::runtimeData;

    LCD_SET(0, 0);
    LCD_PRINT(F("AQI"));
    LCD_SET(0, 1);
    LCD_PRINT(F("Biezaca: "));
    if (data.hasGasSample) {
      LCD_PRINT(data.aqi);
    } else {
      LCD_PRINT(F("--"));
    }
    LCD_SET(0, 2);
    if (s_ens160UiHistory.hasAqi) {
      LCD_PRINT(F("Min:"));
      LCD_PRINT(s_ens160UiHistory.minAqi);
      LCD_PRINT(F(" Max:"));
      LCD_PRINT(s_ens160UiHistory.maxAqi);
    } else {
      LCD_PRINT(F("Min:-- Max:--"));
    }
    LCD_SET(0, 3);
    LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }
  case STATE_ENS160_AHT21_GAS_TVOC: {
    const ENS160AHT21Screen::RuntimeData& data = ENS160AHT21Screen::runtimeData;

    LCD_SET(0, 0);
    LCD_PRINT(F("TVOC"));
    LCD_SET(0, 1);
    LCD_PRINT(F("Biezaca: "));
    if (data.hasGasSample) {
      LCD_PRINT(data.tvoc);
      LCD_PRINT(F(" ppb"));
    } else {
      LCD_PRINT(F("--"));
    }
    LCD_SET(0, 2);
    if (s_ens160UiHistory.hasTvoc) {
      LCD_PRINT(F("Min:"));
      LCD_PRINT(s_ens160UiHistory.minTvoc);
      LCD_PRINT(F(" Max:"));
      LCD_PRINT(s_ens160UiHistory.maxTvoc);
    } else {
      LCD_PRINT(F("Min:-- Max:--"));
    }
    LCD_SET(0, 3);
    LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }
  case STATE_ENS160_AHT21_GAS_ECO2: {
    const ENS160AHT21Screen::RuntimeData& data = ENS160AHT21Screen::runtimeData;

    LCD_SET(0, 0);
    LCD_PRINT(F("eCO2"));
    LCD_SET(0, 1);
    LCD_PRINT(F("Biezaca: "));
    if (data.hasGasSample) {
      LCD_PRINT(data.eco2);
      LCD_PRINT(F(" ppm"));
    } else {
      LCD_PRINT(F("--"));
    }
    LCD_SET(0, 2);
    if (s_ens160UiHistory.hasEco2) {
      LCD_PRINT(F("Min:"));
      LCD_PRINT(s_ens160UiHistory.minEco2);
      LCD_PRINT(F(" Max:"));
      LCD_PRINT(s_ens160UiHistory.maxEco2);
    } else {
      LCD_PRINT(F("Min:-- Max:--"));
    }
    LCD_SET(0, 3);
    LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }
  case STATE_ENS160_AHT21_CLIMATE_TEMP: {
    const ENS160AHT21Screen::RuntimeData& data = ENS160AHT21Screen::runtimeData;

    LCD_SET(0, 0);
    LCD_PRINT(F("Temperatura"));
    LCD_SET(0, 1);
    LCD_PRINT(F("Biezaca: "));
    printEnsFloatOrDash(data.hasClimateSample, data.temperatureC);
    if (data.hasClimateSample) {
      LCD_PRINT(F(" C"));
    }
    LCD_SET(0, 2);
    if (s_ens160UiHistory.hasTemp) {
      LCD_PRINT(F("Min:"));
      printEnsFloatOrDash(true, s_ens160UiHistory.minTemp, 4, 1);
      LCD_PRINT(F(" Max:"));
      printEnsFloatOrDash(true, s_ens160UiHistory.maxTemp, 4, 1);
    } else {
      LCD_PRINT(F("Min:-- Max:--"));
    }
    LCD_SET(0, 3);
    LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }
  case STATE_ENS160_AHT21_CLIMATE_HUM: {
    const ENS160AHT21Screen::RuntimeData& data = ENS160AHT21Screen::runtimeData;

    LCD_SET(0, 0);
    LCD_PRINT(F("Wilgotnosc"));
    LCD_SET(0, 1);
    LCD_PRINT(F("Biezaca: "));
    printEnsFloatOrDash(data.hasClimateSample, data.humidityPct);
    if (data.hasClimateSample) {
      LCD_PRINT(F(" %"));
    }
    LCD_SET(0, 2);
    if (s_ens160UiHistory.hasHum) {
      LCD_PRINT(F("Min:"));
      printEnsFloatOrDash(true, s_ens160UiHistory.minHum, 3, 0);
      LCD_PRINT(F(" Max:"));
      printEnsFloatOrDash(true, s_ens160UiHistory.maxHum, 3, 0);
    } else {
      LCD_PRINT(F("Min:-- Max:--"));
    }
    LCD_SET(0, 3);
    LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }
  case STATE_ENS160_AHT21_STATUS: {
    const ENS160AHT21Screen::RuntimeData& data = ENS160AHT21Screen::runtimeData;

    LCD_SET(0, 0);
    LCD_PRINT(F("Status"));
    LCD_SET(0, 1);
    LCD_PRINT(F("Status: "));
    LCD_PRINT(data.statusText);
    LCD_SET(0, 2);
    LCD_PRINT(F("Gaz:"));
    LCD_PRINT(data.hasGasSample ? F("OK") : F("--"));
    LCD_PRINT(F(" Klim:"));
    LCD_PRINT(data.hasClimateSample ? F("OK") : F("--"));
    LCD_SET(0, 3);
    LCD_PRINT(F("Ostatnia: "));
    printEnsAgeSeconds(data.lastUpdateMs);
    break;
  }
  case STATE_BMP280: {
    LCD_SET(2, 0);
    LCD_PRINT(F("BMP280"));

    const int first = (bmp280MenuIndex / 3) * 3;
    for (int row = 0; row < 3; row++) {
      const int i = first + row;
      if (i >= bmp280MenuCount) break;

      LCD_SET(0, row + 1);
      LCD_PRINT(i == bmp280MenuIndex ? F("> ") : F("  "));
      printBmp280MenuValue(i, BMP280Screen::runtimeData);
    }
    break;
  }
  case STATE_BMP280_TEMP: {
    const BMP280Screen::RuntimeData& data = BMP280Screen::runtimeData;

    LCD_SET(0, 0);
    LCD_PRINT(F("Temperatura"));
    LCD_SET(0, 1);
    LCD_PRINT(F("Biezaca: "));
    printBmp280FloatOrDash(data.hasTemperature, data.temperatureC, 5, 1);
    if (data.hasTemperature) {
      LCD_PRINT(F(" C"));
    }
    LCD_SET(0, 2);
    if (s_bmp280UiHistory.hasTemp) {
      LCD_PRINT(F("Min:"));
      printBmp280FloatOrDash(true, s_bmp280UiHistory.minTemp, 5, 1);
      LCD_PRINT(F(" Max:"));
      printBmp280FloatOrDash(true, s_bmp280UiHistory.maxTemp, 5, 1);
    } else {
      LCD_PRINT(F("Min:-- Max:--"));
    }
    LCD_SET(0, 3);
    LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }
  case STATE_BMP280_PRESSURE: {
    const BMP280Screen::RuntimeData& data = BMP280Screen::runtimeData;

    LCD_SET(0, 0);
    LCD_PRINT(F("Cisnienie"));
    LCD_SET(0, 1);
    LCD_PRINT(F("Biezace: "));
    printBmp280FloatOrDash(data.hasPressure, data.pressureHpa, 5, 1);
    if (data.hasPressure) {
      LCD_PRINT(F(" hPa"));
    }
    LCD_SET(0, 2);
    if (s_bmp280UiHistory.hasPressure) {
      LCD_PRINT(F("Min:"));
      printBmp280FloatOrDash(true, s_bmp280UiHistory.minPressure, 5, 1);
      LCD_PRINT(F(" Max:"));
      printBmp280FloatOrDash(true, s_bmp280UiHistory.maxPressure, 5, 1);
    } else {
      LCD_PRINT(F("Min:-- Max:--"));
    }
    LCD_SET(0, 3);
    LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }
  case STATE_BMP280_STATUS: {
    const BMP280Screen::RuntimeData& data = BMP280Screen::runtimeData;

    LCD_SET(0, 0);
    LCD_PRINT(F("Status BMP280"));
    LCD_SET(0, 1);
    LCD_PRINT(F("Stan: "));
    LCD_PRINT(data.statusText);
    LCD_SET(0, 2);
    LCD_PRINT(F("Pomiar: "));
    LCD_PRINT(data.hasSample ? F("OK") : F("--"));
    LCD_PRINT(F(" Alt: "));
    LCD_PRINT(data.altitudeAvailable ? F("ON") : F("OFF"));
    LCD_SET(0, 3);
    LCD_PRINT(F("Ostatnia: "));
    printBmp280AgeSeconds(data.lastSampleMs);
    break;
  }
  case STATE_BMP280_ALTITUDE: {
    const BMP280Screen::RuntimeData& data = BMP280Screen::runtimeData;

    LCD_SET(0, 0);
    LCD_PRINT(F("Wysokosc"));
    LCD_SET(0, 1);
    LCD_PRINT(F("Biezaca: "));
    printBmp280FloatOrDash(data.hasAltitude, data.altitudeM, 5, 1);
    if (data.hasAltitude) {
      LCD_PRINT(F(" m"));
    }
    LCD_SET(0, 2);
    if (s_bmp280UiHistory.hasAltitude) {
      LCD_PRINT(F("Min:"));
      printBmp280FloatOrDash(true, s_bmp280UiHistory.minAltitude, 5, 1);
      LCD_PRINT(F(" Max:"));
      printBmp280FloatOrDash(true, s_bmp280UiHistory.maxAltitude, 5, 1);
    } else {
      LCD_PRINT(F("Min:-- Max:--"));
    }
    LCD_SET(0, 3);
    LCD_PRINT(F("Dostepne tylko z ref."));
    break;
  }
  // === 1c. MENU USTAWIEŃ (Settings) ===
  case STATE_SETTINGS: {
    LCD_SET(2, 0);
    LCD_PRINT(F("USTAWIENIA"));

    const int first = (settingsMenuIndex / 3) * 3;

    for (int row = 0; row < 3; row++) {
      const int i = first + row;
      if (i >= settingsMenuCount) break;

      LCD_SET(0, row + 1);
      LCD_PRINT(i == settingsMenuIndex ? F("> ") : F("  "));
      LCD_PRINT(settingsMenuItems[i]);
    }
    break;
  }
  // === 1d. USTAWIENIA PMS5003 (włącz/wyłącz) ===
  case STATE_SETTINGS_PMS5003: {
    lcdPrintCentered(0, F("PMS5003"));
    lcdPrintCentered(1, F(ALIGN_CENTER));

    char pbuf[21];
    const char* pstate = settingsPmsMenuIndex == 0 ? "ON" : "OFF";
    snprintf(pbuf, sizeof(pbuf), "SENSOR: <  %s  >", pstate);
    lcdPrintCentered(2, pbuf);
    clearRow(3);
    break;
  }
  // === 1e. USTAWIENIA BUZERA (włącz/wyłącz) ===
  case STATE_SETTINGS_BUZZER: {
    lcdPrintCentered(0, F("BUZZER"));
    lcdPrintCentered(1, F(ALIGN_CENTER));

    char bbuf[21];
    const char* bstate = settingsBuzzerMenuIndex == 0 ? "ON" : "OFF";
    snprintf(bbuf, sizeof(bbuf), "STAN:   <  %s  >", bstate);
    lcdPrintCentered(2, bbuf);
    clearRow(3);
    break;
  }
  // === 1f. USTAWIENIA MQTT (włącz/wyłącz) ===
  case STATE_SETTINGS_MQTT: {
    lcdPrintCentered(0, F("MQTT"));
    lcdPrintCentered(1, F(ALIGN_CENTER));

    char buf[21];
    const char* state = settingsMqttMenuIndex == 0 ? "ON" : "OFF";
    snprintf(buf, sizeof(buf), "BROKER: <  %s  >", state);
    lcdPrintCentered(2, buf);
    clearRow(3);
    break;
  }
  // === 1g. USTAWIENIE: ROTACJA EKRANU (1..10s, enkoder) ===
  case STATE_SETTINGS_ROTATION: {
    lcdPrintCentered(0, F("ROTACJA EKRANU"));
    lcdPrintCentered(1, F(ALIGN_CENTER));

    char valueBuf[32];
    snprintf(valueBuf, sizeof(valueBuf), "CZAS: < %2ds >", settingsRotationSec);
    lcdPrintCentered(2, valueBuf);
    clearRow(3);
    break;
  }
  // === 1g1. USTAWIENIA UI EKRAN (wizualny wybór profilu) ===
  case STATE_SETTINGS_UI_SCREEN: {
    LCD_SET(2, 0);
    LCD_PRINT(F("UI EKRAN"));

    for (int row = 0; row < settingsUiScreenCount; ++row) {
      LCD_SET(0, row + 1);
      if (row == settingsUiScreenIndex) {
        LCD_PRINT(F("> ["));
        LCD_PRINT(settingsUiScreenItems[row]);
        LCD_PRINT(F("]"));
      } else {
        LCD_PRINT(F("  "));
        LCD_PRINT(settingsUiScreenItems[row]);
      }
    }
    break;
  }
  // === 1x. Lista budzików ===
  case STATE_ALARMS_LIST: {
    lcdPrintCentered(0, F("BUDZIKI"));
    LCD_SET(0, 1);
    const int first = (alarmsMenuIndex / 3) * 3;
    for (int row = 0; row < 3; ++row) {
      int idx = first + row;
      LCD_SET(0, row + 1);
      if (idx > alarmsCount) break;
      if (idx == alarmsMenuIndex) LCD_PRINT(F("> ")); else LCD_PRINT(F("  "));
      if (idx < alarmsCount) {
        char buf[21];
        snprintf(buf, sizeof(buf), "%2d. %02d:%02d [%s]   ", idx+1, alarms[idx].hour, alarms[idx].minute, alarms[idx].enabled ? "ON" : "OFF");
        LCD_PRINT(buf);
      } else if (idx == alarmsCount) {
        LCD_PRINT(F("[+] DODAJ NOWY   "));
      } else {
        LCD_PRINT(F("                    "));
      }
    }
    break;
  }
  // === 1y. Edycja budzika (ergonomiczna: kursor po lewej, opcja USUN) ===
  case STATE_ALARM_EDIT: {
    char header[21];
    snprintf(header, sizeof(header), "EDYCJA BUDZIKA %d", selectedAlarmIndex + 1);
    lcdPrintCentered(0, header);

    lcdPrintCentered(1, F(ALIGN_CENTER));

    LCD_SET(0, 2);
    char tline[21];
    const int ah = alarms[selectedAlarmIndex].hour;
    const int am = alarms[selectedAlarmIndex].minute;
    if (editState == EDIT_HOURS) {
      LCD_PRINT(F("> "));
      snprintf(tline, sizeof(tline), "CZAS:  [%02d]:%02d   ", ah, am);
    } else if (editState == EDIT_MINUTES) {
      LCD_PRINT(F("> "));
      snprintf(tline, sizeof(tline), "CZAS:   %02d:[%02d]  ", ah, am);
    } else {
      if (alarmEditCursor == 0) LCD_PRINT(F("> ")); else LCD_PRINT(F("  "));
      snprintf(tline, sizeof(tline), "CZAS:   %02d:%02d    ", ah, am);
    }
    LCD_PRINT(tline);

    LCD_SET(0, 3);
    if (alarmEditCursor == 2) {
      LCD_PRINT(F("> USUN BUDZIK   "));
    } else {
      if (alarmEditCursor == 1) LCD_PRINT(F("> ")); else LCD_PRINT(F("  "));
      char sline[21];
      snprintf(sline, sizeof(sline), "STATUS: [ %s ]    ", alarms[selectedAlarmIndex].enabled ? "ON" : "OFF");
      LCD_PRINT(sline);
    }
    break;
  }
  
  // === 1h. USTAWIENIE: SYNCHRONIZACJA NTP (10..360 min, enkoder) ===
  case STATE_SETTINGS_SYNC: {
    lcdPrintCentered(0, F("SYNCHRONIZACJA"));
    lcdPrintCentered(1, F(ALIGN_CENTER));

    char valueBuf[32];
    snprintf(valueBuf, sizeof(valueBuf), "CZAS: < %3dmin >", settingsSyncMinutes);
    lcdPrintCentered(2, valueBuf);
    clearRow(3);
    break;
  }
  // === 2. WIDOK KLIKNIĘĆ ===
  case STATE_STATS_CLICKS: {
    LCD_SET(0, 0);
    LCD_PRINT(F("LICZNIK KLIKNIEC"));
    LCD_SET(0, 1);
    LCD_PRINT(F("Razem: "));
    LCD_PRINT(stats.totalClicks);
    LCD_SET(0, 3);
    LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }
  // === 3. WIDOK KROKÓW ===
  case STATE_STATS_STEPS: {
    LCD_SET(0, 0);
    LCD_PRINT(F("LICZNIK KROKOW"));
    LCD_SET(0, 1);
    LCD_PRINT(F("L: "));
    LCD_PRINT(stats.stepsLeft);
    LCD_SET(10, 1);
    LCD_PRINT(F("R: "));
    LCD_PRINT(stats.stepsRight);
    LCD_SET(0, 2);
    LCD_PRINT(F("Suma: "));
    LCD_PRINT(statsManager.getTotalSteps());
    LCD_SET(0, 3);
    LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }
  // === 4. WIDOK TEMPERATURY MIN/MAX ===
  case STATE_STATS_TEMP: {
    const EnvStats e = statsManager.getEnvStats();
    char buf[10];

    LCD_SET(0, 0);
    LCD_PRINT(F("TEMPERATURA"));

    LCD_SET(0, 1);
    LCD_PRINT(F("MIN: "));
    dtostrf(e.tempMin, 4, 1, buf);
    LCD_PRINT(buf);

    LCD_SET(0, 2);
    LCD_PRINT(F("MAX: "));
    dtostrf(e.tempMax, 4, 1, buf);
    LCD_PRINT(buf);

    LCD_SET(0, 3);
    LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }
  // === 5. WIDOK WILGOTNOŚCI MIN/MAX ===
  case STATE_STATS_HUM: {
    const EnvStats e = statsManager.getEnvStats();
    char buf[10];

    LCD_SET(0, 0);
    LCD_PRINT(F("WILGOTNOSC"));

    LCD_SET(0, 1);
    LCD_PRINT(F("MIN: "));
    dtostrf(e.humMin, 4, 1, buf);
    LCD_PRINT(buf);

    LCD_SET(0, 2);
    LCD_PRINT(F("MAX: "));
    dtostrf(e.humMax, 4, 1, buf);
    LCD_PRINT(buf);

    LCD_SET(0, 3);
    LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }
  // === 5b. MENU ZASOBÓW SYSTEMU ===
  case STATE_STATS_RESOURCES_MENU: {
    LCD_SET(1, 0);
    LCD_PRINT(F("ZASOBY SYSTEMU"));

    const int first = (resourcesMenuIndex / 3) * 3;

    for (int row = 0; row < 3; row++) {
      const int i = first + row;
      if (i >= resourcesMenuCount) break;

      LCD_SET(0, row + 1);
      LCD_PRINT(i == resourcesMenuIndex ? F("> ") : F("  "));
      LCD_PRINT(resourcesMenuItems[i]);
    }
    break;
  }
  // === 5c. WIDOK PAMIĘCI RAM ===
  case STATE_STATS_RESOURCES_RAM: {
    uint32_t ramMB = ramFreeBytes / (1024 * 1024);
    uint32_t ramKB = (ramFreeBytes % (1024 * 1024)) / 1024;

    LCD_SET(0, 0);
    LCD_PRINT(F("PAMIEC RAM"));

    LCD_SET(0, 1);
    LCD_PRINT(F("Free: "));
    LCD_PRINT(ramMB);
    LCD_PRINT(F("."));
    LCD_PRINT(ramKB);
    LCD_PRINT(F(" MB"));

    LCD_SET(0, 2);
    LCD_PRINT(F("Bytes: "));
    LCD_PRINT(ramFreeBytes);

    LCD_SET(0, 3);
    LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }
  // === 5d. WIDOK OBCIĄŻENIA CPU ===
  case STATE_STATS_RESOURCES_CPU: {
    LCD_SET(0, 0);
    LCD_PRINT(F("CPU LOAD"));

    LCD_SET(0, 1);
    LCD_PRINT(F("Calkowite: "));
    LCD_PRINT(cpuLoadPercent);
    LCD_PRINT(F("%"));

    LCD_SET(0, 2);
    LCD_PRINT(F("CORE0: "));
    LCD_PRINT(cpuCore0Percent);
    LCD_PRINT(F("%"));

    LCD_SET(0, 3);
    LCD_PRINT(F("CORE1: "));
    LCD_PRINT(cpuCore1Percent);
    LCD_PRINT(F("%"));
    break;
  }
  // === 5e. WIDOK PAMIĘCI FLASH ===
  case STATE_STATS_RESOURCES_FLASH: {
    uint32_t flashMB = flashFreeBytes / (1024 * 1024);
    uint32_t flashKB = (flashFreeBytes % (1024 * 1024)) / 1024;

    LCD_SET(0, 0);
    LCD_PRINT(F("PAMIEC FLASH"));

    LCD_SET(0, 1);
    LCD_PRINT(F("Free: "));
    LCD_PRINT(flashMB);
    LCD_PRINT(F("."));
    LCD_PRINT(flashKB);
    LCD_PRINT(F(" MB"));

    LCD_SET(0, 2);
    LCD_PRINT(F("Bytes: "));
    LCD_PRINT(flashFreeBytes);

    LCD_SET(0, 3);
    LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }
  // === 6. WIDOK PMS5003 TRYB FABRYCZNY CF=1 (BIEŻĄCE DANE Z WYBOREM) ===
  case STATE_PMS5003_CF1: {
    LCD_SET(0, 0);
    LCD_PRINT(F("PMS5003 CF=1"));
    
    // Wiersz 1: PM1.0 (z > jeśli wybrany)
    LCD_SET(0, 1);
    LCD_PRINT(pms5003CF1MenuIndex == 0 ? F(">") : F(" "));
    LCD_PRINT(F(" PM1.0: "));
    LCD_PRINT(pms5003_PM1_0_CF1 > 0 ? pms5003_PM1_0_CF1 : 0);
    LCD_PRINT(F(" \xE4g/m3"));
    
    // Wiersz 2: PM2.5
    LCD_SET(0, 2);
    LCD_PRINT(pms5003CF1MenuIndex == 1 ? F(">") : F(" "));
    LCD_PRINT(F(" PM2.5: "));
    LCD_PRINT(pms5003_PM2_5_CF1 > 0 ? pms5003_PM2_5_CF1 : 0);
    LCD_PRINT(F(" \xE4g/m3"));
    
    // Wiersz 3: PM10
    LCD_SET(0, 3);
    LCD_PRINT(pms5003CF1MenuIndex == 2 ? F(">") : F(" "));
    LCD_PRINT(F(" PM10:  "));
    LCD_PRINT(pms5003_PM10_CF1 > 0 ? pms5003_PM10_CF1 : 0);
    LCD_PRINT(F(" \xE4g/m3"));
    break;
  }
  // === 6a. WIDOK SZCZEGÓŁÓW PM1.0 TRYB CF=1 (MIN/MAX) ===
  case STATE_PMS5003_CF1_PM1: {
    LCD_SET(0, 0); LCD_PRINT(F("PM1.0 CF=1"));
    LCD_SET(0, 1); LCD_PRINT(F("Biezaca: ")); LCD_PRINT(pms5003_PM1_0_CF1); LCD_PRINT(F(" \xE4g/m3"));
    LCD_SET(0, 2); LCD_PRINT(F("Min:")); LCD_PRINT(pms5003_PM1_0_CF1_MIN < 9999 ? pms5003_PM1_0_CF1_MIN : 0);
    LCD_PRINT(F(" Max:")); LCD_PRINT(pms5003_PM1_0_CF1_MAX);
    LCD_SET(0, 3); LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }
  // === 6b. WIDOK SZCZEGÓŁÓW PM2.5 TRYB CF=1 (MIN/MAX) ===
  case STATE_PMS5003_CF1_PM25: {
    LCD_SET(0, 0); LCD_PRINT(F("PM2.5 CF=1"));
    LCD_SET(0, 1); LCD_PRINT(F("Biezaca: ")); LCD_PRINT(pms5003_PM2_5_CF1); LCD_PRINT(F(" \xE4g/m3"));
    LCD_SET(0, 2); LCD_PRINT(F("Min:")); LCD_PRINT(pms5003_PM2_5_CF1_MIN < 9999 ? pms5003_PM2_5_CF1_MIN : 0);
    LCD_PRINT(F(" Max:")); LCD_PRINT(pms5003_PM2_5_CF1_MAX);
    LCD_SET(0, 3); LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }
  // === 6c. WIDOK SZCZEGÓŁÓW PM10 TRYB CF=1 (MIN/MAX) ===
  case STATE_PMS5003_CF1_PM10: {
    LCD_SET(0, 0); LCD_PRINT(F("PM10 CF=1"));
    LCD_SET(0, 1); LCD_PRINT(F("Biezaca: ")); LCD_PRINT(pms5003_PM10_CF1); LCD_PRINT(F(" \xE4g/m3"));
    LCD_SET(0, 2); LCD_PRINT(F("Min:")); LCD_PRINT(pms5003_PM10_CF1_MIN < 9999 ? pms5003_PM10_CF1_MIN : 0);
    LCD_PRINT(F(" Max:")); LCD_PRINT(pms5003_PM10_CF1_MAX);
    LCD_SET(0, 3); LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }
  // === 7. WIDOK PMS5003 TRYB ATMOSFERYCZNY (BIEŻĄCE DANE Z WYBOREM) ===
  case STATE_PMS5003_ATM: {
    LCD_SET(0, 0);
    LCD_PRINT(F("PMS5003 ATM"));
    
    // Wiersz 1: PM1.0
    LCD_SET(0, 1);
    LCD_PRINT(pms5003ATMMenuIndex == 0 ? F(">") : F(" "));
    LCD_PRINT(F(" PM1.0: "));
    LCD_PRINT(pms5003_PM1_0_ATM > 0 ? pms5003_PM1_0_ATM : 0);
    LCD_PRINT(F(" \xE4g/m3"));
    
    // Wiersz 2: PM2.5
    LCD_SET(0, 2);
    LCD_PRINT(pms5003ATMMenuIndex == 1 ? F(">") : F(" "));
    LCD_PRINT(F(" PM2.5: "));
    LCD_PRINT(pms5003_PM2_5_ATM > 0 ? pms5003_PM2_5_ATM : 0);
    LCD_PRINT(F(" \xE4g/m3"));
    
    // Wiersz 3: PM10
    LCD_SET(0, 3);
    LCD_PRINT(pms5003ATMMenuIndex == 2 ? F(">") : F(" "));
    LCD_PRINT(F(" PM10:  "));
    LCD_PRINT(pms5003_PM10_ATM > 0 ? pms5003_PM10_ATM : 0);
    LCD_PRINT(F(" \xE4g/m3"));
    break;
  }
  // === 7a. WIDOK SZCZEGÓŁÓW PM1.0 TRYB ATM (MIN/MAX) ===
  case STATE_PMS5003_ATM_PM1: {
    LCD_SET(0, 0); LCD_PRINT(F("PM1.0 ATM"));
    LCD_SET(0, 1); LCD_PRINT(F("Biezaca: ")); LCD_PRINT(pms5003_PM1_0_ATM); LCD_PRINT(F(" \xE4g/m3"));
    LCD_SET(0, 2); LCD_PRINT(F("Min:")); LCD_PRINT(pms5003_PM1_0_ATM_MIN < 9999 ? pms5003_PM1_0_ATM_MIN : 0);
    LCD_PRINT(F(" Max:")); LCD_PRINT(pms5003_PM1_0_ATM_MAX);
    LCD_SET(0, 3); LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }
  // === 7b. WIDOK SZCZEGÓŁÓW PM2.5 TRYB ATM (MIN/MAX) ===
  case STATE_PMS5003_ATM_PM25: {
    LCD_SET(0, 0); LCD_PRINT(F("PM2.5 ATM"));
    LCD_SET(0, 1); LCD_PRINT(F("Biezaca: ")); LCD_PRINT(pms5003_PM2_5_ATM); LCD_PRINT(F(" \xE4g/m3"));
    LCD_SET(0, 2); LCD_PRINT(F("Min:")); LCD_PRINT(pms5003_PM2_5_ATM_MIN < 9999 ? pms5003_PM2_5_ATM_MIN : 0);
    LCD_PRINT(F(" Max:")); LCD_PRINT(pms5003_PM2_5_ATM_MAX);
    LCD_SET(0, 3); LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }
  // === 7c. WIDOK SZCZEGÓŁÓW PM10 TRYB ATM (MIN/MAX) ===
  case STATE_PMS5003_ATM_PM10: {
    LCD_SET(0, 0); LCD_PRINT(F("PM10 ATM"));
    LCD_SET(0, 1); LCD_PRINT(F("Biezaca: ")); LCD_PRINT(pms5003_PM10_ATM); LCD_PRINT(F(" \xE4g/m3"));
    LCD_SET(0, 2); LCD_PRINT(F("Min:")); LCD_PRINT(pms5003_PM10_ATM_MIN < 9999 ? pms5003_PM10_ATM_MIN : 0);
    LCD_PRINT(F(" Max:")); LCD_PRINT(pms5003_PM10_ATM_MAX);
    LCD_SET(0, 3); LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }

  // ========== Particles ==========
  case STATE_PMS5003_PARTICLES: {
    LCD_SET(0, 0);
    LCD_PRINT(F("Liczba Czastek"));
    
    // Pagination: show 3 items per page
    const int itemsPerPage = 3;
    const int first = (pms5003ParticlesMenuIndex / itemsPerPage) * itemsPerPage;
    
    const char* const particleLabels[] = {"0.3um", "0.5um", "1.0um", "2.5um", "5.0um", "10um"};
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
      LCD_PRINT(i == pms5003ParticlesMenuIndex ? F(">") : F(" "));
      LCD_PRINT(F(" "));
      LCD_PRINT(particleLabels[i]);
      LCD_PRINT(F(": "));
      LCD_PRINT(particleValues[i]);
    }
    break;
  }

  case STATE_PMS5003_PARTICLES_0_3: {
    LCD_SET(0, 0); LCD_PRINT(F("0.3um"));
    LCD_SET(0, 1); LCD_PRINT(F("Biezaca: ")); LCD_PRINT(pms5003_particleCount_0_3);
    LCD_SET(0, 2); LCD_PRINT(F("Min:")); LCD_PRINT(pms5003_particleCount_0_3_MIN < 9999 ? pms5003_particleCount_0_3_MIN : 0);
    LCD_PRINT(F(" Max:")); LCD_PRINT(pms5003_particleCount_0_3_MAX);
    LCD_SET(0, 3); LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }

  case STATE_PMS5003_PARTICLES_0_5: {
    LCD_SET(0, 0); LCD_PRINT(F("0.5um"));
    LCD_SET(0, 1); LCD_PRINT(F("Biezaca: ")); LCD_PRINT(pms5003_particleCount_0_5);
    LCD_SET(0, 2); LCD_PRINT(F("Min:")); LCD_PRINT(pms5003_particleCount_0_5_MIN < 9999 ? pms5003_particleCount_0_5_MIN : 0);
    LCD_PRINT(F(" Max:")); LCD_PRINT(pms5003_particleCount_0_5_MAX);
    LCD_SET(0, 3); LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }

  case STATE_PMS5003_PARTICLES_1_0: {
    LCD_SET(0, 0); LCD_PRINT(F("1.0um"));
    LCD_SET(0, 1); LCD_PRINT(F("Biezaca: ")); LCD_PRINT(pms5003_particleCount_1_0);
    LCD_SET(0, 2); LCD_PRINT(F("Min:")); LCD_PRINT(pms5003_particleCount_1_0_MIN < 9999 ? pms5003_particleCount_1_0_MIN : 0);
    LCD_PRINT(F(" Max:")); LCD_PRINT(pms5003_particleCount_1_0_MAX);
    LCD_SET(0, 3); LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }

  case STATE_PMS5003_PARTICLES_2_5: {
    LCD_SET(0, 0); LCD_PRINT(F("2.5um"));
    LCD_SET(0, 1); LCD_PRINT(F("Biezaca: ")); LCD_PRINT(pms5003_particleCount_2_5);
    LCD_SET(0, 2); LCD_PRINT(F("Min:")); LCD_PRINT(pms5003_particleCount_2_5_MIN < 9999 ? pms5003_particleCount_2_5_MIN : 0);
    LCD_PRINT(F(" Max:")); LCD_PRINT(pms5003_particleCount_2_5_MAX);
    LCD_SET(0, 3); LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }

  case STATE_PMS5003_PARTICLES_5_0: {
    LCD_SET(0, 0); LCD_PRINT(F("5.0um"));
    LCD_SET(0, 1); LCD_PRINT(F("Biezaca: ")); LCD_PRINT(pms5003_particleCount_5_0);
    LCD_SET(0, 2); LCD_PRINT(F("Min:")); LCD_PRINT(pms5003_particleCount_5_0_MIN < 9999 ? pms5003_particleCount_5_0_MIN : 0);
    LCD_PRINT(F(" Max:")); LCD_PRINT(pms5003_particleCount_5_0_MAX);
    LCD_SET(0, 3); LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }

  case STATE_PMS5003_PARTICLES_10_0: {
    LCD_SET(0, 0); LCD_PRINT(F("10um"));
    LCD_SET(0, 1); LCD_PRINT(F("Biezaca: ")); LCD_PRINT(pms5003_particleCount_10_0);
    LCD_SET(0, 2); LCD_PRINT(F("Min:")); LCD_PRINT(pms5003_particleCount_10_0_MIN < 9999 ? pms5003_particleCount_10_0_MIN : 0);
    LCD_PRINT(F(" Max:")); LCD_PRINT(pms5003_particleCount_10_0_MAX);
    LCD_SET(0, 3); LCD_PRINT(F("Dlugi -> Powrot"));
    break;
  }

  // ========== Telemetria ==========
  case STATE_PMS5003_TELEMETRY: {
    LCD_SET(0, 0);
    LCD_PRINT(F("Telemetria"));
    
    LCD_SET(0, 1);
    LCD_PRINT(F("Bledy: "));
    LCD_PRINT(pms5003_errorCount_current);
    LCD_PRINT(F("/"));
    LCD_PRINT(pms5003_errorCount_total);
    
    LCD_SET(0, 2);
    LCD_PRINT(F("Bajty: "));
    if (pms5003_bytesReceived < 10) LCD_PRINT(F("0"));
    LCD_PRINT(pms5003_bytesReceived);
    
    LCD_SET(0, 3);
    LCD_PRINT(F("Latencja: "));
    if (pms5003_latency_ms < 10) LCD_PRINT(F("0"));
    if (pms5003_latency_ms < 100) LCD_PRINT(F("0"));
    LCD_PRINT(pms5003_latency_ms);
    LCD_PRINT(F(" ms"));
    break;
  }
  default:
    break;
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
        LCD_PRINT(F("Temperatura"));
        LCD_SET(0, 1);
        LCD_PRINT(F("Odczyt..."));
        LCD_DUMP();
        return;
    }

    // Optymalizacja: nie rysuj jeśli nic się nie zmieniło
    if (!dhtScreenDirty && dhtTemperature == lastTemp) return;
    
    dhtScreenDirty = false;
    lastTemp = dhtTemperature;

    LCD_CLEAR();
    LCD_SET(0, 0);
    LCD_PRINT(F("Temperatura"));
    
    LCD_SET(0, 1);
    LCD_PRINT(dhtTemperature); 
    LCD_WRITE(223); // Znak stopnia
    LCD_PRINT(F("C"));

    LCD_SET(0, 3);
    LCD_PRINT(F("Dlugi -> Wyjscie"));
    
    LCD_DUMP();
}

void drawHumidity() {
    static float lastHum = -1000;

    if (!dhtReady) {
        LCD_CLEAR();
        LCD_SET(0, 0);
        LCD_PRINT(F("Wilgotnosc"));
        LCD_SET(0, 1);
        LCD_PRINT(F("Odczyt..."));
        LCD_DUMP();
        return;
    }

    if (!dhtScreenDirty && dhtHumidity == lastHum) return;
    
    dhtScreenDirty = false;
    lastHum = dhtHumidity;

    LCD_CLEAR();
    LCD_SET(0, 0);
    LCD_PRINT(F("Wilgotnosc"));
    
    LCD_SET(0, 1);
    LCD_PRINT((int)dhtHumidity); // Rzutowanie na int dla ładniejszego wyglądu
    LCD_PRINT(F(" %"));

    LCD_SET(0, 3);
    LCD_PRINT(F("Dlugi -> Wyjscie"));

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

    // --- FLASH INFO ---
    const uint32_t usedFlash = ESP.getSketchSize();
    const uint32_t freeFlash = ESP.getFreeSketchSpace();

    char buf[21];

    // Wiersz 0: Nagłówek
    LCD_SET(0, 0);
    LCD_PRINT(F("ZASOBY SYSTEMU"));

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
// EKRAN PRZEJŚCIA TRYBU (WiFi <-> Bluetooth)
// ============================================================================

void drawModeTransition() {
    LCD_CLEAR();

    lcdPrintCentered(0, F("ZMIANA TRYBU"));
    clearRow(1);
    lcdPrintCentered(2, F("RESET"));
    clearRow(3);

    LCD_DUMP();
}

static void updateBmp280UiHistory(const BMP280Screen::RuntimeData& data) {
  if (data.sampleRevision == 0 || data.sampleRevision == s_bmp280UiHistory.lastProcessedRevision) {
    return;
  }

  s_bmp280UiHistory.lastProcessedRevision = data.sampleRevision;

  if (data.hasTemperature) {
    if (!s_bmp280UiHistory.hasTemp) {
      s_bmp280UiHistory.minTemp = data.temperatureC;
      s_bmp280UiHistory.maxTemp = data.temperatureC;
      s_bmp280UiHistory.hasTemp = true;
    } else {
      s_bmp280UiHistory.minTemp = min(s_bmp280UiHistory.minTemp, data.temperatureC);
      s_bmp280UiHistory.maxTemp = max(s_bmp280UiHistory.maxTemp, data.temperatureC);
    }
  }

  if (data.hasPressure) {
    if (!s_bmp280UiHistory.hasPressure) {
      s_bmp280UiHistory.minPressure = data.pressureHpa;
      s_bmp280UiHistory.maxPressure = data.pressureHpa;
      s_bmp280UiHistory.hasPressure = true;
    } else {
      s_bmp280UiHistory.minPressure = min(s_bmp280UiHistory.minPressure, data.pressureHpa);
      s_bmp280UiHistory.maxPressure = max(s_bmp280UiHistory.maxPressure, data.pressureHpa);
    }
  }

  if (data.hasAltitude) {
    if (!s_bmp280UiHistory.hasAltitude) {
      s_bmp280UiHistory.minAltitude = data.altitudeM;
      s_bmp280UiHistory.maxAltitude = data.altitudeM;
      s_bmp280UiHistory.hasAltitude = true;
    } else {
      s_bmp280UiHistory.minAltitude = min(s_bmp280UiHistory.minAltitude, data.altitudeM);
      s_bmp280UiHistory.maxAltitude = max(s_bmp280UiHistory.maxAltitude, data.altitudeM);
    }
  }
}

static void printBmp280MenuValue(int itemIndex, const BMP280Screen::RuntimeData& data) {
  switch (itemIndex) {
    case 0:
      LCD_PRINT(F("Temp: "));
      printBmp280FloatOrDash(data.hasTemperature, data.temperatureC, 5, 1);
      if (data.hasTemperature) {
        LCD_PRINT(F(" C"));
      }
      break;
    case 1:
      LCD_PRINT(F("Cisn: "));
      printBmp280FloatOrDash(data.hasPressure, data.pressureHpa, 5, 1);
      if (data.hasPressure) {
        LCD_PRINT(F(" hPa"));
      }
      break;
    case 2:
      LCD_PRINT(F("Status: "));
      LCD_PRINT(data.statusText);
      break;
    case 3:
      LCD_PRINT(F("Wys: "));
      printBmp280FloatOrDash(data.hasAltitude, data.altitudeM, 5, 1);
      if (data.hasAltitude) {
        LCD_PRINT(F(" m"));
      }
      break;
    default:
      break;
  }
}