#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "ErriezDS3231.h"
#include "I2C_eeprom.h"
#include "I2C_bus_shared.h"

// Pins and settings
#define SDA_PIN 21
#define SCL_PIN 22
#define SERIAL_BAUD 115200
#define PRINT_INTERVAL_MS 1000UL
#define SERIAL_RX_RING_SIZE 128
#define SERIAL_LINE_SIZE 96
#define SERIAL_TX_MESSAGE_SIZE 96
#define RTC_CMD_QUEUE_LENGTH 8
#define RTC_EVT_QUEUE_LENGTH 8
#define SERIAL_TX_QUEUE_LENGTH 16
#define MAIN_LOOP_DELAY_MS 2UL
#define RTC_TASK_PERIOD_MS 20UL

// RTC object
ErriezDS3231 rtc;

// EEPROM settings
static I2C_eeprom ee(0x57, I2C_DEVICESIZE_24LC32);
static const uint16_t EEPROM_TEST_ADDR = 10;
static uint16_t ee_last_write_len = 0;

enum RtcCommandType {
    RTC_CMD_SET_TIME,
    RTC_CMD_GET_TIME,
};

enum RtcEventType {
    RTC_EVT_INFO,
    RTC_EVT_ERROR,
    RTC_EVT_TIME,
    RTC_EVT_SET_OK,
};

struct RtcCommand {
    RtcCommandType type;
    struct tm dateTime;
};

struct RtcEvent {
    RtcEventType type;
    struct tm dateTime;
    char message[SERIAL_TX_MESSAGE_SIZE];
};

struct SerialMessage {
    char text[SERIAL_TX_MESSAGE_SIZE];
};

static QueueHandle_t rtcCmdQ = nullptr;
static QueueHandle_t rtcEvtQ = nullptr;
static QueueHandle_t serialTxQ = nullptr;

static char serialRxRing[SERIAL_RX_RING_SIZE];
static size_t serialRxHead = 0;
static size_t serialRxTail = 0;

// Sakamoto's algorithm: returns 0=Sunday..6=Saturday
static int dayOfWeek(int y, int m, int d)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

static void queueSerialMessage(const char *text)
{
    if (serialTxQ == nullptr) {
        return;
    }

    SerialMessage message = {};
    snprintf(message.text, sizeof(message.text), "%s", text);
    xQueueSend(serialTxQ, &message, 0);
}

static void formatDateTime(const struct tm *dateTime, char *buffer, size_t bufferSize)
{
    snprintf(buffer,
             bufferSize,
             "%04d-%02d-%02d %02d:%02d:%02d",
             dateTime->tm_year + 1900,
             dateTime->tm_mon + 1,
             dateTime->tm_mday,
             dateTime->tm_hour,
             dateTime->tm_min,
             dateTime->tm_sec);
}

static bool serialRingIsEmpty()
{
    return serialRxHead == serialRxTail;
}

static void serialRingPush(char c)
{
    size_t nextHead = (serialRxHead + 1) % SERIAL_RX_RING_SIZE;
    if (nextHead == serialRxTail) {
        serialRxTail = (serialRxTail + 1) % SERIAL_RX_RING_SIZE;
    }

    serialRxRing[serialRxHead] = c;
    serialRxHead = nextHead;
}

static bool serialRingPopLine(char *lineBuffer, size_t lineBufferSize)
{
    if (serialRingIsEmpty()) {
        return false;
    }

    size_t scan = serialRxTail;
    bool hasLine = false;
    while (scan != serialRxHead) {
        if (serialRxRing[scan] == '\n') {
            hasLine = true;
            break;
        }
        scan = (scan + 1) % SERIAL_RX_RING_SIZE;
    }

    if (!hasLine) {
        return false;
    }

    size_t outIndex = 0;
    while (serialRxTail != serialRxHead) {
        char c = serialRxRing[serialRxTail];
        serialRxTail = (serialRxTail + 1) % SERIAL_RX_RING_SIZE;

        if (c == '\n') {
            break;
        }

        if (c == '\r') {
            continue;
        }

        if (outIndex < (lineBufferSize - 1)) {
            lineBuffer[outIndex++] = c;
        }
    }

    lineBuffer[outIndex] = '\0';
    return outIndex > 0;
}

static void sendRtcEventMessage(RtcEventType type, const char *message)
{
    if (rtcEvtQ == nullptr) {
        return;
    }

    RtcEvent event = {};
    event.type = type;
    snprintf(event.message, sizeof(event.message), "%s", message);
    xQueueSend(rtcEvtQ, &event, 0);
}

static void sendRtcTimeEvent(const struct tm *dateTime)
{
    if (rtcEvtQ == nullptr) {
        return;
    }

    RtcEvent event = {};
    event.type = RTC_EVT_TIME;
    event.dateTime = *dateTime;
    xQueueSend(rtcEvtQ, &event, 0);
}

static void serialTxTask(void *parameter)
{
    (void)parameter;

    SerialMessage message = {};
    for (;;) {
        if (xQueueReceive(serialTxQ, &message, portMAX_DELAY) == pdPASS) {
            Serial.println(message.text);
        }
    }
}

static void rtcTask(void *parameter)
{
    (void)parameter;

    sendRtcEventMessage(RTC_EVT_INFO, "Init RTC...");
    if (!rtc.begin()) {
        sendRtcEventMessage(RTC_EVT_ERROR, "RTC not detected on I2C bus");
    } else {
        sendRtcEventMessage(RTC_EVT_INFO, "RTC detected");
        if (!rtc.isRunning()) {
            if (rtc.clockEnable(true)) {
                sendRtcEventMessage(RTC_EVT_INFO, "RTC oscillator stopped, oscillator enabled. Set time if needed.");
            } else {
                sendRtcEventMessage(RTC_EVT_ERROR, "Failed to enable RTC oscillator");
            }
        }
    }

    TickType_t lastWake = xTaskGetTickCount();
    TickType_t lastPollTick = lastWake;
    bool lastReadOk = true;

    for (;;) {
        RtcCommand command = {};
        while (xQueueReceive(rtcCmdQ, &command, 0) == pdPASS) {
            if (command.type == RTC_CMD_SET_TIME) {
                bool ok = rtc.setDateTime((uint8_t)command.dateTime.tm_hour,
                                          (uint8_t)command.dateTime.tm_min,
                                          (uint8_t)command.dateTime.tm_sec,
                                          (uint8_t)command.dateTime.tm_mday,
                                          (uint8_t)(command.dateTime.tm_mon + 1),
                                          (uint16_t)(command.dateTime.tm_year + 1900),
                                          (uint8_t)command.dateTime.tm_wday);
                if (ok) {
                    sendRtcEventMessage(RTC_EVT_SET_OK, "RTC time set successfully");
                } else {
                    sendRtcEventMessage(RTC_EVT_ERROR, "Failed to write time to RTC");
                }
            } else if (command.type == RTC_CMD_GET_TIME) {
                struct tm dateTime = {};
                if (rtc.read(&dateTime)) {
                    sendRtcTimeEvent(&dateTime);
                    lastReadOk = true;
                } else {
                    sendRtcEventMessage(RTC_EVT_ERROR, "Failed to read time from RTC");
                    lastReadOk = false;
                }
            }
        }

        TickType_t nowTick = xTaskGetTickCount();
        if ((nowTick - lastPollTick) >= pdMS_TO_TICKS(PRINT_INTERVAL_MS)) {
            lastPollTick += pdMS_TO_TICKS(PRINT_INTERVAL_MS);

            struct tm dateTime = {};
            if (rtc.read(&dateTime)) {
                sendRtcTimeEvent(&dateTime);
                lastReadOk = true;
            } else if (lastReadOk) {
                sendRtcEventMessage(RTC_EVT_ERROR, "Failed to read time from RTC");
                lastReadOk = false;
            }
        }

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(RTC_TASK_PERIOD_MS));
    }
}

static void processCommand(const char *line)
{
    while (*line == ' ' || *line == '\t') {
        ++line;
    }

    if (strncmp(line, "SET ", 4) == 0) {
        int y = 0;
        int mo = 0;
        int d = 0;
        int hr = 0;
        int mi = 0;
        int se = 0;
        int parsed = sscanf(line + 4, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &hr, &mi, &se);
        if (parsed != 6) {
            queueSerialMessage("Invalid SET format. Use: SET YYYY-MM-DD HH:MM:SS");
            return;
        }

        if (y < 2000 || mo < 1 || mo > 12 || d < 1 || d > 31 || hr < 0 || hr > 23 || mi < 0 || mi > 59 || se < 0 || se > 59) {
            queueSerialMessage("Invalid date/time values");
            return;
        }

        RtcCommand command = {};
        command.type = RTC_CMD_SET_TIME;
        command.dateTime.tm_year = y - 1900;
        command.dateTime.tm_mon = mo - 1;
        command.dateTime.tm_mday = d;
        command.dateTime.tm_hour = hr;
        command.dateTime.tm_min = mi;
        command.dateTime.tm_sec = se;
        command.dateTime.tm_wday = dayOfWeek(y, mo, d);

        if (xQueueSend(rtcCmdQ, &command, 0) != pdPASS) {
            queueSerialMessage("RTC command queue full");
        }
        return;
    }

    if (strcmp(line, "GET") == 0) {
        RtcCommand command = {};
        command.type = RTC_CMD_GET_TIME;
        if (xQueueSend(rtcCmdQ, &command, 0) != pdPASS) {
            queueSerialMessage("RTC command queue full");
        }
        return;
    }

    if (strcmp(line, "SCAN") == 0) {
        // Run an I2C bus scan to list detected devices (useful to find EEPROM addresses)
        queueSerialMessage("Starting I2C scan...");
        for (uint8_t address = 1; address < 127; ++address) {
            if (I2cShared::probeAddress(address, 2, 2)) {
                char buf[64];
                snprintf(buf, sizeof(buf), "I2C device found at 0x%02X (%d)", address, address);
                queueSerialMessage(buf);
            }
        }
        queueSerialMessage("I2C scan completed");
        return;
    }

    // WRITE : <text>  -> write text (including NUL) to EEPROM at EEPROM_TEST_ADDR
    if (strncmp(line, "WRITE", 5) == 0) {
        const char *p = strchr(line, ':');
        if (p == nullptr) {
            queueSerialMessage("WRITE syntax: WRITE : your text");
            return;
        }
        // skip colon and spaces
        ++p;
        while (*p == ' ' || *p == '\t') ++p;

        size_t len = strlen(p);
        if (len == 0) {
            queueSerialMessage("Nothing to write");
            return;
        }
        if (len + 1 > 64) {
            queueSerialMessage("Text too long (max 63)");
            return;
        }

        uint8_t buf[64];
        memset(buf, 0, sizeof(buf));
        memcpy(buf, p, len);
        int rc = ee.writeBlock(EEPROM_TEST_ADDR, buf, (uint16_t)(len + 1));
        if (rc == 0) {
            ee_last_write_len = (uint16_t)(len + 1);
            queueSerialMessage("WRITE: OK");
        } else {
            char t[48];
            snprintf(t, sizeof(t), "WRITE error: %d", rc);
            queueSerialMessage(t);
        }
        return;
    }

    // READ -> read back data from EEPROM_TEST_ADDR
    if (strncmp(line, "READ", 4) == 0) {
        uint16_t len = ee_last_write_len;
        if (len == 0) {
            // no remembered length (e.g., after power cycle) - read up to 64 bytes
            len = 64;
        }
        if (len > 128) len = 128;
        char rbuf[129];
        memset(rbuf, 0, sizeof(rbuf));
        ee.readBlock(EEPROM_TEST_ADDR, (uint8_t*)rbuf, len);

        // ensure NUL-terminated output
        rbuf[128] = '\0';

        // trim at first NUL for nicer output
        size_t outlen = strnlen(rbuf, len);
        char out[160];
        if (outlen == 0) {
            snprintf(out, sizeof(out), "READ: <empty>");
        } else {
            snprintf(out, sizeof(out), "READ: %.*s", (int)outlen, rbuf);
        }
        queueSerialMessage(out);
        return;
    }

    queueSerialMessage("Unknown command");
}

static void drainRtcEvents()
{
    RtcEvent event = {};
    while (xQueueReceive(rtcEvtQ, &event, 0) == pdPASS) {
        if (event.type == RTC_EVT_TIME) {
            char buffer[32];
            formatDateTime(&event.dateTime, buffer, sizeof(buffer));
            queueSerialMessage(buffer);
        } else {
            queueSerialMessage(event.message);
        }
    }
}

void setup()
{
    Serial.begin(SERIAL_BAUD);

    // Initialize I2C on ESP32 through shared access layer
    I2cShared::initMaster(SDA_PIN, SCL_PIN, 400000);

    // Initialize EEPROM object
    if (!ee.begin()) {
        Serial.println("EEPROM init failed");
    } else {
        ee.setExtraWriteCycleTime(20); // for 24LC32
        char buf[64];
        snprintf(buf, sizeof(buf), "EEPROM connected: %d", ee.isConnected());
        Serial.println(buf);
    }

    rtcCmdQ = xQueueCreate(RTC_CMD_QUEUE_LENGTH, sizeof(RtcCommand));
    rtcEvtQ = xQueueCreate(RTC_EVT_QUEUE_LENGTH, sizeof(RtcEvent));
    serialTxQ = xQueueCreate(SERIAL_TX_QUEUE_LENGTH, sizeof(SerialMessage));

    if (rtcCmdQ == nullptr || rtcEvtQ == nullptr || serialTxQ == nullptr) {
        Serial.println("Queue allocation failed");
        return;
    }

    xTaskCreatePinnedToCore(serialTxTask,
                            "serial_tx",
                            3072,
                            nullptr,
                            1,
                            nullptr,
                            1);

    xTaskCreatePinnedToCore(rtcTask,
                            "rtc_task",
                            4096,
                            nullptr,
                            1,
                            nullptr,
                            0);
}

void loop()
{
    char lineBuffer[SERIAL_LINE_SIZE];

    while (Serial.available()) {
        int c = Serial.read();
        if (c < 0) {
            break;
        }

        serialRingPush((char)c);
    }

    while (serialRingPopLine(lineBuffer, sizeof(lineBuffer))) {
        processCommand(lineBuffer);
    }

    drainRtcEvents();

    taskYIELD();
}

