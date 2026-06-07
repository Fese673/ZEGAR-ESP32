#ifndef LIQUIDCRYSTAL_I2C_H
#define LIQUIDCRYSTAL_I2C_H

#include <Arduino.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
class LiquidCrystal_I2C : public hd44780_I2Cexp {
public:
  LiquidCrystal_I2C(uint8_t addr, uint8_t cols, uint8_t rows)
    : _addr(addr), _cols(cols), _rows(rows) {}

  void init() {
    (void)_addr;
    begin(_cols, _rows);
  }

  void backlight() {
    setBacklight(HIGH);
  }

  void noBacklight() {
    setBacklight(LOW);
  }

private:
  uint8_t _addr;
  uint8_t _cols;
  uint8_t _rows;
};

#endif