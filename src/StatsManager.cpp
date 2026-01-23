#include "StatsManager.h"

StatsManager statsManager;

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

    if (changed) envDirty = true;
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

    if (changed) envDirty = true;
}

EnvStats StatsManager::getEnvStats() const {
    return envStats;
}
