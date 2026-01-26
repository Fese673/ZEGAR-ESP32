#include "UI_Draw.h"
#include "LCDMirror.h"
#include "StatsManager.h"
#include <LiquidCrystal_I2C.h>
#include <Esp.h>

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

    if (item == 9) {  // Pozycja radio toggle
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