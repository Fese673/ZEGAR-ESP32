/*
  * AppState.h
  * Definicje typów i zmiennych globalnych stanu aplikacji
*/
#pragma once

enum AppState {
  // --- Główne tryby ---
  STATE_HOME,
  STATE_MENU,
  STATE_GAMES_MENU,      // Menu gier (lista z >)
  STATE_BT_MUSIC_CONTROL, // Ekran sterowania muzyką BT
  STATE_SAFE_CRACKER,    // Gra Safe Cracker
  STATE_TANK_GAME,       // Gra Tank Game
  STATE_SET_TIME,
  STATE_TIMER,
  STATE_STOPER,
  STATE_DEBUG_STM32,

  // --- Statystyki ---
  STATE_STATS,          // Menu statystyk (lista z >)
  STATE_STATS_CLICKS,   // Widok samych kliknięć
  STATE_STATS_STEPS,    // Widok kroków L/R
  STATE_STATS_TEMP,     // Widok min/max temperatury
  STATE_STATS_HUM,      // Widok min/max wilgotności

  // --- Zasoby systemu ---
  STATE_STATS_RESOURCES_MENU,   // Menu zasobów (RAM, CPU, Flash, Audio)
  STATE_STATS_RESOURCES_RAM,    // Widok pamięci RAM
  STATE_STATS_RESOURCES_CPU,    // Widok obciążenia CPU
  STATE_STATS_RESOURCES_FLASH,  // Widok pamięci Flash
  STATE_STATS_RESOURCES_AUDIO,  // Widok stabilności audio

  // --- PMS5003 / ENS160 + AHT21 ---
  STATE_PMS5003,
  STATE_PMS5003_CF1,
  STATE_ENS160_AHT21,
  STATE_ENS160_AHT21_SUMMARY,
  STATE_ENS160_AHT21_GAS,
  STATE_ENS160_AHT21_GAS_AQI,
  STATE_ENS160_AHT21_GAS_TVOC,
  STATE_ENS160_AHT21_GAS_ECO2,
  STATE_ENS160_AHT21_CLIMATE,
  STATE_ENS160_AHT21_CLIMATE_TEMP,
  STATE_ENS160_AHT21_CLIMATE_HUM,
  STATE_ENS160_AHT21_STATUS,

  // --- BMP280 ---
  STATE_BMP280,
  STATE_BMP280_TEMP,
  STATE_BMP280_PRESSURE,
  STATE_BMP280_STATUS,
  STATE_BMP280_ALTITUDE,

  // --- Ustawienia (Settings) ---
  STATE_SETTINGS,              // Menu Ustawień (lista z >)
  STATE_SETTINGS_PMS5003,      // Ustawienia PMS5003 (włącz/wyłącz)
  STATE_SETTINGS_BUZZER,       // Ustawienia Buzera (włącz/wyłącz)
  STATE_SETTINGS_TOUCH,        // Ustawienia DOTYK (włącz/wyłącz)
  STATE_SETTINGS_BACKGROUND_MUSIC, // Muzyka w tle (włącz/wyłącz)
  STATE_SETTINGS_MQTT,         // Ustawienia MQTT (włącz/wyłącz)
  STATE_SETTINGS_ALARM_MELODY, // Wybór melodii alarmu
  STATE_SETTINGS_BOOT_INTRO,   // Ustawienia intro startowego (włącz/wyłącz)
  STATE_SETTINGS_SYNC,         // Ustawienia synchronizacji NTP (minuty)
  STATE_SETTINGS_ROTATION,     // Ustawienie: Rotacja ekranu (1..10s)
  STATE_SETTINGS_UI_SCREEN,    // Profil UI ekranu

  // --- Budziki (alarmy) ---
  STATE_ALARMS_LIST,          // Lista budzików
  STATE_ALARM_EDIT,           // Edycja pojedynczego budzika

  // --- PMS5003 szczegóły ---
  STATE_PMS5003_CF1_PM1,      // Szczegóły PM1.0 w CF=1
  STATE_PMS5003_CF1_PM25,     // Szczegóły PM2.5 w CF=1
  STATE_PMS5003_CF1_PM10,     // Szczegóły PM10 w CF=1
  STATE_PMS5003_ATM,          // Menu trybu atmosferycznego (dane bieżące)
  STATE_PMS5003_ATM_PM1,      // Szczegóły PM1.0 w ATM
  STATE_PMS5003_ATM_PM25,     // Szczegóły PM2.5 w ATM
  STATE_PMS5003_ATM_PM10,     // Szczegóły PM10 w ATM
  STATE_PMS5003_PARTICLES,    // Menu liczby cząstek (dane bieżące)
  STATE_PMS5003_PARTICLES_0_3,
  STATE_PMS5003_PARTICLES_0_5,
  STATE_PMS5003_PARTICLES_1_0,
  STATE_PMS5003_PARTICLES_2_5,
  STATE_PMS5003_PARTICLES_5_0,
  STATE_PMS5003_PARTICLES_10_0,
  STATE_PMS5003_TELEMETRY,    // Telemetria czujnika (błędy, bajty, latencja)
};

// --- Tryby edycji czasu (EditState) ---
enum EditState {
  EDIT_HOURS,
  EDIT_MINUTES,
  EDIT_SECONDS,
  EDIT_DONE,
};

// --- Tryby radia (RadioMode) ---
#include <atomic>

enum RadioMode {
  WIFI_ONLY,
  BT_ONLY,
};

//--- Globalne zmienne stanu aplikacji ---
extern AppState appState;
extern EditState editState;
extern std::atomic<RadioMode> radioMode;

// Flaga: true gdy użytkownik edytuje alarm enkoderem (blokada dla Gution)
extern volatile bool g_alarmEditActive;
