/*
 * stepper_frames.h — KONTRAKT ramek steppera ZEGAR(ESP32) <-> STM32.
 *
 * Jedyny właściciel numerów typów i układu payloadu. Identyczny kontrakt
 * jest zaszyty w STM32 (STM32-BPM, include/silnik_krokowy/stepper_link.h).
 * Zmieniasz tu = zmieniasz tam. Nic innego w projekcie tego nie dotyka.
 *
 * Kanał: soft-UART GPIO16/17 @ 19200, COBS+CRC16 (jak reszta STM32_Data).
 *
 *   0xB1  ZEGAR -> STM32  KOMENDA   [cmd][args...]
 *     cmd 0x01 MOVE_REL : [steps int32 LE][speed u16 LE][accel u16 LE]  (9 B)
 *     cmd 0x02 MOVE_ABS : [pos   int32 LE][speed u16 LE][accel u16 LE]  (9 B)
 *     cmd 0x03 STOP     : (1 B)
 *     cmd 0x04 STATUS   : (1 B)  -> STM32 odpowiada 0xB2
 *     cmd 0x05 SET_ZERO : (1 B)  adopcja biezacej pozycji jako licznikowe 0
 *     cmd 0x06 SET_SPAN : [speed u16 LE][accel u16 LE] (5 B) adopcja 90 st
 *                         + powrot do 0 podana predkoscia
 *     cmd 0x07 GET_REF  : (1 B)  -> STM32 odpowiada 0xB4
 *     cmd 0x08 MOVE_DEG : [deg u16 LE][speed u16 LE][accel u16 LE] (7 B)
 *                         cel = zero + deg*span/90 z WLASNEGO spanu STM32
 *     cmd 0x09 SET_SPEED: [speed u16 LE] (3 B) predkosc autonomicznego
 *                         powrotu STM32 (persist w truth)
 *     cmd 0x0A SET_HOME : [deg u16 LE 0..360] (3 B) cel powrotu STM32
 *                         po resecie (persist w truth + push 0xB4)
 *   0xB2  STM32 -> ZEGAR STATUS    [busy 0/1][pos int32 LE]
 *                                  [flags: bit0=calOk]                (6 B)
 *   0xB3  STM32 -> ZEGAR DONE      [pos int32 LE]                       (4 B)
 *     DONE przychodzi DOKŁADNIE RAZ po zakończeniu ruchu.
 *   0xB4  STM32 -> ZEGAR REF       [zero int32 LE][span int32 LE]
 *                                  [flags: bit0=calOk][homeDeg u16 LE] (11 B)
 *     Prawda o ramie. Po GET_REF, po SET_ZERO/SET_SPAN/SET_HOME i raz po starcie.
 *
 * ZASADA WARIANTU B: ZEGAR NIE TRZYMA mapy stopni ani bazy pozycji.
 * Jedyne zrodlo prawdy to stepper_truth we flash STM32. ZEGAR forwarduje
 * komendy Guitiona (EXECUTE->MOVE_DEG, CAL_*->SET_*) i stan (0xB2/0xB4->0x3B).
 */

#pragma once
#ifndef STEPPER_FRAMES_H
#define STEPPER_FRAMES_H

#include <Arduino.h>

namespace StepperFrames {

constexpr uint8_t kTypeCmd    = 0xB1;
constexpr uint8_t kTypeStatus = 0xB2;
constexpr uint8_t kTypeDone   = 0xB3;
constexpr uint8_t kTypeRef    = 0xB4;

constexpr uint8_t kCmdMoveRel = 0x01;
constexpr uint8_t kCmdMoveAbs = 0x02;
constexpr uint8_t kCmdStop    = 0x03;
constexpr uint8_t kCmdStatus  = 0x04;
constexpr uint8_t kCmdSetZero = 0x05;
constexpr uint8_t kCmdSetSpan = 0x06;
constexpr uint8_t kCmdGetRef  = 0x07;
constexpr uint8_t kCmdMoveDeg = 0x08;
constexpr uint8_t kCmdSetSpeed = 0x09;
constexpr uint8_t kCmdSetHome = 0x0A;
constexpr uint8_t kCmdSetRadar = 0x0B;
constexpr uint8_t kCmdSetTarget = 0x0C;
constexpr uint8_t kCmdCalMode   = 0x0D;  /* [en]: wstrzymaj automat na czas kalibracji */
constexpr uint8_t kCmdSetAutoClose = 0x0E; /* [s u16 LE]: czas auto-zjazdu radaru */
constexpr uint8_t kCmdSetRadarSens = 0x0F; /* [kod]: czulosc OUT radaru 0/1/2 */
constexpr uint8_t kCmdSetOnOff = 0x10; /* [en]: "plynne wylaczenie" (dim 7-seg) */

constexpr uint8_t kRefFlagCalOk = 0x01;
constexpr uint8_t kRefFlagRadarEn = 0x02;
constexpr uint8_t kRefFlagOnOff = 0x10; /* bit4: plynne wylaczenie (STM32 truth) */

constexpr int32_t  kMaxMove   = 32768;
constexpr uint16_t kSpeedMin  = 1;
constexpr uint16_t kSpeedMax  = 1000;
constexpr uint16_t kSpeedDef  = 400;
constexpr uint16_t kAccelMin  = 50;
constexpr uint16_t kAccelMax  = 2000;
constexpr uint16_t kAccelDef  = 400;
constexpr int32_t  kChunkDef  = 2048;

/* Payload komendy ruchu: [cmd][i32 LE][u16 LE speed][u16 LE accel] = 9 B. */
inline uint8_t encodeMove(uint8_t cmd, int32_t arg, uint16_t speed,
                          uint16_t accel, uint8_t *out9) {
  if (arg > kMaxMove) arg = kMaxMove;
  if (arg < -kMaxMove) arg = -kMaxMove;
  if (speed < kSpeedMin || speed > kSpeedMax) speed = kSpeedDef;
  if (accel < kAccelMin || accel > kAccelMax) accel = kAccelDef;
  out9[0] = cmd;
  out9[1] = static_cast<uint8_t>(arg & 0xFF);
  out9[2] = static_cast<uint8_t>((arg >> 8) & 0xFF);
  out9[3] = static_cast<uint8_t>((arg >> 16) & 0xFF);
  out9[4] = static_cast<uint8_t>((arg >> 24) & 0xFF);
  out9[5] = static_cast<uint8_t>(speed & 0xFF);
  out9[6] = static_cast<uint8_t>((speed >> 8) & 0xFF);
  out9[7] = static_cast<uint8_t>(accel & 0xFF);
  out9[8] = static_cast<uint8_t>((accel >> 8) & 0xFF);
  return 9;
}

inline int32_t rdI32LE(const uint8_t *p) {
  return static_cast<int32_t>(
      static_cast<uint32_t>(p[0]) |
      (static_cast<uint32_t>(p[1]) << 8) |
      (static_cast<uint32_t>(p[2]) << 16) |
      (static_cast<uint32_t>(p[3]) << 24));
}

/* Payload MOVE_DEG: [0x08][deg u16 LE][speed u16 LE][accel u16 LE] = 7 B. */
inline uint8_t encodeDeg(uint16_t deg, uint16_t speed, uint16_t accel,
                         uint8_t *out7) {
  if (deg > 360U) deg = 360U;
  if (speed < kSpeedMin || speed > kSpeedMax) speed = kSpeedDef;
  if (accel < kAccelMin || accel > kAccelMax) accel = kAccelDef;
  out7[0] = kCmdMoveDeg;
  out7[1] = static_cast<uint8_t>(deg & 0xFF);
  out7[2] = static_cast<uint8_t>((deg >> 8) & 0xFF);
  out7[3] = static_cast<uint8_t>(speed & 0xFF);
  out7[4] = static_cast<uint8_t>((speed >> 8) & 0xFF);
  out7[5] = static_cast<uint8_t>(accel & 0xFF);
  out7[6] = static_cast<uint8_t>((accel >> 8) & 0xFF);
  return 7;
}

/* Payload SET_SPEED: [0x09][speed u16 LE] = 3 B. */
inline uint8_t encodeSetSpeed(uint16_t speed, uint8_t *out3) {
  if (speed < kSpeedMin || speed > kSpeedMax) speed = kSpeedDef;
  out3[0] = kCmdSetSpeed;
  out3[1] = static_cast<uint8_t>(speed & 0xFF);
  out3[2] = static_cast<uint8_t>((speed >> 8) & 0xFF);
  return 3;
}
/* Payload SET_SPAN: [0x06][speed u16 LE][accel u16 LE] = 5 B
 * (predkosc powrotu do 0 po kalibracji). */
inline uint8_t encodeSetSpan(uint16_t speed, uint16_t accel, uint8_t *out5) {
  if (speed < kSpeedMin || speed > kSpeedMax) speed = kSpeedDef;
  if (accel < kAccelMin || accel > kAccelMax) accel = kAccelDef;
  out5[0] = kCmdSetSpan;
  out5[1] = static_cast<uint8_t>(speed & 0xFF);
  out5[2] = static_cast<uint8_t>((speed >> 8) & 0xFF);
  out5[3] = static_cast<uint8_t>(accel & 0xFF);
  out5[4] = static_cast<uint8_t>((accel >> 8) & 0xFF);
  return 5;
}

/* Payload SET_HOME: [0x0A][deg u16 LE 0..360] = 3 B. */
inline uint8_t encodeSetHome(uint16_t deg, uint8_t *out3) {
  if (deg > 360U) deg = 360U;
  out3[0] = kCmdSetHome;
  out3[1] = static_cast<uint8_t>(deg & 0xFF);
  out3[2] = static_cast<uint8_t>((deg >> 8) & 0xFF);
  return 3;
}

/* Payload SET_RADAR: [0x0B][en] = 2 B (automat LD2410C, persist we flash STM32). */
inline uint8_t encodeSetRadar(bool en, uint8_t *out2) {
  out2[0] = kCmdSetRadar;
  out2[1] = en ? 1 : 0;
  return 2;
}

/* Payload SET_TARGET: [0x0C][deg u16 LE 0..360] = 3 B.
 * Wybrany kat z UI (single-click presetu) — persist we flash STM32. */
inline uint8_t encodeSetTarget(uint16_t deg, uint8_t *out3) {
  if (deg > 360U) deg = 360U;
  out3[0] = kCmdSetTarget;
  out3[1] = static_cast<uint8_t>(deg & 0xFF);
  out3[2] = static_cast<uint8_t>((deg >> 8) & 0xFF);
  return 3;
}

/* Payload CAL_MODE: [0x0D][en] = 2 B. Wstrzymuje automat radaru w STM32
 * na czas kalibracji (auto-timeout po stronie STM32; en=0 gasi od razu). */
inline uint8_t encodeCalMode(bool en, uint8_t *out2) {
  out2[0] = kCmdCalMode;
  out2[1] = en ? 1 : 0;
  return 2;
}

/* Payload SET_AUTOCLOSE: [0x0E][s u16 LE] = 3 B. Czas auto-zjazdu radaru
 * (presety 30/180/600/2700 s); walidacja presetow po stronie STM32 —
 * inna wartosc = fabryczna. Persist we flash truth + echo REF. */
inline uint8_t encodeSetAutoClose(uint16_t seconds, uint8_t *out3) {
  out3[0] = kCmdSetAutoClose;
  out3[1] = static_cast<uint8_t>(seconds & 0xFF);
  out3[2] = static_cast<uint8_t>((seconds >> 8) & 0xFF);
  return 3;
}

/* Payload SET_RADAR_SENS: [0x0F][kod 0/1/2] = 2 B. Czulosc OUT radaru
 * (0=SREDNIA, 1=WYSOKA, 2=MALA); walidacja po stronie STM32.
 * Persist we flash truth + aplikacja w module + echo REF. */
inline uint8_t encodeSetRadarSens(uint8_t code, uint8_t *out2) {
  out2[0] = kCmdSetRadarSens;
  out2[1] = static_cast<uint8_t>(code <= 2U ? code : 0U);
  return 2;
}

/* Payload SET_ONOFF: [0x10][en] = 2 B. "Plynne wylaczenie" — bramka
 * premium dimu 7-seg przy zjezdzie (persist we flash STM32, echo REF
 * bit4). Automat ruchu bez zmian. */
inline uint8_t encodeSetOnOff(bool en, uint8_t *out2) {
  out2[0] = kCmdSetOnOff;
  out2[1] = en ? 1 : 0;
  return 2;
}

/* Rama REF 0xB4 v3: [zero i32][span i32][flags][homeDeg u16][targetDeg u16]
 * [speed u16][autoCloseS u16].
 * flags: bit0 = calOk, bit1 = radarEn, bity 2-3 = czulosc OUT radaru,
 * bit4 = onoff ("plynne wylaczenie").
 * Stary STM32 wysyla 11/15 B — brakujace pola = 0xFFFF (nie adoptuj).
 * 9 B = bez homeDeg (klasyczne zero). */
struct RefState {
  int32_t zero = 0;
  int32_t span = 0;
  uint8_t flags = 0;
  uint16_t homeDeg = 0;
  uint16_t targetDeg = 0xFFFF;
  uint16_t speed = 0xFFFF;
  uint16_t autoCloseS = 0xFFFF;
  uint8_t radarSens = 0; // z bitow 2-3 flags (stary STM32 = 0 = SREDNIA)
  bool onoff = false;    // bit4 flags (stary STM32 = false = OFF)
};

inline bool parseRef(const uint8_t *p, uint8_t len, RefState &out) {
  if (p == nullptr || len < 9) return false;
  out.zero = rdI32LE(p);
  out.span = rdI32LE(p + 4);
  out.flags = p[8];
  out.radarSens = static_cast<uint8_t>((p[8] >> 2) & 0x03U);
  out.onoff = (p[8] & kRefFlagOnOff) != 0;
  out.homeDeg = (len >= 11)
      ? static_cast<uint16_t>(p[9] | (static_cast<uint16_t>(p[10]) << 8))
      : 0U;
  out.targetDeg = (len >= 13)
      ? static_cast<uint16_t>(p[11] | (static_cast<uint16_t>(p[12]) << 8))
      : 0xFFFFU;
  out.speed = (len >= 15)
      ? static_cast<uint16_t>(p[13] | (static_cast<uint16_t>(p[14]) << 8))
      : 0xFFFFU;
  out.autoCloseS = (len >= 17)
      ? static_cast<uint16_t>(p[15] | (static_cast<uint16_t>(p[16]) << 8))
      : 0xFFFFU;
  return true;
}

}  // namespace StepperFrames

#endif  // STEPPER_FRAMES_H
