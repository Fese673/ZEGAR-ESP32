#pragma once
#include <Arduino.h>
#include <Preferences.h>

struct AppStats {
    uint32_t totalClicks;
    uint32_t stepsLeft;
    uint32_t stepsRight;
};

class StatsManager {
private:
    Preferences prefs;
    AppStats currentStats;
    bool isDirty;
    unsigned long lastSaveTime;
    const unsigned long SAVE_INTERVAL_MS = 120000; // Zmiana interwału zapisu na 120s
    const char* PREFS_NAMESPACE = "app_stats";

public:
    void begin() {
        prefs.begin(PREFS_NAMESPACE, true); // Read-only mode first
        currentStats.totalClicks = prefs.getUInt("clicks", 0);
        currentStats.stepsLeft = prefs.getUInt("left", 0);
        currentStats.stepsRight = prefs.getUInt("right", 0);
        prefs.end();
        
        isDirty = false;
        lastSaveTime = millis();
    }

    void registerClick() {
        currentStats.totalClicks++;
        isDirty = true;
    }

    void registerStepLeft() {
        currentStats.stepsLeft++;
        isDirty = true;
    }

    void registerStepRight() {
        currentStats.stepsRight++;
        isDirty = true;
    }

    // Wywoływać w loop()
    void update() {
        if (isDirty && (millis() - lastSaveTime > SAVE_INTERVAL_MS)) {
            saveStats();
        }
    }

    // Wymuszenie zapisu (np. przy wyłączaniu lub resecie)
    void saveStats() {
        if (!isDirty) return;
        
        prefs.begin(PREFS_NAMESPACE, false); // Read-write mode
        prefs.putUInt("clicks", currentStats.totalClicks);
        prefs.putUInt("left", currentStats.stepsLeft);
        prefs.putUInt("right", currentStats.stepsRight);
        prefs.end();
        
        isDirty = false;
        lastSaveTime = millis();
    }
    
    // Funkcja do resetu (ukryta w UI)
    void resetStats() {
        currentStats = {0, 0, 0};
        prefs.begin(PREFS_NAMESPACE, false);
        prefs.clear(); // Czyści cały namespace
        prefs.end();
        isDirty = false;
    }

    AppStats getStats() const {
        return currentStats;
    }
    
    uint32_t getTotalSteps() const {
        return currentStats.stepsLeft + currentStats.stepsRight;
    }
};

extern StatsManager statsManager;
