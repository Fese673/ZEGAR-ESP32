/*
 * stepper_bridge.cpp — implementacja mostu PC <-> STM32.
 *
 * Protokół PC (USB Serial, baud z firmware — patrz GUI profil ESP32):
 *   MOVE:kroki,speed[,accel] -> ramka 0xB1 MOVE_REL + "OK JOB ..." + "DONE POS=n"
 *   GOTO:poz,speed[,accel]   -> ramka 0xB1 MOVE_ABS + jak wyżej
 *   STEP:n                   -> ramka 0xB1 MOVE_REL (speed/accel zapamiętane)
 *   SPEED:x / ACCEL:x        -> tylko lokalnie (do STEP/DIR), bez ramki
 *   CHUNK:n                  -> wielkość kroku dla DIR, bez ramki
 *   DIR:LEFT/RIGHT           -> jeden chunk (most jest krokowy)
 *   STOP / DIR:STOP          -> ramka STOP + natychmiast "OK STOP"
 *   STATUS                   -> ramka STATUS_REQ, odpowiedź async "ST ..."
 *                              (timeout 500 ms -> odpowiedź z cache + CACHED=1)
 *   MODE:STEP                -> OK (tryb natywny)
 *   MODE:CONT                -> OK MODE=STEP NOTE=bridge-step-only (szczerze)
 *   PING                     -> PONG (lokalnie, bez ramki — szybki handshake)
 *
 * Ramki TX do STM32 idą przez STM32data_sendStepperCmd() (typ 0xB1),
 * odpowiedzi 0xB2/0xB3/0xA2 wracają hookiem STM32data_setStepperHook().
 */

#include "stepper_bridge.h"

#include <Arduino.h>
#include <string.h>

#include "AppLog.h"
#include "comms/esp_to_gution/EsptoGuitionState.h"
#include "STM32_Data.h"
#include "stepper_frames.h"

namespace StepperBridge {

// Publiczne API mostu (definicje na dole pliku). Sciezka PC uzywa ich tak
// samo jak sciezka Guitiona 0x3A — jeden punkt wejscia do kolejki TX.
bool issueMoveRel(int32_t steps, uint16_t speed, uint16_t accel);
bool issueMoveAbs(int32_t pos, uint16_t speed, uint16_t accel);
bool issueMoveDegFlush(uint16_t deg, uint16_t speed, uint16_t accel);
bool issueSetZero();
bool issueSetSpan(uint16_t speed, uint16_t accel);
bool issueSetSpeed(uint16_t speed);
bool issueSetHome(uint16_t deg);
bool issueSetRadar(bool en);
bool issueSetTarget(uint16_t deg);
bool issueCalMode(bool en);
bool issueSetAutoClose(uint16_t seconds);
bool issueSetRadarSens(uint8_t code);
bool seedTargetSpeed(uint16_t deg, uint16_t speed);
bool requestRef();
void requestStop();
bool requestStatus();
int32_t lastKnownPos();
bool lastKnownBusy();
bool lastKnownValid();
int32_t refZero();
int32_t refSpan();
bool refCalOk();
bool refRadarEn();
uint16_t refTargetDeg();
uint16_t refSpeed();
bool refValid();
uint16_t refHomeDeg();
uint32_t refAgeMs();

namespace {

constexpr const char *kTag = "STEPPER";
constexpr uint8_t kMaxLinesPerUpdate = 8;
constexpr uint8_t kLineBufSize = 64;
constexpr uint32_t kTxMinGapMs = 100;     // max 10 ramek/s (ochrona kolejki STM32)
constexpr uint32_t kStatusTimeoutMs = 500;
constexpr uint8_t kQueueDepth = 4;        // FIFO ramek (STOP omija kolejkę)

struct PendingTx {
  uint8_t payload[9];
  uint8_t len = 0;
  bool has = false;
};

char s_line[kLineBufSize + 1];
uint8_t s_lineLen = 0;

PendingTx s_queue[kQueueDepth];
uint8_t s_qHead = 0;
uint8_t s_qTail = 0;
uint8_t s_qCount = 0;
bool s_stopPriority = false;  // STOP do wysłania przed kolejką
// Tor priorytetowy preferencji (SET_SPEED / SET_HOME, max 9 B): flush
// preempcji czysci TYLKO kolejke, wiec preferencje dla STM32 nie gina razem
// ze starymi chunkami (bug: sztywne 400 w auto-home). Flaga trzyma az do
// faktycznej wysylki; ostatni zapis wygrywa (oba idempotentne).
bool s_prioPending = false;
uint8_t s_prioPayload[9] = {0};
uint8_t s_prioLen = 0;
uint32_t s_lastTxMs = 0;

uint16_t s_speed = StepperFrames::kSpeedDef;
uint16_t s_accel = StepperFrames::kAccelDef;
int32_t s_chunk = StepperFrames::kChunkDef;

// Cache ostatniego znanego stanu STM32 (do STATUS i STOP).
bool s_stmBusy = false;
int32_t s_stmPos = 0;
bool s_stmKnown = false;
// Rama odniesienia z STM32 (truth): jedyna prawda o kalibracji.
// 0xB4 niesie zero/span/flage; 6-bajtowy 0xB2 odswieza sama flage.
// Waznosc = dostalismy 0xB4 przynajmniej raz (po starcie prosimy sami).
int32_t s_refZero = 0;
int32_t s_refSpan = 0;
bool s_refOk = false;
bool s_refRadarEn = false;
uint16_t s_refTargetDeg = 0xFFFF; // wybrany kat (REF v2; 0xFFFF = brak pola)
uint16_t s_refSpeed = 0xFFFF;     // predkosc auto-powrotu (REF v2; 0xFFFF = brak)
bool s_refValid = false;
uint16_t s_refHomeDeg = 0; // cel powrotu STM32 po resecie (z 0xB4, default 0)
uint16_t s_refAutoCloseS = 0xFFFF; // auto-zjazd radaru (REF v3; 0xFFFF = brak)
uint8_t s_refRadarSens = 0; // czulosc OUT radaru (bity 2-3 flags REF)
uint32_t s_refMs = 0;
bool s_statusPending = false;
uint32_t s_statusDeadline = 0;

uint32_t s_rxCmd = 0;   // liczniki do LOG_I (debug bez grzebania w UART)
uint32_t s_rxBad = 0;
uint32_t s_txFrames = 0;

uint32_t clampU32(uint32_t v, uint32_t lo, uint32_t hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// Parsuje "a[,b[,c]]" (liczby dziesiętne, a może być ujemne).
// Zwraca liczbę pól (1..3), śmieci -> 0.
uint8_t parseCsv3(const char *s, int32_t &a, int32_t &b, int32_t &c) {
  char *end = nullptr;
  long v1 = strtol(s, &end, 10);
  if (end == s) return 0;
  a = static_cast<int32_t>(v1);
  if (*end == '\0') return 1;
  if (*end != ',') return 0;
  long v2 = strtol(end + 1, &end, 10);
  if (*end != ',' && *end != '\0') return 0;
  b = static_cast<int32_t>(v2);
  if (*end == '\0') return 2;
  long v3 = strtol(end + 1, &end, 10);
  if (*end != '\0') return 0;
  c = static_cast<int32_t>(v3);
  return 3;
}

bool queuePush(const uint8_t *payload, uint8_t len) {
  if (s_qCount >= kQueueDepth) return false;
  memcpy(s_queue[s_qTail].payload, payload, len);
  s_queue[s_qTail].len = len;
  s_queue[s_qTail].has = true;
  s_qTail = static_cast<uint8_t>((s_qTail + 1) % kQueueDepth);
  ++s_qCount;
  return true;
}

bool queuePop(PendingTx &out) {
  if (s_qCount == 0) return false;
  out = s_queue[s_qHead];
  s_qHead = static_cast<uint8_t>((s_qHead + 1) % kQueueDepth);
  --s_qCount;
  return true;
}

// Jedyna funkcja dotykająca soft-UART. Max 1 ramka na wywołanie,
// nie częściej niż kTxMinGapMs. Kolejnosc: STOP, SET_SPEED, kolejka.
void pumpTx(uint32_t now) {
  if (now - s_lastTxMs < kTxMinGapMs) return;
  if (s_stopPriority) {
    const uint8_t stop = StepperFrames::kCmdStop;
    if (STM32data_sendStepperCmd(&stop, 1)) {
      s_stopPriority = false;
      s_lastTxMs = now;
      ++s_txFrames;
    }
    return;
  }
  if (s_prioPending) {
    if (STM32data_sendStepperCmd(s_prioPayload, s_prioLen)) {
      s_prioPending = false;
      s_lastTxMs = now;
      ++s_txFrames;
    }
    return;
  }
  PendingTx tx;
  if (!queuePop(tx)) return;
  if (STM32data_sendStepperCmd(tx.payload, tx.len)) {
    s_lastTxMs = now;
    ++s_txFrames;
  } else {
    // Nadawca zajęty — oddaj na początek kolejki, spróbujemy za 100 ms.
    // (sendStepperCmd nie blokuje, więc to tylko pełny ring TX.)
    for (uint8_t i = s_qCount; i > 0; --i) {
      s_queue[(s_qHead + i) % kQueueDepth] = s_queue[(s_qHead + i - 1) % kQueueDepth];
    }
    s_queue[s_qHead] = tx;
    ++s_qCount;
  }
}

bool tryEnqueueMove(uint8_t cmd, int32_t arg, uint16_t speed, uint16_t accel) {
  uint8_t payload[9];
  const uint8_t len =
      StepperFrames::encodeMove(cmd, arg, speed, accel, payload);
  return queuePush(payload, len);
}

void sendMoveFrame(uint8_t cmd, int32_t arg) {
  if (!tryEnqueueMove(cmd, arg, s_speed, s_accel)) {
    Serial.println("ERR BUSY");
    return;
  }
  Serial.print("OK JOB ");
  Serial.print(cmd == StepperFrames::kCmdMoveAbs ? "GOTO=" : "MOVE=");
  Serial.print(arg);
  Serial.print(" SPEED=");
  Serial.print(s_speed);
  Serial.print(" ACCEL=");
  Serial.println(s_accel);
}

void handlePcLine(char *line) {
  // trim + upper (in place, bufor nasz)
  while (*line == ' ' || *line == '\t') ++line;
  size_t n = strlen(line);
  while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t')) line[--n] = '\0';
  for (size_t i = 0; i < n; ++i) {
    if (line[i] >= 'a' && line[i] <= 'z') line[i] -= 32;
  }
  if (n == 0) return;
  ++s_rxCmd;

  if (strcmp(line, "PING") == 0) {
    Serial.println("PONG");
  } else if (strcmp(line, "STATUS") == 0 || strcmp(line, "STATUS?") == 0) {
    if (requestStatus()) {
      s_statusPending = true;
      s_statusDeadline = millis() + kStatusTimeoutMs;
    } else if (s_stmKnown) {
      Serial.print("ST MODE=STEP SPEED=");
      Serial.print(s_speed);
      Serial.print(" POS=");
      Serial.print(s_stmPos);
      Serial.println(" CACHED=1");
    } else {
      Serial.println("ERR BUSY");
    }
  } else if (strcmp(line, "STOP") == 0 || strcmp(line, "DIR:STOP") == 0) {
    requestStop();  // ominie kolejkę w najbliższym pumpTx
    Serial.print("OK STOP POS=");
    Serial.println(s_stmPos);
  } else if (strcmp(line, "MODE:STEP") == 0) {
    Serial.print("OK MODE=STEP SPEED=");
    Serial.println(s_speed);
  } else if (strcmp(line, "MODE:CONT") == 0) {
    Serial.println("OK MODE=STEP NOTE=bridge-step-only");
  } else if (strcmp(line, "DIR:LEFT") == 0) {
    sendMoveFrame(StepperFrames::kCmdMoveRel, -s_chunk);
  } else if (strcmp(line, "DIR:RIGHT") == 0) {
    sendMoveFrame(StepperFrames::kCmdMoveRel, s_chunk);
  } else if (strncmp(line, "SPEED:", 6) == 0) {
    int32_t a = 0, b = 0, c = 0;
    if (!parseCsv3(line + 6, a, b, c)) {
      Serial.println("ERR BADNUM");
      ++s_rxBad;
      return;
    }
    s_speed = static_cast<uint16_t>(
        clampU32(static_cast<uint32_t>(a < 0 ? 0 : a),
                 StepperFrames::kSpeedMin, StepperFrames::kSpeedMax));
    Serial.print("OK MODE=STEP SPEED=");
    Serial.println(s_speed);
  } else if (strncmp(line, "ACCEL:", 6) == 0) {
    int32_t a = 0, b = 0, c = 0;
    if (!parseCsv3(line + 6, a, b, c)) {
      Serial.println("ERR BADNUM");
      ++s_rxBad;
      return;
    }
    s_accel = static_cast<uint16_t>(
        clampU32(static_cast<uint32_t>(a < 0 ? 0 : a),
                 StepperFrames::kAccelMin, StepperFrames::kAccelMax));
    Serial.print("OK MODE=STEP ACCEL=");
    Serial.println(s_accel);
  } else if (strncmp(line, "CHUNK:", 6) == 0) {
    int32_t a = 0, b = 0, c = 0;
    if (!parseCsv3(line + 6, a, b, c) || a <= 0 || a > 16384) {
      Serial.println("ERR BADNUM");
      ++s_rxBad;
      return;
    }
    s_chunk = a;
    Serial.print("OK CHUNK=");
    Serial.println(s_chunk);
  } else if (strncmp(line, "STEP:", 5) == 0) {
    int32_t a = 0, b = 0, c = 0;
    if (!parseCsv3(line + 5, a, b, c)) {
      Serial.println("ERR BADNUM");
      ++s_rxBad;
      return;
    }
    sendMoveFrame(StepperFrames::kCmdMoveRel, a);
  } else if (strncmp(line, "MOVE:", 5) == 0) {
    int32_t a = 0;
    int32_t b = s_speed;
    int32_t c = s_accel;
    const uint8_t fields = parseCsv3(line + 5, a, b, c);
    if (fields < 2) {
      Serial.println("ERR BADNUM");
      ++s_rxBad;
      return;
    }
    if (fields >= 2) {
      s_speed = static_cast<uint16_t>(clampU32(static_cast<uint32_t>(b < 0 ? 0 : b),
                                               StepperFrames::kSpeedMin,
                                               StepperFrames::kSpeedMax));
    }
    if (fields >= 3) {
      s_accel = static_cast<uint16_t>(clampU32(static_cast<uint32_t>(c < 0 ? 0 : c),
                                               StepperFrames::kAccelMin,
                                               StepperFrames::kAccelMax));
    }
    sendMoveFrame(StepperFrames::kCmdMoveRel, a);
  } else if (strncmp(line, "GOTO:", 5) == 0) {    int32_t a = 0;
    int32_t b = s_speed;
    int32_t c = s_accel;
    const uint8_t fields = parseCsv3(line + 5, a, b, c);
    if (fields < 2) {
      Serial.println("ERR BADNUM");
      ++s_rxBad;
      return;
    }
    if (fields >= 2) {
      s_speed = static_cast<uint16_t>(clampU32(static_cast<uint32_t>(b < 0 ? 0 : b),
                                               StepperFrames::kSpeedMin,
                                               StepperFrames::kSpeedMax));
    }
    if (fields >= 3) {
      s_accel = static_cast<uint16_t>(clampU32(static_cast<uint32_t>(c < 0 ? 0 : c),
                                               StepperFrames::kAccelMin,
                                               StepperFrames::kAccelMax));
    }
    sendMoveFrame(StepperFrames::kCmdMoveAbs, a);
  } else if (strcmp(line, "STEPREF?") == 0) {
    // Prawda z STM32 (0xB4): zero/span/flaga + wiek — read-only.
    EsptoGuition::printStepperRef();
  } else {
    Serial.print("ERR UNKNOWN:");
    Serial.println(line);
    ++s_rxBad;
  }
}

// Hook wołany synchronicznie z STM32data_update (ten sam task loop).
// Tylko szybkie przepisanie stanu + najwyżej 1 krótka linia na USB.
void onStmFrame(uint8_t type, const uint8_t *payload, uint16_t len) {
  if (type == StepperFrames::kTypeStatus) {
    if (payload == nullptr || len < 5) return;
    s_stmBusy = payload[0] != 0;
    s_stmPos = StepperFrames::rdI32LE(payload + 1);
    s_stmKnown = true;
    if (len >= 6) {
      // 6-bajtowy STATUS niesie flagi truth w STM32 (bit0 kalibracja, bit1 radar).
      s_refOk = (payload[5] & StepperFrames::kRefFlagCalOk) != 0;
      s_refRadarEn = (payload[5] & StepperFrames::kRefFlagRadarEn) != 0;
      s_refMs = millis();
    }
    if (s_statusPending) {
      s_statusPending = false;
      Serial.print("ST MODE=STEP SPEED=");
      Serial.print(s_speed);
      Serial.print(" ACCEL=");
      Serial.print(s_accel);
      Serial.print(" DIR=0 POS=");
      Serial.print(s_stmPos);
      Serial.print(" RUN=");
      Serial.println(s_stmBusy ? 1 : 0);
    }
  } else if (type == StepperFrames::kTypeDone) {
    if (payload == nullptr || len < 4) return;
    s_stmBusy = false;
    s_stmPos = StepperFrames::rdI32LE(payload);
    s_stmKnown = true;
    Serial.print("DONE POS=");
    Serial.println(s_stmPos);
    // DONE = pozycja koncowa: forward do Guitiona + DOKLADNY zapis NVS.
    EsptoGuition::pushStepperDone(s_stmPos);
    return;
  } else if (type == StepperFrames::kTypeRef) {
    StepperFrames::RefState ref;
    if (!StepperFrames::parseRef(payload, (uint8_t)len, ref)) return;
    s_refZero = ref.zero;
    s_refSpan = ref.span;
    s_refOk = (ref.flags & StepperFrames::kRefFlagCalOk) != 0;
    s_refRadarEn = (ref.flags & StepperFrames::kRefFlagRadarEn) != 0;
    s_refTargetDeg = ref.targetDeg; // 0xFFFF = stary STM32 bez pola
    s_refSpeed = ref.speed;         // 0xFFFF = stary STM32 bez pola
    s_refHomeDeg = ref.homeDeg;     // cel powrotu po resecie (len<11 -> 0 jak stare)
    s_refAutoCloseS = ref.autoCloseS; // auto-zjazd radaru (len<17 -> 0xFFFF)
    s_refRadarSens = ref.radarSens;   // czulosc OUT radaru (bity 2-3 flags)
    s_refValid = true;
    s_refMs = millis();
    LOG_I(kTag, "REF: zero=%ld span=%ld calOk=%d homeDeg=%u autoCloseS=%u",
          (long)s_refZero, (long)s_refSpan, s_refOk ? 1 : 0,
          (unsigned)s_refHomeDeg, (unsigned)s_refAutoCloseS);
  } else {
    return; // nieznany typ dla tego hooka — nic nie forwarduj
  }
  // Kazdy 0xB2/0xB4 to stan z jedynego zrodla prawdy (STM32): cache dla PC
  // + forward do Guitiona. Zrodlowo throttled (STATUS na zadanie, REF rzadko).
  EsptoGuition::pushStepperState();
  // ACK 0xA2 (related 0xB1) celowo ignorujemy na TAK — GUI nie czeka na
  // potwierdzenie, tylko na DONE. NACK (ok=0) lądujący w STM32_Data
  // dispatcherze też jest ignorowany żeby nie spamować USB.
}

}  // namespace

void begin() {
  s_lineLen = 0;
  STM32data_setStepperHook(onStmFrame);
  // Po starcie pytamy STM32 o stan i rame (prawda moze juz tam czekac —
  // STM32 wstaje szybciej i sam wysyla REF raz, ale prosba nie zaszkodzi).
  requestStatus();
  requestRef();
  LOG_I(kTag, "bridge ready (PC USB <-> STM32 0xB1/0xB2/0xB3/0xB4)");
  Serial.println("READY STEPPER-BRIDGE v1 HELP: MOVE:GOTO:STEP:SPEED:STOP:STATUS:PING");
}

bool issueMoveRel(int32_t steps, uint16_t speed, uint16_t accel) {
  return tryEnqueueMove(StepperFrames::kCmdMoveRel, steps, speed, accel);
}

bool issueMoveAbs(int32_t pos, uint16_t speed, uint16_t accel) {
  return tryEnqueueMove(StepperFrames::kCmdMoveAbs, pos, speed, accel);
}

bool issueMoveDegFlush(uint16_t deg, uint16_t speed, uint16_t accel) {
  uint8_t payload[7];
  const uint8_t len = StepperFrames::encodeDeg(deg, speed, accel, payload);
  if (!lastKnownBusy() && s_qCount == 0 && !s_stopPriority) {
    return queuePush(payload, len); // fast path: idle, od razu jazda
  }
  // Preempcja: porzuc przestarzale chunki, STOP (czysci tez STM32), potem ruch.
  // Kolejka pusta -> queuePush zawsze sie udaje (komenda nie ginie).
  s_qHead = 0;
  s_qTail = 0;
  s_qCount = 0;
  s_stopPriority = true;
  return queuePush(payload, len);
}

bool issueSetZero() {
  const uint8_t cmd = StepperFrames::kCmdSetZero;
  return queuePush(&cmd, 1);
}

bool issueSetSpan(uint16_t speed, uint16_t accel) {
  uint8_t payload[5];
  const uint8_t len = StepperFrames::encodeSetSpan(speed, accel, payload);
  return queuePush(payload, len);
}

bool issueSetSpeed(uint16_t speed) {
  s_prioLen = StepperFrames::encodeSetSpeed(speed, s_prioPayload);
  s_prioPending = true; // tor priorytetowy — flush kolejki go nie rusza
  return true;
}

bool issueSetHome(uint16_t deg) {
  s_prioLen = StepperFrames::encodeSetHome(deg, s_prioPayload);
  s_prioPending = true; // tor priorytetowy — flush kolejki go nie rusza
  return true;
}

bool issueSetRadar(bool en) {
  s_prioLen = StepperFrames::encodeSetRadar(en, s_prioPayload);
  s_prioPending = true; // tor priorytetowy — jak SET_HOME
  return true;
}

bool issueSetTarget(uint16_t deg) {
  s_prioLen = StepperFrames::encodeSetTarget(deg, s_prioPayload);
  s_prioPending = true; // tor priorytetowy — jak SET_HOME
  return true;
}

bool issueCalMode(bool en) {
  s_prioLen = StepperFrames::encodeCalMode(en, s_prioPayload);
  s_prioPending = true; // tor priorytetowy — bez persistencji (tylko STM32 RAM)
  return true;
}

bool issueSetAutoClose(uint16_t seconds) {
  s_prioLen = StepperFrames::encodeSetAutoClose(seconds, s_prioPayload);
  s_prioPending = true; // tor priorytetowy — persist + echo po stronie STM32
  return true;
}

bool issueSetRadarSens(uint8_t code) {
  s_prioLen = StepperFrames::encodeSetRadarSens(code, s_prioPayload);
  s_prioPending = true; // tor priorytetowy — persist + aplikacja w STM32
  return true;
}

/* Migracja NVS->STM32: dwa frame'y naraz (tor priorytetowy trzyma tylko
 * jeden), wiec ida kolejem FIFO. Wolane raz przy starcie, gdy FIFO puste. */
bool seedTargetSpeed(uint16_t deg, uint16_t speed) {
  if (s_qCount + 2 > kQueueDepth) return false;
  uint8_t p1[3];
  uint8_t p2[3];
  const uint8_t l1 = StepperFrames::encodeSetTarget(deg, p1);
  const uint8_t l2 = StepperFrames::encodeSetSpeed(speed, p2);
  if (!queuePush(p1, l1)) return false;
  if (!queuePush(p2, l2)) return false;
  return true;
}

bool requestRef() {
  const uint8_t req = StepperFrames::kCmdGetRef;
  return queuePush(&req, 1);
}

void requestStop() {
  // STOP znaczy stop: porzucone chunki z FIFO juz nie pojada (STM32 robi
  // xQueueReset po swojej stronie). Flaga = niezawodne, nie FIFO.
  s_qHead = 0;
  s_qTail = 0;
  s_qCount = 0;
  s_stopPriority = true;
}

bool requestStatus() {
  const uint8_t req = StepperFrames::kCmdStatus;
  return queuePush(&req, 1);
}

int32_t lastKnownPos() { return s_stmPos; }
bool lastKnownBusy() { return s_stmBusy; }
bool lastKnownValid() { return s_stmKnown; }
int32_t refZero() { return s_refZero; }
int32_t refSpan() { return s_refSpan; }
bool refCalOk() { return s_refOk; }
bool refRadarEn() { return s_refRadarEn; }
uint16_t refTargetDeg() { return s_refTargetDeg; }
uint16_t refSpeed() { return s_refSpeed; }
bool refValid() { return s_refValid; }
uint16_t refHomeDeg() { return s_refHomeDeg; }
uint16_t refAutoCloseS() { return s_refAutoCloseS; }
uint8_t refRadarSens() { return s_refRadarSens; }
uint32_t refAgeMs() {
  if (!s_refValid) return 0xFFFFFFFFUL;
  return millis() - s_refMs;
}

void update() {
  // 1. Linie z PC — ograniczona porcja na wywołanie.
  uint8_t lines = 0;
  while (lines < kMaxLinesPerUpdate && Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch < 0) break;
    if (ch == '\n' || ch == '\r') {
      if (s_lineLen > 0) {
        s_line[s_lineLen] = '\0';
        s_lineLen = 0;
        handlePcLine(s_line);
        ++lines;
      }
    } else if (s_lineLen < kLineBufSize) {
      s_line[s_lineLen++] = static_cast<char>(ch);
    } else {
      s_lineLen = 0;  // przepełnienie — porzuć linię, nie wieszaj się
      Serial.println("ERR TOOLONG");
      ++s_rxBad;
    }
  }

  // 2. Throttled TX do STM32 (max 1 ramka / 100 ms).
  pumpTx(millis());

  // 3. Timeout STATUS — odpowiedz z cache zamiast wisieć.
  if (s_statusPending && static_cast<int32_t>(millis() - s_statusDeadline) >= 0) {
    s_statusPending = false;
    if (s_stmKnown) {
      Serial.print("ST MODE=STEP SPEED=");
      Serial.print(s_speed);
      Serial.print(" POS=");
      Serial.print(s_stmPos);
      Serial.println(" CACHED=1");
    } else {
      Serial.println("ERR NOSTM32");
    }
  }
}

}  // namespace StepperBridge
