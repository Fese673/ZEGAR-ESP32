// ============================================================================
// PMS_Czujnik.cpp – Sterownik czujnika pyłu PMS5003
// ============================================================================
// Wykorzystuje bibliotekę PMserial (SerialPM).
// Nieblokujący automat stanów wzorowany na przykładzie producenta:
//   Idle → WaitingAtmosphericFrame → WaitingFactoryDelay
//   → WaitingFactoryFrame → CoolingDown
// Dane są przechowywane wewnątrz modułu i udostępniane jako snapshoty
// za pomocą publicznego API PMS5003Sensor::getAtmospheric()/getFactory()/getParticleCounts()/getStats().
// ============================================================================

#include "PMS_Czujnik.h"

#include <PMserial.h>

#include "AppLog.h"
#include "Board_Pins.h"
static SerialPM pms(PMSx003, BoardPins::kPms5003Rx, BoardPins::kPms5003Tx);

namespace {

constexpr const char* TAG = "PMS5003";

constexpr unsigned long kFactoryRequestDelayMs = 2000UL;
constexpr unsigned long kCycleDelayMs = 8000UL;
constexpr unsigned long kFrameTimeoutMs = 650UL;
constexpr uint8_t kFrameLength = 32;
constexpr uint8_t kPassiveReadCommand[] = {0x42, 0x4D, 0xE2, 0x00, 0x00, 0x01, 0x71};

constexpr uint8_t kFrameHeader0 = 0;
constexpr uint8_t kFrameHeader1 = 1;
constexpr uint8_t kFrameLengthOffset = 2;
constexpr uint8_t kFrameChecksumOffset = 30;

constexpr uint8_t kFrameFactoryPm01Offset = 4;
constexpr uint8_t kFrameFactoryPm25Offset = 6;
constexpr uint8_t kFrameFactoryPm10Offset = 8;

constexpr uint8_t kFrameAtmosphericPm01Offset = 10;
constexpr uint8_t kFrameAtmosphericPm25Offset = 12;
constexpr uint8_t kFrameAtmosphericPm10Offset = 14;

constexpr uint8_t kFrameParticle0p3Offset = 16;
constexpr uint8_t kFrameParticle0p5Offset = 18;
constexpr uint8_t kFrameParticle1p0Offset = 20;
constexpr uint8_t kFrameParticle2p5Offset = 22;
constexpr uint8_t kFrameParticle5p0Offset = 24;
constexpr uint8_t kFrameParticle10p0Offset = 26;

enum class FramePollResult : uint8_t {
  InProgress,
  Success,
  Failure,
};

enum class Phase : uint8_t {
  Idle,
  WaitingAtmosphericFrame,
  WaitingFactoryDelay,
  WaitingFactoryFrame,
  CoolingDown,
};

struct SampleData {
  bool valid = false;
  PMS5003Sensor::MassReadings mass;
  PMS5003Sensor::ParticleCounts particles;
};

struct RuntimeState {
  bool enabled = true;
  Phase phase = Phase::Idle;
  unsigned long phaseStartedMs = 0;
  unsigned long requestStartedMs = 0;
  uint8_t frameBuffer[kFrameLength] = {};
  uint8_t frameIndex = 0;
  SampleData atmospheric;
  SampleData factory;
  SampleData latest;
  PMS5003Sensor::MassReadings publishedAtmospheric;
  PMS5003Sensor::MassReadings publishedFactory;
  PMS5003Sensor::ParticleCounts publishedParticles;
  PMS5003Sensor::Stats stats;
  uint32_t totalErrors = 0;
  bool lastCycleOk = false;
  uint32_t lastUpdateTime = 0;
  uint16_t cycleBytesReceived = 0;
  uint32_t cycleLatencyMs = 0;
  bool cycleHasSuccess = false;
};

static RuntimeState s;

static inline void updateRange(PMS5003Sensor::ValueRange& range, uint16_t value) {
  if (value < range.min) range.min = value;
  if (value > range.max) range.max = value;
}

static uint16_t readWord(const uint8_t* frame, uint8_t offset) {
  return (uint16_t(frame[offset]) << 8) | frame[offset + 1];
}

static bool isValidFrame(const uint8_t* frame) {
  if (frame[kFrameHeader0] != 0x42 || frame[kFrameHeader1] != 0x4D) {
    return false;
  }

  if (readWord(frame, kFrameLengthOffset) != 0x001C) {
    return false;
  }

  const uint16_t checksum = readWord(frame, kFrameChecksumOffset);
  uint16_t sum = 0;
  for (uint8_t i = 0; i < 30; ++i) {
    sum += frame[i];
  }

  return sum == checksum;
}

static void resetRequestParser() {
  s.requestStartedMs = 0;
  s.frameIndex = 0;
}

static void resetCycleMetrics() {
  s.cycleBytesReceived = 0;
  s.cycleLatencyMs = 0;
  s.cycleHasSuccess = false;
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
  s.requestStartedMs = now;
  s.frameIndex = 0;
  return true;
}

static void resetCycleSamples() {
  s.atmospheric = SampleData{};
  s.factory = SampleData{};
  s.latest = SampleData{};
}

static void prepareForNewCycle() {
  resetCycleSamples();
  resetCycleMetrics();
  resetRequestParser();
}

static void startAtmosphericRequest(unsigned long now) {
  prepareForNewCycle();

  if (beginRequest(now)) {
    s.phase = Phase::WaitingAtmosphericFrame;
    s.phaseStartedMs = now;
  } else {
    ++s.totalErrors;
    s.phase = Phase::WaitingFactoryDelay;
    s.phaseStartedMs = now;
  }
}

static void startFactoryRequest(unsigned long now) {
  resetRequestParser();

  if (beginRequest(now)) {
    s.phase = Phase::WaitingFactoryFrame;
    s.phaseStartedMs = now;
  } else {
    ++s.totalErrors;
    s.phase = Phase::CoolingDown;
    s.phaseStartedMs = now;
  }
}

static void recordSuccess(bool factoryRead, unsigned long now) {
  const uint16_t pm01 = readWord(s.frameBuffer, factoryRead ? kFrameFactoryPm01Offset : kFrameAtmosphericPm01Offset);
  const uint16_t pm25 = readWord(s.frameBuffer, factoryRead ? kFrameFactoryPm25Offset : kFrameAtmosphericPm25Offset);
  const uint16_t pm10 = readWord(s.frameBuffer, factoryRead ? kFrameFactoryPm10Offset : kFrameAtmosphericPm10Offset);

  constexpr uint16_t kPmMaxValidUgm3 = 500;
  if (pm01 > kPmMaxValidUgm3 || pm25 > kPmMaxValidUgm3 || pm10 > kPmMaxValidUgm3) {
    resetRequestParser();
    return;
  }

  const uint16_t count0p3 = readWord(s.frameBuffer, kFrameParticle0p3Offset);
  const uint16_t count0p5 = readWord(s.frameBuffer, kFrameParticle0p5Offset);
  const uint16_t count1p0 = readWord(s.frameBuffer, kFrameParticle1p0Offset);
  const uint16_t count2p5 = readWord(s.frameBuffer, kFrameParticle2p5Offset);
  const uint16_t count5p0 = readWord(s.frameBuffer, kFrameParticle5p0Offset);
  const uint16_t count10p0 = readWord(s.frameBuffer, kFrameParticle10p0Offset);

  SampleData& sample = factoryRead ? s.factory : s.atmospheric;
  sample.valid = true;
  sample.mass.pm01 = pm01;
  sample.mass.pm25 = pm25;
  sample.mass.pm10 = pm10;
  sample.particles.count0p3 = count0p3;
  sample.particles.count0p5 = count0p5;
  sample.particles.count1p0 = count1p0;
  sample.particles.count2p5 = count2p5;
  sample.particles.count5p0 = count5p0;
  sample.particles.count10p0 = count10p0;

  s.latest = sample;

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

  s.cycleHasSuccess = true;
  s.cycleBytesReceived += kFrameLength;
  s.cycleLatencyMs = now - s.requestStartedMs;
  s.stats.lastFrameTime = now;
}

static FramePollResult pumpFrame(bool factoryRead, unsigned long now) {
  Stream* serial = pms.getSerialPort();
  if (serial == nullptr) {
    pms.status = pms.ERROR_TIMEOUT;
    resetRequestParser();
    return FramePollResult::Failure;
  }

  while (serial->available() > 0) {
    const uint8_t byte = (uint8_t)serial->read();

    if (s.frameIndex == 0) {
      if (byte == 0x42) {
        s.frameBuffer[0] = byte;
        s.frameIndex = 1;
      }
      continue;
    }

    if (s.frameIndex == 1) {
      if (byte == 0x4D) {
        s.frameBuffer[1] = byte;
        s.frameIndex = 2;
      } else if (byte == 0x42) {
        s.frameBuffer[0] = byte;
        s.frameIndex = 1;
      } else {
        s.frameIndex = 0;
      }
      continue;
    }

    s.frameBuffer[s.frameIndex++] = byte;
    if (s.frameIndex == kFrameLength) {
      if (!isValidFrame(s.frameBuffer)) {
        pms.status = pms.ERROR_MSG_CKSUM;
        resetRequestParser();
        return FramePollResult::Failure;
      }

      recordSuccess(factoryRead, now);
      resetRequestParser();
      return FramePollResult::Success;
    }
  }

  if ((now - s.requestStartedMs) >= kFrameTimeoutMs) {
    pms.status = pms.ERROR_TIMEOUT;
    resetRequestParser();
    return FramePollResult::Failure;
  }

  return FramePollResult::InProgress;
}

static void finalizeCycle(unsigned long now) {
  s.lastCycleOk = s.atmospheric.valid || s.factory.valid;
  s.lastUpdateTime = now;

  s.stats.errorCountCurrent = (s.atmospheric.valid ? 0 : 1) + (s.factory.valid ? 0 : 1);
  s.stats.errorCountTotal = s.totalErrors;

  if (s.cycleHasSuccess) {
    s.stats.bytesReceived = s.cycleBytesReceived;
    s.stats.latencyMs = s.cycleLatencyMs;
  } else {
    s.stats.bytesReceived = 0;
    s.stats.latencyMs = 0;
  }

  if (s.factory.valid) {
    s.publishedFactory = s.factory.mass;
    updateRange(s.stats.factoryPm01, s.factory.mass.pm01);
    updateRange(s.stats.factoryPm25, s.factory.mass.pm25);
    updateRange(s.stats.factoryPm10, s.factory.mass.pm10);
  }

  if (s.atmospheric.valid) {
    s.publishedAtmospheric = s.atmospheric.mass;
    updateRange(s.stats.atmosphericPm01, s.atmospheric.mass.pm01);
    updateRange(s.stats.atmosphericPm25, s.atmospheric.mass.pm25);
    updateRange(s.stats.atmosphericPm10, s.atmospheric.mass.pm10);
  }

  if (s.latest.valid) {
    s.publishedParticles = s.latest.particles;
    updateRange(s.stats.particle0p3, s.latest.particles.count0p3);
    updateRange(s.stats.particle0p5, s.latest.particles.count0p5);
    updateRange(s.stats.particle1p0, s.latest.particles.count1p0);
    updateRange(s.stats.particle2p5, s.latest.particles.count2p5);
    updateRange(s.stats.particle5p0, s.latest.particles.count5p0);
    updateRange(s.stats.particle10p0, s.latest.particles.count10p0);
  }
}

}  // namespace

// ============================================================================
// HELPERY
// ============================================================================

uint32_t PMS5003Sensor::getLastUpdateTime() {
  return s.lastUpdateTime;
}

bool PMS5003Sensor::isEnabled() {
  return s.enabled;
}

void PMS5003Sensor::setEnabled(bool enabled) {
  if (s.enabled == enabled) {
    return;
  }

  s.enabled = enabled;
  s.lastCycleOk = false;
  s.phase = Phase::Idle;
  s.phaseStartedMs = millis();
  resetRequestParser();
  resetCycleMetrics();
}

PMS5003Sensor::MassReadings PMS5003Sensor::getAtmospheric() {
  return s.publishedAtmospheric;
}

PMS5003Sensor::MassReadings PMS5003Sensor::getFactory() {
  return s.publishedFactory;
}

PMS5003Sensor::ParticleCounts PMS5003Sensor::getParticleCounts() {
  return s.publishedParticles;
}

PMS5003Sensor::Stats PMS5003Sensor::getStats() {
  return s.stats;
}

// Wymuś natychmiastowy cykl odczytu. Zaczynamy od ATM i dajemy update()
// szansę od razu odebrać dane, jeśli frame już czeka w buforze.
void PMS5003Sensor::requestImmediateRead() {
  if (!s.enabled) {
    return;
  }

  startAtmosphericRequest(millis());
  PMS5003Sensor::update();
}

// ============================================================================
// PMS5003Sensor::begin()
// ============================================================================

void PMS5003Sensor::begin() {
  LOG_I(TAG, "action=init port=Serial1 rx=%u tx=%u", (unsigned)BoardPins::kPms5003Rx, (unsigned)BoardPins::kPms5003Tx);
  pms.init();
  LOG_I(TAG, "status=ready");

  s = RuntimeState{};
  s.phase = Phase::Idle;
  s.phaseStartedMs = millis();

  resetMinMax();
}

// ============================================================================
// PMS5003Sensor::resetMinMax()
// ============================================================================

void PMS5003Sensor::resetMinMax() {
  // CF=1
  s.stats.factoryPm01 = PMS5003Sensor::ValueRange{};
  s.stats.factoryPm25 = PMS5003Sensor::ValueRange{};
  s.stats.factoryPm10 = PMS5003Sensor::ValueRange{};
  // ATM
  s.stats.atmosphericPm01 = PMS5003Sensor::ValueRange{};
  s.stats.atmosphericPm25 = PMS5003Sensor::ValueRange{};
  s.stats.atmosphericPm10 = PMS5003Sensor::ValueRange{};
  // Cząstki
  s.stats.particle0p3 = PMS5003Sensor::ValueRange{};
  s.stats.particle0p5 = PMS5003Sensor::ValueRange{};
  s.stats.particle1p0 = PMS5003Sensor::ValueRange{};
  s.stats.particle2p5 = PMS5003Sensor::ValueRange{};
  s.stats.particle5p0 = PMS5003Sensor::ValueRange{};
  s.stats.particle10p0 = PMS5003Sensor::ValueRange{};
}

// ============================================================================
// PMS5003Sensor::isOk()
// ============================================================================

bool PMS5003Sensor::isOk() {
  return s.enabled && s.lastCycleOk;
}

// ============================================================================
// PMS5003Sensor::update()  - nieblokujący automat stanów
// ============================================================================

void PMS5003Sensor::update() {
  const unsigned long now = millis();

  if (!s.enabled) {
    if (s.phase != Phase::Idle) {
      s.phase = Phase::Idle;
      s.phaseStartedMs = now;
      resetRequestParser();
      resetCycleMetrics();
    }
    return;
  }

  switch (s.phase) {
    case Phase::Idle:
      startAtmosphericRequest(now);
      break;

    case Phase::WaitingAtmosphericFrame: {
      const FramePollResult result = pumpFrame(false, now);
      if (result == FramePollResult::Success) {
        s.phase = Phase::WaitingFactoryDelay;
        s.phaseStartedMs = now;
      } else if (result == FramePollResult::Failure) {
        ++s.totalErrors;
        s.phase = Phase::WaitingFactoryDelay;
        s.phaseStartedMs = now;
      }
      break;
    }

    case Phase::WaitingFactoryDelay:
      if (now - s.phaseStartedMs >= kFactoryRequestDelayMs) {
        startFactoryRequest(now);
      }
      break;

    case Phase::WaitingFactoryFrame: {
      const FramePollResult result = pumpFrame(true, now);
      if (result == FramePollResult::Success) {
        finalizeCycle(now);
        s.phase = Phase::CoolingDown;
        s.phaseStartedMs = now;
      } else if (result == FramePollResult::Failure) {
        ++s.totalErrors;
        finalizeCycle(now);
        s.phase = Phase::CoolingDown;
        s.phaseStartedMs = now;
      }
      break;
    }

    case Phase::CoolingDown:
      if (now - s.phaseStartedMs >= kCycleDelayMs) {
        s.phase = Phase::Idle;
      }
      break;
  }
}
