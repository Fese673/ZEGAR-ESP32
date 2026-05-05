#include "UI_Draw.h"
#include "AppSettings.h"
#include "LCDMirror.h"
#include "StatsManager.h"
#include "RtcSyncService.h"
#include "WiFiSync.h"
#include "ModeManager.h"
#include "AudioBT.h"
#include "RadioModeSwitch.h"
#include <LiquidCrystal_I2C.h>
#include <Esp.h>
#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <SPI.h>
#endif
#include <math.h>
#include "TelemetryComposer.h"
#include "RuntimeTelemetry.h"
#include "PMS_Czujnik.h"
#include "ENS160AHT21Screen.h"
#include "BMP280Sensor.h"
#include "LCDIcons.h"
#include "AlarmMelodies.h"
#include "AlarmRuntime.h"
#include "UIState.h"

extern LiquidCrystal_I2C lcd;

#if UART_LCD_MIRROR
extern LcdMirror20x4 lcdMirror;
#endif

namespace {

static bool s_sevenSegReady = false;

UIState::State& ui = UIState::mutableState();
const AppSettings::State& appSettings = AppSettings::state();

const int& statsMenuIndex = ui.statsMenu.index;
const int& statsMenuCount = ui.statsMenu.count;

const int& resourcesMenuIndex = ui.resourcesMenu.index;
const int& resourcesMenuCount = ui.resourcesMenu.count;
const char* const*& resourcesMenuItems = ui.resourcesMenu.items;

const int& pms5003MenuIndex = ui.pmsMenu.index;
const int& pms5003MenuCount = ui.pmsMenu.count;
const char* const*& pms5003MenuItems = ui.pmsMenu.items;
const int& pms5003CF1MenuIndex = ui.pmsCf1Menu.index;
const int& pms5003CF1MenuCount = ui.pmsCf1Menu.count;
const int& pms5003ATMMenuIndex = ui.pmsAtmMenu.index;
const int& pms5003ATMMenuCount = ui.pmsAtmMenu.count;
const int& pms5003ParticlesMenuIndex = ui.pmsParticlesMenu.index;
const int& pms5003ParticlesMenuCount = ui.pmsParticlesMenu.count;
const int& ens160MenuIndex = ui.ens160Menu.index;
const int& ens160MenuCount = ui.ens160Menu.count;
const int& bmp280MenuIndex = ui.bmp280Menu.index;
const int& bmp280MenuCount = ui.bmp280Menu.count;

const int& settingsMenuIndex = ui.settingsMenu.index;
const int& settingsMenuCount = ui.settingsMenu.count;
const char* const*& settingsMenuItems = ui.settingsMenu.items;
const int& settingsPmsMenuIndex = ui.settingsPmsMenu.index;
const int& settingsPmsMenuCount = ui.settingsPmsMenu.count;
const char* const*& settingsPmsMenuItems = ui.settingsPmsMenu.items;
const int& settingsBuzzerMenuIndex = ui.settingsBuzzerMenu.index;
const int& settingsBuzzerMenuCount = ui.settingsBuzzerMenu.count;
const char* const*& settingsBuzzerMenuItems = ui.settingsBuzzerMenu.items;
const int& settingsTouchMenuIndex = ui.settingsTouchMenu.index;
const int& settingsBgMusicMenuIndex = ui.settingsBackgroundMusicMenu.index;
const int& settingsBgMusicMenuCount = ui.settingsBackgroundMusicMenu.count;
const char* const*& settingsBgMusicMenuItems = ui.settingsBackgroundMusicMenu.items;
const int& settingsMqttMenuIndex = ui.settingsMqttMenu.index;
const int& settingsMqttMenuCount = ui.settingsMqttMenu.count;
const char* const*& settingsMqttMenuItems = ui.settingsMqttMenu.items;
const int& settingsAlarmMelodyIndex = ui.settingsAlarmMelodyMenu.index;
const int& settingsEpicIntroIndex = ui.settingsBootIntroMenu.index;
const int& settingsEpicIntroMenuCount = ui.settingsBootIntroMenu.count;
const char* const*& settingsEpicIntroItems = ui.settingsBootIntroMenu.items;
const int& settingsUiScreenIndex = ui.settingsUiScreenMenu.index;
const int& settingsUiScreenCount = ui.settingsUiScreenMenu.count;
const char* const*& settingsUiScreenItems = ui.settingsUiScreenMenu.items;

const int& alarmsMenuIndex = ui.alarmsMenu.index;
const int& selectedAlarmIndex = ui.selectedAlarmIndex;
const int& alarmEditCursor = ui.alarmEditCursor;
const int& btMusicMenuIndex = ui.btMusicMenu.index;
const int& btMusicMenuCount = ui.btMusicMenu.count;
const char* const*& btMusicMenuItems = ui.btMusicMenu.items;

bool& pmsScreenDirty = ui.pmsScreenDirty;

const bool& mqttEnabled = appSettings.mqttEnabled;
const bool& buzzerEnabled = appSettings.buzzerEnabled;
const int& settingsRotationSec = appSettings.homeOverlaySeconds;
const int& settingsSyncMinutes = appSettings.ntpSyncMinutes;

const AlarmRuntime::State& alarmRuntime = AlarmRuntime::state();
const int& alarmHour = alarmRuntime.alarmHour;
const int& alarmMinute = alarmRuntime.alarmMinute;
const bool& alarmEnabled = alarmRuntime.alarmEnabled;
const bool& alarmRinging = alarmRuntime.alarmRinging;
const AlarmEntry (&alarms)[AlarmRuntime::kMaxAlarms] = alarmRuntime.alarms;
const int& alarmsCount = alarmRuntime.alarmsCount;

}  // namespace

// ============================================================================
// IMPLEMENTACJA FUNKCJI - 7-SEGMENT (74HC595)
// ============================================================================

uint8_t swapNibbles(uint8_t v) {
  return (v << 4) | (v >> 4);
}

static void writeSevenSegFrame(uint8_t first, uint8_t second, uint8_t third);

static uint8_t packTwoDigits(int value) {
  return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static void commitSevenSegFrame(uint8_t first, uint8_t second, uint8_t third) {
  digitalWrite(LATCH_PIN, LOW);
  writeSevenSegFrame(first, second, third);
  digitalWrite(LATCH_PIN, HIGH);
}

static void updateSevenSegDebugSTM32() {
  if (!s_sevenSegReady || !stm32Connected) {
    return;
  }

  const int spo2 = constrain(displayedSPO2, 0, 99);
  const int bpm = constrain(displayedBPM, 0, 255);

  const uint8_t left = packTwoDigits(spo2);
  const int bpmHundreds = bpm / 100;
  const int bpmTens = (bpm / 10) % 10;
  const int bpmOnes = bpm % 10;
  const uint8_t middle = packTwoDigits((1 * 10) + bpmHundreds);
  const uint8_t right = packTwoDigits(bpmTens * 10 + bpmOnes);

  commitSevenSegFrame(swapNibbles(right), swapNibbles(middle), swapNibbles(left));
}

#ifndef ARDUINO_ARCH_ESP32
static void pulse(int pin) {
  digitalWrite(pin, HIGH);
  delayMicroseconds(5);
  digitalWrite(pin, LOW);
  delayMicroseconds(5);
}

static void shiftOutByte(uint8_t v) {
  for (int i = 7; i >= 0; i--) {
    digitalWrite(DATA_PIN, (v >> i) & 1);
    delayMicroseconds(5);
    pulse(CLOCK_PIN);
  }
}

#endif

static void writeSevenSegFrame(uint8_t first, uint8_t second, uint8_t third) {
#ifdef ARDUINO_ARCH_ESP32
  SPI.beginTransaction(SPISettings(4000000UL, MSBFIRST, SPI_MODE0));
  SPI.transfer(first);
  SPI.transfer(second);
  SPI.transfer(third);
  SPI.endTransaction();
#else
  shiftOutByte(first);
  shiftOutByte(second);
  shiftOutByte(third);
#endif
}

void initSevenSeg() {
  pinMode(DATA_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(LATCH_PIN, OUTPUT);

  digitalWrite(DATA_PIN, LOW);
  digitalWrite(CLOCK_PIN, LOW);
  digitalWrite(LATCH_PIN, LOW);

#ifdef ARDUINO_ARCH_ESP32
  SPI.begin(BoardPins::kSevenSegClock, -1, BoardPins::kSevenSegData, -1);
#endif
#ifdef ARDUINO_ARCH_ESP32
  vTaskDelay(pdMS_TO_TICKS(50));
#else
  const unsigned long initWaitUntilMs = millis() + 50UL;
  while ((long)(millis() - initWaitUntilMs) < 0) {
    yield();
  }
#endif

  // Wyzeruj wyświetlacz
  commitSevenSegFrame(0, 0, 0);
  s_sevenSegReady = true;
}

void updateSevenSeg() {
  if (!s_sevenSegReady) {
    return;
  }

  const bool systemTimeValid = RtcSyncService::isSystemTimeValid();
  const bool clockSeeded = RtcSyncService::isClockSeeded();
  const bool timerPreviewVisible = (appState == STATE_TIMER && !timerRunning);
  const bool alarmEditVisible = (appState == STATE_ALARM_EDIT);
  const unsigned long nowMs = millis();

  static bool alarmBlinkVisible = true;
  static bool alarmBlinkInitialized = false;
  static unsigned long alarmBlinkLastToggleMs = 0;

  if (alarmRinging) {
    if (!alarmBlinkInitialized) {
      alarmBlinkVisible = true;
      alarmBlinkLastToggleMs = nowMs;
      alarmBlinkInitialized = true;
    } else if (nowMs - alarmBlinkLastToggleMs >= 500UL) {
      alarmBlinkLastToggleMs = nowMs;
      alarmBlinkVisible = !alarmBlinkVisible;
    }

    if (!alarmBlinkVisible) {
      commitSevenSegFrame(0, 0, 0);
      return;
    }
  } else {
    alarmBlinkInitialized = false;
    alarmBlinkVisible = true;
  }

  if (appState == STATE_DEBUG_STM32 && stm32Connected) {
    updateSevenSegDebugSTM32();
    return;
  }

  if (!alarmRinging && !timerRunning && !timerPreviewVisible && appState != STATE_SET_TIME && !alarmEditVisible && !systemTimeValid && !clockSeeded) {
    return;
  }

  uint8_t HH, MM, SS;

  if (alarmRinging) {
    if (systemTimeValid) {
      time_t now = time(nullptr);
      struct tm timeinfo;
      localtime_r(&now, &timeinfo);
      HH = packTwoDigits(timeinfo.tm_hour);
      MM = packTwoDigits(timeinfo.tm_min);
      SS = packTwoDigits(timeinfo.tm_sec);
    } else {
      HH = packTwoDigits(hours);
      MM = packTwoDigits(minutes);
      SS = packTwoDigits(seconds);
    }
  } else if (timerRunning) {
    unsigned long elapsed = (nowMs >= timerStartMillis) ? (nowMs - timerStartMillis) : 0;
    long remainingMs = (long)timerDurationMs - (long)elapsed;
    if (remainingMs < 0) remainingMs = 0;
    int rh = (int)(remainingMs / 3600000L);
    int rm = (int)((remainingMs % 3600000L) / 60000L);
    int rs = (int)((remainingMs % 60000L) / 1000L);
    if (rh > 99) {
      rh = 99;
    }
    HH = packTwoDigits(rh);
    MM = packTwoDigits(rm);
    SS = packTwoDigits(rs);
  } else if (timerPreviewVisible) {
    HH = packTwoDigits(timerSetHours);
    MM = packTwoDigits(timerSetMinutes);
    SS = packTwoDigits(timerSetSeconds);
  } else if (alarmEditVisible) {
    int previewHours = alarmHour;
    int previewMinutes = alarmMinute;

    if (appState == STATE_ALARM_EDIT && selectedAlarmIndex >= 0 && selectedAlarmIndex < alarmsCount) {
      previewHours = alarms[selectedAlarmIndex].hour;
      previewMinutes = alarms[selectedAlarmIndex].minute;
    }

    HH = packTwoDigits(previewHours);
    MM = packTwoDigits(previewMinutes);
    SS = 0;
  } else if (appState != STATE_SET_TIME && systemTimeValid) {
    struct tm timeinfo;
    time_t now = time(nullptr);
    localtime_r(&now, &timeinfo);
    HH = packTwoDigits(timeinfo.tm_hour);
    MM = packTwoDigits(timeinfo.tm_min);
    SS = packTwoDigits(timeinfo.tm_sec);
  } else {
    HH = packTwoDigits(hours);
    MM = packTwoDigits(minutes);
    SS = packTwoDigits(seconds);
  }

  commitSevenSegFrame(swapNibbles(SS), swapNibbles(MM), swapNibbles(HH));
}

void updateSevenSegStoper(int mins, int secs, int centisec) {
  if (!s_sevenSegReady) {
    return;
  }

  const uint8_t MM = packTwoDigits(mins);
  const uint8_t SS = packTwoDigits(secs);
  const uint8_t CS = packTwoDigits(centisec);

  commitSevenSegFrame(swapNibbles(CS), swapNibbles(SS), swapNibbles(MM));
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

static void lcdPrintCenteredWithIconsAndSuffix(uint8_t row, const char* text, const uint8_t* prefixIcons, uint8_t prefixIconCount, int suffixIcon) {
  const int textLen = (int)strlen(text);
  const int totalLen = textLen + (int)prefixIconCount + (suffixIcon >= 0 ? 1 : 0);
  const int pad = (SCREEN_WIDTH - min(totalLen, (int)SCREEN_WIDTH)) / 2;

  clearRow(row);
  LCD_SET((uint8_t)pad, row);
  for (uint8_t i = 0; i < prefixIconCount; ++i) {
    LCD_WRITE((uint8_t)prefixIcons[i]);
  }
  LCD_PRINT(text);
  if (suffixIcon >= 0) {
    LCD_WRITE((uint8_t)suffixIcon);
  }
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


  static void writeMenuRow(uint8_t row, const char* text) {
    char line[SCREEN_WIDTH + 1];
    memset(line, ' ', SCREEN_WIDTH);
    line[SCREEN_WIDTH] = '\0';

    if (text != nullptr) {
      const size_t len = strlen(text);
      const size_t copyLen = (len > SCREEN_WIDTH) ? SCREEN_WIDTH : len;
      memcpy(line, text, copyLen);
    }

    LCD_SET(0, row);
    LCD_PRINT(line);
  }

// --- Ekran główny ---
static const char* const polishMonths[] PROGMEM = {
    "STY", "LUT", "MAR", "KWI", "MAJ", "CZE",
    "LIP", "SIE", "WRZ", "PAŹ", "LIS", "GRU",
};

static void padRightTo20(char* line) {
  const int len = (int)strlen(line);
  if (len >= SCREEN_WIDTH) {
    line[SCREEN_WIDTH] = '\0';
    return;
  }
  for (int i = len; i < SCREEN_WIDTH; ++i) {
    line[i] = ' ';
  }
  line[SCREEN_WIDTH] = '\0';
}

struct HomeRenderCache {
  bool valid = false;
  LCDIcons::Palette palette = LCDIcons::Palette::Home;
  bool ntpFresh = false;
  bool alarmArmed = false;
  bool showWifiIcon = false;
  char row1[SCREEN_WIDTH + 1] = {};
  char row2[SCREEN_WIDTH + 1] = {};
  char row3[SCREEN_WIDTH + 1] = {};
};

HomeRenderCache s_homeRenderCache;

static void cacheRow(char* target, const char* source) {
  strncpy(target, source, SCREEN_WIDTH);
  target[SCREEN_WIDTH] = '\0';
}

static bool rowMatchesCache(const char* cached, const char* current) {
  return strcmp(cached, current) == 0;
}

static void renderHomeHeader(bool ntpFresh, bool alarmArmed, bool showWifiIcon, LCDIcons::Palette palette) {
  const bool paletteChanged = !s_homeRenderCache.valid || s_homeRenderCache.palette != palette;
  const bool headerChanged = !s_homeRenderCache.valid ||
                             s_homeRenderCache.ntpFresh != ntpFresh ||
                             s_homeRenderCache.alarmArmed != alarmArmed ||
                             s_homeRenderCache.showWifiIcon != showWifiIcon;

  if (paletteChanged) {
    LCDIcons::loadPalette(lcd, palette);
    s_homeRenderCache.palette = palette;
  }

  if (!headerChanged) {
    return;
  }

  const char* title = "WEJHEROWO";
  uint8_t icons[2];
  uint8_t iconCount = 0;
  if (ntpFresh) {
    icons[iconCount++] = LCDIcons::NtpSlot;
  }
  if (alarmArmed) {
    icons[iconCount++] = LCDIcons::BellSlot;
  }

  if (iconCount > 0 || showWifiIcon) {
    lcdPrintCenteredWithIconsAndSuffix(0, title, icons, iconCount, showWifiIcon ? LCDIcons::WifiSlot : -1);
  } else {
    lcdPrintCentered(0, title);
  }

  s_homeRenderCache.ntpFresh = ntpFresh;
  s_homeRenderCache.alarmArmed = alarmArmed;
  s_homeRenderCache.showWifiIcon = showWifiIcon;
}

static void renderHomeRowIfChanged(uint8_t row, const char* text, char* cache) {
  if (s_homeRenderCache.valid && rowMatchesCache(cache, text)) {
    return;
  }

  cacheRow(cache, text);
  lcdPrintCentered(row, text);
}

void invalidateHomeRenderCache() {
  s_homeRenderCache.valid = false;
}

void requestUiFullRedraw() {
  invalidateHomeRenderCache();
  LCDIcons::resetPaletteCache();
  lcdFrame.forceFullRedrawOnce();
}

void drawHome() {
  const bool ntpFresh = WiFiSync::hasNtpSynced() && (millis() - WiFiSync::getLastNtpSyncTime() <= 3600000UL);
  const bool alarmArmed = isAnyAlarmArmed();
  const bool showWifiIcon = ModeManager::isBtOn();
  const LCDIcons::Palette palette = showWifiIcon ? LCDIcons::Palette::HomeWifi : LCDIcons::Palette::Home;

  renderHomeHeader(ntpFresh, alarmArmed, showWifiIcon, palette);

  const bool clockSeeded = RtcSyncService::isClockSeeded();
  const bool systemTimeValid = RtcSyncService::isSystemTimeValid();

  char row1[SCREEN_WIDTH + 1];
  char row2[SCREEN_WIDTH + 1];
  char row3[SCREEN_WIDTH + 1];

  if (systemTimeValid) {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    snprintf(row1, sizeof(row1), "%02d %s %04d", timeinfo.tm_mday, polishMonths[timeinfo.tm_mon], 1900 + timeinfo.tm_year);
    snprintf(row2, sizeof(row2), ">> %02d:%02d:%02d <<", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  } else if (clockSeeded) {
    snprintf(row1, sizeof(row1), "%s", "BRAK DATY RTC/NTP");
    snprintf(row2, sizeof(row2), ">> %02d:%02d:%02d <<", hours, minutes, seconds);
  } else {
    snprintf(row1, sizeof(row1), "%s", "OCZEKIWANIE NA CZAS");
    snprintf(row2, sizeof(row2), "%s", ">> --:--:-- <<");
  }

  const bool bmpValid = BMP280Screen::runtimeData.hasSample;
  const bool ahtValid = ENS160AHT21Screen::runtimeData.hasClimateSample;

  if (bmpValid && ahtValid) {
    const float tempC = BMP280Screen::runtimeData.temperatureC;
    const int humidity = (int)(ENS160AHT21Screen::runtimeData.humidityPct + 0.5f);
    const float pressure = BMP280Screen::runtimeData.pressureHpa;
    snprintf(row3, sizeof(row3), "IN:%4.1fC %2d%% %4.0fhPa", tempC, humidity, pressure);
  } else if (bmpValid) {
    snprintf(row3, sizeof(row3), "IN:%4.1fC --%% %4.0fhPa", BMP280Screen::runtimeData.temperatureC, BMP280Screen::runtimeData.pressureHpa);
  } else if (ahtValid) {
    const int humidity = (int)(ENS160AHT21Screen::runtimeData.humidityPct + 0.5f);
    snprintf(row3, sizeof(row3), "IN: --.-C %2d%% ----hPa", humidity);
  } else {
    snprintf(row3, sizeof(row3), "IN: --.-C --%% ----hPa");
  }

  renderHomeRowIfChanged(1, row1, s_homeRenderCache.row1);
  renderHomeRowIfChanged(2, row2, s_homeRenderCache.row2);
  renderHomeRowIfChanged(3, row3, s_homeRenderCache.row3);

  s_homeRenderCache.valid = true;

  LCD_DUMP();
  updateSevenSeg();
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

void drawAirScreen() {
  LCD_CLEAR();

  // Collect values.
  const bool ensGasValid = ENS160AHT21Screen::runtimeData.hasGasSample;
  const bool ensClimateValid = ENS160AHT21Screen::runtimeData.hasClimateSample;
  const uint8_t aqi = ensGasValid ? ENS160AHT21Screen::runtimeData.aqi : 0;
  const uint16_t eco2 = ensGasValid ? ENS160AHT21Screen::runtimeData.eco2 : 0;

  // PM2.5: bierzemy ATM (bardziej „ambient”), a gdy PMS wyłączony, pokażemy kreski.
  const PMS5003Sensor::MassReadings atmospheric = PMS5003Sensor::getAtmospheric();
  const bool pmValid = PMS5003Sensor::isEnabled() && atmospheric.pm25 > 0;
  const uint16_t pm25 = pmValid ? atmospheric.pm25 : 0;

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
    // Temperature comes from BMP280; humidity comes from AHT21 only.
    if (BMP280Screen::runtimeData.hasTemperature && ensClimateValid) {
      const int hum = (int)(ENS160AHT21Screen::runtimeData.humidityPct + 0.5f);
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
      int left = snprintf(line, sizeof(line), " AQI:%-2u  |", (unsigned)aqi);
      int rem = SCREEN_WIDTH - left;
      int rlen = (int)strlen(right);
      int pad = rem - rlen;
      if (pad < 0) pad = 0;
      int pos = left;
      for (int i = 0; i < pad && pos < SCREEN_WIDTH; ++i) line[pos++] = ' ';
      for (int i = 0; i < rlen && pos < SCREEN_WIDTH; ++i) line[pos++] = right[i];
      for (; pos < SCREEN_WIDTH; ++pos) line[pos] = ' ';
      line[SCREEN_WIDTH] = '\0';
    } else {
      snprintf(line, sizeof(line), " AQI:--  | eCO2:----");
      padRightTo20(line);
    }
    LCD_SET(0, 3);
    LCD_PRINT(line);
  }

  LCD_DUMP();
  updateSevenSeg();
}

void drawMenu() {
  const UIState::MenuState* activeMenu = (appState == STATE_GAMES_MENU) ? &ui.gamesMenu : &ui.mainMenu;
  const int activeIndex = activeMenu->index;
  const int activeCount = activeMenu->count;
  const char* const* activeItems = activeMenu->items;
  const bool gamesMenu = (appState == STATE_GAMES_MENU);
  const int visibleRows = gamesMenu ? (SCREEN_HEIGHT - 1) : SCREEN_HEIGHT;
  const int first = (visibleRows > 0) ? ((activeIndex / visibleRows) * visibleRows) : 0;

  if (gamesMenu) {
    writeMenuRow(0, "RETRO GAME");
  }                  

  for (int row = 0; row < SCREEN_HEIGHT; ++row) {
    if (gamesMenu && row == 0) {
      continue;
    }

    const int item = gamesMenu ? (first + row - 1) : (first + row);
    if (item < 0 || item >= activeCount || activeItems == nullptr) {
      writeMenuRow(static_cast<uint8_t>(row), "");
      continue;
    }

    char line[SCREEN_WIDTH + 1];
    memset(line, ' ', SCREEN_WIDTH);
    line[SCREEN_WIDTH] = '\0';

    line[0] = (item == activeIndex) ? '>' : ' ';
    line[1] = ' ';

    if (appState == STATE_MENU && item == activeCount - 1) {
      RadioModeSwitchState mode = RadioModeSwitch::getCurrentState();
      const char* text = nullptr;
      switch (mode) {
        case RADIO_STATE_WIFI:
          text = "Tryb: BT";
          break;
        case RADIO_STATE_BT:
          text = "Tryb: WIFI";
          break;
        case RADIO_STATE_TRANSITIONING:
        default:
          text = "Tryb: TRANS";
          break;
      }

      const size_t len = strlen(text);
      const size_t copyLen = (len > (SCREEN_WIDTH - 2)) ? (SCREEN_WIDTH - 2) : len;
      memcpy(line + 2, text, copyLen);
    } else {
      const char* text = activeItems[item];
      const size_t len = strlen(text);
      const size_t copyLen = (len > (SCREEN_WIDTH - 2)) ? (SCREEN_WIDTH - 2) : len;
      memcpy(line + 2, text, copyLen);
    }

    writeMenuRow(static_cast<uint8_t>(row), line);
  }

  LCD_DUMP();
  updateSevenSeg();
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
  } else {
    snprintf(line, sizeof(line), " T:  --.- C H: --%% ");
  }
  padRightTo20(line);
  LCD_SET(0, 1);
  LCD_PRINT(line);

  if (ensClimateValid) {
    snprintf(line, sizeof(line), " ENS:%s", ENS160AHT21Screen::runtimeData.statusText);
  } else {
    snprintf(line, sizeof(line), " ENS:WAITING");
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
  updateSevenSeg();
}

void drawExtremeEnvironmentScreen() {
  LCD_CLEAR();

  lcdPrintCentered(0, F("EXTREME DASH"));

  char line[21];
  const bool ensGasValid = ENS160AHT21Screen::runtimeData.hasGasSample;
  const bool ensClimateValid = ENS160AHT21Screen::runtimeData.hasClimateSample;
  const bool bmpValid = BMP280Screen::runtimeData.hasSample;
  const bool pmValid = PMS5003Sensor::isEnabled() && PMS5003Sensor::getLastUpdateTime() != 0;

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

  float temperature = NAN;
  if (bmpValid && BMP280Screen::runtimeData.hasTemperature) {
    temperature = BMP280Screen::runtimeData.temperatureC;
  } else if (ensClimateValid) {
    temperature = ENS160AHT21Screen::runtimeData.temperatureC;
  }

  float humidity = NAN;
  if (ensClimateValid) {
    humidity = ENS160AHT21Screen::runtimeData.humidityPct;
  }

  float dewPoint = NAN;
  float dewDelta = NAN;
  if (isfinite(temperature) && isfinite(humidity)) {
    dewPoint = TelemetryComposer::computeDewPoint(temperature, humidity);
    if (isfinite(dewPoint)) {
      dewDelta = temperature - dewPoint;
    }
  }

  float humidex = NAN;
  float absoluteHumidity = NAN;
  if (isfinite(temperature) && isfinite(humidity)) {
    humidex = TelemetryComposer::computeHumidex(temperature, humidity);
    absoluteHumidity = TelemetryComposer::computeAbsoluteHumidity(temperature, humidity);
  }

  if (isfinite(humidex) && isfinite(absoluteHumidity)) {
    snprintf(line, sizeof(line), "HMDX:%4.1f AH:%3.0fg", humidex, absoluteHumidity);
  } else if (isfinite(humidex)) {
    snprintf(line, sizeof(line), "HMDX:%4.1f DP:%4.1f", humidex, dewPoint);
  } else if (isfinite(dewPoint)) {
    snprintf(line, sizeof(line), "DP:%4.1f D:%4.1f", dewPoint, dewDelta);
  } else {
    snprintf(line, sizeof(line), "HMDX:--.- AH:--.-");
  }
  padRightTo20(line);
  LCD_SET(0, 3);
  LCD_PRINT(line);

  LCD_DUMP();
  updateSevenSeg();
}

void drawExtremeAlgorithmScreen() {
  LCD_CLEAR();

  float temperature = NAN;
  if (BMP280Screen::runtimeData.hasTemperature) {
    temperature = BMP280Screen::runtimeData.temperatureC;
  } else if (ENS160AHT21Screen::runtimeData.hasClimateSample) {
    temperature = ENS160AHT21Screen::runtimeData.temperatureC;
  }

  float humidity = NAN;
  if (ENS160AHT21Screen::runtimeData.hasClimateSample) {
    humidity = ENS160AHT21Screen::runtimeData.humidityPct;
  }

  float dewPoint = NAN;
  float humidex = NAN;
  float absoluteHumidity = NAN;
  float heatIndex = NAN;

  if (isfinite(temperature) && isfinite(humidity)) {
    dewPoint = TelemetryComposer::computeDewPoint(temperature, humidity);
    humidex = TelemetryComposer::computeHumidex(temperature, humidity);
    absoluteHumidity = TelemetryComposer::computeAbsoluteHumidity(temperature, humidity);
    heatIndex = TelemetryComposer::computeHeatIndexNWS(temperature, humidity);
  }

  float dewDelta = NAN;
  if (isfinite(temperature) && isfinite(dewPoint)) {
    dewDelta = temperature - dewPoint;
  }

  const char *condRisk = "N/A";
  if (isfinite(dewDelta)) {
    if (dewDelta < 2.0f) {
      condRisk = "CRIT";
    } else if (dewDelta < 4.0f) {
      condRisk = "WARN";
    } else {
      condRisk = "SAFE";
    }
  }

  const uint8_t phase = (uint8_t)((millis() / 3500UL) % 2UL);
  char line[21];

  if (phase == 0) {
    lcdPrintCentered(0, F("KONDENSACJA:"));

    if (isfinite(dewPoint)) {
      snprintf(line, sizeof(line), "P.Rosy | %5.1f 'C", dewPoint);
    } else {
      snprintf(line, sizeof(line), "P.Rosy |   --.- 'C");
    }
    padRightTo20(line);
    LCD_SET(0, 1);
    LCD_PRINT(line);

    if (isfinite(absoluteHumidity)) {
      snprintf(line, sizeof(line), "W.Abs. | %5.1f g", absoluteHumidity);
    } else {
      snprintf(line, sizeof(line), "W.Abs. |   --.- g");
    }
    padRightTo20(line);
    LCD_SET(0, 2);
    LCD_PRINT(line);

    if (isfinite(dewDelta)) {
      const char *okTag = (strcmp(condRisk, "SAFE") == 0) ? "OK" : (strcmp(condRisk, "WARN") == 0 ? "WARN" : "CRIT");
      snprintf(line, sizeof(line), "DELTA: %4.1f [%s]", dewDelta, okTag);
    } else {
      snprintf(line, sizeof(line), "DELTA:   --.- [N/A]");
    }
    padRightTo20(line);
    LCD_SET(0, 3);
    LCD_PRINT(line);

  } else {
    lcdPrintCentered(0, F("ODCZUCIE CIEPLA"));

    if (isfinite(humidex)) {
      snprintf(line, sizeof(line), "HUMIDEX: %6.1f", humidex);
    } else {
      snprintf(line, sizeof(line), "HUMIDEX:   --.-");
    }
    padRightTo20(line);
    LCD_SET(0, 1);
    LCD_PRINT(line);

    if (isfinite(heatIndex)) {
      snprintf(line, sizeof(line), "IND.UPA: %6.1f", heatIndex);
    } else {
      snprintf(line, sizeof(line), "IND.UPA:   --.-");
    }
    padRightTo20(line);
    LCD_SET(0, 2);
    LCD_PRINT(line);

    if (isfinite(dewDelta)) {
      snprintf(line, sizeof(line), "STATUS : %6s", condRisk);
    } else {
      snprintf(line, sizeof(line), "STATUS :   N/A");
    }
    padRightTo20(line);
    LCD_SET(0, 3);
    LCD_PRINT(line);
  }

  LCD_DUMP();
  updateSevenSeg();
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
  updateSevenSeg();
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
  updateSevenSeg();
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
  updateSevenSeg();
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
    if (hh > 99) {
      hh = 99;
    }
    int rm = (int)((totalMs % 3600000UL) / 60000UL);
    int rs = (int)((totalMs % 60000UL) / 1000UL);
    int rcs = (int)((totalMs / 10) % 100);
    snprintf(buf, sizeof(buf), "> %02d:%02d:%02d.%02d <", hh, rm, rs, rcs);
    lcdPrintCentered(2, buf);
    clearRow(3);

    // Update 7-seg to HH:MM:SS (drop centisec on 7-seg)
    const uint8_t HHb = packTwoDigits(hh);
    const uint8_t MMb = packTwoDigits(rm);
    const uint8_t SSb = packTwoDigits(rs);
    commitSevenSegFrame(swapNibbles(SSb), swapNibbles(MMb), swapNibbles(HHb));
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

  if (stm32Connected) {
    updateSevenSegDebugSTM32();
  }
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

static bool isPmsState(AppState state) {
  return state == STATE_PMS5003 ||
         state == STATE_PMS5003_CF1 ||
         state == STATE_PMS5003_CF1_PM1 ||
         state == STATE_PMS5003_CF1_PM25 ||
         state == STATE_PMS5003_CF1_PM10 ||
         state == STATE_PMS5003_ATM ||
         state == STATE_PMS5003_ATM_PM1 ||
         state == STATE_PMS5003_ATM_PM25 ||
         state == STATE_PMS5003_ATM_PM10 ||
         state == STATE_PMS5003_PARTICLES ||
         state == STATE_PMS5003_PARTICLES_0_3 ||
         state == STATE_PMS5003_PARTICLES_0_5 ||
         state == STATE_PMS5003_PARTICLES_1_0 ||
         state == STATE_PMS5003_PARTICLES_2_5 ||
         state == STATE_PMS5003_PARTICLES_5_0 ||
         state == STATE_PMS5003_PARTICLES_10_0 ||
         state == STATE_PMS5003_TELEMETRY;
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

typedef void (*PagedMenuRowRenderer)(int itemIndex);

static void drawLongBackFooter() {
  LCD_SET(0, 3);
  clearRow(3);
}

static void drawPagedMenu3Rows(uint8_t headerCol,
                               const __FlashStringHelper* header,
                               int selectedIndex,
                               int itemCount,
                               PagedMenuRowRenderer rowRenderer) {
  LCD_SET(headerCol, 0);
  LCD_PRINT(header);

  const int first = (selectedIndex / 3) * 3;
  for (int row = 0; row < 3; ++row) {
    const int item = first + row;
    if (item >= itemCount) {
      break;
    }

    LCD_SET(0, row + 1);
    LCD_PRINT(item == selectedIndex ? F("> ") : F("  "));
    rowRenderer(item);
  }
}

static void drawCenteredSettingLine(const __FlashStringHelper* title, const char* valueLine) {
  lcdPrintCentered(0, title);
  lcdPrintCentered(1, F(ALIGN_CENTER));
  lcdPrintCentered(2, valueLine);
  clearRow(3);
}

static void drawCenteredSettingFormatted(const __FlashStringHelper* title,
                                         const char* lineFormat,
                                         const char* value) {
  char line[32];
  snprintf(line, sizeof(line), lineFormat, value);
  drawCenteredSettingLine(title, line);
}

static void drawPmsMassMenu(const __FlashStringHelper* title,
                            int selectedIndex,
                            uint16_t pm1,
                            uint16_t pm25,
                            uint16_t pm10) {
  LCD_SET(0, 0);
  LCD_PRINT(title);

  LCD_SET(0, 1);
  LCD_PRINT(selectedIndex == 0 ? F(">") : F(" "));
  LCD_PRINT(F(" PM1.0: "));
  LCD_PRINT(pm1 > 0 ? pm1 : 0);
  LCD_PRINT(F(" \xE4g/m3"));

  LCD_SET(0, 2);
  LCD_PRINT(selectedIndex == 1 ? F(">") : F(" "));
  LCD_PRINT(F(" PM2.5: "));
  LCD_PRINT(pm25 > 0 ? pm25 : 0);
  LCD_PRINT(F(" \xE4g/m3"));

  LCD_SET(0, 3);
  LCD_PRINT(selectedIndex == 2 ? F(">") : F(" "));
  LCD_PRINT(F(" PM10:  "));
  LCD_PRINT(pm10 > 0 ? pm10 : 0);
  LCD_PRINT(F(" \xE4g/m3"));
}

static void drawPmsMassDetail(const __FlashStringHelper* title,
                              uint16_t current,
                              uint16_t minValue,
                              uint16_t maxValue) {
  LCD_SET(0, 0);
  LCD_PRINT(title);
  LCD_SET(0, 1);
  LCD_PRINT(F("Biezaca: "));
  LCD_PRINT(current);
  LCD_PRINT(F(" \xE4g/m3"));
  LCD_SET(0, 2);
  LCD_PRINT(F("Min:"));
  LCD_PRINT(minValue != kPms5003UnsetMinValue ? minValue : 0);
  LCD_PRINT(F(" Max:"));
  LCD_PRINT(maxValue);
  drawLongBackFooter();
}

static void drawPmsParticleDetail(const __FlashStringHelper* title,
                                  uint16_t current,
                                  uint16_t minValue,
                                  uint16_t maxValue) {
  LCD_SET(0, 0);
  LCD_PRINT(title);
  LCD_SET(0, 1);
  LCD_PRINT(F("Biezaca: "));
  LCD_PRINT(current);
  LCD_SET(0, 2);
  LCD_PRINT(F("Min:"));
  LCD_PRINT(minValue != kPms5003UnsetMinValue ? minValue : 0);
  LCD_PRINT(F(" Max:"));
  LCD_PRINT(maxValue);
  drawLongBackFooter();
}

static void drawPmsParticlesMenu() {
  static const char* const kParticleLabels[] = {
      "0.3um", "0.5um", "1.0um", "2.5um", "5.0um", "10um",
  };

  const PMS5003Sensor::ParticleCounts particleCounts = PMS5003Sensor::getParticleCounts();
  const uint16_t particleValues[] = {
      particleCounts.count0p3,
      particleCounts.count0p5,
      particleCounts.count1p0,
      particleCounts.count2p5,
      particleCounts.count5p0,
      particleCounts.count10p0,
  };

  LCD_SET(0, 0);
  LCD_PRINT(F("Liczba Czastek"));

  const int first = (pms5003ParticlesMenuIndex / 3) * 3;
  for (int row = 0; row < 3; ++row) {
    const int i = first + row;
    if (i >= 6) {
      break;
    }

    LCD_SET(0, row + 1);
    LCD_PRINT(i == pms5003ParticlesMenuIndex ? F(">") : F(" "));
    LCD_PRINT(F(" "));
    LCD_PRINT(kParticleLabels[i]);
    LCD_PRINT(F(": "));
    LCD_PRINT(particleValues[i]);
  }
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

static void drawPmsMenuRow(int itemIndex) {
  LCD_PRINT(pms5003MenuItems[itemIndex]);
}

static void drawEnsMenuRow(int itemIndex) {
  printEnsMenuValue(itemIndex, ENS160AHT21Screen::runtimeData);
}

static void drawBmpMenuRow(int itemIndex) {
  printBmp280MenuValue(itemIndex, BMP280Screen::runtimeData);
}

static void drawSettingsMenuRow(int itemIndex) {
  LCD_PRINT(settingsMenuItems[itemIndex]);
}

static void drawResourcesMenuRow(int itemIndex) {
  LCD_PRINT(resourcesMenuItems[itemIndex]);
}

void drawStats() {
  // Optymalizacja dla ekranów PMS5003: rysuj tylko gdy są nowe dane
  if (isPmsState(appState)) {
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
  const PMS5003Sensor::MassReadings pmsAtmospheric = PMS5003Sensor::getAtmospheric();
  const PMS5003Sensor::MassReadings pmsFactory = PMS5003Sensor::getFactory();
  const PMS5003Sensor::ParticleCounts pmsParticles = PMS5003Sensor::getParticleCounts();
  const PMS5003Sensor::Stats pmsStats = PMS5003Sensor::getStats();

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
      }
    }
    break;
  }
  // === 1b. MENU PMS5003 (LISTA Z WYBOREM) ===
  case STATE_PMS5003: {
    drawPagedMenu3Rows(2, F("MENU PMS5003"), pms5003MenuIndex, pms5003MenuCount, drawPmsMenuRow);
    break;
  }
  case STATE_ENS160_AHT21: {
    drawPagedMenu3Rows(1, F("AHT21 + ENS160"), ens160MenuIndex, ens160MenuCount, drawEnsMenuRow);
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
    drawLongBackFooter();
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
    drawLongBackFooter();
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
    drawLongBackFooter();
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
    drawLongBackFooter();
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
    drawLongBackFooter();
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
    drawPagedMenu3Rows(2, F("BMP280"), bmp280MenuIndex, bmp280MenuCount, drawBmpMenuRow);
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
    drawLongBackFooter();
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
    drawLongBackFooter();
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
    drawPagedMenu3Rows(2, F("USTAWIENIA"), settingsMenuIndex, settingsMenuCount, drawSettingsMenuRow);
    break;
  }
  // === 1d. USTAWIENIA PMS5003 (włącz/wyłącz) ===
  case STATE_SETTINGS_PMS5003: {
    drawCenteredSettingFormatted(F("PMS5003"), "SENSOR: <  %s  >", settingsPmsMenuIndex == 0 ? "ON" : "OFF");
    break;
  }
  // === 1e. USTAWIENIA BUZERA (włącz/wyłącz) ===
  case STATE_SETTINGS_BUZZER: {
    drawCenteredSettingFormatted(F("BUZZER"), "STAN:   <  %s  >", settingsBuzzerMenuIndex == 0 ? "ON" : "OFF");
    break;
  }
  // === 1e1. USTAWIENIA DOTYK (włącz/wyłącz) ===
  case STATE_SETTINGS_TOUCH: {
    drawCenteredSettingFormatted(F("DOTYK"), "STAN:   <  %s  >", settingsTouchMenuIndex == 0 ? "ON" : "OFF");
    break;
  }
  // === 1f1. USTAWIENIA MUZYKI W TLE (włącz/wyłącz) ===
  case STATE_SETTINGS_BACKGROUND_MUSIC: {
    drawCenteredSettingFormatted(F("MUZYKA W TLE"), "STAN:   <  %s  >", settingsBgMusicMenuIndex == 0 ? "ON" : "OFF");
    break;
  }
  // === 1f. USTAWIENIA MQTT (włącz/wyłącz) ===
  case STATE_SETTINGS_MQTT: {
    drawCenteredSettingFormatted(F("MQTT"), "BROKER: <  %s  >", settingsMqttMenuIndex == 0 ? "ON" : "OFF");
    break;
  }
  // === 1f1. Wybór melodii alarmu ===
  case STATE_SETTINGS_ALARM_MELODY: {
    char header[21];
    snprintf(header, sizeof(header), "ALARMY %02d/%02d", settingsAlarmMelodyIndex + 1, AlarmMelodies::kCount);
    lcdPrintCentered(0, header);

    const int first = (settingsAlarmMelodyIndex / 3) * 3;
    for (int row = 0; row < 3; ++row) {
      const int item = first + row;
      if (item >= AlarmMelodies::kCount) break;

      char line[21];
      snprintf(line, sizeof(line), "%c%2d. %-15.15s", item == settingsAlarmMelodyIndex ? '>' : ' ', item + 1, AlarmMelodies::name((uint8_t)item));
      LCD_SET(0, row + 1);
      LCD_PRINT(line);
    }
    break;
  }
  // === 1g. USTAWIENIE: ROTACJA EKRANU (1..10s, enkoder) ===
  case STATE_SETTINGS_ROTATION: {
    char valueBuf[32];
    snprintf(valueBuf, sizeof(valueBuf), "CZAS: < %2ds >", settingsRotationSec);
    drawCenteredSettingLine(F("ROTACJA EKRANU"), valueBuf);
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
  // === 1g2. USTAWIENIA BOOT INTRO (włącz/wyłącz) ===
  case STATE_SETTINGS_BOOT_INTRO: {
    const int introIndex = (settingsEpicIntroIndex == 0) ? 0 : 1;
    drawCenteredSettingFormatted(F("BOOT INTRO"), "START: <  %s  >", settingsEpicIntroItems[introIndex]);
    break;
  }
  // === 1x. Lista budzików ===
  case STATE_ALARMS_LIST: {
    lcdPrintCentered(0, F("ALARMY"));
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
        LCD_PRINT(F("[+] DODAJ ALARM  "));
      } else {
        LCD_PRINT(F("                    "));
      }
    }
    break;
  }
  // === 1y. Edycja budzika (ergonomiczna: kursor po lewej, opcja USUN) ===
  case STATE_ALARM_EDIT: {
    char header[21];
    snprintf(header, sizeof(header), "EDYCJA ALARMU %d", selectedAlarmIndex + 1);
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
    char valueBuf[32];
    snprintf(valueBuf, sizeof(valueBuf), "CZAS: < %3dmin >", settingsSyncMinutes);
    drawCenteredSettingLine(F("SYNCHRONIZACJA"), valueBuf);
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
    drawLongBackFooter();
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
    drawLongBackFooter();
    break;
  }
  // === 4. WIDOK TEMPERATURY MIN/MAX ===
  case STATE_STATS_TEMP: {
    const BMP280Screen::RuntimeData& data = BMP280Screen::runtimeData;
    char buf[10];

    LCD_SET(0, 0);
    LCD_PRINT(F("TEMPERATURA"));

    LCD_SET(0, 1);
    LCD_PRINT(F("MIN: "));
    if (s_bmp280UiHistory.hasTemp) {
      dtostrf(s_bmp280UiHistory.minTemp, 4, 1, buf);
      LCD_PRINT(buf);
    } else {
      LCD_PRINT(F("--.--"));
    }

    LCD_SET(0, 2);
    LCD_PRINT(F("MAX: "));
    if (s_bmp280UiHistory.hasTemp) {
      dtostrf(s_bmp280UiHistory.maxTemp, 4, 1, buf);
      LCD_PRINT(buf);
    } else {
      LCD_PRINT(F("--.--"));
    }

    LCD_SET(0, 3);
    LCD_PRINT(F("Biezaca: "));
    printBmp280FloatOrDash(data.hasTemperature, data.temperatureC, 5, 1);
    if (data.hasTemperature) {
      LCD_PRINT(F(" C"));
    }
    break;
  }
  // === 5. WIDOK WILGOTNOŚCI MIN/MAX ===
  case STATE_STATS_HUM: {
    const ENS160AHT21Screen::RuntimeData& data = ENS160AHT21Screen::runtimeData;
    char buf[10];

    LCD_SET(0, 0);
    LCD_PRINT(F("WILGOTNOSC"));

    LCD_SET(0, 1);
    LCD_PRINT(F("MIN: "));
    if (s_ens160UiHistory.hasHum) {
      dtostrf(s_ens160UiHistory.minHum, 4, 1, buf);
      LCD_PRINT(buf);
    } else {
      LCD_PRINT(F("--.--"));
    }

    LCD_SET(0, 2);
    LCD_PRINT(F("MAX: "));
    if (s_ens160UiHistory.hasHum) {
      dtostrf(s_ens160UiHistory.maxHum, 4, 1, buf);
      LCD_PRINT(buf);
    } else {
      LCD_PRINT(F("--.--"));
    }

    LCD_SET(0, 3);
    LCD_PRINT(F("Biezaca: "));
    printEnsFloatOrDash(data.hasClimateSample, data.humidityPct, 3, 0);
    if (data.hasClimateSample) {
      LCD_PRINT(F(" %"));
    }
    break;
  }
  // === 5b. MENU ZASOBÓW SYSTEMU ===
  case STATE_STATS_RESOURCES_MENU: {
    drawPagedMenu3Rows(1, F("ZASOBY SYSTEMU"), resourcesMenuIndex, resourcesMenuCount, drawResourcesMenuRow);
    break;
  }
  // === 5c. WIDOK PAMIĘCI RAM ===
  case STATE_STATS_RESOURCES_RAM: {
    uint32_t ramKB = ramFreeBytes / 1024;
    uint32_t largestKB = ramLargestBlockBytes / 1024;
    uint32_t dmaKB = ramDmaFreeBytes / 1024;

    LCD_SET(0, 0);
    LCD_PRINT(F("PAMIEC RAM"));

    LCD_SET(0, 1);
    LCD_PRINT(F("Free: "));
    LCD_PRINT(ramKB);
    LCD_PRINT(F(" KB"));

    LCD_SET(0, 2);
    LCD_PRINT(F("Largest: "));
    LCD_PRINT(largestKB);
    LCD_PRINT(F(" KB"));

    LCD_SET(0, 3);
    LCD_PRINT(F("DMA: "));
    LCD_PRINT(dmaKB);
    LCD_PRINT(F(" KB"));
    break;
  }
  // === 5d. WIDOK OBCIĄŻENIA CPU ===
  case STATE_STATS_RESOURCES_CPU: {
    LCD_SET(0, 0);
    LCD_PRINT(F("HEAP LOAD"));

    LCD_SET(0, 1);
    LCD_PRINT(F("Calkowite: "));
    LCD_PRINT(heapUsagePercent);
    LCD_PRINT(F("%"));

    LCD_SET(0, 2);
    LCD_PRINT(F("CORE0: "));
    LCD_PRINT(heapUsageCore0Percent);
    LCD_PRINT(F("%"));

    LCD_SET(0, 3);
    LCD_PRINT(F("CORE1: "));
    LCD_PRINT(heapUsageCore1Percent);
    LCD_PRINT(F("%"));
    break;
  }
  // === 5d2. WIDOK STABILNOŚCI AUDIO ===
  case STATE_STATS_RESOURCES_AUDIO: {
    const RuntimeTelemetry::Snapshot audioSnapshot = RuntimeTelemetry::snapshot();

    LCD_SET(0, 0);
    LCD_PRINT(F("AUDIO STABILNOSC"));

    LCD_SET(0, 1);
    LCD_PRINT(F("Underrun: "));
    LCD_PRINT(audioSnapshot.audio_underruns);

    LCD_SET(0, 2);
    LCD_PRINT(F("Overflow: "));
    LCD_PRINT(audioSnapshot.audio_overflows);

    LCD_SET(0, 3);
    LCD_PRINT(F("Drops: "));
    LCD_PRINT(audioSnapshot.audio_drops);
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
    drawLongBackFooter();
    break;
  }
  // === 6. WIDOK PMS5003 TRYB FABRYCZNY CF=1 (BIEŻĄCE DANE Z WYBOREM) ===
  case STATE_PMS5003_CF1: {
    drawPmsMassMenu(F("PMS5003 CF=1"),
                    pms5003CF1MenuIndex,
                    pmsFactory.pm01,
                    pmsFactory.pm25,
                    pmsFactory.pm10);
    break;
  }
  // === 6a. WIDOK SZCZEGÓŁÓW PM1.0 TRYB CF=1 (MIN/MAX) ===
  case STATE_PMS5003_CF1_PM1: {
    drawPmsMassDetail(F("PM1.0 CF=1"), pmsFactory.pm01, pmsStats.factoryPm01.min, pmsStats.factoryPm01.max);
    break;
  }
  // === 6b. WIDOK SZCZEGÓŁÓW PM2.5 TRYB CF=1 (MIN/MAX) ===
  case STATE_PMS5003_CF1_PM25: {
    drawPmsMassDetail(F("PM2.5 CF=1"), pmsFactory.pm25, pmsStats.factoryPm25.min, pmsStats.factoryPm25.max);
    break;
  }
  // === 6c. WIDOK SZCZEGÓŁÓW PM10 TRYB CF=1 (MIN/MAX) ===
  case STATE_PMS5003_CF1_PM10: {
    drawPmsMassDetail(F("PM10 CF=1"), pmsFactory.pm10, pmsStats.factoryPm10.min, pmsStats.factoryPm10.max);
    break;
  }
  // === 7. WIDOK PMS5003 TRYB ATMOSFERYCZNY (BIEŻĄCE DANE Z WYBOREM) ===
  case STATE_PMS5003_ATM: {
    drawPmsMassMenu(F("PMS5003 ATM"),
                    pms5003ATMMenuIndex,
                    pmsAtmospheric.pm01,
                    pmsAtmospheric.pm25,
                    pmsAtmospheric.pm10);
    break;
  }
  // === 7a. WIDOK SZCZEGÓŁÓW PM1.0 TRYB ATM (MIN/MAX) ===
  case STATE_PMS5003_ATM_PM1: {
    drawPmsMassDetail(F("PM1.0 ATM"), pmsAtmospheric.pm01, pmsStats.atmosphericPm01.min, pmsStats.atmosphericPm01.max);
    break;
  }
  // === 7b. WIDOK SZCZEGÓŁÓW PM2.5 TRYB ATM (MIN/MAX) ===
  case STATE_PMS5003_ATM_PM25: {
    drawPmsMassDetail(F("PM2.5 ATM"), pmsAtmospheric.pm25, pmsStats.atmosphericPm25.min, pmsStats.atmosphericPm25.max);
    break;
  }
  // === 7c. WIDOK SZCZEGÓŁÓW PM10 TRYB ATM (MIN/MAX) ===
  case STATE_PMS5003_ATM_PM10: {
    drawPmsMassDetail(F("PM10 ATM"), pmsAtmospheric.pm10, pmsStats.atmosphericPm10.min, pmsStats.atmosphericPm10.max);
    break;
  }

  // ========== Particles ==========
  case STATE_PMS5003_PARTICLES: {
    drawPmsParticlesMenu();
    break;
  }

  case STATE_PMS5003_PARTICLES_0_3: {
    drawPmsParticleDetail(F("0.3um"), pmsParticles.count0p3, pmsStats.particle0p3.min, pmsStats.particle0p3.max);
    break;
  }

  case STATE_PMS5003_PARTICLES_0_5: {
    drawPmsParticleDetail(F("0.5um"), pmsParticles.count0p5, pmsStats.particle0p5.min, pmsStats.particle0p5.max);
    break;
  }

  case STATE_PMS5003_PARTICLES_1_0: {
    drawPmsParticleDetail(F("1.0um"), pmsParticles.count1p0, pmsStats.particle1p0.min, pmsStats.particle1p0.max);
    break;
  }

  case STATE_PMS5003_PARTICLES_2_5: {
    drawPmsParticleDetail(F("2.5um"), pmsParticles.count2p5, pmsStats.particle2p5.min, pmsStats.particle2p5.max);
    break;
  }

  case STATE_PMS5003_PARTICLES_5_0: {
    drawPmsParticleDetail(F("5.0um"), pmsParticles.count5p0, pmsStats.particle5p0.min, pmsStats.particle5p0.max);
    break;
  }

  case STATE_PMS5003_PARTICLES_10_0: {
    drawPmsParticleDetail(F("10um"), pmsParticles.count10p0, pmsStats.particle10p0.min, pmsStats.particle10p0.max);
    break;
  }

  // ========== Telemetria ==========
  case STATE_PMS5003_TELEMETRY: {
    LCD_SET(0, 0);
    LCD_PRINT(F("Telemetria"));
    
    LCD_SET(0, 1);
    LCD_PRINT(F("Bledy: "));
    LCD_PRINT(pmsStats.errorCountCurrent);
    LCD_PRINT(F("/"));
    LCD_PRINT(pmsStats.errorCountTotal);
    
    LCD_SET(0, 2);
    LCD_PRINT(F("Bajty: "));
    if (pmsStats.bytesReceived < 10) LCD_PRINT(F("0"));
    LCD_PRINT(pmsStats.bytesReceived);
    
    LCD_SET(0, 3);
    LCD_PRINT(F("Latencja: "));
    if (pmsStats.latencyMs < 10) LCD_PRINT(F("0"));
    if (pmsStats.latencyMs < 100) LCD_PRINT(F("0"));
    LCD_PRINT(pmsStats.latencyMs);
    LCD_PRINT(F(" ms"));
    break;
  }
  default:
    break;
  }

  LCD_DUMP();
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
    updateSevenSeg();
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

void drawBtMusicControl() {
  LCD_CLEAR();

  const bool btActive = ModeManager::isBtOn();
  const bool btConnected = audioBT_isConnected();
  char header[21];
  if (btActive) {
    snprintf(header, sizeof(header), "BT MUZYKA  %s", btConnected ? "TEL:ON" : "TEL:WAIT");
  } else {
    snprintf(header, sizeof(header), "BT MUZYKA  BT:OFF");
  }
  lcdPrintCentered(0, header);

  auto renderPair = [&](uint8_t row, int leftIndex, int rightIndex) {
    char line[21];
    const char* leftLabel = (leftIndex < btMusicMenuCount && btMusicMenuItems != nullptr) ? btMusicMenuItems[leftIndex] : "";
    const char* rightLabel = (rightIndex < btMusicMenuCount && btMusicMenuItems != nullptr) ? btMusicMenuItems[rightIndex] : "";
    const bool leftSelected = btMusicMenuIndex == leftIndex;
    const bool rightSelected = btMusicMenuIndex == rightIndex;
    snprintf(line,
             sizeof(line),
             "%c%-7.7s %c%-7.7s",
             leftSelected ? '>' : ' ',
             leftLabel,
             rightSelected ? '>' : ' ',
             rightLabel);
    padRightTo20(line);
    LCD_SET(0, row);
    LCD_PRINT(line);
  };

  renderPair(1, 0, 1);
  renderPair(2, 2, 3);
  renderPair(3, 4, 5);

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