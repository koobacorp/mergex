// MergeX — shared test helpers: synthetic sources (vocal-like, synth, drums, pad), rendering and measurement.
#pragma once
#include "Engine.h"
#include "Presets.h"
#include <chrono>
#include <complex>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tu {
using mx::kPi;
using Buf = std::vector<float>;
struct Stereo { Buf L, R; size_t size() const { return L.size(); } };

inline Stereo make(size_t n) { Stereo s; s.L.assign(n, 0); s.R.assign(n, 0); return s; }
inline Stereo sine(double sr, double f, double sec, float amp = 0.5f) { Stereo s = make((size_t)(sr * sec)); for (size_t i = 0; i < s.size(); ++i) s.L[i] = s.R[i] = amp * (float)std::sin(2 * kPi * f * (double)i / sr); return s; }
/// Additive band-limited saw with a spectral tilt: amplitude of harmonic h = 1/h * (h*f0/1000)^tiltPower... simple: 1/h^p
inline Stereo saw(double sr, double f0, double sec, float amp = 0.3f, double p = 1.0, double cutoff = 0) {
    Stereo s = make((size_t)(sr * sec)); int H = (int)(0.45 * sr / f0);
    std::vector<double> a((size_t)H + 1); for (int h = 1; h <= H; ++h) { double v = 1.0 / std::pow(h, p); if (cutoff > 0) v /= std::sqrt(1 + std::pow(h * f0 / cutoff, 4)); a[(size_t)h] = v; }
    for (size_t i = 0; i < s.size(); ++i) { double t = (double)i / sr, v = 0; for (int h = 1; h <= H; ++h) v += a[(size_t)h] * std::sin(2 * kPi * f0 * h * t); s.L[i] = s.R[i] = amp * (float)v; }
    return s;
}
/// Harmonic tone whose harmonic amplitudes follow a resonance (formant) at fc with bandwidth bw on top of a 1/h slope.
inline Stereo formantTone(double sr, double f0, double sec, double fc, double bw, float amp = 0.3f, double floor = 0.15) {
    Stereo s = make((size_t)(sr * sec)); int H = (int)(0.45 * sr / f0); std::vector<double> a((size_t)H + 1);
    for (int h = 1; h <= H; ++h) { double f = h * f0; a[(size_t)h] = (floor + 1.0 / (1 + std::pow((f - fc) / bw, 2))) / std::sqrt((double)h); }
    for (size_t i = 0; i < s.size(); ++i) { double t = (double)i / sr, v = 0; for (int h = 1; h <= H; ++h) v += a[(size_t)h] * std::sin(2 * kPi * f0 * h * t + 0.3 * h); s.L[i] = s.R[i] = amp * (float)v; }
    float pk = 0; for (float v : s.L) pk = std::max(pk, std::abs(v)); for (size_t i = 0; i < s.size(); ++i) s.L[i] = s.R[i] = s.L[i] * amp / pk;
    return s;
}
/// Two-pole resonator (bandpass) used to build vowel-like sounds.
struct Reso { double a1 = 0, a2 = 0, g = 0, y1 = 0, y2 = 0; void set(double sr, double f, double bw) { double r = std::exp(-kPi * bw / sr); a1 = 2 * r * std::cos(2 * kPi * f / sr); a2 = -r * r; g = 1 - r; } double run(double x) { double y = g * x + a1 * y1 + a2 * y2; y2 = y1; y1 = y; return y; } };
/// Vocal-like source: a glottal pulse train with vibrato through three formant resonators, moving through vowels; syllables gated.
inline Stereo vocal(double sr, double sec, double f0 = 196, bool gaps = true, uint32_t seed = 3) {
    Stereo s = make((size_t)(sr * sec)); const double F[5][3] = {{730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}, {570, 840, 2410}, {300, 870, 2240}};
    Reso r[3]; double ph = 0; mx::Rng rng; rng.s = seed; double syl = 0.38;
    for (size_t i = 0; i < s.size(); ++i) {
        double t = (double)i / sr; int v = (int)(t / syl) % 5; double in = t - std::floor(t / syl) * syl;
        if (i % 64 == 0) for (int k = 0; k < 3; ++k) r[k].set(sr, F[v][k], 60 + 40 * k);
        double f = f0 * (1 + 0.012 * std::sin(2 * kPi * 5.2 * t)) * (v == 2 ? 1.122 : (v == 4 ? 0.891 : 1.0));
        ph += f / sr; double pulse = 0; if (ph >= 1) { ph -= 1; pulse = 1; }
        double x = pulse + 0.02 * rng.bi();
        double env = gaps ? (in < 0.02 ? in / 0.02 : (in > syl - 0.07 ? std::max(0.0, (syl - in) / 0.05 - 0.4) : 1.0)) : 1.0; env = std::max(0.0, std::min(1.0, env));
        double y = 0; for (int k = 0; k < 3; ++k) y += r[k].run(x) * (k == 0 ? 1.0 : 0.6 / k);
        s.L[i] = s.R[i] = (float)(y * env);
    }
    float pk = 0; for (float v : s.L) pk = std::max(pk, std::abs(v)); for (size_t i = 0; i < s.size(); ++i) { s.L[i] = s.L[i] * 0.5f / pk; s.R[i] = s.L[i]; }
    return s;
}
/// Synth: detuned saw chord stab sequence (original), stereo.
inline Stereo synth(double sr, double sec, double bpm = 100) {
    Stereo s = make((size_t)(sr * sec)); const int prog[4] = {57, 53, 60, 55}; double beat = 60 / bpm; mx::Saw o[6]; float lp[2] = {0, 0};
    for (size_t i = 0; i < s.size(); ++i) {
        double t = (double)i / sr; int bar = (int)(t / (4 * beat)) % 4; float l = 0, r = 0;
        for (int k = 0; k < 3; ++k) { float f = mx::noteHz((float)(prog[bar] + (k == 0 ? 0 : (k == 1 ? 4 : 7)))); o[2 * k].setFreq(f * 0.997, sr); o[2 * k + 1].setFreq(f * 1.003, sr); l += o[2 * k].next(); r += o[2 * k + 1].next(); }
        float a = 0.25f; lp[0] += a * (l - lp[0]); lp[1] += a * (r - lp[1]); s.L[i] = 0.12f * lp[0]; s.R[i] = 0.12f * lp[1];
    }
    return s;
}
/// Drum loop: kick, snare, closed hats (synthesised), 4/4 at bpm.
inline Stereo drums(double sr, double sec, double bpm = 100, uint32_t seed = 5) {
    Stereo s = make((size_t)(sr * sec)); double six = 60 / bpm / 4; mx::Rng rng; rng.s = seed;
    size_t steps = (size_t)(sec / six) + 1;
    for (size_t st = 0; st < steps; ++st) {
        size_t start = (size_t)(st * six * sr); int k = (int)(st % 16);
        bool kick = k == 0 || k == 6 || k == 8 || k == 11, snare = k == 4 || k == 12, hat = k % 2 == 0;
        for (size_t j = 0; j < (size_t)(0.4 * sr) && start + j < s.size(); ++j) {
            double t = (double)j / sr; double v = 0;
            if (kick) v += 0.9 * std::sin(2 * kPi * (50 * t + 90 * (1 - std::exp(-t * 30)) / 30)) * std::exp(-t * 9);
            if (snare) v += 0.5 * (rng.bi() * std::exp(-t * 18) + 0.6 * std::sin(2 * kPi * 190 * t) * std::exp(-t * 25));
            if (hat && t < 0.06) v += 0.18 * rng.bi() * std::exp(-t * 80) * (k % 4 == 2 ? 1.0 : 0.6);
            s.L[start + j] += (float)v; s.R[start + j] += (float)v;
        }
    }
    float pk = 0; for (float v : s.L) pk = std::max(pk, std::abs(v)); for (size_t i = 0; i < s.size(); ++i) { s.L[i] *= 0.7f / pk; s.R[i] *= 0.7f / pk; }
    return s;
}
/// Pad: slow detuned chord (original voicing), soft.
inline Stereo pad(double sr, double sec) {
    Stereo s = make((size_t)(sr * sec)); const int notes[4] = {50, 57, 62, 65}; mx::Saw o[8]; float lp1[2] = {0, 0}, lp2[2] = {0, 0}; float a = 1.f - std::exp(-2.f * (float)kPi * 1500.f / (float)sr);
    for (int k = 0; k < 4; ++k) { o[2 * k].setFreq(mx::noteHz((float)notes[k]) * 0.996, sr); o[2 * k + 1].setFreq(mx::noteHz((float)notes[k]) * 1.004, sr); o[2 * k].ph = 0.1 * k; o[2 * k + 1].ph = 0.37 * k; }
    for (size_t i = 0; i < s.size(); ++i) {
        float l = 0, r = 0; for (int k = 0; k < 4; ++k) { l += o[2 * k].next(); r += o[2 * k + 1].next(); }
        lp1[0] += a * (l - lp1[0]); lp2[0] += a * (lp1[0] - lp2[0]); lp1[1] += a * (r - lp1[1]); lp2[1] += a * (lp1[1] - lp2[1]);
        double t = (double)i / sr; float sw = (float)(1 + 0.15 * std::sin(2 * kPi * 0.25 * t)); s.L[i] = 0.12f * lp2[0] * sw; s.R[i] = 0.12f * lp2[1] * sw;
    }
    return s;
}
inline Stereo noise(double sr, double sec, float amp, uint32_t seed = 1) { mx::Rng r; r.s = seed; Stereo s = make((size_t)(sr * sec)); for (size_t i = 0; i < s.size(); ++i) { s.L[i] = amp * r.bi(); s.R[i] = amp * r.bi(); } return s; }
inline Stereo silence(double sr, double sec) { return make((size_t)(sr * sec)); }
inline void append(Stereo& a, const Stereo& b) { a.L.insert(a.L.end(), b.L.begin(), b.L.end()); a.R.insert(a.R.end(), b.R.begin(), b.R.end()); }
inline Stereo fit(const Stereo& s, size_t n) { Stereo o = make(n); for (size_t i = 0; i < n; ++i) { o.L[i] = s.L[i % s.size()]; o.R[i] = s.R[i % s.size()]; } return o; }

struct Host { double bpm = 120, ppq = 0; bool playing = true; };
struct Run { Stereo out; double seconds = 0; std::vector<float> morphTrace; };
/// Render A (with optional sidechain B) through an engine in blocks; onBlock(sample, params) may change parameters.
inline Run render(mx::Engine& e, float* p, const Stereo& in, const Stereo* sc = nullptr, int block = 256, Host h = {}, std::function<void(size_t, float*)> onBlock = nullptr) {
    Run r; r.out = in; size_t n = in.size(); double sr = e.sampleRate();
    for (size_t pos = 0; pos < n; pos += (size_t)block) {
        int m = (int)std::min<size_t>((size_t)block, n - pos);
        if (onBlock) onBlock(pos, p);
        mx::HostInfo hi; hi.bpm = h.bpm; hi.playing = h.playing; hi.hasPpq = true; hi.ppq = h.ppq + (double)pos * h.bpm / 60.0 / sr;
        auto t0 = std::chrono::steady_clock::now();
        e.process(r.out.L.data() + pos, r.out.R.data() + pos, m, sc ? sc->L.data() + pos : nullptr, sc ? sc->R.data() + pos : nullptr, hi, p);
        r.seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        r.morphTrace.push_back(e.meterMorph.load());
    }
    return r;
}
inline std::unique_ptr<mx::Engine> engine(double sr = 48000) { auto e = std::make_unique<mx::Engine>(); e->prepare(sr, 512); return e; }
/// Neutral settings for isolating a style: no motion, no smoothing, limiter off.
inline void neutral(float* p) { mx::setDefaults(p); p[mx::P_SMOOTH] = 0; p[mx::P_LIMIT] = 0; p[mx::P_MOT_MODE] = 0; }

inline double rms(const Buf& x, size_t a = 0, size_t b = SIZE_MAX) { b = std::min(b, x.size()); double s = 0; for (size_t i = a; i < b; ++i) s += (double)x[i] * x[i]; return b > a ? std::sqrt(s / (double)(b - a)) : 0; }
inline float peak(const Buf& x, size_t a = 0, size_t b = SIZE_MAX) { b = std::min(b, x.size()); float p = 0; for (size_t i = a; i < b; ++i) p = std::max(p, std::abs(x[i])); return p; }
inline bool finite(const Stereo& s) { for (float v : s.L) if (!std::isfinite(v)) return false; for (float v : s.R) if (!std::isfinite(v)) return false; return true; }
inline int subnormals(const Stereo& s) { int c = 0; for (float v : s.L) c += std::fpclassify(v) == FP_SUBNORMAL; for (float v : s.R) c += std::fpclassify(v) == FP_SUBNORMAL; return c; }
inline double db(double g) { return 20 * std::log10(std::max(g, 1e-12)); }
/// Magnitude of frequency f in x[a..b) with a Hann window (single-bin DFT), relative to a unit sine.
inline double binMag(const Buf& x, double sr, double f, size_t a, size_t b) {
    std::complex<double> acc = 0; double ws = 0; for (size_t i = a; i < b; ++i) { double w = 0.5 - 0.5 * std::cos(2 * kPi * (double)(i - a) / (double)(b - a)); ws += w; acc += w * (double)x[i] * std::exp(std::complex<double>(0, -2 * kPi * f * (double)i / sr)); }
    return 2 * std::abs(acc) / ws;
}
/// Energy (sum of squared harmonic magnitudes) in [lo, hi) Hz from a Hann-windowed power spectrum (naive DFT on a coarse grid).
inline double bandEnergy(const Buf& x, double sr, double lo, double hi, size_t a, size_t b, double step = 0) {
    if (step <= 0) step = sr / (double)(b - a); double e = 0; for (double f = lo; f < hi; f += step) { double m = binMag(x, sr, f, a, b); e += m * m; } return e;
}
/// max |second difference| in [a,b): spikes here are clicks
inline float maxD2(const Buf& x, size_t a, size_t b) { float m = 0; for (size_t i = std::max<size_t>(a, 2); i < std::min(b, x.size()); ++i) m = std::max(m, std::abs(x[i] - 2 * x[i - 1] + x[i - 2])); return m; }
inline double corr(const Buf& x, const Buf& y, size_t a, size_t b, long lag = 0) { double sxy = 0, sxx = 0, syy = 0; for (size_t i = a; i < b; ++i) { long j = (long)i - lag; if (j < 0 || j >= (long)y.size()) continue; sxy += (double)x[i] * y[(size_t)j]; sxx += (double)x[i] * x[i]; syy += (double)y[(size_t)j] * y[(size_t)j]; } return sxy / std::sqrt(std::max(sxx * syy, 1e-30)); }

inline void writeWav(const std::string& path, const Stereo& s, double sr) {
    FILE* f = std::fopen(path.c_str(), "wb"); if (!f) return;
    uint32_t n = (uint32_t)s.size(), data = n * 2 * 3, riff = 36 + data, sr32 = (uint32_t)sr, br = sr32 * 6; uint16_t fmt = 1, ch = 2, ba = 6, bits = 24; uint32_t fs = 16;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f); std::fwrite(&fs, 4, 1, f); std::fwrite(&fmt, 2, 1, f); std::fwrite(&ch, 2, 1, f);
    std::fwrite(&sr32, 4, 1, f); std::fwrite(&br, 4, 1, f); std::fwrite(&ba, 2, 1, f); std::fwrite(&bits, 2, 1, f); std::fwrite("data", 1, 4, f); std::fwrite(&data, 4, 1, f);
    for (uint32_t i = 0; i < n; ++i) for (int c = 0; c < 2; ++c) { float v = mx::clampf(c ? s.R[i] : s.L[i], -1, 1); int32_t q = (int32_t)std::lround(v * 8388607.0); uint8_t b[3] = {(uint8_t)(q & 255), (uint8_t)((q >> 8) & 255), (uint8_t)((q >> 16) & 255)}; std::fwrite(b, 1, 3, f); }
    std::fclose(f);
}
} // namespace tu
