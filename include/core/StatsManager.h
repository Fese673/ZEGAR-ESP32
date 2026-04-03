#pragma once
#include <Arduino.h>
#include <Preferences.h>

struct AppStats {        // Struktura statystyk
    uint32_t totalClicks;
    uint32_t stepsLeft;
    uint32_t stepsRight;
};

struct EnvStats {        // Struktura do przechowywania min/max temperatury i wilgotności
    float tempMin;
    float tempMax;
    float humMin;
    float humMax;
};


class StatsManager {   // Zarządza statystykami aplikacji, zapisuje do NVS i odczytuje przy starcie
private:
    Preferences prefs;
    AppStats currentStats;
    bool isDirty;
    unsigned long lastSaveTime;
    const unsigned long SAVE_INTERVAL_MS = 120000; // Zmiana interwału zapisu na 120s tzw leniwe zapisywanie, aby zmniejszyć zużycie flasha
    const char* PREFS_NAMESPACE = "app_stats";

    EnvStats envStats; 
    bool envDirty;
    unsigned long lastEnvSaveTime;
    const unsigned long ENV_SAVE_INTERVAL_MS = 300000; // 5 minut (lazywriter co 5 minut dla ENV)

public:
      // Funkcje do aktualizacji 
      void updateTemperature(float t);
      void updateHumidity(float h);
      EnvStats getEnvStats() const;

    void begin() {
         // Open read-write to create namespace on first boot and avoid NOT_FOUND noise.
         if (prefs.begin(PREFS_NAMESPACE, false)) {
            // Kliknięcia / kroki
            currentStats.totalClicks = prefs.isKey("clicks") ? prefs.getUInt("clicks", 0) : 0;
            currentStats.stepsLeft  = prefs.isKey("left") ? prefs.getUInt("left", 0) : 0;
            currentStats.stepsRight = prefs.isKey("right") ? prefs.getUInt("right", 0) : 0;

            // ENV
            envStats.tempMin = prefs.isKey("tmin") ? prefs.getFloat("tmin", 1000.0f) : 1000.0f;
            envStats.tempMax = prefs.isKey("tmax") ? prefs.getFloat("tmax", -1000.0f) : -1000.0f;
            envStats.humMin  = prefs.isKey("hmin") ? prefs.getFloat("hmin", 1000.0f) : 1000.0f;
            envStats.humMax  = prefs.isKey("hmax") ? prefs.getFloat("hmax", -1000.0f) : -1000.0f;

            prefs.end();
         } else {
            currentStats = {0, 0, 0};
            envStats = {1000.0f, -1000.0f, 1000.0f, -1000.0f};
         }

     isDirty = false;
     envDirty = false;
     lastSaveTime = millis();
     lastEnvSaveTime = millis();

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
 
    // Zapis klików/kroków
    if (isDirty && (millis() - lastSaveTime > SAVE_INTERVAL_MS)) {
        saveStats();
    }

    // Zapis ENV 
    if (envDirty && (millis() - lastEnvSaveTime > ENV_SAVE_INTERVAL_MS)) {
        prefs.begin(PREFS_NAMESPACE, false);
        prefs.putFloat("tmin", envStats.tempMin);
        prefs.putFloat("tmax", envStats.tempMax);
        prefs.putFloat("hmin", envStats.humMin);
        prefs.putFloat("hmax", envStats.humMax);
        prefs.end();

        envDirty = false;
        lastEnvSaveTime = millis();
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
