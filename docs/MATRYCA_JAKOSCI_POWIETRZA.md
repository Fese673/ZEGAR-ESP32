# Uniwersalna Matryca Jakości Powietrza

> **Źródło prawdy** dla klasyfikacji jakości powietrza w obu projektach: **ZEGAR-ESP32** i **ESP32-S3-JC8048W550-LVGL-ESPIDF-EEZ**.


---

## Uniwersalna Matryca UI

| Poziom UI | Kolor | Stan powietrza | Opis / Zachowanie |
|-----------|-------|----------------|-------------------|
| **Poziom 1** | 🟢 Zielony | **Dobry** | Powietrze czyste, idealne do aktywności. |
| **Poziom 2** | 🟡 Żółty | **Umiarkowany** | Dopuszczalne, ale wrażliwe osoby mogą to odczuć. |
| **Poziom 3** | 🟠 Pomarańczowy / Czerwony | **Zły** | Przekroczone normy, nie wietrz pokoju. |
| **Poziom 4** | 🟣 Fioletowy / Bordowy | **Alarm / Smog** | Krytyczne zanieczyszczenie, odpalaj oczyszczacz na max. |

---

## Konkretne progi dla każdego sensora (Wersja Ostateczna)

### 1. Pyły zawieszone (PM)

Progi dla PM2.5 oparłem na dobowej normie WHO (15 µg/m³) oraz europejskim progu ostrzegania przed smogiem (50 µg/m³). PM10 i PM1.0 są przeskalowane proporcjonalnie do PM2.5, co eliminuje sytuację, gdzie PM2.5 świeci na czerwono, a PM10 na zielono.

| Sensor / Parametr | 🟢 Dobry | 🟡 Umiarkowany | 🟠 Zły | 🔴 Alarm / Smog |
|-------------------|----------|---------------|--------|-----------------|
| **PM2.5** | ≤ 15 µg/m³ | 16 – 25 µg/m³ | 26 – 50 µg/m³ | > 50 µg/m³ |
| **PM10** | ≤ 30 µg/m³ | 31 – 50 µg/m³ | 51 – 90 µg/m³ | > 90 µg/m³ |
| **PM1.0** | ≤ 10 µg/m³ | 11 – 18 µg/m³ | 19 – 35 µg/m³ | > 35 µg/m³ |

> **Dlaczego tak?** ZEGAR-ESP32 był najbliżej prawdy. Wyrzucamy amerykańskie progi z EEZ (12/35), bo tylko robiły śmietnik w logice.

---

### 2. Gazy: eCO₂ oraz ENS160 AQI

Dla **ENS160 AQI** oficjalna dokumentacja ScioSense opiera się na niemieckiej skali UBA (skala 1–5). Ponieważ nasze UI ma 4 stopnie, stopnie 1 i 2 (Excellent i Good) łączymy w jeden wspólny stan „Dobry".

Dla **eCO₂** progi wynikają wprost z norm wentylacji budynków ASHRAE.

| Sensor / Parametr | 🟢 Dobry | 🟡 Umiarkowany | 🟠 Zły | 🔴 Alarm / Smog |
|-------------------|----------|---------------|--------|-----------------|
| **ENS160 AQI (UBA)** | 1 lub 2 | 3 | 4 | 5 |
| **eCO₂** | ≤ 800 ppm | 801 – 1200 ppm | 1201 – 1800 ppm | > 1800 ppm |

---

### 3. TVOC (Lotne Związki Organiczne)

Progi dla TVOC są wyciągnięte z Sensor Wiki w EEZ (jedyna implementacja w projekcie, która w ogóle klasyfikuje TVOC). ZEGAR-ESP32 wyświetla TVOC surowo bez kolorów.

| Sensor / Parametr | 🟢 Dobry | 🟡 Umiarkowany | 🟠 Zły |
|-------------------|----------|---------------|--------|
| **TVOC** | ≤ 250 ppb | 251 – 500 ppb | > 500 ppb |

> TVOC nie ma poziomu Alarm. W skrajnych stężeniach sygnalizuje je eCO₂ lub AQI.

---

### 4. Sensory klimatyczne: BME280 / BMP280 + AHT21

**Te sensory NIE MAJĄ progów jakości powietrza** — ich wartości są wyświetlane jako surowe liczby, bez klasyfikacji na Poziomy 1–4. Stanowią kontekst dla interpretacji pozostałych pomiarów.

| Sensor | Parametry | Uwagi |
|--------|-----------|-------|
| **BME280 / BMP280** | Temperatura (°C), Ciśnienie (hPa) | Źródło temperatury w ZEGAR-ESP32 (lokalny sensor na boardzie). W EEZ nazwany BME280 ale w ZEGAR-ESP32 jako BMP280. |
| **AHT21** | Temperatura (°C), Wilgotność (%) | Zintegrowany z ENS160 na jednym module. Źródło wilgotności i temperatury w obu projektach. |

**Docelowo**: wyświetlać jako surowe wartości. Brak matrycy kolorów dla temperatury/ciśnienia/wilgotności — to nie są wskaźniki jakości powietrza.

---

### 5. Liczniki cząstek PMS5003 (6 binów spektrometru)

PMS5003 raportuje fizyczną liczbę cząstek w 6 zakresach wielkości na 0,1 litra powietrza. **Brak jakichkolwiek norm ani progów jakości** — to dane spektrometryczne dla zaawansowanych użytkowników.

| Bin | Zakres wielkości | Typ cząstek |
|-----|------------------|-------------|
| count0p3 | > 0.3 µm | Ultrafrakcja (spalanie, silniki diesla) |
| count0p5 | > 0.5 µm | Sadza węglowa |
| count1p0 | > 1.0 µm | Smog PM1.0 |
| count2p5 | > 2.5 µm | Pył mineralny PM2.5 |
| count5p0 | > 5.0 µm | Pył z opon, budowy |
| count10p0 | > 10.0 µm | Pyłki, roztocza, pleśń |

**Docelowo**: wyświetlać jako surowe wartości, bez klasyfikacji na Poziomy 1–4.

---

## Podsumowanie: co ma progi, a co nie

| Sensor | Ma matrycę 1–4? | Uwagi |
|--------|----------------|-------|
| **PM2.5** | ✅ Tak | Główny wskaźnik jakości |
| **PM10** | ✅ Tak | Skalowany proporcjonalnie do PM2.5 |
| **PM1.0** | ✅ Tak | Skalowany proporcjonalnie do PM2.5 |
| **eCO₂** | ✅ Tak | Normy ASHRAE |
| **ENS160 AQI** | ✅ Tak | Skala UBA (1–5 → 4 poziomy) |
| **TVOC** | ✅ Tak (3 poziomy, bez Alarm) | Tylko w Sensor Wiki EEZ |
| **BME280/BMP280** (temp/ciśnienie) | ❌ Nie | Surowe wartości |
| **AHT21** (temp/wilgotność) | ❌ Nie | Surowe wartości |
| **PMS5003 liczniki cząstek** | ❌ Nie | Surowe wartości, 6 binów |

---

## Zasady łączenia wielu źródeł

Kiedy na ekranie są widoczne jednocześnie PM2.5, eCO₂ i AQI, **ostateczny poziom UI** jest określany przez **najgorszy** z odczytanych parametrów (zasada „worst wins"):

```
poziom_UI = max(poziom_PM25, poziom_eCO2, poziom_AQI)
```

Np. jeśli PM2.5 = 10 (🟢 Dobry), eCO₂ = 900 (🟡 Umiarkowany), AQI = 3 (🟡 Umiarkowany) → 🟡 Umiarkowany.

> Sensory klimatyczne (temp, ciśnienie, wilgotność) i liczniki cząstek nie biorą udziału w tym łączeniu.

---

## Różnice między projektami (stan obecny → docelowy)

| Parametr | ZEGAR-ESP32 (było) | EEZ 5" (było) | **Matryca (docelowe)** |
|----------|---------------------|----------------|------------------------|
| PM2.5 próg „Dobry" | < 15 | ≤ 12 | **≤ 15** |
| PM2.5 próg „Średni" | < 25 | ≤ 35 | **16 – 25** |
| PM2.5 próg „Zły" | < 50 | > 35 | **26 – 50** |
| PM2.5 próg „Alarm" | ≥ 50 | *(brak)* | **> 50** |
| PM10 progi | *(brak w UI)* | ≤ 35 / ≤ 50 / > 50 | **30 / 50 / 90** |
| PM1.0 progi | *(brak w UI)* | ≤ 15 / ≤ 25 / > 25 | **10 / 18 / 35** |
| ENS160 AQI | UBA 1–5 (osobne) | Przeskalowany 1–3 | **UBA 1–5 (połączony 1+2)** |
| eCO₂ próg „Umiarkowany" | *(brak)* | > 800 | **801 – 1200** |
| eCO₂ próg „Zły" | ≥ 1500 | > 1200 | **1201 – 1800** |
| eCO₂ próg „Alarm" | ≥ 2000 | *(brak)* | **> 1800** |
| TVOC progi | *(brak)* | 250 / 500 | **250 / 500** |
| BME280 / AHT21 | Surowe wartości | Surowe wartości | **Surowe wartości** |
| Liczniki cząstek | Surowe wartości | Surowe wartości | **Surowe wartości** |

---

**Dokument utworzony: 2026-06-02**
**Ostatnia aktualizacja: 2026-06-02** — dodano BME280, AHT21, liczniki cząstek PMS5003, TVOC
*Status: Źródło prawdy — nie edytować projektów do momentu wspólnej decyzji.*
