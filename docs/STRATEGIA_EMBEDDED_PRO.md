# Strategia Architektoniczna "Embedded Pro" — ZEGAR & GUITION

## Cel Główny
**Osiągnięcie pełnej stabilności klasy produkcyjnej oraz płynności interfejsu (target 60 FPS) poprzez rygorystyczną separację zasobów, inteligentne wykorzystanie pamięci PSRAM oraz asynchroniczny dekupling systemów komunikacji od interfejsu użytkownika.**

---

## 1. Architektura Pamięci (SRAM vs PSRAM)

### Wytyczne:
*   **SRAM (Internal):** Rezerwowany wyłącznie dla operacji o niskiej latencji i wysokiej częstotliwości.
    *   Stosy zadań (Task Stacks).
    *   Bufory DMA dla Audio (I2S) i Wyświetlacza (LCD_CAM).
    *   Krytyczne bufory kołowe sterowników (Ring Buffers).
*   **PSRAM (External):** "Magazyn" dla dużych danych i procesów tła.
    *   Wszystkie bufory robocze komunikacji (COBS, ramki danych).
    *   Warstwy renderowania LVGL 9 (Draw Layers).
    *   Cache obrazów i czcionek.
    *   Duże struktury danych (np. bufory JSON, telemetria).

### Akcja:
Przeniesienie wszystkich statycznych i dynamicznych buforów pomocniczych z sekcji `.bss` (SRAM) do sterty PSRAM przy użyciu `MALLOC_CAP_SPIRAM`.

---

## 2. Rurociąg Danych (Asynchroniczny Data-Broker)

### Problem:
Bezpośrednia aktualizacja zmiennych UI w przerwaniach lub zadaniach komunikacyjnych powoduje mikro-przycięcia i ryzyko korupcji pamięci.

### Rozwiązanie:
*   **Implementacja Kolejki Wiadomości (Message Queue):** Zadanie komunikacyjne (Core 0) parsuje ramkę i wrzuca gotowy "Snapshot" do kolejki.
*   **Konsumpcja w UI:** Zadanie LVGL (Core 1) pobiera dane z kolejki tylko w wyznaczonych momentach (idle time), aktualizując interfejs bez blokowania potoku renderowania.
*   **Zero-Copy:** Wykorzystanie wskaźników do buforów w PSRAM zamiast kopiowania całych struktur między zadaniami.

---

## 3. Optymalizacja Interfejsu (Smart Rendering)

### Strategia:
*   **Layer Management:** Aktywne unikanie dużych warstw półprzezroczystych (opacity < 255) na pełnym ekranie. Wymuszenie alokacji niezbędnych warstw wyłącznie w PSRAM.
*   **V-Sync & DMA:** Pełna synchronizacja odświeżania z sygnałem V-Sync kontrolera RGB, aby uniknąć efektu "tearingu" przy minimalnym obciążeniu procesora.
*   **Dynamiczny FPS:** Redukcja częstotliwości odświeżania do 10-15 FPS w stanie spoczynku, skok do 60 FPS podczas interakcji.

---

## 4. Niezawodność i Diagnostyka (Black-Box)

### Mechanizmy:
*   **RTC Memory Telemetry:** Zapisywanie przyczyn ostatniego restartu (Watchdog, Panic) oraz kluczowych parametrów (wolny RAM, stan I2C) w pamięci RTC, która nie ulega skasowaniu przy software'owym restarcie.
*   **Heap Health Monitor:** Regularne logowanie fragmentacji sterty SRAM i PSRAM.
*   **Watchdog Resilience:** Każde zadanie musi jawnie raportować swoją żywotność do TWDT. Zadania długotrwałe (renderowanie) muszą mieć precyzyjnie określone punkty kontrolne.

---

## 5. Roadmap Wdrożenia

1.  **Faza 1:** Refaktoryzacja `UartTransport` — bufory do PSRAM, optymalizacja stosów.
2.  **Faza 2:** Implementacja kolejki Snapshotów (Data-Broker) między Core 0 a Core 1.
3.  **Faza 3:** Optymalizacja warstw LVGL 9 i wdrożenie dynamicznego FPS.
4.  **Faza 4:** System diagnostyki w pamięci RTC.

---

> [!IMPORTANT]
> Każda linia kodu musi być pisana z myślą o ograniczonej pamięci SRAM. Jeśli coś może być w PSRAM — **musi** tam być.
