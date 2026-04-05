#pragma once

#include <Arduino.h>

constexpr uint16_t kPms5003UnsetMinValue = 0xFFFFu;

// ============================================================================
// KLASA PMS5003 CZUJNIK
// ============================================================================
// Sterownik czujnika pyłu PMS5003 oparty na bibliotece PMserial.
// Realizuje nieblokujący automat stanów:
//   1) Odczyt ATM (atmosferyczny) → pauza 2 s
//   2) Odczyt CF=1 (fabryczny)    → raport + pauza 8 s
// Dane są utrzymywane wewnątrz modułu i udostępniane przez snapshoty,
// dzięki czemu UI i MQTT nie zależą od globalnych pól implementacyjnych.
// ============================================================================

namespace PMS5003Sensor {

struct MassReadings {
  uint16_t pm01 = 0;
  uint16_t pm25 = 0;
  uint16_t pm10 = 0;
};

struct ParticleCounts {
  uint16_t count0p3 = 0;
  uint16_t count0p5 = 0;
  uint16_t count1p0 = 0;
  uint16_t count2p5 = 0;
  uint16_t count5p0 = 0;
  uint16_t count10p0 = 0;
};

struct ValueRange {
  uint16_t min = kPms5003UnsetMinValue;
  uint16_t max = 0;
};

struct Stats {
  uint16_t errorCountCurrent = 0;
  uint32_t errorCountTotal = 0;
  uint16_t bytesReceived = 0;
  uint32_t lastFrameTime = 0;
  uint32_t latencyMs = 0;

  ValueRange factoryPm01;
  ValueRange factoryPm25;
  ValueRange factoryPm10;
  ValueRange atmosphericPm01;
  ValueRange atmosphericPm25;
  ValueRange atmosphericPm10;
  ValueRange particle0p3;
  ValueRange particle0p5;
  ValueRange particle1p0;
  ValueRange particle2p5;
  ValueRange particle5p0;
  ValueRange particle10p0;
};

/// Inicjalizacja czujnika — wywołaj raz w setup()
void begin();

/// Nieblokujący automat stanów — wywołaj w każdym loop()
void update();

/// Czy sensor jest aktualnie włączony.
bool isEnabled();

/// Włącza lub wyłącza sensor.
void setEnabled(bool enabled);

/// Czy ostatni odczyt był poprawny (przynajmniej ATM OK)
bool isOk();

/// Zwraca timestamp (ms) ostatniej aktualizacji danych z czujnika
uint32_t getLastUpdateTime();

/// Bieżące dane atmosferyczne (ATM)
MassReadings getAtmospheric();

/// Bieżące dane fabryczne (CF=1)
MassReadings getFactory();

/// Bieżące liczniki cząstek
ParticleCounts getParticleCounts();

/// Telemetria i statystyki min/max
Stats getStats();

/// Wymuś natychmiastowy cykl odczytu (rozpoczyna nowy START_ATM_READ)
void requestImmediateRead();

/// Wymuś reset statystyk min/max (wywoływane wewnętrznie w begin(),
/// ale można też z menu)
void resetMinMax();

}  // namespace PMS5003Sensor
