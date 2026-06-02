# Plan implementacji Uniwersalnej Matrycy Jakości Powietrza

> Oparty na `MATRYCA_JAKOSCI_POWIETRZA.md` — źródle prawdy.
> **Zasada**: każdy parametr klasyfikowany osobno (1–4), potem `max()` daje ostateczny poziom UI.

---

## 1. ZEGAR-ESP32 (LCD 20×4 + ENS160 + PMS5003 + BMP280)

### 1.1. `src/ui/UI_Draw.cpp` — główna zmiana

#### a) Zamienić `airHeaderFor()` na nowy system klas

**Stan obecny**: jedna funkcja zwraca `FlashStringHelper*` z tekstem, logika wymieszana (if-all-good-kombinacje).

**Stan docelowy**:
- Nowa funkcja `int classifyPmLevel(uint16_t pm25)` — zwraca 1–4 dla PM2.5
- Nowa funkcja `int classifyEco2Level(uint16_t eco2)` — zwraca 1–4 dla eCO₂
- Nowa funkcja `int classifyAqiLevel(uint8_t aqi)` — zwraca 1–4 dla AQI UBA
- Nowa funkcja `int classifyPm10Level(uint16_t pm10)` — zwraca 1–4 dla PM10
- Nowa funkcja `int classifyPm1Level(uint16_t pm1)` — zwraca 1–4 dla PM1.0
- Nowa funkcja `int combinedAirQualityLevel(pm25, pm10, pm1, eco2, aqi)` — `max(...)`
- Nowa funkcja `airHeaderFor(level)` — zwraca `FlashStringHelper*` na podstawie levelu

**Mapowanie poziomu na tekst**:
| Level | Tekst |
|-------|-------|
| 1 | `"POWIETRZE: DOBRE"` |
| 2 | `"POWIETRZE: UMIARKOWANE"` |
| 3 | `"POWIETRZE: ZLE"` |
| 4 | `"ALARM: SMOG"` (lub `"! ALARM SMOG !"`) |

#### b) Rozszerzyć `drawAirScreen()` o PM10 i PM1.0

Obecnie wiersz 2 pokazuje tylko PM2.5. W nowej wersji:
- Row 0: header z `combinedAirQualityLevel()` → `airHeaderFor()`
- Row 1: temp + humidity (bez zmian)
- Row 2: `PM2.5: xx  PM10: xx` (oba obok siebie, 2.5 i 10)
- Row 3: `AQI:x  eCO2:xxxx` (bez zmian, ewentualnie + TVOC)

#### c) Dodać kolory/wizualizację

Jeśli LCD wspiera kolory (RGB backlight) — zmienić podświetlenie wg levelu:
- Level 1 → zielony
- Level 2 → żółty  
- Level 3 → pomarańczowy
- Level 4 → czerwony/bordowy

Jeśli nie ma kolorowego podświetlenia, same teksty + ewentualnie ikona (smiley/alert).

### 1.2. `src/ui/UI_Controller.cpp`

- Dodać nowy stan `STATE_AIR_QUALITY` (albo użyć istniejącego) — sprawdzić czy nawigacja z menu powietrza działa
- Jeśli potrzeba, dodać podgląd szczegółów dla PM10 w air screen

### 1.3. `src/comms/TelemetryComposer.cpp` (opcjonalnie)

- Jeśli MQTT publikuje airQualityIndex (1–3), zmienić na nowy 1–4

---

## 2. ESP32-S3-JC8048W550-LVGL-ESPIDF-EEZ (wyświetlacz 5" + LVGL)

### 2.1. `main/communication/CommunicationState.cpp` — AQI text

#### Zamienić `deriveAirQualityText()`

**Stan obecny** (tylko PM2.5, 3 poziomy):
```cpp
pm25 ≤ 12 → "Dobra" (idx 1)
pm25 ≤ 35 → "Średnia" (idx 2)
pm25 > 35 → "Zła" (idx 3)
```

**Stan docelowy** (wszystkie sensory, 4 poziomy):
```cpp
// Dodać nową funkcję:
int classifyCombinedLevel(pm25, pm10, pm1, eco2, aqi) // max z indywidualnych

// deriveAirQualityText() używa combined level:
level 1 → "Dobra"
level 2 → "Umiarkowana"
level 3 → "Zła"
level 4 → "Alarm"
```

#### Dodać nowe pole `airQualityLevel` do Snapshot (uint8_t, 1–4)

Przyda się do EEZ flow zamiast parsowania tekstu.

### 2.2. `main/communication/CommUiMapper.cpp`

- Pushować nową zmienną globalną `FLOW_GLOBAL_VARIABLE_AIR_QUALITY_LEVEL` (int 1–4)
- Zachować `AIR_QUALITY_TEXT_IN` dla kompatybilności (ale tekst generowany z levelu)
- Dodać zmienne dla PM10 i PM1.0 poziomu (lub combined już uwzględnia)

### 2.3. `main/ui/vars.h`

- Dodać nowy enum (np. `FLOW_GLOBAL_VARIABLE_AIR_QUALITY_LEVEL`)

### 2.4. `main/services/sensor/sensors_wiki.cpp` — aktualizacja kolorów

#### PM1.0 (linia ~680):
| Obecnie (EEZ) | Docelowo (matryca) |
|---|---|
| ≤ 15 biały | ≤ 10 zielony |
| 16–25 pomarańcz | 11–18 żółty |
| >25 czerwony | 19–35 pomarańcz |
| — | >35 czerwony |

#### PM2.5 (linia ~700):
| Obecnie (EEZ) | Docelowo (matryca) |
|---|---|
| ≤ 20 biały | ≤ 15 zielony |
| 21–35 pomarańcz | 16–25 żółty |
| >35 czerwony | 26–50 pomarańcz |
| — | >50 czerwony |

#### PM10 (linia ~720):
| Obecnie (EEZ) | Docelowo (matryca) |
|---|---|
| ≤ 35 biały | ≤ 30 zielony |
| 36–50 pomarańcz | 31–50 żółty |
| >50 czerwony | 51–90 pomarańcz |
| — | >90 czerwony |

#### eCO₂ (linia ~765):
| Obecnie (EEZ) | Docelowo (matryca) |
|---|---|
| ≤ 800 biały | ≤ 800 zielony |
| 801–1200 pomarańcz | 801–1200 żółty |
| >1200 czerwony | 1201–1800 pomarańcz |
| — | >1800 czerwony |

#### AQI (linia ~790):
| Obecnie (EEZ) | Docelowo (matryca) |
|---|---|
| airQualityIndex ≥ 4 → "BARDZO ZŁA" czerwony | airQualityLevel == 4 → poziom 4 |
| airQualityIndex ≥ 3 → "UMIARKOWANA" pomarańcz | airQualityLevel == 3 → poziom 3 |
| airQualityIndex < 3 → "DOBRA" zielony | airQualityLevel == 2 → poziom 2 |
| — | airQualityLevel == 1 → poziom 1 |

> UWAGA: EEZ używa `sn.airQualityIndex` (1–3) który pochodzi z `deriveAirQualityText()`. Po zmianie na 4 poziomy, `airQualityIndex` = 4 oznacza Alarm.

#### TVOC (linia ~775):
Obecne progi (250/500) są już zgodne z matrycą. **Bez zmian**. Dodać zielony dla ≤250.

### 2.5. EEZ Flow (GUI designer) — zmiany w projekcie `.eez-project`

- Jeśli w EEZ flow są elementy UI wyświetlające jakość powietrza, zaktualizować ich logikę wizualną (kolory, teksty) w oparciu o nowy `AIR_QUALITY_LEVEL` (1–4)
- Konkretne zmiany wymagają otwarcia EEZ Studio i edycji flow

---

## 3. Kolejność realizacji

| Krok | Co | Projekt | Zależności |
|------|----|---------|------------|
| 1 | Nowe funkcje `classify*Level()` | Oba | — |
| 2 | Nowa `combinedAirQualityLevel()` + `airHeaderFor(level)` | Oba | Krok 1 |
| 3 | `drawAirScreen()` — nowy układ + PM10 | ZEGAR | Krok 2 |
| 4 | Kolory LCD (jeśli wspierane) | ZEGAR | Krok 3 |
| 5 | `deriveAirQualityText()` — nowa logika | EEZ | Krok 2 |
| 6 | `airQualityLevel` w Snapshot + flow vars | EEZ | Krok 5 |
| 7 | Sensor Wiki — nowe progi kolorów | EEZ | Krok 1 |
| 8 | EEZ Flow GUI — wizualna strona | EEZ | Krok 6 |
| 9 | MQTT aktualizacja (jeśli potrzeba) | ZEGAR | Krok 2 |

---

## 4. Ryzyka / uwagi

- **ZEGAR-ESP32**: LCD 20×4 ma ograniczoną przestrzeń. Zmieszczenie PM2.5 + PM10 w jednym wierszu wymaga skrócenia etykiet: `PM2.5:xx PM10:xx` (17 znaków, mieści się).
- **EEZ Sensor Wiki**: Zmiana kolorów z 3-stopniowych (biały/pomarańcz/czerwony) na 4-stopniowe (zielony/żółty/pomarańcz/czerwony) może wymagać dodania nowego warunku. Obecnie brak koloru zielonego w PM — po zmianie ≤ progu będzie zielony zamiast białego.
- **EEZ Flow**: Zmiana `airQualityIndex` z 1–3 na 1–4 może połamać istniejące warunki w flow (jeśli są). Trzeba sprawdzić jak flow używa tej zmiennej.
- **Nazewnictwo**: W ZEGAR-ESP32 jest BMP280, w EEZ BME280 — to fizycznie ten sam sensor (BMP280 nie ma wilgotności). Nie zmieniać, obie nazwy są poprawne w kontekście.
