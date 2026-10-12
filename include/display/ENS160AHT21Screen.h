#pragma once

#include <Arduino.h>
namespace ENS160AHT21Screen {

struct RuntimeData {
  uint8_t aqi;
  uint16_t tvoc;
  uint16_t eco2;
  float temperatureC;
  float rawTemperatureC;
  float humidityPct;
  char statusText[16];
  uint32_t lastUpdateMs;
  bool hasSample;
  bool hasGasSample;
  bool hasClimateSample;
  // Stopien kalibracji ENS160 widoczny dla UI/ramki 0x01:
  // 0=ROZGRZEWANIE (flaga 1), 1=NIESKALIBROWANE (flaga 2 / init start-up),
  // 2=WSTEPNIE SKALIBROWANE (flaga 0 po 1. godzinie pracy w tej sesji),
  // 3=W PELNI SKALIBROWANE (flaga 0 + NVM sprzed 24h ciaglej pracy),
  // 0xFF = brak danych.
  uint8_t gasStage;
};

extern RuntimeData runtimeData;
extern bool screenDirty;

void resetRuntimeData();
void markScreenDirty();

}  // namespace ENS160AHT21Screen