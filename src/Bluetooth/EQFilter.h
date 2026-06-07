#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
struct BiquadState {
    float x1 = 0.0f, x2 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f;
    
    void reset() {
        x1 = x2 = y1 = y2 = 0.0f;
    }
};

struct BiquadCoeffs {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    bool bypass = true;
};

class EQFilter {
public:
    BiquadCoeffs coeffs[2];
    std::atomic<int> activeIdx{0};
    BiquadState left;
    BiquadState right;

    void updatePeaking(float f0, float fs, float dBgain, float Q) {
        int nextIdx = 1 - activeIdx.load(std::memory_order_relaxed);
        BiquadCoeffs &c = coeffs[nextIdx];

        if (std::fabs(dBgain) < 0.05f) {
            c.bypass = true;
        } else {
            c.bypass = false;
            float A = std::pow(10.0f, dBgain / 40.0f);
            float omega = 2.0f * 3.1415926535f * f0 / fs;
            float sn = std::sin(omega);
            float cs = std::cos(omega);
            float alpha = sn / (2.0f * Q);

            float a0 = 1.0f + alpha / A;
            c.b0 = (1.0f + alpha * A) / a0;
            c.b1 = (-2.0f * cs) / a0;
            c.b2 = (1.0f - alpha * A) / a0;
            c.a1 = (-2.0f * cs) / a0;
            c.a2 = (1.0f - alpha / A) / a0;
        }
        activeIdx.store(nextIdx, std::memory_order_release);
    }

    inline float processLeft(float x, const BiquadCoeffs &c) {
        float y = c.b0 * x + c.b1 * left.x1 + c.b2 * left.x2 - c.a1 * left.y1 - c.a2 * left.y2;
        left.x2 = left.x1;
        left.x1 = x;
        left.y2 = left.y1;
        left.y1 = y;
        return y;
    }

    inline float processRight(float x, const BiquadCoeffs &c) {
        float y = c.b0 * x + c.b1 * right.x1 + c.b2 * right.x2 - c.a1 * right.y1 - c.a2 * right.y2;
        right.x2 = right.x1;
        right.x1 = x;
        right.y2 = right.y1;
        right.y1 = y;
        return y;
    }

    void reset() {
        left.reset();
        right.reset();
    }
};

extern EQFilter s_bassFilter;
extern EQFilter s_midFilter;
extern EQFilter s_trebleFilter;

void updateEQFilters(uint8_t bass, uint8_t mid, uint8_t treble);

