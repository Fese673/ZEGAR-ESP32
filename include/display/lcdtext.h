#ifndef LCDTEXT_H
#define LCDTEXT_H

#include <Arduino.h>

// UTF-8 -> HD44780 (ROM A00) folder used by the 20x4 LCD write path.
// Polish diacritics are folded to ASCII, UTF-8 degree/micro map to ROM glyphs
// 0xDF/0xE4, and intentional raw ROM bytes are passed through untouched.
class LcdUtf8Folder {
 public:
  void reset() {
    codepoint = 0;
    remaining = 0;
    sequenceLength = 0;
  }

  uint8_t feed(uint8_t ch, uint8_t out[2]) {
    if (remaining > 0) {
      if ((ch & 0xC0) == 0x80) {
        codepoint = static_cast<uint16_t>((codepoint << 6) | (ch & 0x3F));
        if (--remaining == 0) {
          out[0] = (sequenceLength == 4) ? static_cast<uint8_t>('?')
                                         : foldCodepoint(codepoint);
          return 1;
        }
        return 0;
      }
      remaining = 0;
      out[0] = '?';
      return static_cast<uint8_t>(1 + feed(ch, out + 1));
    }

    if (ch < 0x80) {
      out[0] = ch;
      return 1;
    }
    if (ch == 0xDF || ch == 0xE4) {
      out[0] = ch;
      return 1;
    }
    if (ch >= 0xC2 && ch <= 0xDF) {
      codepoint = ch & 0x1F;
      remaining = 1;
      sequenceLength = 2;
      return 0;
    }
    if (ch >= 0xE0 && ch <= 0xEF) {
      codepoint = ch & 0x0F;
      remaining = 2;
      sequenceLength = 3;
      return 0;
    }
    if (ch >= 0xF0 && ch <= 0xF4) {
      codepoint = 0;
      remaining = 3;
      sequenceLength = 4;
      return 0;
    }

    out[0] = '?';
    return 1;
  }

 private:
  static uint8_t foldCodepoint(uint16_t cp) {
    if (cp == 0x00A0) {
      return ' ';
    }
    if (cp == 0x00B0) {
      return 0xDF;
    }
    if (cp == 0x00B5) {
      return 0xE4;
    }
    if (cp >= 0x00C0 && cp <= 0x00FF) {
      static const char kLatin1[] =
          "AAAAAAECEEEEIIIIDNOOOOOxOUUUUY?s"
          "aaaaaaeceeeeiiiidnooooo/ouuuuy?y";
      return static_cast<uint8_t>(kLatin1[cp - 0x00C0]);
    }
    switch (cp) {
      case 0x0104: return 'A';
      case 0x0105: return 'a';
      case 0x0106: return 'C';
      case 0x0107: return 'c';
      case 0x0118: return 'E';
      case 0x0119: return 'e';
      case 0x0141: return 'L';
      case 0x0142: return 'l';
      case 0x0143: return 'N';
      case 0x0144: return 'n';
      case 0x015A: return 'S';
      case 0x015B: return 's';
      case 0x0179: return 'Z';
      case 0x017A: return 'z';
      case 0x017B: return 'Z';
      case 0x017C: return 'z';
      case 0x2013:
      case 0x2014: return '-';
      case 0x2018:
      case 0x2019: return '\'';
      case 0x201C:
      case 0x201D: return '"';
      case 0x2026: return '.';
      default: return '?';
    }
  }

  uint16_t codepoint = 0;
  uint8_t remaining = 0;
  uint8_t sequenceLength = 0;
};

inline int lcdVisibleLength(const char* text) {
  if (text == nullptr) {
    return 0;
  }
  int cells = 0;
  for (const uint8_t* p = reinterpret_cast<const uint8_t*>(text); *p != 0; ++p) {
    if ((*p & 0xC0) == 0x80) {
      continue;
    }
    ++cells;
  }
  return cells;
}

#endif
