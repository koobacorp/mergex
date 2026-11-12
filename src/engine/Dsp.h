// MergeX — small DSP toolkit: FFT, windows, random numbers, one-pole smoothing, running max/mean, tone generators.
// Original code. No JUCE, so the engine tests compile anywhere.
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mx {
constexpr double kPi = 3.14159265358979323846;
using cpx = std::complex<float>;

inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
inline float dbToGain(float db) { return std::pow(10.f, db / 20.f); }
inline float flushTiny(float x) { return std::abs(x) < 1e-25f ? 0.f : x; }
inline float princarg(float p) { return p - 2.f * (float)kPi * std::floor((p + (float)kPi) / (2.f * (float)kPi)); }

/// xorshift32: deterministic, cheap, good enough for grain choices and noise.
struct Rng {
    uint32_t s = 0x9E3779B9u;
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float)(next() >> 8) * (1.f / 16777216.f); }   // [0,1)
    float bi() { return uni() * 2.f - 1.f; }                            // [-1,1)
};

/// Iterative radix-2 complex FFT with precomputed twiddles (double-accurate tables, float data).
class FFT {
public:
    void init(int n) {
        N = n; logN = 0; while ((1 << logN) < N) ++logN;
        tw.resize((size_t)N / 2); for (int k = 0; k < N / 2; ++k) tw[(size_t)k] = cpx((float)std::cos(-2 * kPi * k / N), (float)std::sin(-2 * kPi * k / N));
        rev.resize((size_t)N); for (int i = 0; i < N; ++i) { int r = 0; for (int b = 0; b < logN; ++b) if (i & (1 << b)) r |= 1 << (logN - 1 - b); rev[(size_t)i] = r; }
    }
    int size() const { return N; }
    /// in-place forward (inverse = false) or inverse (unscaled) transform
    void run(cpx* x, bool inverse) const {
        for (int i = 0; i < N; ++i) { int r = rev[(size_t)i]; if (r > i) std::swap(x[i], x[r]); }
        for (int len = 2; len <= N; len <<= 1) {
            int half = len >> 1, step = N / len;
            for (int i = 0; i < N; i += len)
                for (int j = 0; j < half; ++j) {
                    cpx w = tw[(size_t)(j * step)]; if (inverse) w = std::conj(w);
                    cpx u = x[i + j], v = x[i + j + half] * w; x[i + j] = u + v; x[i + j + half] = u - v;
                }
        }
    }
private:
    int N = 0, logN = 0; std::vector<cpx> tw; std::vector<int> rev;
};

/// Periodic Hann window (sums to a constant under 50% and 75% overlap).
inline std::vector<float> hannPeriodic(int n) { std::vector<float> w((size_t)n); for (int i = 0; i < n; ++i) w[(size_t)i] = (float)(0.5 - 0.5 * std::cos(2 * kPi * i / n)); return w; }

/// Running maximum over [k-W, k+W] (van Herk / Gil-Werman, O(n)). Out-of-range positions are ignored.
inline void runningMax(const float* x, float* out, int n, int W, std::vector<float>& g, std::vector<float>& h) {
    int len = 2 * W + 1; if (W <= 0) { std::memcpy(out, x, sizeof(float) * (size_t)n); return; }
    int m = n + 2 * W; g.resize((size_t)m); h.resize((size_t)m);
    auto at = [&](int i) { int k = i - W; return (k < 0 || k >= n) ? 0.f : x[k]; };   // padded with zeros (magnitudes are >= 0)
    for (int i = 0; i < m; ++i) g[(size_t)i] = (i % len == 0) ? at(i) : std::max(g[(size_t)i - 1], at(i));
    for (int i = m - 1; i >= 0; --i) h[(size_t)i] = (i % len == len - 1 || i == m - 1) ? at(i) : std::max(h[(size_t)i + 1], at(i));
    for (int k = 0; k < n; ++k) { int a = k, b = k + 2 * W; out[k] = std::max(h[(size_t)a], g[(size_t)b]); }
}
/// Running mean over [k-W, k+W], clipped to the array (prefix sums in double).
inline void runningMean(const float* x, float* out, int n, int W, std::vector<double>& pre) {
    pre.resize((size_t)n + 1); pre[0] = 0; for (int i = 0; i < n; ++i) pre[(size_t)i + 1] = pre[(size_t)i] + x[i];
    for (int k = 0; k < n; ++k) { int a = std::max(0, k - W), b = std::min(n, k + W + 1); out[k] = (float)((pre[(size_t)b] - pre[(size_t)a]) / (double)(b - a)); }
}

/// Linear ramp smoother: reaches the target in `steps` samples, then holds the exact target value.
struct Ramp {
    float cur = 0, target = 0, inc = 0; int left = 0, steps = 256;
    void reset(float v) { cur = target = v; left = 0; inc = 0; }
    void set(float t) { if (t != target) { target = t; left = steps; inc = (target - cur) / (float)steps; } }
    float next() { if (left > 0) { cur += inc; if (--left == 0) cur = target; } return cur; }
    bool settled() const { return left == 0; }
};

/// PolyBLEP sawtooth oscillator.
struct Saw {
    double ph = 0, dt = 0;
    void setFreq(double f, double sr) { dt = f / sr; }
    static double blep(double t, double dt) { if (t < dt) { t /= dt; return t + t - t * t - 1; } if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; } return 0; }
    float next() { double v = 2 * ph - 1 - blep(ph, dt); ph += dt; if (ph >= 1) ph -= 1; return (float)v; }
};
} // namespace mx
