#include "StatsManager.h"

StatsManager statsManager;

void StatsManager::begin() {
    if (prefs.begin(PREFS_NAMESPACE, false)) {
        loadStats();
        loadEnvStats();
        prefs.end();
    } else {
        currentStats = {};
        envStats = {1000.0f, -1000.0f, 1000.0f, -1000.0f};
    }

    isDirty = false;
    envDirty = false;
    lastSaveTime = millis();
    lastEnvSaveTime = millis();
}

void StatsManager::loadStats() {
    currentStats.totalClicks = prefs.isKey(PREFS_KEY_CLICKS) ? prefs.getUInt(PREFS_KEY_CLICKS, 0) : 0;
    currentStats.stepsLeft = prefs.isKey(PREFS_KEY_LEFT) ? prefs.getUInt(PREFS_KEY_LEFT, 0) : 0;
    currentStats.stepsRight = prefs.isKey(PREFS_KEY_RIGHT) ? prefs.getUInt(PREFS_KEY_RIGHT, 0) : 0;
}

void StatsManager::loadEnvStats() {
    envStats.tempMin = prefs.isKey(PREFS_KEY_TMIN) ? prefs.getFloat(PREFS_KEY_TMIN, 1000.0f) : 1000.0f;
    envStats.tempMax = prefs.isKey(PREFS_KEY_TMAX) ? prefs.getFloat(PREFS_KEY_TMAX, -1000.0f) : -1000.0f;
    envStats.humMin = prefs.isKey(PREFS_KEY_HMIN) ? prefs.getFloat(PREFS_KEY_HMIN, 1000.0f) : 1000.0f;
    envStats.humMax = prefs.isKey(PREFS_KEY_HMAX) ? prefs.getFloat(PREFS_KEY_HMAX, -1000.0f) : -1000.0f;
}

void StatsManager::update() {
    if (isDirty && (millis() - lastSaveTime) > kSaveIntervalMs) {
        saveStats();
    }

    if (envDirty && (millis() - lastEnvSaveTime) > kEnvSaveIntervalMs) {
        saveEnvStats();
    }
}

void StatsManager::registerClick() {
    currentStats.totalClicks++;
    isDirty = true;
}

void StatsManager::registerStepLeft() {
    currentStats.stepsLeft++;
    isDirty = true;
}

void StatsManager::registerStepRight() {
    currentStats.stepsRight++;
    isDirty = true;
}

void StatsManager::updateTemperature(float t) {
    bool changed = false;

    if (t < envStats.tempMin) {
        envStats.tempMin = t;
        changed = true;
    }
    if (t > envStats.tempMax) {
        envStats.tempMax = t;
        changed = true;
    }

    if (changed) {
        envDirty = true;
    }
}

void StatsManager::updateHumidity(float h) {
    bool changed = false;

    if (h < envStats.humMin) {
        envStats.humMin = h;
        changed = true;
    }
    if (h > envStats.humMax) {
        envStats.humMax = h;
        changed = true;
    }

    if (changed) {
        envDirty = true;
    }
}

void StatsManager::saveStats() {
    if (!isDirty) {
        return;
    }

    if (prefs.begin(PREFS_NAMESPACE, false)) {
        prefs.putUInt(PREFS_KEY_CLICKS, currentStats.totalClicks);
        prefs.putUInt(PREFS_KEY_LEFT, currentStats.stepsLeft);
        prefs.putUInt(PREFS_KEY_RIGHT, currentStats.stepsRight);
        prefs.end();
    }

    isDirty = false;
    lastSaveTime = millis();
}

void StatsManager::saveEnvStats() {
    if (!envDirty) {
        return;
    }

    if (prefs.begin(PREFS_NAMESPACE, false)) {
        prefs.putFloat(PREFS_KEY_TMIN, envStats.tempMin);
        prefs.putFloat(PREFS_KEY_TMAX, envStats.tempMax);
        prefs.putFloat(PREFS_KEY_HMIN, envStats.humMin);
        prefs.putFloat(PREFS_KEY_HMAX, envStats.humMax);
        prefs.end();
    }

    envDirty = false;
    lastEnvSaveTime = millis();
}

void StatsManager::resetStats() {
    currentStats = {};
    envStats = {1000.0f, -1000.0f, 1000.0f, -1000.0f};

    if (prefs.begin(PREFS_NAMESPACE, false)) {
        prefs.clear();
        prefs.end();
    }

    isDirty = false;
    envDirty = false;
}

AppStats StatsManager::getStats() const {
    return currentStats;
}

EnvStats StatsManager::getEnvStats() const {
    return envStats;
}

uint32_t StatsManager::getTotalSteps() const {
    return currentStats.stepsLeft + currentStats.stepsRight;
}
