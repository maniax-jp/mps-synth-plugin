// 合成部品(オシレーター、フィルター、歪み、Phaser、オールパス)。すべて 1 サンプルずつ処理する。
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mps {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

inline double mtof(double m) { return 440.0 * std::pow(2.0, (m - 69.0) / 12.0); }
inline double dbToGain(double db) { return std::pow(10.0, db / 20.0); }
inline double frac(double x) { return x - std::floor(x); }

struct Rng {
    uint64_t s;
    bool hasSpare = false;
    double spare = 0;
    explicit Rng(uint64_t seed = 0x9E3779B97F4A7C15ull) : s(seed ? seed : 1) {}
    uint64_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
    double uniform() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
    double gauss() {
        if (hasSpare) { hasSpare = false; return spare; }
        double u = std::max(1e-12, uniform()), v = uniform();
        double r = std::sqrt(-2.0 * std::log(u));
        spare = r * std::sin(kTwoPi * v);
        hasSpare = true;
        return r * std::cos(kTwoPi * v);
    }
};

// ---- 帯域制限オシレーター(polyBLEP)
inline double polyblep(double t, double dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}
inline double sawAt(double ph, double dt) {
    ph = frac(ph);
    return 2.0 * ph - 1.0 - polyblep(ph, dt);
}
// 振幅 ±1 のパルス(pw = 正の区間の割合)
inline double pulseAt(double ph, double dt, double pw) {
    return sawAt(ph + 1.0 - pw, dt) - sawAt(ph, dt) - (1.0 - 2.0 * pw);
}
inline double triAt(double ph) {
    ph = frac(ph);
    return 4.0 * std::fabs(ph - 0.5) - 1.0;
}

// ---- Cytomic の TPT 状態変数フィルター(係数を動かしても安定。RBJ の biquad と同じ周波数特性)
struct Svf {
    double ic1 = 0, ic2 = 0, a1 = 1, a2 = 0, a3 = 0, k = 1.414;
    void set(double fc, double q, double sr) {
        fc = std::min(std::max(fc, 10.0), sr * 0.45);
        double g = std::tan(kPi * fc / sr);
        k = 1.0 / std::max(q, 0.05);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0; }
    // lp / bp(ピーク 0 dB に正規化)/ hp
    inline void tick(double v0, double& lp, double& bp, double& hp) {
        double v3 = v0 - ic2;
        double v1 = a1 * ic1 + a2 * v3;
        double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        lp = v2;
        bp = k * v1;
        hp = v0 - k * v1 - v2;
    }
};

// ---- 転置直接形 II の biquad(固定のポストフィルター・オールパス用)
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline double tick(double x) {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
    void setRaw(double nb0, double nb1, double nb2, double na0, double na1, double na2) {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }
    // RBJ。butter(2) は q = 1/sqrt(2)
    void lowpass(double fc, double q, double sr) {
        fc = std::min(std::max(fc, 10.0), sr * 0.45);
        double w = kTwoPi * fc / sr, c = std::cos(w), al = std::sin(w) / (2 * q);
        setRaw((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highpass(double fc, double q, double sr) {
        fc = std::min(std::max(fc, 10.0), sr * 0.45);
        double w = kTwoPi * fc / sr, c = std::cos(w), al = std::sin(w) / (2 * q);
        setRaw((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void allpass(double fc, double q, double sr) {
        fc = std::min(std::max(fc, 10.0), sr * 0.45);
        double w = kTwoPi * fc / sr, c = std::cos(w), al = std::sin(w) / (2 * q);
        setRaw(1 - al, -2 * c, 1 + al, 1 + al, -2 * c, 1 - al);
    }
};

// ---- 歪み(tanh は tanh(d*x)/tanh(d) でピークを保つ、diode は負側を弱めた非対称)
enum DriveType { kDriveOff = 0, kDriveTanh, kDriveDiode, kDriveFold, kDriveHard };
inline double drive(int type, double x, double amt) {
    switch (type) {
        case kDriveTanh: return std::tanh(amt * x) / std::tanh(amt);
        case kDriveDiode: return x > 0 ? std::tanh(amt * x) : 0.7 * std::tanh(1.4 * amt * x);
        case kDriveFold: return std::sin(amt * x * kPi * 0.5);
        case kDriveHard: return std::max(-1.0, std::min(1.0, amt * x));
        default: return x;
    }
}

// ---- Phaser: 1 次オールパス 4 段を LFO で動かす
struct Phaser {
    double xp[4] = {0, 0, 0, 0}, yp[4] = {0, 0, 0, 0};
    double t = 0;
    void reset() { for (int k = 0; k < 4; ++k) xp[k] = yp[k] = 0; t = 0; }
    inline double tick(double x, double center, double depth, double rate, double sr, bool advance) {
        double v = x;
        for (int k = 0; k < 4; ++k) {
            double fc = std::max(20.0, center + depth * std::sin(kTwoPi * rate * t + k));
            double tn = std::tan(kPi * std::min(fc, sr * 0.45) / sr);
            double a = (tn - 1) / (tn + 1);
            double y = a * v + xp[k] - a * yp[k];
            xp[k] = v;
            yp[k] = y;
            v = y;
        }
        if (advance) t += 1.0 / sr;
        return v;
    }
};

// ---- オールパス(位相の分散): 2 次オールパスを何段も重ねて、指定した周波数付近の位相を引き伸ばす
struct AllpassChain {
    static constexpr int kMax = 48;
    Biquad st[kMax];
    int n = 0;
    double f = -1, sr = 0;
    void configure(int stages, double freq, double rate) {
        stages = std::min(stages, kMax);
        if (freq != f || rate != sr) {
            for (auto& s : st) s.allpass(freq, 0.9, rate);
            f = freq;
            sr = rate;
        }
        if (stages > n)
            for (int i = n; i < stages; ++i) st[i].reset();
        n = stages;
    }
    inline double tick(double x) {
        for (int i = 0; i < n; ++i) x = st[i].tick(x);
        return x;
    }
};

}  // namespace mps
