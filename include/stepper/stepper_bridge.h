/*
 * stepper_bridge.h — MOST PC(USB-Serial) <-> STM32(soft-UART COBS).
 *
 * Rola: tłumaczy tekstowy protokół z apki PC (ten sam co Uno v2:
 * MOVE/GOTO/STEP/SPEED/STOP/STATUS/PING) na ramki 0xB1 do STM32
 * i odsyła DONE/STATUS z powrotem jako linie tekstowe.
 *
 * ZASADY NIEBLOKOWANIA (ten projekt łatwo rozsypać, więc):
 *  - update() nigdy nie czeka: max 8 linii PC na wywołanie, max 1 ramka
 *    na 100 ms (throttle), zero delay(), zero pętli while na Serial.
 *  - Stan współdzielony z hookiem STM32_Data żyje w TYM SAMYM tasku loop,
 *    więc brak mutexów i brak wyścigów (hook wołany synchronicznie
 *    z STM32data_update, update() mostu zaraz po nim).
 *  - Diagnostyka tylko przez LOG_I (nigdy Serial.print poza liniami
 *    protokołu — apka PC filtruje resztę).
 *  - STM32 nie ma trybu ciągłego: DIR:LEFT/RIGHT = jeden chunk (jak STEP).
 *
 * USUWANIE: skasuj include/stepper + src/stepper, 2 linijki STEPPER_BRIDGE
 * w AppBoot/AppLoop, blok STEPPER_BRIDGE w STM32_Data.* i flagę -I.
 */

#pragma once
#ifndef STEPPER_BRIDGE_H
#define STEPPER_BRIDGE_H

#include <stdint.h>

namespace StepperBridge {

// Rejestruje hook 0xB2/0xB3 w STM32_Data. Wołać raz z initSensors,
// PO STM32data_begin(). Drukuje jedną linię READY (handshake apki).
void begin();

// Pompa mostu. Wołać raz na obrót loop, ZARAZ PO STM32data_update().
// Koszt gdy idle: jedno sprawdzenie Serial.available().
void update();

// --- API ruchu (wspólne dla ścieżki PC-USB i Guitiona 0x3A) ---
// Kolejkuje ramke 0xB1 MOVE_REL/MOVE_ABS. Zwraca false gdy FIFO pelne
// (wywolujacy decyduje: PC drukuje ERR BUSY, Guition dostaje stan).
// Nie drukuje nic na Serial — odpowiedzi tekstowe robi tylko sciezka PC.
bool issueMoveRel(int32_t steps, uint16_t speed, uint16_t accel);
bool issueMoveAbs(int32_t pos, uint16_t speed, uint16_t accel);

// Ruch w stopniach — cel liczy STM32 z WLASNEGO spanu (truth).
// Wariant z PREEMPCJA dla EXECUTE: porzuca przestarzale chunki z FIFO,
// wstawia STOP przed ruchem, potem MOVE_DEG. Fast path gdy idle.
// Bezblokujace: tylko flagi + kolejka, wysylka w pumpTx (sloty 100 ms).
bool issueMoveDegFlush(uint16_t deg, uint16_t speed, uint16_t accel);

// Kalibracja w STM32 (truth): SET_ZERO adoptuje biezaca pozycje jako 0,
// SET_SPAN adoptuje ja jako 90 st (+ powrot do 0 podana predkoscia).
// Zwraca false gdy FIFO pelne.
bool issueSetZero();
bool issueSetSpan(uint16_t speed, uint16_t accel);
// Predkosc autonomicznego powrotu STM32 do zera (persist w truth).
// Wysylac przy zmianie (ZEGAR pilnuje ostatniego wyslania).
// Tor priorytetowy (jak STOP): preempcja kolejki go nie zjada.
// Zawsze przyjmowane (flaga az do wysylki w pumpTx).
bool issueSetSpeed(uint16_t speed);
// Cel powrotu STM32 po resecie (double-click presetu w Guitionie).
// Tor priorytetowy jak wyzej.
bool issueSetHome(uint16_t deg);
// Automat LD2410C we flash STM32 (przycisk na Guitionie).
// Tor priorytetowy jak wyzej.
bool issueSetRadar(bool en);
// Wybrany kat z UI (single-click/WYKONAJ) — persist we flash STM32.
// Tor priorytetowy jak wyzej.
bool issueSetTarget(uint16_t deg);
// Kalibracja w toku (Guition CAL_MODE): wstrzymaj automat radaru w STM32
// (en=0 gasi; brak odswiezen = auto-timeout po stronie STM32).
// Tor priorytetowy jak wyzej, bez persistencji.
bool issueCalMode(bool en);
// Czas auto-zjazdu radaru (Guition): presety 30/180/600/2700 s; STM32
// waliduje i zapisuje we flash, echo wraca REF. Tor priorytetowy.
bool issueSetAutoClose(uint16_t seconds);
// Czulosc OUT radaru (Guition, ekran radaru): 0=SREDNIA, 1=WYSOKA,
// 2=MALA; STM32 waliduje, zapisuje i aplikuje w module, echo w REF.
// Tor priorytetowy jak wyzej.
bool issueSetRadarSens(uint8_t code);
// Jednorazowy seed migracyjny NVS->STM32 (dwa frame'y FIFO).
// false = kolejka pelna, retry przy nastepnym starcie.
bool seedTargetSpeed(uint16_t deg, uint16_t speed);

// Zadanie ramy odniesienia 0xB4 (odpowiedz async: hook -> cache + forward).
// Zwraca false gdy FIFO pelne.
bool requestRef();

// STOP z priorytetem przed kolejka + czyszczenie FIFO (porzucone chunki juz
// nie pojada — zgodnie z semantyka STOP na STM32: xQueueReset + brak DONE).
// Niezawodny (flaga, nie FIFO). Zmiana vs v1: wczesniej kolejka zostawala
// i zalegle ruchy wykonywaly sie PO stopie.
void requestStop();

// Zadanie statusu 0xB1 0x04. Odpowiedz async: hook -> Serial (jezeli PC pytal)
// + forward pushStepperState() do Guitiona. Zwraca false gdy FIFO pelne.
bool requestStatus();

// Cache ostatniego znanego stanu STM32 (do odpowiedzi i forwardow).
int32_t lastKnownPos();
bool lastKnownBusy();
bool lastKnownValid();
// Rama odniesienia z STM32 (0xB4; flaga calOk tez z 0xB2 gdy 6-bajtowy).
// Jedyna prawda o kalibracji — ZEGAR jej nie liczy ani nie przechowuje.
int32_t refZero();
int32_t refSpan();
bool refCalOk();
bool refRadarEn();  // automat LD2410C z flash STM32 (default OFF)
uint16_t refTargetDeg(); // wybrany kat z REF v2 (0xFFFF = stary STM32, brak)
uint16_t refSpeed();     // predkosc auto-powrotu z REF v2 (0xFFFF = brak)
bool refValid();
uint16_t refHomeDeg();  // cel powrotu STM32 po resecie (default 0)
uint16_t refAutoCloseS(); // auto-zjazd radaru z REF v3 (0xFFFF = brak pola)
uint8_t refRadarSens();   // czulosc OUT radaru (bity 2-3 flags REF; 0 = SREDNIA)
uint32_t refAgeMs();  // wiek ostatniego REF/flag; 0xFFFFFFFF gdy nigdy

}  // namespace StepperBridge

#endif  // STEPPER_BRIDGE_H
