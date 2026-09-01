#pragma once

/*
 * Etap 3 debug: diagnostyka flow jasnosci 7-seg.
 *
 * Uzywane w ZEGAR-ESP32 do sledzenia sciezki: GUTION -> COBS -> ZEGAR -> STM32.
 * Wylaczane jednym define (DEBUG_7SEG=0).
 *
 * Logi ida na Serial (UART ZEGAR) przez AppLog. Format: "[7SEG][I] ...".
 * Nie spamuje — max ~kilka linii na zmiane jasnosci z GUTION.
 */

#include "AppLog.h"

#ifndef DEBUG_7SEG
#define DEBUG_7SEG 0
#endif

#if DEBUG_7SEG
#define DBG_7SEG_LOG(fmt, ...) do { \
    LOG_I("7SEG", fmt, ##__VA_ARGS__); \
} while (0)
#else
#define DBG_7SEG_LOG(fmt, ...) ((void)0)
#endif
