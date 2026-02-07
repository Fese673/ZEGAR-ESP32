#include "UI_Draw.h"
#include "LCDMirror.h"
#include "StatsManager.h"
#include <LiquidCrystal_I2C.h>
#include <Esp.h>
#include "PMS_Czujnik.h"

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
  const uint8_t HH = ((hours / 10) << 4) | (hours % 10);
  const uint8_t MM = ((minutes / 10) << 4) | (minutes % 10);
  const uint8_t SS = ((seconds / 10) << 4) | (seconds % 10);

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
void drawHome() {
  LCD_CLEAR();
  LCD_SET(4, 1);
  printTime(false);

  if (alarmEnabled) {
    LCD_SET(0, 1);
    LCD_WRITE(byte(0));  // Ikona budzika
  }

  LCD_SET(2, 3);
  LCD_PRINT("Klik -> MENU");
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
    LCD_PRINT(item == menuIndex ? ">" : " ");

    if (item == 11) {  // Pozycja radio toggle (teraz na indeksie 11 po dodaniu Ustawienia)
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
  LCD_SET(2, 1);
  printTime(true);
  LCD_SET(2, 3);
  LCD_PRINT("Klik -> dalej");
  LCD_DUMP();
}

// --- Ekran budzika ---
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

  LCD_SET(4, 2);
  if (m < 10) LCD_PRINT("0");
  LCD_PRINT(m);
  LCD_PRINT(":");
  if (s < 10) LCD_PRINT("0");
  LCD_PRINT(s);
  LCD_PRINT(".");
  if (cs < 10) LCD_PRINT("0");
  LCD_PRINT(cs);

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

  LCD_CLEAR();
  const AppStats stats = statsManager.getStats();

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
    LCD_SET(0, 0);
    LCD_PRINT("PMS5003");

    LCD_SET(0, 1);
    LCD_PRINT("Stan: ");

    LCD_SET(0, 2);
    LCD_PRINT(settingsPmsMenuIndex == 0 ? "> Wlaczony " : "  Wylaczony");

    LCD_SET(0, 3);
    LCD_PRINT("Klik -> zapisz");
  }
  // === 1e. USTAWIENIA BUZERA (włącz/wyłącz) ===
  else if (appState == STATE_SETTINGS_BUZZER) {
    LCD_SET(0, 0);
    LCD_PRINT("BUZZER");

    LCD_SET(0, 1);
    LCD_PRINT("Stan: ");

    LCD_SET(0, 2);
    LCD_PRINT(settingsBuzzerMenuIndex == 0 ? "> Wlaczony " : "  Wylaczony");

    LCD_SET(0, 3);
    LCD_PRINT("Klik -> zapisz");
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