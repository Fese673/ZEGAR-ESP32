#ifndef LCDMIRROR_H
#define LCDMIRROR_H

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>

#include "AppLog.h"

extern LiquidCrystal_I2C lcd;

constexpr uint8_t LCD_COLS = 20;
constexpr uint8_t LCD_ROWS = 4;

class LcdFrameBuffer20x4 : public Print {
public:
  void begin() { clear(); }

  void clear() {
    for (uint8_t r = 0; r < LCD_ROWS; ++r) {
      for (uint8_t c = 0; c < LCD_COLS; ++c) {
        frame[r][c] = ' ';
      }
    }
    cursorX = 0;
    cursorY = 0;
    dirty = true;
  }

  void clearRow(uint8_t row) {
    if (row >= LCD_ROWS) {
      return;
    }
    for (uint8_t c = 0; c < LCD_COLS; ++c) {
      frame[row][c] = ' ';
    }
    dirty = true;
  }

  void setCursor(uint8_t col, uint8_t row) {
    cursorX = (col < LCD_COLS) ? col : LCD_COLS;
    cursorY = (row < LCD_ROWS) ? row : LCD_ROWS;
  }

  size_t write(uint8_t ch) override {
    if (cursorY < LCD_ROWS && cursorX < LCD_COLS) {
      frame[cursorY][cursorX] = ch;
      dirty = true;
    }
    if (cursorX < LCD_COLS) {
      ++cursorX;
    }
    return 1;
  }

  bool commit() {
    if (!dirty && !fullForceNext) {
      return false;
    }

    const uint32_t commitStartUs = micros();
    bool changed = false;

    for (uint8_t row = 0; row < LCD_ROWS; ++row) {
      if (fullForceNext) {
        // write entire row unconditionally
        lcd.setCursor(0, row);
        for (uint8_t i = 0; i < LCD_COLS; ++i) {
          lcd.write(frame[row][i]);
          shadow[row][i] = frame[row][i];
        }
        changed = true;
      } else {
        uint8_t col = 0;
        while (col < LCD_COLS) {
          while (col < LCD_COLS && frame[row][col] == shadow[row][col]) {
            ++col;
          }
          if (col >= LCD_COLS) {
            break;
          }

          const uint8_t start = col;
          while (col < LCD_COLS && frame[row][col] != shadow[row][col]) {
            ++col;
          }

          lcd.setCursor(start, row);
          for (uint8_t i = start; i < col; ++i) {
            lcd.write(frame[row][i]);
            shadow[row][i] = frame[row][i];
          }
          changed = true;
        }
      }
    }

    dirty = false;
    fullForceNext = false;

#if CORE_DEBUG_LEVEL > 0
    lastCommitUs = micros() - commitStartUs;
    totalCommitUs += lastCommitUs;
    ++commitCount;
    if (lastCommitUs > maxCommitUs) {
      maxCommitUs = lastCommitUs;
    }
#endif
    return changed;
  }

  void syncToCurrentFrame() {
    for (uint8_t r = 0; r < LCD_ROWS; ++r) {
      for (uint8_t c = 0; c < LCD_COLS; ++c) {
        shadow[r][c] = frame[r][c];
      }
    }
    dirty = false;
  }

  // Force the next commit to write full rows (one-shot). Useful for boot/splash.
  void forceFullRedrawOnce() {
    fullForceNext = true;
    dirty = true;
  }

#if CORE_DEBUG_LEVEL > 0
  void resetStats() {
    lastCommitUs = 0;
    maxCommitUs = 0;
    totalCommitUs = 0;
    commitCount = 0;
  }

  void reportTiming(const char* tag) const {
    LOG_I("LCD",
          "%s render last_us=%lu avg_us=%lu max_us=%lu commits=%lu",
          tag,
          (unsigned long)lastCommitUs,
          (unsigned long)averageCommitTimeUs(),
          (unsigned long)maxCommitUs,
          (unsigned long)commitCount);
  }

  uint32_t lastCommitTimeUs() const { return lastCommitUs; }
  uint32_t maxCommitTimeUs() const { return maxCommitUs; }
  uint32_t averageCommitTimeUs() const {
    return commitCount ? (totalCommitUs / commitCount) : 0;
  }
#endif

private:
  uint8_t frame[LCD_ROWS][LCD_COLS] = {};
  uint8_t shadow[LCD_ROWS][LCD_COLS] = {};
  uint8_t cursorX = 0;
  uint8_t cursorY = 0;
  bool dirty = false;
  bool fullForceNext = false;

#if CORE_DEBUG_LEVEL > 0
  uint32_t lastCommitUs = 0;
  uint32_t maxCommitUs = 0;
  uint64_t totalCommitUs = 0;
  uint32_t commitCount = 0;
#endif
};

// Konfiguracja: 1 = mirror włączony, 0 = wyłączony
#ifndef UART_LCD_MIRROR
#if CORE_DEBUG_LEVEL > 0
#define UART_LCD_MIRROR 1
#else
#define UART_LCD_MIRROR 0
#endif
#endif

#if UART_LCD_MIRROR
class LcdMirror20x4 : public Print {
public:
  void begin() { clear(); }

  void clear() {
    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 20; c++) buf[r][c] = ' ';
      buf[r][20] = '\0';
    }
    x = 0; y = 0;
  }

  void clearRow(uint8_t row) {
    if (row >= 4) {
      return;
    }
    for (int c = 0; c < 20; ++c) buf[row][c] = ' ';
  }

  void setCursor(uint8_t col, uint8_t row) {
    x = col; y = row;
  }

  size_t write(uint8_t ch) override {
    if (y < 4 && x < 20) {
      char out = (ch >= 32) ? (char)ch : '?';
      if (ch == 0) out = '*';
      buf[y][x] = out;
    }
    if (x < 20) x++;
    return 1;
  }

  void dumpUART() {
    Serial.println();
    Serial.println("+--------------------+");
    for (int r = 0; r < 4; r++) {
      Serial.print("|");
      for (int c = 0; c < 20; ++c) {
        uint8_t ch = (uint8_t)buf[r][c];
        if (ch == 0xDF) {
          const uint8_t deg[] = {0xC2, 0xB0}; // UTF-8 degree sign
          Serial.write(deg, sizeof(deg));
        } else if (ch >= 32 && ch < 127) {
          Serial.write(ch);
        } else {
          Serial.write('?');
        }
      }
      Serial.println("|");
    }
    Serial.println("+--------------------+");
  }

private:
  char buf[4][21];
  uint8_t x = 0, y = 0;
};
#endif

// Globalne obiekty
extern LcdFrameBuffer20x4 lcdFrame;
#if UART_LCD_MIRROR
extern LcdMirror20x4 lcdMirror;
#endif

// Wrappery - działają niezależnie od UART_LCD_MIRROR
#if UART_LCD_MIRROR
  #define LCD_CLEAR() do { lcdFrame.clear(); lcdMirror.clear(); } while(0)
  #define LCD_CLEAR_ROW(r) do { lcdFrame.clearRow(r); lcdMirror.clearRow(r); } while(0)
  #define LCD_SET(c, r) do { lcdFrame.setCursor(c, r); lcdMirror.setCursor(c, r); } while(0)
  #define LCD_PRINT(v) do { lcdFrame.print(v); lcdMirror.print(v); } while(0)
  #define LCD_WRITE(b) do { lcdFrame.write(b); lcdMirror.write(b); } while(0)
  #define LCD_DUMP() do { lcdFrame.commit(); lcdMirror.dumpUART(); } while(0)
#else
  #define LCD_CLEAR() lcdFrame.clear()
  #define LCD_CLEAR_ROW(r) lcdFrame.clearRow(r)
  #define LCD_SET(c, r) lcdFrame.setCursor(c, r)
  #define LCD_PRINT(v) lcdFrame.print(v)
  #define LCD_WRITE(b) lcdFrame.write(b)
  #define LCD_DUMP() do { lcdFrame.commit(); } while(0)
#endif

#endif