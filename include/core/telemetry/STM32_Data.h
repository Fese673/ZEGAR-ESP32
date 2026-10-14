/*
 * STM32_Data.h — komunikacja z STM32 (BPM / SpO2 / PPG / Brightness)
 *
 * Kanał: EspSoftwareSerial na GPIO16 (RX) / GPIO17 (TX), 19200 baud.
 * (SoftwareSerial — Serial2 jest zajęty przez Guition, Serial1 przez PMS5003.)
 * Protokół: COBS + CRC-16-CCITT (taki sam jak ZEGAR↔GUTION).
 *
 * Typy ramek:
 *   0xA0  STM32→ZEGAR  PPG diff     payload=[diff_lo, diff_hi]
 *   0xA1  STM32→ZEGAR  BPM/SpO2     payload=[bpm, spo2]
 *   0xA2  STM32→ZEGAR  ACK          payload=[ok, related_type]
 *   0xB0  ZEGAR→STM32  Brightness   payload=[value 0..100]
 */

#pragma once
#ifndef STM32_DATA_H
#define STM32_DATA_H

#include <Arduino.h>
#include <SoftwareSerial.h>

/*--- Dane od STM32 (aktualizowane przez STM32data_update) ---*/
extern int     bpmNumber;        // beats per minute
extern int     spo2Number;       // SpO2 (%)
extern bool    stmDataUpdated;   // true gdy przyszla ramka STM32 (heartbeat linku: PPG/BPM)
extern int16_t stm32PpgDiff;     // ostatnia wartość PPG diff

/*--- LD2410C radar (0xA3) ---*/
extern bool    ld2410Presence;        // true gdy ktoś wykryty (mov lub stat)
extern bool    ld2410MovingDetected;
extern uint16_t ld2410MovingDistance; // cm
extern uint8_t ld2410MovingEnergy;    // 0..100
extern bool    ld2410StationaryDetected;
extern uint16_t ld2410StationaryDistance;
extern uint8_t ld2410StationaryEnergy;
extern uint16_t ld2410DetectionDistance;
extern bool    ld2410DataUpdated;
extern uint32_t ld2410LastFrameMs;

/*--- PPG stream aktywny (true jeśli ramki 0xA0 odbierane są regularnie) ---*/
extern bool stm32PpgActive;

/*--- Inicjalizacja (setup). Otwiera EspSoftwareSerial na 19200 baud. ---*/
void STM32data_begin(int rxPin = 16, int txPin = 17, uint32_t baudRate = 19200UL);

/*--- Obsługa odbioru (wywoływać cyklicznie z loop()). ---*/
void STM32data_update();

/*--- Wyślij komendę jasności 7-seg do STM32 (COBS frame 0xB0). ---*/
void STM32data_sendBrightness(uint8_t percent);

/*--- STEPPER_BRIDGE: hook + nadawca ramek steppera (0xB1). ---
 * Hook dostaje NIEZNANE dispatcherowi typy ramek (0xB2/0xB3/DONE/STATUS
 * steppera). Wołany synchronicznie z STM32data_update() — ma być szybki
 * i nieblokujący. nullptr = brak mostu (zachowanie jak przed zmianą). */
typedef void (*STM32StepperHook_t)(uint8_t type, const uint8_t *payload,
                                   uint16_t len);
void STM32data_setStepperHook(STM32StepperHook_t hook);

/*--- STEPPER_BRIDGE: wyślij payload jako ramkę 0xB1 do STM32.
 * Zwraca false gdy kanał niegotowy (nie blokuje). ---*/
bool STM32data_sendStepperCmd(const uint8_t *payload, uint16_t len);

#endif // STM32_DATA_H
