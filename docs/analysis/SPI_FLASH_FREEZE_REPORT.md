# Raport Techniczny: Pułapka SPI Flash Cache
**Data:** 2026-05-08
**Status:** Rozwiązane (Fix: ERR_030)
**Problem:** Periodyczne zamrożenia systemu (do 1.2s), lagujący stoper, jitter UI.

---

## 1. Wyrok: Dlaczego system "umierał"?

Większość programistów traktuje wywołania typu `ESP.getFreeHeap()` czy `ESP.getSketchSize()` jako proste i szybkie operacje odczytu zmiennej z pamięci RAM. Na ESP32 to **niebezpieczne założenie**.

O ile odczyt RAM jest niemal natychmiastowy, o tyle funkcje sprawdzające rozmiar partycji (`getSketchSize`, `getFreeSketchSpace`) muszą fizycznie odczytać **tablicę partycji zapisaną w pamięci SPI Flash**.

## 2. Mechanizm "Globalnego Zamrożenia" (Cache Disable)

ESP32 posiada architekturę **XIP (Execute In Place)**. Oznacza to, że Twój kod nie jest kopiowany w całości do RAM, ale procesor czyta instrukcje bezpośrednio z zewnętrznej kości Flash przez mechanizm Cache.

Gdy wywołujesz funkcję czytającą dane techniczne z Flash:
1. **Wyłączenie Cache:** Sterownik SPI Flash musi przełączyć kość w tryb odczytu danych. W tym momencie Cache instrukcji musi zostać **wyłączony**.
2. **Blokada Rdzeni:** Ponieważ Cache jest wyłączony, procesor nie może pobrać kolejnej instrukcji kodu. System operacyjny (ESP-IDF) musi **zatrzymać oba rdzenie (Core 0 i Core 1)**, aby zapobiec próbie wykonania kodu, którego nie ma jak pobrać.
3. **Blokada Przerwań:** Wszystkie przerwania (WiFi, Bluetooth, UART, I2C) zostają zamrożone. Sprzęt nie może obsłużyć żadnego zdarzenia w tym oknie czasowym.
4. **Odczyt SPI:** Następuje fizyczny odczyt danych przez magistralę SPI.
5. **Re-animacja:** Cache zostaje włączony, rdzenie ruszają, przerwania zostają odblokowane.

**Efekt:** Przez te kilkaset milisekund Twój zegar był klinicznie martwy. Nie tykał stoper, nie działał UART, nie odpowiadało WiFi. Stąd brały się te "skoki" na stoperze (np. z 00:00.54 od razu na 00:01.16).

## 3. Lista "Zabójców" w naszym kodzie

Zidentyfikowaliśmy trzy miejsca, które regularnie "mroziły" procesor:

| Plik | Funkcja | Częstotliwość | Rola |
| :--- | :--- | :--- | :--- |
| `SystemResourcesService.cpp` | `ESP.getFreeSketchSpace()` | **Co 1 sekundę** | Sprawdzanie wolnego miejsca na flash. |
| `EsptoGuitionState.cpp` | `ESP.getSketchSize()` | **Co 2 sekundy** | Raportowanie rozmiaru firmware do ekranu Guition. |
| `UI_Draw.cpp` | `ESP.getSketchSize()` | Przy odświeżaniu | Wyświetlanie statystyk na LCD. |

## 4. Rozwiązanie: Caching (Buforowanie)

Ponieważ rozmiar firmware (`SketchSize`) oraz rozmiar partycji (`FreeSketchSpace`) są wartościami **stałymi** (nie zmieniają się w trakcie działania programu), nie ma żadnego powodu, aby odczytywać je częściej niż raz.

Zastosowaliśmy mechanizm cache'owania: wartość jest czytana z Flash tylko raz — przy pierwszym wywołaniu funkcji. Każde kolejne wywołanie zwraca zapisaną w RAM wartość, co jest operacją natychmiastową i nie blokuje procesora.

## 5. Wnioski

- Nigdy nie używaj funkcji `ESP.get*Flash*` w pętlach (`loop`, `update`).
- Każda operacja na Flash na ESP32 (nawet odczyt diagnostyczny) jest operacją blokującą i "drogą".
- Dzięki tej poprawce stoper i UI mają teraz pełną przepustowość procesora bez nagłych przerw w życiorysie.

