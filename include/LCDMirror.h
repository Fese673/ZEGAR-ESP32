#ifndef LCDMIRROR_H
#define LCDMIRROR_H

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>

// Konfiguracja: 1 = mirror włączony, 0 = wyłączony
#define UART_LCD_MIRROR 1

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
extern LiquidCrystal_I2C lcd;
#if UART_LCD_MIRROR
extern LcdMirror20x4 lcdMirror;
#endif

// Wrappery - działają niezależnie od UART_LCD_MIRROR
#if UART_LCD_MIRROR
  #define LCD_CLEAR() do { lcd.clear(); lcdMirror.clear(); } while(0)
  #define LCD_SET(c, r) do { lcd.setCursor(c, r); lcdMirror.setCursor(c, r); } while(0)
  #define LCD_PRINT(v) do { lcd.print(v); lcdMirror.print(v); } while(0)
  #define LCD_WRITE(b) do { lcd.write(b); lcdMirror.write(b); } while(0)
  #define LCD_DUMP() lcdMirror.dumpUART()
#else
  #define LCD_CLEAR() lcd.clear()
  #define LCD_SET(c, r) lcd.setCursor(c, r)
  #define LCD_PRINT(v) lcd.print(v)
  #define LCD_WRITE(b) lcd.write(b)
  #define LCD_DUMP() do {} while(0)
#endif

#endif