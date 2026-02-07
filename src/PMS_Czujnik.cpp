// ============================================================================
// PMS_Czujnik.cpp – Sterownik czujnika pyłu PMS5003
// ============================================================================
// Wykorzystuje bibliotekę PMserial (SerialPM).
// Nieblokujący automat stanów wzorowany na przykładzie producenta:
//   START_ATM_READ  →  WAIT_FOR_FACTORY  →  PUSH_DATA  →  WAIT_BETWEEN
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
// WEWNĘTRZNE ZMIENNE MODUŁU (static — widoczne tylko w tym pliku)
// ============================================================================

// Instancja SerialPM na Serial1, piny RX=34 TX=13 (ESP32)
static SerialPM pms(PMSx003, PMS_RX, PMS_TX);

// Automat stanów
enum LoopState {
  START_ATM_READ,
  WAIT_FOR_FACTORY,
  PUSH_DATA,
  WAIT_BETWEEN_CYCLES
};

static LoopState  state = START_ATM_READ;
static unsigned long ts = 0;

// Lokalne bufory na jeden cykl odczytu
static bool     atm_ok = false;
static uint16_t pm01_atm = 0, pm25_atm = 0, pm10_atm = 0;
static uint16_t n0p3 = 0, n0p5 = 0, n1p0 = 0, n2p5 = 0, n5p0 = 0, n10p0 = 0;

static bool     fac_ok = false;
static uint16_t pm01_fac = 0, pm25_fac = 0, pm10_fac = 0;

// Liczniki telemetrii
static uint16_t total_readings = 0;
static uint16_t total_errors   = 0;

static bool last_cycle_ok = false;
static uint32_t lastUpdateTime = 0;  // Timestamp ostatniej aktualizacji (ms)

// ============================================================================
// HELPERY: aktualizuj wartość + min/max
// ============================================================================

uint32_t PMS5003Sensor::getLastUpdateTime() {
  return lastUpdateTime;
}

// Wymuś natychmiastowy cykl odczytu. Ustawiamy stan na START_ATM_READ
// i wywołujemy update(), aby rozpocząć odczyt bez czekania na kolejny loop().
void PMS5003Sensor::requestImmediateRead() {
  state = START_ATM_READ;
  ts = 0;
  // Uruchom update od razu (wykona START_ATM_READ -> READ ATM)
  PMS5003Sensor::update();
}

static inline void updateVal(uint16_t &cur, uint16_t &mn, uint16_t &mx, uint16_t v) {
  cur = v;
  if (v < mn) mn = v;
  if (v > mx) mx = v;
}

// ============================================================================
// PMS5003Sensor::begin()
// ============================================================================

void PMS5003Sensor::begin() {
  Serial.println(F("[PMS5003] Initializing on Serial1 (RX=34, TX=13)..."));
  pms.init();
  Serial.println(F("[PMS5003] Initialized!"));

  state = START_ATM_READ;
  ts    = millis();
  total_readings = 0;
  total_errors   = 0;

  resetMinMax();
}

// ============================================================================
// PMS5003Sensor::resetMinMax()
// ============================================================================

void PMS5003Sensor::resetMinMax() {
  // CF=1
  pms5003_PM1_0_CF1_MIN = 9999;  pms5003_PM1_0_CF1_MAX = 0;
  pms5003_PM2_5_CF1_MIN = 9999;  pms5003_PM2_5_CF1_MAX = 0;
  pms5003_PM10_CF1_MIN  = 9999;  pms5003_PM10_CF1_MAX  = 0;
  // ATM
  pms5003_PM1_0_ATM_MIN = 9999;  pms5003_PM1_0_ATM_MAX = 0;
  pms5003_PM2_5_ATM_MIN = 9999;  pms5003_PM2_5_ATM_MAX = 0;
  pms5003_PM10_ATM_MIN  = 9999;  pms5003_PM10_ATM_MAX  = 0;
  // Cząstki
  pms5003_particleCount_0_3_MIN  = 9999;  pms5003_particleCount_0_3_MAX  = 0;
  pms5003_particleCount_0_5_MIN  = 9999;  pms5003_particleCount_0_5_MAX  = 0;
  pms5003_particleCount_1_0_MIN  = 9999;  pms5003_particleCount_1_0_MAX  = 0;
  pms5003_particleCount_2_5_MIN  = 9999;  pms5003_particleCount_2_5_MAX  = 0;
  pms5003_particleCount_5_0_MIN  = 9999;  pms5003_particleCount_5_0_MAX  = 0;
  pms5003_particleCount_10_0_MIN = 9999;  pms5003_particleCount_10_0_MAX = 0;
}

// ============================================================================
// PMS5003Sensor::isOk()
// ============================================================================

bool PMS5003Sensor::isOk() {
  return last_cycle_ok;
}

// ============================================================================
// PMS5003Sensor::update()  —  nieblokujący automat stanów
// ============================================================================

void PMS5003Sensor::update() {
  const unsigned long now = millis();

  switch (state) {

    // ── 1. Odczyt atmosferyczny ──────────────────────────────────────────
    case START_ATM_READ:
      pms.read();                  // odczyt ATM (tsi_mode = false)
      total_readings++;

      atm_ok  = (pms.status == pms.OK) && pms.has_particulate_matter();
      pm01_atm = atm_ok ? pms.pm01 : 0;
      pm25_atm = atm_ok ? pms.pm25 : 0;
      pm10_atm = atm_ok ? pms.pm10 : 0;

      // Cząstki dostępne tylko w odczycie ATM
      if (atm_ok && pms.has_number_concentration()) {
        n0p3  = pms.n0p3;
        n0p5  = pms.n0p5;
        n1p0  = pms.n1p0;
        n2p5  = pms.n2p5;
        n5p0  = pms.n5p0;
        n10p0 = pms.n10p0;
      } else {
        n0p3 = n0p5 = n1p0 = n2p5 = n5p0 = n10p0 = 0;
      }

      if (!atm_ok) total_errors++;

      ts    = now;
      state = WAIT_FOR_FACTORY;
      break;

    // ── 2. Czekaj 2 s, potem odczyt fabryczny CF=1 ──────────────────────
    case WAIT_FOR_FACTORY:
      if (now - ts >= 2000UL) {
        pms.read(true);            // odczyt CF=1 (tsi_mode = true)
        total_readings++;

        fac_ok  = (pms.status == pms.OK);
        pm01_fac = fac_ok ? pms.pm01 : 0;
        pm25_fac = fac_ok ? pms.pm25 : 0;
        pm10_fac = fac_ok ? pms.pm10 : 0;

        if (!fac_ok) total_errors++;

        ts    = now;
        state = PUSH_DATA;
      }
      break;

    // ── 3. Zapisz dane do zmiennych globalnych ───────────────────────────
    case PUSH_DATA: {
      last_cycle_ok = atm_ok || fac_ok;
      lastUpdateTime = millis();  // Zapamiętaj moment aktualizacji

      // --- Telemetria ---
      pms5003_errorCount_current = (atm_ok ? 0 : 1) + (fac_ok ? 0 : 1);
      pms5003_errorCount_total   = total_errors;
      pms5003_bytesReceived      = pms.bytes_read();
      pms5003_latency_ms         = pms.waited_ms();
      pms5003_lastFrameTime      = millis();

      // --- CF=1 ---
      updateVal(pms5003_PM1_0_CF1, pms5003_PM1_0_CF1_MIN, pms5003_PM1_0_CF1_MAX, pm01_fac);
      updateVal(pms5003_PM2_5_CF1, pms5003_PM2_5_CF1_MIN, pms5003_PM2_5_CF1_MAX, pm25_fac);
      updateVal(pms5003_PM10_CF1,  pms5003_PM10_CF1_MIN,  pms5003_PM10_CF1_MAX,  pm10_fac);

      // --- ATM ---
      updateVal(pms5003_PM1_0_ATM, pms5003_PM1_0_ATM_MIN, pms5003_PM1_0_ATM_MAX, pm01_atm);
      updateVal(pms5003_PM2_5_ATM, pms5003_PM2_5_ATM_MIN, pms5003_PM2_5_ATM_MAX, pm25_atm);
      updateVal(pms5003_PM10_ATM,  pms5003_PM10_ATM_MIN,  pms5003_PM10_ATM_MAX,  pm10_atm);

      // --- Cząstki ---
      updateVal(pms5003_particleCount_0_3,  pms5003_particleCount_0_3_MIN,  pms5003_particleCount_0_3_MAX,  n0p3);
      updateVal(pms5003_particleCount_0_5,  pms5003_particleCount_0_5_MIN,  pms5003_particleCount_0_5_MAX,  n0p5);
      updateVal(pms5003_particleCount_1_0,  pms5003_particleCount_1_0_MIN,  pms5003_particleCount_1_0_MAX,  n1p0);
      updateVal(pms5003_particleCount_2_5,  pms5003_particleCount_2_5_MIN,  pms5003_particleCount_2_5_MAX,  n2p5);
      updateVal(pms5003_particleCount_5_0,  pms5003_particleCount_5_0_MIN,  pms5003_particleCount_5_0_MAX,  n5p0);
      updateVal(pms5003_particleCount_10_0, pms5003_particleCount_10_0_MIN, pms5003_particleCount_10_0_MAX, n10p0);

      ts    = millis();
      state = WAIT_BETWEEN_CYCLES;
      break;
    }

    // ── 4. Pauza 8 s przed kolejnym cyklem ──────────────────────────────
    case WAIT_BETWEEN_CYCLES:
      if (now - ts >= 8000UL) {
        state = START_ATM_READ;
      }
      break;
  }
}
