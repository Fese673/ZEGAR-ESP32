#include "LCDMirror.h"

// Definicje globalnych obiektów
LiquidCrystal_I2C lcd(0x27, 20, 4); 
LcdFrameBuffer20x4 lcdFrame;

#if UART_LCD_MIRROR
LcdMirror20x4 lcdMirror;
#endif