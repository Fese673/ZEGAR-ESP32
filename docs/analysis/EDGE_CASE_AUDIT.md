# Raport Audytu: Błędy Brzegowe i Logiczne (v1.50)

## 1. Przepełnienie `millis()` (Uptime 49.7 dnia)
**Ryzyko:** High / Critical (po ok. 50 dniach)

**Opis problemu:** 
W kilku miejscach (np. `EventBus.cpp`, `MQTTSync.cpp`) czas jest porównywany za pomocą bezpośredniego odejmowania lub snapshotów bez uwzględnienia przepełnienia licznika `millis()`. Po 49,7 dniach licznik `uint32_t` zeruje się. 

**Dlaczego jest podstępny:** 
Błąd jest całkowicie niewykrywalny podczas testów trwających krócej niż 50 dni. Urządzenie po prostu nagle "zamarza" lub przestaje wysyłać dane po prawie dwóch miesiącach pracy.

**Diff / Fix:**
Zawsze używaj wzoru `(now - lastMs >= interval)`, który dzięki arytmetyce uzupełnienia do dwójki działa poprawnie nawet przy przepełnieniu.
```cpp
// Przykład poprawki w EventBus.cpp lub Timerach:
- if (nowMs > s_timers[i].lastFireMs + s_timers[i].periodMs)
+ if (nowMs - s_timers[i].lastFireMs >= s_timers[i].periodMs)
```

---

## 2. "Zombie WiFi" podczas handshake SSL
**Ryzyko:** Medium / High

**Opis problemu:** 
W `meteoSync.cpp` zadanie pobierania pogody (Core 0) wykonuje długie żądanie HTTPS. Jeśli WiFi zostanie rozłączone *w trakcie* trwania sesji SSL, `WiFiClientSecure` może wejść w nieskończoną pętlę oczekiwania na dane, które nigdy nie nadejdą, blokując Rdzeń 0.

**Dlaczego jest podstępny:** 
Zależy od specyficznego zachowania routera i momentu zerwania linku. Większość testów polega na "braku WiFi na starcie", a nie "utracie WiFi podczas SSL handshake".

**Diff / Fix:**
Dodaj twardy `setTimeout` dla klienta WiFi przed rozpoczęciem żądania.
```cpp
// src/comms/meteoSync.cpp
client.setTimeout(10); // 10 sekund to max na operację sieciową
```

---

## 3. Wyścig o dane czujników (Race Condition)
**Ryzyko:** Medium

**Opis problemu:** 
Zadanie `onSensorRead` (Rdzeń 1) aktualizuje globalne zmienne z danymi (temperatura, wilgotność), a zadanie `EsptoGuition` (Rdzeń 1, ale inny interwał) lub `UI_Draw` je czyta. Mimo że to ten sam rdzeń, EventBus może wywłaszczyć rysowanie między bajtami zmiennej `float`.

**Dlaczego jest podstępny:** 
Float ma 4 bajty. Istnieje szansa (1 na milion), że rysowanie odczyta 2 bajty "starej" temperatury i 2 bajty "nowej", co zaowocuje kompletnie absurdalnym odczytem na ekranie (np. 1256.4 stopnia) przez ułamek sekundy.

**Diff / Fix:**
Użyj typów atomowych `std::atomic<float>` dla współdzielonych wyników sensorów.
```cpp
// include/sensors/SensorState.h
- float temperature;
+ std::atomic<float> temperature;
```

---

## 4. Efekt "Drżenia" Alarma (Jitter)
**Ryzyko:** Low / Medium

**Opis problemu:** 
Przy przejściu z minuty 08:59:59 na 09:00:00, jeśli `onClockTick` spóźni się o 10ms (bo np. I2C było zajęte), a logika alarmu sprawdzi czas w 09:00:00.010, może dojść do podwójnego wyzwolenia alarmu w tej samej sekundzie lub całkowitego pominięcia, jeśli flaga `lastTriggerDay` nie zostanie ustawiona natychmiast.

**Dlaczego jest podstępny:** 
Zależy od milisekundowych opóźnień w pętli `EventBus`.

**Diff / Fix:**
Logika alarmu powinna sprawdzać nie tylko "czy jest ta godzina", ale "czy minęła ta godzina i czy już dzisiaj dzwoniłem".
```cpp
// AlarmRuntime.cpp
if (currentMinute != lastAlarmMinute) {
    lastAlarmMinute = currentMinute;
    // trigger alarm
}
```

---

## 5. Blokada magistrali przez "Stuck SCL"
**Ryzyko:** Medium (Produkcyjne)

**Opis problemu:** 
Jeśli jeden z czujników (np. AHT21) zostanie zakłócony (np. przez szum z zasilacza) w trakcie transmisji, może zostawić linię SCL w stanie niskim. Twoja funkcja `lock()` nie ma mechanizmu "Bus Reset", więc cała magistrala I2C umiera do czasu twardego restartu.

**Dlaczego jest podstępny:** 
W laboratorium na krótkich kablach się nie zdarza. Na produkcji, przy dłuższych ścieżkach lub zakłóceniach, zdarza się raz na kilka dni.

**Diff / Fix:**
W `I2cShared::lock()`, jeśli timeout zostanie przekroczony kilkakrotnie, wykonaj "Manual SCL Toggling" (wysłanie 9 impulsów zegara na pinie SCL), aby zmusić sensor do zwolnienia szyny.
```cpp
// src/drivers/I2C_bus_shared.cpp
if (lock_failed_many_times) {
    performManualBusReset();
}
```
