# Raport Audytu: Kompatybilność Zegar ↔ Guition
**Data:** 2026-05-08
**Status:** Audit Completed (Wymagane zmiany synchronizacyjne)

---

## 1. Analiza Protokołu (CRC & COBS)

**Werdykt:** KOMPATYBILNE.
Oba projekty używają identycznej implementacji:
- **Framing:** COBS z ogranicznikami `0x00`.
- **CRC:** CRC16-CCITT (Polynomial: `0x1021`, Initial: `0xFFFF`).
- **Endianness:** Little-endian dla pól wielobajtowych.

---

## 2. Wykryte Problemy (Drift Danych)

### A. SystemResourcesPayload (Mismatch)
W Zegarze (Fix ERR_030) rozszerzyłeś strukturę o statystyki I2C:
```cpp
// Zegar (EsptoGuitionState.h)
uint16_t errorsI2c;   // +2 bajty
uint16_t timeoutsI2c; // +2 bajty
// Suma: 32 bajty
```
W Guition parser (`CommunicationState.cpp:330`) wciąż ma "twardy" warunek:
```cpp
if (payloadLength < 28U) return; // Guition nie widzi dodatkowych 4 bajtów
```
**Skutek:** Guition odbierze ramkę, ale nie zaktualizuje pól `errorsI2c` i `timeoutsI2c` w UI.

### B. OutdoorWeatherPayload (Ryzyko)
Struktura osiągnęła 33 bajty. Zegar wysyła flagi na końcu (`payload[32]`). Guition poprawnie wykrywa tryb `extended`, ale struktury są "na styku". Zalecana ostrożność przy dodawaniu kolejnych pól pogodowych.

---

## 3. "Ukryte Błędy" w Guition (Mirroring Zegara)

### A. Buforowanie UART
Guition (`UartTransport.cpp:24`) ma bufor TX ustawiony na `256`. 
- **Zagrożenie:** Jeśli Guition wyśle serię ramek (np. `kTypeSetSettings` + `kTypeRequest`), a Zegar nie odbierze ich natychmiast, funkcja `uart_write_bytes` w Guition **zablokuje pętlę UI** (LVGL) do czasu zwolnienia miejsca w buforze.
- **Naprawa:** Zwiększyć `kTxBufferSize` do `1024` i dodać guard `availableForWrite` (analogicznie do ERR_027 w Zegarze).

### B. Brak Cache'owania Flash (Potencjalne)
Guition obecnie nie raportuje własnych zasobów systemowych do Zegara (tylko odbiera), więc nie woła `esp_ota_get_running_partition`. 
- **Uwaga:** Jeśli planujesz dodać diagnostykę Guition widoczną na Zegarze, **musisz** użyć cache'owania, bo ESP32-S3 w Guition zamarznie tak samo jak Zegar (Cache Disable).

---

## 4. Checklist: Co trzeba zaktualizować w Guition?

1.  **CommunicationProtocol.h:** Dodać pola `errorsI2c` i `timeoutsI2c` do struktury `SystemResources`.
2.  **CommunicationState.cpp:** 
    - Zaktualizować `applySystemResourcesPayload`, aby poprawnie mapował bajty 28-31 na nowe pola.
    - Zmienić warunek długości ramki.
3.  **UartTransport.cpp:**
    - Zwiększyć `kTxBufferSize` do `1024`.
    - Sprawdzić, czy `uart_write_bytes` nie blokuje (użyć `uart_get_tx_buffer_free_size`).

---

