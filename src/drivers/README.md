# drivers

Low-level hardware driver abstractions.

## Scope
- Bus-level shared drivers (I2C/SPI/GPIO wrappers)
- Device communication primitives

## Rules
- No UI or protocol policy here.
- Keep APIs small, deterministic, and reusable.

---

# ⚠️ KRYTYCZNA INFORMACJA: I2C SHARED BUS & WORKER TASK ⚠️

W pliku `I2C_bus_shared.cpp` znajduje się **w pełni przygotowany asynchroniczny silnik kolejkowania (Worker Task)**.

### OBECNY STATUS: 💤 UŚPIONY (DORMANT) 💤
Magistrala pracuje obecnie w trybie **SYNCHRONICZNYM** (blokującym), ponieważ zadanie workera nie jest uruchamiane w pętli `AppBoot`.

### JAK AKTYWOWAĆ?
Aby włączyć tryb asynchroniczny (kolejkowany), należy w `AppBoot.cpp` wywołać:
```cpp
I2cShared::startWorkerTask(TaskConfig::I2cWorkerTask::kPriority, TaskConfig::I2cWorkerTask::kCore);
```

### DLACZEGO TO JEST WAŻNE?
Kod został poprawiony i "ostreczowany" (v1.50). Jest gotowy do pracy, ale celowo nie został wpięty, aby utrzymać prostotę stabilnej wersji. **Uruchomienie go odciąży pętlę główną (UI) od czekania na fizyczną odpowiedź magistrali.**

---
