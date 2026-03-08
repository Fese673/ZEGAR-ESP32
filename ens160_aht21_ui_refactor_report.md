# Refaktoryzacja UI ENS160 + AHT21

## 1. Analiza submenu PMS5003

Submenu PMS5003 w projekcie jest zbudowane jako wielopoziomowe drzewo stanów `AppState`.

- `STATE_PMS5003` pełni rolę menu głównego modułu PMS5003.
- Kolejne stany, takie jak `STATE_PMS5003_CF1`, `STATE_PMS5003_ATM`, `STATE_PMS5003_PARTICLES` i `STATE_PMS5003_TELEMETRY`, reprezentują widoki kategorii lub danych.
- Dalsze stany szczegółowe, np. `STATE_PMS5003_CF1_PM1`, `STATE_PMS5003_ATM_PM25`, pokazują jeden parametr w bardziej czytelnym układzie.

Sterowanie działa według spójnego wzorca:

- obrót enkodera zmienia indeks aktualnego submenu,
- klik wchodzi w zaznaczony widok lub szczegół,
- długie przytrzymanie wraca poziom wyżej.

Renderowanie PMS5003 w `drawStats()` ma dwa istotne cechy:

- menu są listami z kursorem `>` i paginacją po 3 pozycje,
- widoki danych są rozbite na logiczne ekrany: lista bieżących wartości i osobne ekrany szczegółów.

## 2. Różnice względem poprzedniego ENS160 + AHT21

Poprzedni ekran ENS160 + AHT21 był pojedynczym widokiem pod stanem `STATE_ENS160_AHT21`.

- wszystkie dane były upchnięte na jednym ekranie 20x4,
- nie było submenu ani podziału na kategorie,
- obrót enkodera nie robił nic,
- długie przytrzymanie tylko wychodziło do głównego menu,
- UI nie korzystało z istniejącego wzorca PMS5003.

To działało technicznie, ale odstawało od reszty projektu pod względem ergonomii i spójności interfejsu.

## 3. Nowy model UI dla ENS160 + AHT21

Nowy model został zaprojektowany jako odpowiednik PMS5003, ale dopasowany do danych gazowych i klimatycznych.

Struktura:

- `STATE_ENS160_AHT21` – menu modułu ENS160 + AHT21
- `STATE_ENS160_AHT21_SUMMARY` – ekran podsumowania
- `STATE_ENS160_AHT21_GAS` – lista danych gazowych
- `STATE_ENS160_AHT21_GAS_AQI` – szczegóły AQI
- `STATE_ENS160_AHT21_GAS_TVOC` – szczegóły TVOC
- `STATE_ENS160_AHT21_GAS_ECO2` – szczegóły eCO2
- `STATE_ENS160_AHT21_CLIMATE` – lista danych klimatycznych
- `STATE_ENS160_AHT21_CLIMATE_TEMP` – szczegóły temperatury
- `STATE_ENS160_AHT21_CLIMATE_HUM` – szczegóły wilgotności
- `STATE_ENS160_AHT21_STATUS` – ekran statusu sensora i świeżości danych

Nawigacja:

- obrót: zmiana pozycji w menu ENS160, menu gazów i menu klimatu,
- klik: wejście do wybranej kategorii lub szczegółu,
- długie przytrzymanie: powrót o jeden poziom wyżej, zgodnie z PMS5003.

## 4. Opis zmian w kodzie

Zmiany objęły cztery główne obszary:

1. `include/AppState.h`
- dodano pełny zestaw stanów dla submenu ENS160 + AHT21.

2. `src/main.cpp`
- dodano indeksy i listy pozycji dla menu ENS160, gazów i klimatu,
- rozszerzono warunek okresowego odświeżania `drawStats()` o wszystkie nowe stany ENS160.

3. `src/UI_Controller.cpp`
- dodano obsługę obrotu enkodera dla nowych menu ENS160,
- dodano przejścia kliknięciem między menu, kategoriami i szczegółami,
- dodano logikę powrotu długim przytrzymaniem poziom wyżej.

4. `src/UI_Draw.cpp`
- pojedynczy ekran ENS160 został zastąpiony wieloma widokami,
- renderowanie ENS160 zostało dopasowane do wzorca PMS5003,
- odświeżanie live działa dla całego drzewa stanów ENS160, a nie tylko jednego ekranu.

## 5. Przykładowy układ ekranów LCD 20x4

### Menu ENS160

```text
 ENS160 + AHT21
> Podsumowanie
  Gazy
  Klimat
```

### Podsumowanie

```text
ENS160 Podsum.
AQI:1 TVOC:48
eCO2:454
T:25.2C H:34.5%
```

### Gazy

```text
Gazy ENS160
> AQI: 1
  TVOC: 48 ppb
  eCO2: 454 ppm
```

### Szczegóły TVOC

```text
TVOC
Biezaca: 48 ppb
Status: INIT_STARTUP
Dlugi -> Powrot
```

### Klimat

```text
Klimat AHT21
> Temp: 25.2 C
  Wilg: 34.5 %
Klik -> Szczegoly
```

### Status

```text
Status ENS160
Stan: INIT_STARTUP
Gaz:OK Klim:OK
Ostatnia: 3s
```

## Wynik

UI ENS160 + AHT21 zostało doprowadzone do tego samego modelu interakcji co PMS5003:

- ma wielopoziomowe submenu,
- reaguje na enkoder,
- dzieli dane na logiczne widoki,
- zachowuje spójność z obecną architekturą projektu,
- projekt kompiluje się poprawnie po zmianach.