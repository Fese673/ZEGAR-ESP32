/*
 * StatsManager.h
 * Zarządza statystykami aplikacji i ich przechowywaniem w NVS.
 */
#pragma once

#include <Arduino.h>
#include <Preferences.h>

// --- Dane statystyk aplikacji ---
struct AppStats {
    uint32_t totalClicks = 0;
    uint32_t stepsLeft = 0;
    uint32_t stepsRight = 0;
};

// --- Dane statystyk środowiskowych ---
struct EnvStats {
    float tempMin = 1000.0f;
    float tempMax = -1000.0f;
    float humMin = 1000.0f;
    float humMax = -1000.0f;
};

// --- Manager statystyk ---
class StatsManager {
public:
    void begin();
    void update();

    void registerClick();
    void registerStepLeft();
    void registerStepRight();

    void updateTemperature(float t);
    void updateHumidity(float h);

    void saveStats();
    void resetStats();

    AppStats getStats() const;
    EnvStats getEnvStats() const;
    uint32_t getTotalSteps() const;

private:
    void loadStats();
    void loadEnvStats();
    void saveEnvStats();

    Preferences prefs;
    AppStats currentStats;
    EnvStats envStats;

    bool isDirty = false;
    bool envDirty = false;

    unsigned long lastSaveTime = 0;
    unsigned long lastEnvSaveTime = 0;

    const char* PREFS_NAMESPACE = "app_stats";
    const char* PREFS_KEY_CLICKS = "clicks";
    const char* PREFS_KEY_LEFT = "left";
    const char* PREFS_KEY_RIGHT = "right";
    const char* PREFS_KEY_TMIN = "tmin";
    const char* PREFS_KEY_TMAX = "tmax";
    const char* PREFS_KEY_HMIN = "hmin";
    const char* PREFS_KEY_HMAX = "hmax";

    static constexpr unsigned long kSaveIntervalMs = 120000UL;
    static constexpr unsigned long kEnvSaveIntervalMs = 300000UL;
};

extern StatsManager statsManager;
