// ============================================================================
// PMS_Czujnik.cpp – Sterownik czujnika pyłu PMS5003
// ============================================================================
// Wykorzystuje bibliotekę PMserial (SerialPM).
// Nieblokujący automat stanów wzorowany na przykładzie producenta:
//   START_ATM_READ  →  WAIT_FOR_ATM_FRAME  →  WAIT_FOR_FACTORY
//   →  WAIT_FOR_FACTORY_FRAME  →  PUSH_DATA  →  WAIT_BETWEEN_CYCLES
// Dane trafiają do zmiennych extern zadeklarowanych w main.cpp.
// ============================================================================

#include "PMS_Czujnik.h"

// Włącz dostęp do bytes_read() i waited_ms()
#define PMS_DEBUG

// Biblioteka PMserial — nagłówek jest już w include/
#include <PMserial.h>

// ============================================================================
// EXTERN – zmienne z main.cpp, do których wpisujemy odczyty
// ============================================================================

// Telemetria
extern uint16_t pms5003_errorCount_current;
extern uint16_t pms5003_errorCount_total;
extern uint16_t pms5003_bytesReceived;
extern uint32_t pms5003_lastFrameTime;
extern uint32_t pms5003_latency_ms;

// Dane bieżące — CF=1
extern uint16_t pms5003_PM1_0_CF1;
extern uint16_t pms5003_PM2_5_CF1;
extern uint16_t pms5003_PM10_CF1;

// Dane bieżące — ATM
extern uint16_t pms5003_PM1_0_ATM;
extern uint16_t pms5003_PM2_5_ATM;
extern uint16_t pms5003_PM10_ATM;

// Dane bieżące — cząstki
extern uint16_t pms5003_particleCount_0_3;
extern uint16_t pms5003_particleCount_0_5;
extern uint16_t pms5003_particleCount_1_0;
extern uint16_t pms5003_particleCount_2_5;
extern uint16_t pms5003_particleCount_5_0;
extern uint16_t pms5003_particleCount_10_0;

// Min/Max — CF=1
extern uint16_t pms5003_PM1_0_CF1_MIN;  extern uint16_t pms5003_PM1_0_CF1_MAX;
extern uint16_t pms5003_PM2_5_CF1_MIN;  extern uint16_t pms5003_PM2_5_CF1_MAX;
extern uint16_t pms5003_PM10_CF1_MIN;   extern uint16_t pms5003_PM10_CF1_MAX;

// Min/Max — ATM
extern uint16_t pms5003_PM1_0_ATM_MIN;  extern uint16_t pms5003_PM1_0_ATM_MAX;
extern uint16_t pms5003_PM2_5_ATM_MIN;  extern uint16_t pms5003_PM2_5_ATM_MAX;
extern uint16_t pms5003_PM10_ATM_MIN;   extern uint16_t pms5003_PM10_ATM_MAX;

// Min/Max — cząstki
extern uint16_t pms5003_particleCount_0_3_MIN;  extern uint16_t pms5003_particleCount_0_3_MAX;
extern uint16_t pms5003_particleCount_0_5_MIN;  extern uint16_t pms5003_particleCount_0_5_MAX;
extern uint16_t pms5003_particleCount_1_0_MIN;  extern uint16_t pms5003_particleCount_1_0_MAX;
extern uint16_t pms5003_particleCount_2_5_MIN;  extern uint16_t pms5003_particleCount_2_5_MAX;
extern uint16_t pms5003_particleCount_5_0_MIN;  extern uint16_t pms5003_particleCount_5_0_MAX;
extern uint16_t pms5003_particleCount_10_0_MIN; extern uint16_t pms5003_particleCount_10_0_MAX;

// ============================================================================
// WEWNĘTRZNE ZMIENNE MODUŁU (static - widoczne tylko w tym pliku)
// ============================================================================

// Instancja SerialPM na Serial1, piny RX=34 TX=13 (ESP32)
static SerialPM pms(PMSx003, PMS_RX, PMS_TX);

namespace {

constexpr unsigned long kFactoryDelayMs = 2000UL;
constexpr unsigned long kCycleDelayMs = 8000UL;
constexpr unsigned long kFrameTimeoutMs = 650UL;
constexpr uint8_t kFrameLength = 32;
constexpr uint8_t kPassiveReadCommand[] = {0x42, 0x4D, 0xE2, 0x00, 0x00, 0x01, 0x71};

enum class FramePollResult : uint8_t {
  InProgress,
  Success,
  Failure,
};

enum LoopState {
  START_ATM_READ,
  WAIT_FOR_ATM_FRAME,
  WAIT_FOR_FACTORY,
  WAIT_FOR_FACTORY_FRAME,
  PUSH_DATA,
  WAIT_BETWEEN_CYCLES,
};

static LoopState state = START_ATM_READ;
static unsigned long ts = 0;
static unsigned long requestStartedMs = 0;
static uint8_t frameBuffer[kFrameLength];
static uint8_t frameIndex = 0;

// Lokalne bufory na jeden cykl odczytu
static bool atm_ok = false;
static uint16_t pm01_atm = 0;
static uint16_t pm25_atm = 0;
static uint16_t pm10_atm = 0;
static uint16_t n0p3 = 0;
static uint16_t n0p5 = 0;
static uint16_t n1p0 = 0;
static uint16_t n2p5 = 0;
static uint16_t n5p0 = 0;
static uint16_t n10p0 = 0;

static bool fac_ok = false;
static uint16_t pm01_fac = 0;
static uint16_t pm25_fac = 0;
static uint16_t pm10_fac = 0;

// Liczniki telemetrii
static uint16_t total_readings = 0;
static uint16_t total_errors = 0;

static bool last_cycle_ok = false;
static uint32_t lastUpdateTime = 0;  // Timestamp ostatniej aktualizacji (ms)
static uint16_t cycleBytesReceived = 0;
static uint32_t cycleLatencyMs = 0;
static bool cycleHasSuccess = false;

static inline void updateVal(uint16_t& cur, uint16_t& mn, uint16_t& mx, uint16_t v) {
  cur = v;
  if (v < mn) mn = v;
  if (v > mx) mx = v;
}

static uint16_t readWord(const uint8_t* frame, uint8_t offset) {
  return (uint16_t(frame[offset]) << 8) | frame[offset + 1];
}

static bool isValidFrame(const uint8_t* frame) {
  if (frame[0] != 0x42 || frame[1] != 0x4D) {
    return false;
  }

  if (readWord(frame, 2) != 0x001C) {
    return false;
  }

  uint16_t checksum = readWord(frame, 30);
  uint16_t sum = 0;
  for (uint8_t i = 0; i < 30; ++i) {
    sum += frame[i];
  }

  return sum == checksum;
}

static void resetCycleMetrics() {
  cycleBytesReceived = 0;
  cycleLatencyMs = 0;
  cycleHasSuccess = false;
}

static void resetRequestParser() {
  requestStartedMs = 0;
  frameIndex = 0;
}

static bool beginRequest(unsigned long now) {
  Stream* serial = pms.getSerialPort();
  if (serial == nullptr) {
    pms.status = pms.ERROR_TIMEOUT;
    return false;
  }

  while (serial->available() > 0) {
    serial->read();
  }

  serial->write(kPassiveReadCommand, sizeof(kPassiveReadCommand));
  requestStartedMs = now;
  frameIndex = 0;
  return true;
}

static void recordSuccess(bool factoryRead, unsigned long now) {
  const uint16_t pm01 = readWord(frameBuffer, factoryRead ? 4 : 10);
  const uint16_t pm25 = readWord(frameBuffer, factoryRead ? 6 : 12);
  const uint16_t pm10 = readWord(frameBuffer, factoryRead ? 8 : 14);
  const uint16_t count0p3 = readWord(frameBuffer, 16);
  const uint16_t count0p5 = readWord(frameBuffer, 18);
  const uint16_t count1p0 = readWord(frameBuffer, 20);
  const uint16_t count2p5 = readWord(frameBuffer, 22);
  const uint16_t count5p0 = readWord(frameBuffer, 24);
  const uint16_t count10p0 = readWord(frameBuffer, 26);

  pms.status = pms.OK;
  pms.pm01 = pm01;
  pms.pm25 = pm25;
  pms.pm10 = pm10;
  pms.n0p3 = count0p3;
  pms.n0p5 = count0p5;
  pms.n1p0 = count1p0;
  pms.n2p5 = count2p5;
  pms.n5p0 = count5p0;
  pms.n10p0 = count10p0;

  if (factoryRead) {
    fac_ok = true;
    pm01_fac = pm01;
    pm25_fac = pm25;
    pm10_fac = pm10;
  } else {
    atm_ok = true;
    pm01_atm = pm01;
    pm25_atm = pm25;
    pm10_atm = pm10;
  }

  n0p3 = count0p3;
  n0p5 = count0p5;
  n1p0 = count1p0;
  n2p5 = count2p5;
  n5p0 = count5p0;
  n10p0 = count10p0;

  cycleHasSuccess = true;
  cycleBytesReceived = kFrameLength;
  cycleLatencyMs = now - requestStartedMs;
  pms5003_lastFrameTime = now;
}

static FramePollResult pumpFrame(bool factoryRead, unsigned long now) {
  Stream* serial = pms.getSerialPort();
  if (serial == nullptr) {
    pms.status = pms.ERROR_TIMEOUT;
    return FramePollResult::Failure;
  }

  while (serial->available() > 0) {
    const uint8_t byte = (uint8_t)serial->read();

    if (frameIndex == 0) {
      if (byte == 0x42) {
        frameBuffer[0] = byte;
        frameIndex = 1;
      }
      continue;
    }

    if (frameIndex == 1) {
      if (byte == 0x4D) {
        frameBuffer[1] = byte;
        frameIndex = 2;
      } else if (byte == 0x42) {
        frameBuffer[0] = byte;
        frameIndex = 1;
      } else {
        frameIndex = 0;
      }
      continue;
    }

    frameBuffer[frameIndex++] = byte;
    if (frameIndex == kFrameLength) {
      if (!isValidFrame(frameBuffer)) {
        pms.status = pms.ERROR_MSG_CKSUM;
        frameIndex = 0;
        return FramePollResult::Failure;
      }

      recordSuccess(factoryRead, now);
      frameIndex = 0;
      return FramePollResult::Success;
    }
  }

  if ((now - requestStartedMs) >= kFrameTimeoutMs) {
    pms.status = pms.ERROR_TIMEOUT;
    frameIndex = 0;
    return FramePollResult::Failure;
  }

  return FramePollResult::InProgress;
}

static void finalizeCycle(unsigned long now) {
  last_cycle_ok = atm_ok || fac_ok;
  lastUpdateTime = now;

  pms5003_errorCount_current = (atm_ok ? 0 : 1) + (fac_ok ? 0 : 1);
  pms5003_errorCount_total = total_errors;

  if (cycleHasSuccess) {
    pms5003_bytesReceived = cycleBytesReceived;
    pms5003_latency_ms = cycleLatencyMs;
    pms5003_lastFrameTime = now;
  } else {
    pms5003_bytesReceived = 0;
    pms5003_latency_ms = 0;
  }

  if (fac_ok) {
    updateVal(pms5003_PM1_0_CF1, pms5003_PM1_0_CF1_MIN, pms5003_PM1_0_CF1_MAX, pm01_fac);
    updateVal(pms5003_PM2_5_CF1, pms5003_PM2_5_CF1_MIN, pms5003_PM2_5_CF1_MAX, pm25_fac);
    updateVal(pms5003_PM10_CF1, pms5003_PM10_CF1_MIN, pms5003_PM10_CF1_MAX, pm10_fac);
  }

  if (atm_ok) {
    updateVal(pms5003_PM1_0_ATM, pms5003_PM1_0_ATM_MIN, pms5003_PM1_0_ATM_MAX, pm01_atm);
    updateVal(pms5003_PM2_5_ATM, pms5003_PM2_5_ATM_MIN, pms5003_PM2_5_ATM_MAX, pm25_atm);
    updateVal(pms5003_PM10_ATM, pms5003_PM10_ATM_MIN, pms5003_PM10_ATM_MAX, pm10_atm);
  }

  if (atm_ok || fac_ok) {
    updateVal(pms5003_particleCount_0_3, pms5003_particleCount_0_3_MIN, pms5003_particleCount_0_3_MAX, n0p3);
    updateVal(pms5003_particleCount_0_5, pms5003_particleCount_0_5_MIN, pms5003_particleCount_0_5_MAX, n0p5);
    updateVal(pms5003_particleCount_1_0, pms5003_particleCount_1_0_MIN, pms5003_particleCount_1_0_MAX, n1p0);
    updateVal(pms5003_particleCount_2_5, pms5003_particleCount_2_5_MIN, pms5003_particleCount_2_5_MAX, n2p5);
    updateVal(pms5003_particleCount_5_0, pms5003_particleCount_5_0_MIN, pms5003_particleCount_5_0_MAX, n5p0);
    updateVal(pms5003_particleCount_10_0, pms5003_particleCount_10_0_MIN, pms5003_particleCount_10_0_MAX, n10p0);
  }
}

}  // namespace

// ============================================================================
// HELPERY
// ============================================================================

uint32_t PMS5003Sensor::getLastUpdateTime() {
  return lastUpdateTime;
}

// Wymuś natychmiastowy cykl odczytu. Ustawiamy stan na START_ATM_READ
// i wywołujemy update(), aby rozpocząć odczyt bez czekania na kolejny loop().
void PMS5003Sensor::requestImmediateRead() {
  state = START_ATM_READ;
  ts = 0;
  resetRequestParser();
  PMS5003Sensor::update();
}

// ============================================================================
// PMS5003Sensor::begin()
// ============================================================================

void PMS5003Sensor::begin() {
  Serial.println(F("[PMS5003] Initializing on Serial1 (RX=34, TX=13)..."));
  pms.init();
  Serial.println(F("[PMS5003] Initialized!"));

  state = START_ATM_READ;
  ts = millis();
  requestStartedMs = 0;
  total_readings = 0;
  total_errors = 0;
  resetCycleMetrics();
  resetRequestParser();

  resetMinMax();
}

// ============================================================================
// PMS5003Sensor::resetMinMax()
// ============================================================================

void PMS5003Sensor::resetMinMax() {
  // CF=1
  pms5003_PM1_0_CF1_MIN = 9999;  pms5003_PM1_0_CF1_MAX = 0;
  pms5003_PM2_5_CF1_MIN = 9999;  pms5003_PM2_5_CF1_MAX = 0;
  pms5003_PM10_CF1_MIN = 9999;   pms5003_PM10_CF1_MAX = 0;
  // ATM
  pms5003_PM1_0_ATM_MIN = 9999;  pms5003_PM1_0_ATM_MAX = 0;
  pms5003_PM2_5_ATM_MIN = 9999;  pms5003_PM2_5_ATM_MAX = 0;
  pms5003_PM10_ATM_MIN = 9999;   pms5003_PM10_ATM_MAX = 0;
  // Cząstki
  pms5003_particleCount_0_3_MIN = 9999;  pms5003_particleCount_0_3_MAX = 0;
  pms5003_particleCount_0_5_MIN = 9999;  pms5003_particleCount_0_5_MAX = 0;
  pms5003_particleCount_1_0_MIN = 9999;  pms5003_particleCount_1_0_MAX = 0;
  pms5003_particleCount_2_5_MIN = 9999;  pms5003_particleCount_2_5_MAX = 0;
  pms5003_particleCount_5_0_MIN = 9999;  pms5003_particleCount_5_0_MAX = 0;
  pms5003_particleCount_10_0_MIN = 9999; pms5003_particleCount_10_0_MAX = 0;
}

// ============================================================================
// PMS5003Sensor::isOk()
// ============================================================================

bool PMS5003Sensor::isOk() {
  return last_cycle_ok;
}

// ============================================================================
// PMS5003Sensor::update()  - nieblokujący automat stanów
// ============================================================================

void PMS5003Sensor::update() {
  const unsigned long now = millis();

  switch (state) {
    case START_ATM_READ:
      resetCycleMetrics();
      atm_ok = false;
      fac_ok = false;
      ++total_readings;
      if (beginRequest(now)) {
        state = WAIT_FOR_ATM_FRAME;
      } else {
        ++total_errors;
        state = WAIT_FOR_FACTORY;
        ts = now;
      }
      break;

    case WAIT_FOR_ATM_FRAME: {
      const FramePollResult result = pumpFrame(false, now);
      if (result == FramePollResult::Success) {
        state = WAIT_FOR_FACTORY;
        ts = now;
      } else if (result == FramePollResult::Failure) {
        ++total_errors;
        atm_ok = false;
        state = WAIT_FOR_FACTORY;
        ts = now;
      }
      break;
    }

    case WAIT_FOR_FACTORY:
      if (now - ts >= kFactoryDelayMs) {
        ++total_readings;
        if (beginRequest(now)) {
          state = WAIT_FOR_FACTORY_FRAME;
        } else {
          ++total_errors;
          fac_ok = false;
          state = PUSH_DATA;
        }
      }
      break;

    case WAIT_FOR_FACTORY_FRAME: {
      const FramePollResult result = pumpFrame(true, now);
      if (result == FramePollResult::Success) {
        state = PUSH_DATA;
      } else if (result == FramePollResult::Failure) {
        ++total_errors;
        fac_ok = false;
        state = PUSH_DATA;
      }
      break;
    }

    case PUSH_DATA:
      finalizeCycle(now);
      ts = now;
      state = WAIT_BETWEEN_CYCLES;
      break;

    case WAIT_BETWEEN_CYCLES:
      if (now - ts >= kCycleDelayMs) {
        state = START_ATM_READ;
      }
      break;
  }
}
