#pragma once

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>

namespace LCDIcons {

constexpr uint8_t kCgramSlots = 8;
constexpr uint8_t NtpSlot = 0;
constexpr uint8_t BellSlot = 1;
constexpr uint8_t AlarmSlot = NtpSlot;
constexpr uint8_t WifiSlot = 7;

enum class IconId : uint8_t {
  Ntp = 0,
  Alarm = Ntp,
  Gauge = Ntp,
  Heart = 1,
  Bell = 2,
  Cross = 3,
  Check = 4,
  Smile = 5,
  Sad = 6,
  Note = 7,
  Wifi = 8,
  ArrowUp = 9,
  ArrowDown = 10,
  BatteryFull = 11,
  BatteryMed = 12,
  BatteryLow = 13,
  BatteryEmpty = 14,
  Cup = 15,
  Sleep = 16,
  Tag = 17,
  MapPin = 18,
  Star = 19,
  Star2 = 20,
  Flag = 21,
  Edit = 22,
  Trash = 23,
  Save = 24,
  Folder = 25,
  Eye = 26,
  Key = 27,
  Magnify = 28,
  Power = 29,
  Cursor = 30,
  Menu3 = 31,
  OkBox = 32,
  Robot = 33,
  ChartBar = 34,
  Cloud = 35,
  Rain = 36,
  Snow = 37,
  Wind = 38,
  Pressure = 39,
  Snowflake = 40,
  Umbrella = 41,
  Flame = 42,
  Leaf = 43,
  Degree = 44,
  Lock = 45,
  Unlock = 46,
  Speaker = 47,
  Mute = 48,
  Settings = 49,
  Person = 50,
  Plug = 51,
  Signal = 52,
  Antenna = 53,
  Target = 54,
  Lightning = 55,
  CloudSun = 56,
  CloudRain2 = 57,
  TempHi = 58,
  TempLo = 59,
  Compass = 60,
  Waves = 61,
  CO2 = 62,
  AQI = 63,
  Pollen = 64,
  Sunrise = 65,
  Sunset = 66,
  NightStar = 67,
  Fog = 68,
  Hail = 69,
  Snowman = 70,
  Hot = TempHi,
  Cold = 71,
  Clock2 = 72,
  Count = 73,
};

void loadIcon(LiquidCrystal_I2C& lcd, uint8_t slot, IconId iconId);
void loadIcons(LiquidCrystal_I2C& lcd, const IconId* iconIds, uint8_t count, uint8_t firstSlot = 0);

enum class Palette : uint8_t {
  Home,
  HomeWifi,
  Weather,
  System,
  Media,
};

void loadPalette(LiquidCrystal_I2C& lcd, Palette palette);

}  // namespace LCDIcons