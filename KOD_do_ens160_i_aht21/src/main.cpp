#include <Arduino.h>
#include <Wire.h>
#include <ScioSense_ENS16x.h>
#include <AHTxx.h>

// ━━━ ENS160 Raw Resistance Data Limitation ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// ENS160 has FOUR internal MOS sensors (R1, R2, R3, R4) but only TWO are exposed
// in GPR_READ registers per datasheet Table 8:
//   - R1raw at GPR_READ[0:1] (bytes 0x48–0x49)  ← Valid
//   - R4raw at GPR_READ[6:7] (bytes 0x4E–0x4F)  ← Valid
//   - GPR_READ[2:3], [4:5] are reserved/undocumented (filled with 0x0001)
//
// The ScioSense library maps both output registers as four uint16_t indices:
//   - RS0 (index 0) → R1raw [0:1]      ✓ Valid sensor
//   - RS1 (index 2) → Reserved [2:3]   ✗ Artefact: reads 0x0001 → Ω ≈ 1
//   - RS2 (index 4) → Reserved [4:5]   ✗ No real data
//   - RS3 (index 6) → R4raw [6:7]      ✓ Valid sensor
//
// This code filters RS1 and RS2 by setting them to NAN (printed as "--").
// See: https://www.sciosense.com/wp-content/uploads/2023/12/ENS160-Datasheet.pdf
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

#define ENS160_I2C_ADDRESS     0x52
#define ENS160_I2C_ADDRESS_ALT 0x53

// I2C pins for common ESP32 dev boards. Change if your board differs.
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
#define SERIAL_BAUD 115200
#define ENABLE_ENS160_DEBUG 0
#define ENABLE_I2C_SCAN 0
#define ENS160_INIT_RETRIES 3
#define ENS160_INIT_RETRY_DELAY_MS 150
#define ENS160_REINIT_ERROR_THRESHOLD 3
#define ENS160_REINIT_COOLDOWN_MS 5000UL
#define ENS160_COMM_ERR_LOG_INTERVAL_MS 2000UL
#define ENS160_NO_NEW_DATA_LOG_INTERVAL_MS 10000UL
#define LOOP_POLL_INTERVAL_MS (ENS16X_SYSTEM_TIMING_STANDARD_MEASURE + 50UL)
#define AHT_DATA_MAX_AGE_MS 5000UL
#define AHT_DISPLAY_TEMPERATURE_OFFSET_C 0.0f
#define ENS160_COMPENSATION_TEMPERATURE_OFFSET_C 0.0f

// LED pin for initialization status indicator
// D2 on ESP32-WROOM-32D = GPIO2
#define LED_D2_PIN  2  // Will blink when sensors reach OK status (validity=0)

ENS160 ens160;
AHTxx  aht21(AHTXX_ADDRESS_X38, AHT2x_SENSOR);
bool   ens160_present = false;
bool   aht_present = false;

// Pretty table helpers
int _printed_rows = 0;
bool _debug_gpr_printed = false;  // Print GPR debug info only once per session

// LED status tracking
uint8_t _last_validity = 0xFF;  // Track last validity state (0xFF = uninitialized)
uint8_t _pending_validity = 0xFF;
uint8_t _pending_validity_count = 0;
bool _led_blink_enabled = false;  // Becomes true when validity == OK (0)
unsigned long _led_blink_time = 0;  // Timestamp for LED blink timing
bool _led_steady_enabled = false; // When true, LED stays steadily ON (e.g., INIT)
uint8_t _ens160_address = ENS160_I2C_ADDRESS;
uint8_t _ens160_error_streak = 0;
unsigned long _last_ens160_comm_err_log_ms = 0;
unsigned long _last_ens160_no_new_data_log_ms = 0;
unsigned long _last_ens160_reinit_attempt_ms = 0;
unsigned long _last_poll_ms = 0;

enum Ens160RuntimeState : uint8_t {
    ENS160_STATE_OK = 0,
    ENS160_STATE_WARMUP,
    ENS160_STATE_INIT_STARTUP,
    ENS160_STATE_NO_NEW_DATA,
    ENS160_STATE_COMM_ERR,
    ENS160_STATE_RECOVERING,
    ENS160_STATE_OFFLINE,
    ENS160_STATE_INVALID_OUTPUT,
};

Ens160RuntimeState _ens160_runtime_state = ENS160_STATE_OFFLINE;

// DEBUG: Read and print raw GPR_READ register bytes to diagnose RS0/RS1/RS2/RS3 mapping
// This reveals whether bytes [2:3] and [4:5] contain real data or artifacts (0x0001, etc.)
void debugPrintGPRRawBytes() {
#if !ENABLE_ENS160_DEBUG
    return;
#else
    if (_debug_gpr_printed) return;  // Print only first time
    
    uint8_t gprBuf[8] = {0};
    // Read 8 bytes from register 0x48 (GPR_READ start address)
    Result readResult = ens160.read(0x48, gprBuf, 8);
    
    if (readResult == 0) {  // RESULT_OK = 0
        Serial.println("\n=== DEBUG: Raw GPR_READ bytes (0x48-0x4F) ===");
        Serial.print("Bytes [0:1] (RS0 input): 0x");
        Serial.print(gprBuf[1], HEX); Serial.print(gprBuf[0], HEX);
        Serial.print(" (decimal: ");
        uint16_t rs0_raw = (gprBuf[1] << 8) | gprBuf[0];
        Serial.print(rs0_raw); Serial.println(")");
        
        Serial.print("Bytes [2:3] (RS1 input): 0x");
        Serial.print(gprBuf[3], HEX); Serial.print(gprBuf[2], HEX);
        Serial.print(" (decimal: ");
        uint16_t rs1_raw = (gprBuf[3] << 8) | gprBuf[2];
        Serial.print(rs1_raw); Serial.println(")");
        
        Serial.print("Bytes [4:5] (RS2 input): 0x");
        Serial.print(gprBuf[5], HEX); Serial.print(gprBuf[4], HEX);
        Serial.print(" (decimal: ");
        uint16_t rs2_raw = (gprBuf[5] << 8) | gprBuf[4];
        Serial.print(rs2_raw); Serial.println(")");
        
        Serial.print("Bytes [6:7] (RS3 input): 0x");
        Serial.print(gprBuf[7], HEX); Serial.print(gprBuf[6], HEX);
        Serial.print(" (decimal: ");
        uint16_t rs3_raw = (gprBuf[7] << 8) | gprBuf[6];
        Serial.print(rs3_raw); Serial.println(")");
        Serial.println("=== END DEBUG ===\n");
    } else {
        Serial.println("DEBUG: Failed to read GPR_READ");
    }
    _debug_gpr_printed = true;
#endif
}

// Extract VALIDITY FLAG from ENS160 Device Status register (bits [3:2])
// 0 = Normal operation, 1 = Warm-up, 2 = Initial Start-Up, 3 = Invalid output
uint8_t getENS160ValidityFlag(Ens16x_DeviceStatus status) {
    uint8_t validityHigh = (status & ENS16X_DEVICE_STATUS_VALID_HIGH) >> 3;
    uint8_t validityLow  = (status & ENS16X_DEVICE_STATUS_VALID_LOW) >> 2;
    return (validityHigh << 1) | validityLow;
}

// LED Control: Blink D2 when initialization is complete (validity == OK)
void updateLEDStatus() {
    unsigned long now = millis();
    
    if (_led_steady_enabled) {
        // Steady ON (used for INIT state)
        digitalWrite(LED_D2_PIN, HIGH);
    } else if (_led_blink_enabled) {
        // Toggle LED every 500ms (250ms on, 250ms off)
        if ((now - _led_blink_time) >= 500) {
            digitalWrite(LED_D2_PIN, !digitalRead(LED_D2_PIN));
            _led_blink_time = now;
        }
    } else {
        // LED off when not yet initialized or in warm-up
        digitalWrite(LED_D2_PIN, LOW);
    }
}



const char* getValidityStatusString(uint8_t validity) {
    switch (validity) {
        case 0: return "OK";           // Normal operation
        case 1: return "WARM-UP";      // Warm-up phase (first ~45–60 min after power-on)
        case 2: return "INIT";         // Initial Start-Up phase (first ~24h after power-on) — full sensor calibration
        case 3: return "INVALID";      // Invalid output
        default: return "UNKNOWN";
    }
}

const char* getEns160RuntimeStateString(Ens160RuntimeState state) {
    switch (state) {
        case ENS160_STATE_OK: return "OK";
        case ENS160_STATE_WARMUP: return "WARMUP";
        case ENS160_STATE_INIT_STARTUP: return "INIT_STARTUP";
        case ENS160_STATE_NO_NEW_DATA: return "NO_NEW_DATA";
        case ENS160_STATE_COMM_ERR: return "COMM_ERR";
        case ENS160_STATE_RECOVERING: return "RECOVERING";
        case ENS160_STATE_OFFLINE: return "OFFLINE";
        case ENS160_STATE_INVALID_OUTPUT: return "INVALID_OUTPUT";
        default: return "UNKNOWN";
    }
}

Ens160RuntimeState getEns160RuntimeStateFromValidity(uint8_t validity) {
    switch (validity) {
        case 0: return ENS160_STATE_OK;
        case 1: return ENS160_STATE_WARMUP;
        case 2: return ENS160_STATE_INIT_STARTUP;
        case 3: return ENS160_STATE_INVALID_OUTPUT;
        default: return ENS160_STATE_INVALID_OUTPUT;
    }
}

void applyEns160ValidityToLed(uint8_t validityFlag) {
    if (validityFlag == _pending_validity) {
        if (_pending_validity_count < 255) {
            _pending_validity_count++;
        }
    } else {
        _pending_validity = validityFlag;
        _pending_validity_count = 1;
    }

    if (_pending_validity_count < 2) {
        return;
    }

    if (validityFlag == _last_validity) {
        return;
    }

    _last_validity = validityFlag;
    if (validityFlag == 0) {
        _led_blink_enabled = true;
        _led_steady_enabled = false;
        Serial.println("ENS160: status=OK");
    } else if (validityFlag == 1) {
        _led_blink_enabled = false;
        _led_steady_enabled = false;
        Serial.println("ENS160: status=WARMUP");
    } else if (validityFlag == 2) {
        _led_blink_enabled = false;
        _led_steady_enabled = true;
        Serial.println("ENS160: status=INIT_STARTUP");
    } else {
        _led_blink_enabled = false;
        _led_steady_enabled = false;
        Serial.println("ENS160: status=INVALID_OUTPUT");
    }
}

bool isEns160ValidityStable(uint8_t validityFlag) {
    return (_pending_validity == validityFlag) && (_pending_validity_count >= 2);
}

void logEns160Measurement(Ens160RuntimeState state, int aqi, float tvoc, float eco2,
                          bool th_valid, float th_temp, float th_hum) {
    Serial.print("ENS160: status=");
    Serial.print(getEns160RuntimeStateString(state));

    if (aqi >= 0) {
        Serial.print(" AQI=");
        Serial.print(aqi);
        if (!isnan(tvoc)) {
            Serial.print(" TVOC=");
            Serial.print((int)tvoc);
        }
        if (!isnan(eco2)) {
            Serial.print(" eCO2=");
            Serial.print((int)eco2);
        }
    }

    if (th_valid) {
        Serial.print(" T=");
        Serial.print(th_temp, 2);
        Serial.print(" H=");
        Serial.print(th_hum, 2);
    }

    Serial.println();
}

void setENS160OfflineState() {
    ens160_present = false;
    _debug_gpr_printed = false;
    _last_validity = 0xFF;
    _pending_validity = 0xFF;
    _pending_validity_count = 0;
    _ens160_error_streak = 0;
    _ens160_runtime_state = ENS160_STATE_OFFLINE;
    _led_blink_enabled = false;
    _led_steady_enabled = false;
}

void scanI2CBus() {
    Serial.println("Scanning I2C bus...");
    byte count = 0;
    for (byte address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        byte error = Wire.endTransmission();
        if (error == 0) {
            Serial.print("I2C device found at 0x");
            if (address < 16) Serial.print("0");
            Serial.print(address, HEX);
            Serial.println("  !");
            count++;
        }
    }

    if (count == 0) {
        Serial.println("No I2C devices found. Check wiring.");
        return;
    }

    Serial.print("Found ");
    Serial.print(count);
    Serial.println(" device(s).");
}

bool tryInitENS160AtAddress(uint8_t address) {
    ens160.begin(&Wire, address);

    for (uint8_t attempt = 0; attempt < ENS160_INIT_RETRIES; ++attempt) {
        if (ens160.init()) {
            _ens160_address = address;
            _debug_gpr_printed = false;
            _last_validity = 0xFF;
            _ens160_error_streak = 0;
            return ens160.startStandardMeasure() == RESULT_OK;
        }

        if ((attempt + 1) < ENS160_INIT_RETRIES) {
            delay(ENS160_INIT_RETRY_DELAY_MS);
            yield();
        }
    }

    return false;
}

bool initializeENS160() {
    Serial.print("Trying ENS160 at 0x");
    Serial.println(ENS160_I2C_ADDRESS, HEX);
    if (tryInitENS160AtAddress(ENS160_I2C_ADDRESS)) {
        return true;
    }

    Serial.print("Trying ENS160 at alternate address 0x");
    Serial.println(ENS160_I2C_ADDRESS_ALT, HEX);
    return tryInitENS160AtAddress(ENS160_I2C_ADDRESS_ALT);
}

void printTableHeader() {
    Serial.println();
    Serial.println("+----------+--------+--------+--------+-----+--------+--------+----------+----------+----------+----------+----------+");
    Serial.printf("| %8s | %6s | %6s | %6s | %3s | %6s | %6s | %8s | %8s | %8s | %8s | %8s |\r\n",
                  "Time(ms)", "Source", "Temp", "Hum", "AQI", "TVOC", "eCO2", "RS0", "RS1", "RS2", "RS3", "VALID");
    Serial.println("+----------+--------+--------+--------+-----+--------+--------+----------+----------+----------+----------+----------+");
    _printed_rows = 0;
}

void printTableRow(unsigned long time_ms, const char *source, bool th_valid, float th_temp, float th_hum,
                   int aqi, float tvoc, float eco2, float rs0, float rs1, float rs2, float rs3, const char *validity_str) {
    char tempBuf[16] = "   --";
    char humBuf[16]  = "   --";
    char aqiBuf[8];
    char tvocBuf[16];
    char eco2Buf[16];
    char rs0Buf[24], rs1Buf[24], rs2Buf[24], rs3Buf[24];

    if (th_valid) {
        snprintf(tempBuf, sizeof(tempBuf), "%6.2f", th_temp);
        snprintf(humBuf, sizeof(humBuf), "%6.2f", th_hum);
    }
    if (aqi >= 0) snprintf(aqiBuf, sizeof(aqiBuf), "%3d", aqi); else snprintf(aqiBuf, sizeof(aqiBuf), " --");
    if (!isnan(tvoc)) snprintf(tvocBuf, sizeof(tvocBuf), "%6.2f", tvoc); else snprintf(tvocBuf, sizeof(tvocBuf), "  --  ");
    if (!isnan(eco2)) snprintf(eco2Buf, sizeof(eco2Buf), "%6.2f", eco2); else snprintf(eco2Buf, sizeof(eco2Buf), "  --  ");
    if (!isnan(rs0)) snprintf(rs0Buf, sizeof(rs0Buf), "%8ld", (long)rs0); else snprintf(rs0Buf, sizeof(rs0Buf), "      --");
    if (!isnan(rs1)) snprintf(rs1Buf, sizeof(rs1Buf), "%8ld", (long)rs1); else snprintf(rs1Buf, sizeof(rs1Buf), "      --");
    if (!isnan(rs2)) snprintf(rs2Buf, sizeof(rs2Buf), "%8ld", (long)rs2); else snprintf(rs2Buf, sizeof(rs2Buf), "      --");
    if (!isnan(rs3)) snprintf(rs3Buf, sizeof(rs3Buf), "%8ld", (long)rs3); else snprintf(rs3Buf, sizeof(rs3Buf), "      --");

    Serial.printf("| %8lu | %6s | %6s | %6s | %3s | %6s | %6s | %8s | %8s | %8s | %8s | %8s |\r\n",
                  time_ms, source, tempBuf, humBuf, aqiBuf, tvocBuf, eco2Buf, rs0Buf, rs1Buf, rs2Buf, rs3Buf, validity_str);

    _printed_rows++;
    if ((_printed_rows % 16) == 0) {
        Serial.println("+----------+--------+--------+--------+-----+--------+--------+----------+----------+----------+----------+----------+");
        printTableHeader();
    }
}

void setup()
{
    Serial.begin(SERIAL_BAUD);
#if ENABLE_ENS160_DEBUG
    ens160.enableDebugging(Serial);
#endif

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // SENSOR INITIALIZATION PHASES EXPLANATION:
    //
    // 1. ENS160 (Metal Oxide Gas Sensor)
    //    ├─ Hardware init: ~10 ms (reset via Ens16x_Reset)
    //    ├─ OPMODE switch to STANDARD: immediate (register write)
    //    └─ Validity Stages (chip-internal state machine):
    //       ├─ WARM-UP (validity=1):  ~45–60 minutes (heater stabilization)
    //       ├─ INIT (validity=2):     ~24 hours total (full MOX calibration)
    //       └─ OK (validity=0):       data fully reliable
    //    
    //    IMPORTANT: The VALIDITY flag is set BY THE CHIP, not by code timing.
    //    It's internal calibration progress — no way to "accelerate" it.
    //    Your table shows VALID=INIT because sensor is in first phase.
    //
    // 2. AHT21 (Temperature/Humidity Sensor)
    //    ├─ Power-on wait: 100 ms (AHT2X_POWER_ON_MS)
    //    ├─ Soft reset:    25 ms (AHTXX_SOFT_RESET_MS)
    //    ├─ Init register: 10 ms (AHTXX_CMD_DELAY_MS)
    //    ├─ Each measurement: 85 ms (AHTXX_MEASUREMENT_MS)
    //    └─ Interval: 3000 ms between reads (configurable, currently 3s)
    //    
    //    Built-in filtering (state machine driven):
    //    ├─ Median filter (5-sample window) for outlier rejection
    //    ├─ Kalman filter (Q=0.003, R=0.15 for temp) for noise reduction
    //    └─ Self-heating compensation (0.1°C subtraction at 3s reads)
    //
    // 3. Loop Behavior
    //    └─ Both sensors use state machines, while the application throttles
    //       polling and logging with millis() to avoid blocking the main loop.
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    // Initialize Wire with explicit SDA/SCL pins to avoid board-dependent defaults
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    // Initialize LED D2 pin (GPIO2) — will blink when init is complete
    pinMode(LED_D2_PIN, OUTPUT);
    digitalWrite(LED_D2_PIN, LOW);  // LED off initially
    _led_blink_time = millis();

    #if ENABLE_I2C_SCAN
    scanI2CBus();
    #endif

    ens160_present = initializeENS160();
    if (!ens160_present) {
        setENS160OfflineState();
        Serial.println("ENS160 not responding. Check wiring, power, and pull-ups.\nIf your module uses a different I2C address, update ENS160_I2C_ADDRESS in src/main.cpp.");
    } else {
        Serial.print("ENS160 init success at 0x");
        Serial.println(_ens160_address, HEX);
    }

    // Initialize AHT21 state machine only after the device answers on I2C.
    Serial.print("Starting AHT21 init...");
    if (aht21.begin(I2C_SDA_PIN, I2C_SCL_PIN)) {
        // Configure advanced filtering:
        //   Kalman temp: Q=0.003 (slow process), R=0.15 (sensor noise ~0.3C => R=variance)
        //   Kalman hum:  Q=0.01,  R=1.0  (sensor noise ~2%)
        aht21.setKalmanParams(0.003f, 0.15f, 0.01f, 1.0f);
        aht21.setSelfHeatingCompensation(0.1f); // subtract ~0.1C self-heating at 3s interval
        aht21.setMeasurementInterval(3000);     // 3s between physical reads
        aht_present = true;
        Serial.println(" started");
    } else {
        Serial.println(" AHT21 not detected on I2C");
    }

    Serial.println("Runtime monitor ready.");
}

void loop()
{
    unsigned long now = millis();

    // Determine temperature/humidity source and values
    bool th_valid = false;
    const char *th_source = "none";
    float th_temp = 0.0f;
    float th_hum = 0.0f;

    if (aht_present) {
        // Drive AHT21 non-blocking state machine
        bool newData = aht21.update();
        bool freshData = aht21.hasFreshData(AHT_DATA_MAX_AGE_MS);

        // Get Kalman-filtered temperature and humidity values
        float t = aht21.getTemperature();      // Already filtered (Kalman + Median)
        float h = aht21.getHumidity();         // Already filtered (Kalman + Median)
        if (freshData && !isnan(t) && !isnan(h)) {
            float displayTemp = t + AHT_DISPLAY_TEMPERATURE_OFFSET_C;

            th_temp = displayTemp;
            th_hum = h;
            th_source = "AHT21";
            th_valid = true;
            if (newData && ens160_present) {
                float compensationTemp = t + ENS160_COMPENSATION_TEMPERATURE_OFFSET_C;
                uint16_t tempRaw = Ens16x_CalcTempInFromCelsius(compensationTemp);
                uint16_t humRaw  = Ens16x_CalcRhIn(h);
                Result compResult = ens160.writeCompensation(tempRaw, humRaw);
                if (compResult != RESULT_OK) {
                    Serial.printf("AHT->ENS160 compensation write failed: %d\r\n", (int)compResult);
                }
            }
        } else if (aht21.isReady()) {
            th_source = "STALE";
        } else {
            th_source = "INIT";
        }
    }

    updateLEDStatus();

    if ((now - _last_poll_ms) < LOOP_POLL_INTERVAL_MS) {
        return;
    }

    _last_poll_ms = now;

    if (ens160_present) {
        Result ensResult = ens160.update();

        if (ensResult == RESULT_OK) {
            _ens160_error_streak = 0;
            _ens160_runtime_state = ENS160_STATE_OK;

            int aqi = -1;
            float tvoc = NAN;
            float eco2 = NAN;
            bool hasMeasurement = false;

            if (ens160.hasNewData()) {
                aqi = (int)(uint8_t)ens160.getAirQualityIndex_UBA();
                tvoc = ens160.getTvoc();
                eco2 = ens160.getEco2();
                hasMeasurement = true;
            }
            if (ens160.hasNewGeneralPurposeData()) {
                debugPrintGPRRawBytes();
            }

            Ens16x_DeviceStatus devStatus = ens160.getDeviceStatus();
            uint8_t validityFlag = getENS160ValidityFlag(devStatus);
            Ens160RuntimeState state = getEns160RuntimeStateFromValidity(validityFlag);
            _ens160_runtime_state = state;
            applyEns160ValidityToLed(validityFlag);

            if (hasMeasurement && isEns160ValidityStable(validityFlag)) {
                logEns160Measurement(state, aqi, tvoc, eco2, th_valid, th_temp, th_hum);
            }
        } else if (ensResult == RESULT_INVALID) {
            _ens160_error_streak = 0;
            _ens160_runtime_state = ENS160_STATE_NO_NEW_DATA;

            if ((now - _last_ens160_no_new_data_log_ms) >= ENS160_NO_NEW_DATA_LOG_INTERVAL_MS) {
                Serial.println("ENS160: status=NO_NEW_DATA");
                _last_ens160_no_new_data_log_ms = now;
            }
        } else {
            _ens160_error_streak++;
            _ens160_runtime_state = ENS160_STATE_COMM_ERR;

            if ((now - _last_ens160_comm_err_log_ms) >= ENS160_COMM_ERR_LOG_INTERVAL_MS) {
                Serial.printf("ENS160: status=COMM_ERR code=%d streak=%u\r\n", (int)ensResult, _ens160_error_streak);
                _last_ens160_comm_err_log_ms = now;
            }

            if (ensResult == RESULT_IO_ERROR
                && _ens160_error_streak >= ENS160_REINIT_ERROR_THRESHOLD
                && (now - _last_ens160_reinit_attempt_ms) >= ENS160_REINIT_COOLDOWN_MS) {
                _last_ens160_reinit_attempt_ms = now;
                _ens160_runtime_state = ENS160_STATE_RECOVERING;
                Serial.println("ENS160: status=COMM_ERR -> restarting sensor");
                Serial.println("ENS160: status=RECOVERING");
                setENS160OfflineState();
                ens160_present = initializeENS160();
                if (ens160_present) {
                    Serial.print("ENS160 recovered at 0x");
                    Serial.println(_ens160_address, HEX);
                    _ens160_runtime_state = ENS160_STATE_RECOVERING;
                } else {
                    Serial.println("ENS160 reinit failed; sensor remains offline.");
                    _ens160_runtime_state = ENS160_STATE_OFFLINE;
                }
            }
        }
    } else {
        if ((now - _last_ens160_comm_err_log_ms) >= ENS160_COMM_ERR_LOG_INTERVAL_MS) {
            Serial.println("ENS160: status=OFFLINE");
            _last_ens160_comm_err_log_ms = now;
        }
    }
}