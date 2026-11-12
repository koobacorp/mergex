// MergeX — the morphing engine. Original code.
//
// Signal flow (per channel, stereo):
//   A (main input) ──┬─────────────── dry delay (exactly L samples) ───────────────┐
//                    ├─ STFT (N, hop N/4, sqrt-Hann) ─ Smooth / Talk / Swap ─ OLA ─┤─ style blend ─ Dry/Wet ─ Before ─ Output ─ Limiter
//   B (sidechain or  ┴─ grain history ─ Grain shuffle (read L samples back) ───────┘
//      built-in tone)
// L = N, so the dry path, the spectral path and the grain path line up sample for sample and L is reported to the host.
// Morph 0% is exactly A: an output sample uses the processed paths only if some frame/grain that touches it used Morph > 0;
// otherwise it is the delayed input itself, bit for bit.
#pragma once
#include "Dsp.h"
#include "Params.h"
#include <atomic>

namespace mx {

struct HostInfo { double bpm = 120, ppq = 0; bool hasPpq = false, playing = false; };

/// Frame size for a sample rate: ~21 ms at every rate (1024 @ 44.1/48 k, 2048 @ 88.2/96 k, 4096 @ 176.4/192 k).
inline int frameSizeFor(double sr) { return sr <= 64000 ? 1024 : (sr <= 128000 ? 2048 : 4096); }

/// Morph-motion value in [0,1] for an LFO shape at a phase in [0,1).
inline float lfoShape(int shape, double ph) {
    ph -= std::floor(ph);
    switch (shape) {
        case 1: return (float)(ph < 0.5 ? 2 * ph : 2 - 2 * ph);
        case 2: return (float)ph;
        case 3: return (float)(1 - ph);
        default: return (float)(0.5 - 0.5 * std::cos(2 * kPi * ph));
    }
}
/// Swap's pitch path: 0..1 morph -> 0..1 share of the pitch move. glide 1 = straight (log-pitch) glide, glide 0 = a quick switch around 50%.
inline float swapPitchCurve(float m, float glide) {
    float x = clampf((m - 0.4f) / 0.2f, 0, 1); float step = x * x * (3 - 2 * x);
    return (1 - glide) * step + glide * m;
}
/// Smooth's "lead": how far the tone (envelope) and texture (fine detail) have moved at morph m. y = 0.5 moves both together.
inline void leadCurves(float m, float y, float& mEnv, float& mFine) {
    float t = (y - 0.5f) * 2.f; mEnv = std::pow(m, std::pow(2.f, -1.5f * t)); mFine = std::pow(m, std::pow(2.f, 1.5f * t));
}
/// Follower level (linear peak) -> 0..1 over -48..-6 dBFS.
inline float followAmount(float lin) { float d = 20.f * std::log10(std::max(lin, 1e-9f)); return clampf((d + 48.f) / 42.f, 0, 1); }

class Engine {
public:
    // ---- live readouts for the UI (written on the audio thread)
    std::atomic<float> meterMorph {0}, meterF0A {0}, meterF0B {0}, meterA {0}, meterB {0}, meterGR {1};
    std::atomic<int> meterBSource {0};   // 0 = sidechain, 1 noise, 2 saw, 3 pad
    bool forceWet = false;               // test hook: use the processed paths even where Morph is 0

    void prepare(double sampleRate, int /*maxBlock*/) {
        sr = sampleRate; N = frameSizeFor(sr); hop = N / 4; nb = N / 2 + 1; binHz = (float)(sr / N);
        fft.init(N); hann = hannPeriodic(N); win.resize((size_t)N); for (int i = 0; i < N; ++i) win[(size_t)i] = std::sqrt(hann[(size_t)i]);
        // window autocorrelation, for normalising the pitch detector (Boersma 1993)
        std::vector<cpx> t((size_t)N, cpx(0)); for (int i = 0; i < N; ++i) t[(size_t)i] = cpx(win[(size_t)i], 0);
        std::vector<cpx> tp((size_t)2 * N, cpx(0)); FFT f2; f2.init(2 * N); for (int i = 0; i < N; ++i) tp[(size_t)i] = t[(size_t)i];
        f2.run(tp.data(), false); for (auto& v : tp) v = cpx(std::norm(v), 0); f2.run(tp.data(), true);
        rw.resize((size_t)N); for (int l = 0; l < N; ++l) rw[(size_t)l] = tp[(size_t)l].real() / tp[0].real();
        gMaxLen = (int)(0.25 * sr) & ~1; int need = N + 2 * gMaxLen + 64; H = 1; while (H < need) H <<= 1;
        for (auto& c : ch) c.alloc(N, nb, H);
        z.assign((size_t)N, cpx(0)); acf.assign((size_t)N, cpx(0)); outSpec.assign((size_t)N, cpx(0));
        bandCache = -1;
        // size every work buffer here so the audio thread never allocates
        for (auto* v : {&magA, &magB, &envA, &envB, &fA, &fB, &env, &fine, &ym, &tmpMax, &omA, &omB}) v->assign((size_t)nb, 0.f);
        ph.assign((size_t)nb, cpx(0)); pre.reserve((size_t)nb + 1); vg.reserve((size_t)nb + 128); vh.reserve((size_t)nb + 128);
        for (int c = 0; c < 2; ++c) { spec[c].assign((size_t)N, cpx(0)); XA[c].assign((size_t)nb, cpx(0)); XB[c].assign((size_t)nb, cpx(0)); outY[c].assign((size_t)nb, cpx(0)); }
        nrBuf.assign((size_t)N, 0.f); edgeBuf.reserve(64); bandCentre.reserve(64); bandOf.reserve((size_t)nb); interpB0.reserve((size_t)nb); interpFr.reserve((size_t)nb);
        bandSum.reserve(64); bandCnt.reserve(64); bandAmp.reserve(64);
        reset();
    }
    int latency() const { return N; }
    int frameSize() const { return N; }
    int hopSize() const { return hop; }
    double sampleRate() const { return sr; }

    void reset() {
        for (auto& c : ch) c.clear();
        T = 0; hopCount = 0; for (auto& g : grains) g.active = false; grainTimer = 0;
        morphR.steps = std::max(1, (int)(0.02 * sr)); mixR.steps = morphR.steps; gainR.steps = morphR.steps; cmpR.steps = std::max(1, (int)(0.01 * sr));
        styleR.steps = std::max(1, (int)(0.03 * sr)); scR.steps = std::max(1, (int)(0.05 * sr));
        morphR.reset(-1); mixR.reset(-1); gainR.reset(-1); cmpR.reset(-1); styleR.reset(-1); scR.reset(-1); first = true;
        folA = folB = gateA = fastA = slowA = 0; scFol = 0; lastSc = -(int64_t)(10 * sr); limG = 1; Es = 0; trHold = 0; logRatio = 0;
        f0A = f0B = 0; freeBeats = 0; rng.s = 0x12345u; noiseL = noiseR = 0; padLp1[0] = padLp1[1] = padLp2[0] = padLp2[1] = 0;
        for (auto& s : saws) s.ph = 0; for (size_t i = 0; i < 6; ++i) saws[i].ph = 0.13 * (double)i;
    }

    /// Process n samples in place. bL/bR = sidechain (nullptr when the host gives none). p = plain parameter values.
    void process(float* L, float* R, int n, const float* bL, const float* bR, const HostInfo& host, const float* p, bool hostBypass = false) {
        if (first) { morphR.reset(p[P_MORPH] * 0.01f); mixR.reset(p[P_MIX] * 0.01f); gainR.reset(dbToGain(p[P_OUT])); cmpR.reset((p[P_COMPARE] >= 0.5f || p[P_BYPASS] >= 0.5f || hostBypass) ? 1.f : 0.f);
                     styleR.reset((int)p[P_STYLE] == ST_GRAIN ? 1.f : 0.f); scR.reset(bL ? 1.f : 0.f); first = false; }
        morphR.set(p[P_MORPH] * 0.01f); mixR.set(p[P_MIX] * 0.01f); float gt = p[P_OUT] == 0.f ? 1.f : dbToGain(p[P_OUT]); gainR.set(gt);
        cmpR.set((p[P_COMPARE] >= 0.5f || p[P_BYPASS] >= 0.5f || hostBypass) ? 1.f : 0.f); style = (int)p[P_STYLE]; styleR.set(style == ST_GRAIN ? 1.f : 0.f);
        params = p; hostI = host; blockStartT = T; bSrc = (int)p[P_BSRC]; gB = p[P_BAL] == 0.f ? 1.f : dbToGain(p[P_BAL]);
        bool limitOn = p[P_LIMIT] >= 0.5f; const float thr = 0.96605f; // -0.3 dBFS
        float relLim = 1.f - std::exp(-1.f / (0.08f * (float)sr));
        float aF = 1.f - std::exp(-1.f / (0.005f * (float)sr)), rF = 1.f - std::exp(-1.f / (0.15f * (float)sr));
        float aG = 1.f - std::exp(-1.f / (0.001f * (float)sr));
        float aFast = 1.f - std::exp(-1.f / (0.0005f * (float)sr)), rFast = 1.f - std::exp(-1.f / (0.02f * (float)sr));
        float aSlow = 1.f - std::exp(-1.f / (0.03f * (float)sr)), rSlow = 1.f - std::exp(-1.f / (0.3f * (float)sr));
        int64_t scHold = (int64_t)(0.5 * sr);
        const int maskN = N - 1, mask2 = 2 * N - 1, maskH = H - 1;
        float pkA = 0, pkB = 0;

        for (int i = 0; i < n; ++i) {
            float a[2] = {L[i], R ? R[i] : L[i]};
            // ---------------- B: sidechain or built-in tone, then balance
            float sc[2] = {0, 0}; bool haveSc = bL != nullptr;
            if (haveSc) { sc[0] = bL[i]; sc[1] = bR ? bR[i] : bL[i]; float m = std::max(std::abs(sc[0]), std::abs(sc[1])); if (m > 1e-4f) lastSc = T; }
            bool useSc = bSrc == BS_AUTO && haveSc && (T - lastSc) < scHold;
            scR.set(useSc ? 1.f : 0.f); float wSc = scR.next();
            float am = std::max(std::abs(a[0]), std::abs(a[1]));
            gateA += (am > gateA ? aG : rF) * (am - gateA);
            float b[2] = {sc[0] * wSc, sc[1] * wSc};
            if (wSc < 1.f) { float t[2]; builtin(bSrc == BS_AUTO ? BS_PAD : bSrc, p[P_NOTE], t); float g = (1.f - wSc) * (gateA > 3e-5f ? gateA : 0.f); b[0] += t[0] * g; b[1] += t[1] * g; }
            b[0] *= gB; b[1] *= gB;
            float bm = std::max(std::abs(b[0]), std::abs(b[1]));
            folA += (am > folA ? aF : rF) * (am - folA); folB += (bm > folB ? aF : rF) * (bm - folB);
            fastA += (am > fastA ? aFast : rFast) * (am - fastA); slowA += (am > slowA ? aSlow : rSlow) * (am - slowA);
            folA = flushTiny(folA); folB = flushTiny(folB); gateA = flushTiny(gateA); fastA = flushTiny(fastA); slowA = flushTiny(slowA);
            pkA = std::max(pkA, am); pkB = std::max(pkB, bm);
            float mKnob = morphR.next();
            // ---------------- dry delay, rings, history
            float dry[2], sp[2], gr[2];
            int w = (int)(T & maskN), r2 = (int)(T & mask2), h = (int)(T & maskH);
            bool touched = false;
            for (int c = 0; c < 2; ++c) {
                auto& C = ch[c]; dry[c] = C.inA[(size_t)w]; C.inA[(size_t)w] = a[c]; C.inB[(size_t)w] = b[c];
                C.hA[(size_t)h] = a[c]; C.hB[(size_t)h] = b[c];
                sp[c] = C.acc[(size_t)r2]; C.acc[(size_t)r2] = 0;
            }
            touched = ch[0].tch[(size_t)r2] != 0 || forceWet; ch[0].tch[(size_t)r2] = 0;
            // ---------------- grains
            if (--grainTimer <= 0) startGrain(mKnob, i);
            bool gTouched = forceWet; gr[0] = gr[1] = 0;
            for (auto& g : grains) if (g.active) {
                float wv = 0.5f - 0.5f * std::cos(2.f * (float)kPi * (float)g.pos / (float)g.len);
                int idx = (int)((g.read + g.pos) & maskH);
                const auto& srcL = g.src ? ch[0].hB : ch[0].hA; const auto& srcR = g.src ? ch[1].hB : ch[1].hA;
                gr[0] += wv * srcL[(size_t)idx]; gr[1] += wv * srcR[(size_t)idx]; gTouched |= g.nontrivial;
                if (++g.pos >= g.len) g.active = false;
            }
            gr[0] = flushTiny(gr[0]); gr[1] = flushTiny(gr[1]);
            // ---------------- spectral frame
            if (++hopCount >= hop) { hopCount = 0; frame(mKnob, i); }
            // ---------------- blend
            float wg = styleR.next(), mx = mixR.next(), cmp = cmpR.next(), gain = gainR.next();
            float o[2];
            for (int c = 0; c < 2; ++c) {
                float s = touched ? sp[c] : dry[c], g = gTouched ? gr[c] : dry[c];
                float wet = wg <= 0.f ? s : (wg >= 1.f ? g : s + wg * (g - s));
                float mixed = mx >= 1.f ? wet : (mx <= 0.f ? dry[c] : dry[c] + mx * (wet - dry[c]));
                float v = cmp >= 1.f ? dry[c] : (cmp <= 0.f ? mixed : mixed + cmp * (dry[c] - mixed));
                o[c] = gain == 1.f ? v : v * gain;
            }
            if (limitOn) {
                float pk = std::max(std::abs(o[0]), std::abs(o[1])); float need = pk > thr ? thr / pk : 1.f;
                if (need < limG) limG = need; else if (limG < 1.f) { limG = std::min(need, limG + (1.f - limG) * relLim); if (limG > 0.99999f) limG = 1.f; }
                if (limG < 1.f) { o[0] *= limG; o[1] *= limG; }
            } else limG = 1.f;
            L[i] = o[0]; if (R) R[i] = o[1];
            ++T;
        }
        meterA.store(pkA); meterB.store(pkB); meterGR.store(limG);
        meterBSource.store(scR.target >= 0.5f ? 0 : (bSrc == BS_AUTO ? BS_PAD : bSrc));
    }

    /// Morph including motion, at sample offset i of the current block.
    float effectiveMorph(float knob, int i) {
        const float* p = params; int mode = (int)p[P_MOT_MODE]; float depth = p[P_MOT_DEPTH] * 0.01f, m = knob;
        if (mode == MM_LFO) {
            double beats; double bpm = hostI.bpm > 0 ? hostI.bpm : 120;
            if (hostI.hasPpq && hostI.playing) beats = hostI.ppq + (double)i * bpm / 60.0 / sr;
            else beats = (double)T * bpm / 60.0 / sr;                       // host stopped: free-running clock at the host tempo
            m += depth * lfoShape((int)p[P_MOT_SHAPE], beats / kMotionDivBeats[(int)p[P_MOT_DIV]]);
        } else if (mode == MM_FOLLOW_A) m += depth * followAmount(folA);
        else if (mode == MM_FOLLOW_B) m += depth * followAmount(folB);
        return clampf(m, 0, 1);
    }

private:
    struct Chan {
        std::vector<float> inA, inB, acc, hA, hB; std::vector<uint8_t> tch;
        std::vector<float> envAs, envBs, fBs, phAprev, phBprev, phOut; float eAs = 0, eBs = 0;
        void alloc(int frame, int bins, int hist) {
            inA.assign((size_t)frame, 0); inB.assign((size_t)frame, 0); acc.assign((size_t)2 * frame, 0); tch.assign((size_t)2 * frame, 0);
            hA.assign((size_t)hist, 0); hB.assign((size_t)hist, 0);
            for (auto* v : {&envAs, &envBs, &fBs, &phAprev, &phBprev, &phOut}) v->assign((size_t)bins, 0);
        }
        void clear() { for (auto* v : {&inA, &inB, &acc, &hA, &hB, &envAs, &envBs, &fBs, &phAprev, &phBprev, &phOut}) std::fill(v->begin(), v->end(), 0.f); std::fill(tch.begin(), tch.end(), (uint8_t)0); eAs = eBs = 0; }
    };
    struct Grain { bool active = false, nontrivial = false; int src = 0, len = 0, pos = 0; int64_t read = 0; };


    // ------------------------------------------------------------------------------------------------ built-in B tones
    void builtin(int type, float note, float* out) {
        if (type == BS_NOISE) {  // gently tilted (pink-ish) stereo noise
            float wl = rng.bi(), wr = rng.bi(); noiseL = 0.97f * noiseL + 0.03f * wl; noiseR = 0.97f * noiseR + 0.03f * wr;
            out[0] = 0.55f * wl + 2.2f * noiseL; out[1] = 0.55f * wr + 2.2f * noiseR; out[0] *= 0.8f; out[1] *= 0.8f; return;
        }
        float f0 = noteHz(note);
        if (type == BS_SAW) { saws[0].setFreq(f0, sr); float v = 0.8f * saws[0].next(); out[0] = out[1] = v; return; }
        // pad: root, fifth, octave; two detuned saws each, one per side; two gentle low-pass poles
        static const float semis[3] = {0, 7, 12}; float l = 0, r = 0;
        for (int k = 0; k < 3; ++k) {
            float f = f0 * std::pow(2.f, semis[k] / 12.f);
            saws[(size_t)(2 * k)].setFreq(f * 0.99596f, sr); saws[(size_t)(2 * k + 1)].setFreq(f * 1.00405f, sr);
            l += saws[(size_t)(2 * k)].next(); r += saws[(size_t)(2 * k + 1)].next();
        }
        float a = 1.f - std::exp(-2.f * (float)kPi * 2400.f / (float)sr);
        padLp1[0] += a * (l - padLp1[0]); padLp2[0] += a * (padLp1[0] - padLp2[0]); padLp1[1] += a * (r - padLp1[1]); padLp2[1] += a * (padLp1[1] - padLp2[1]);
        out[0] = 0.55f * padLp2[0]; out[1] = 0.55f * padLp2[1];
    }

    // ------------------------------------------------------------------------------------------------ grains
    void startGrain(float knob, int i) {
        float y = params[P_SHAPE] * 0.01f; int len = (int)(0.01 * sr * std::pow(25.0, (double)y)) & ~1; len = std::max(16, std::min(len, gMaxLen));
        grainTimer = len / 2;
        float m = effectiveMorph(knob, i);
        float tr = slowA > 1e-6f ? clampf((fastA / slowA - 1.4f) / 1.5f, 0, 1) : 0.f;
        float pB = m * (1.f - params[P_TRANS] * 0.01f * tr);
        float ramp = clampf(m / 0.2f, 0, 1); float spray = params[P_SMOOTH] * 0.01f * ramp;
        Grain* g = nullptr; for (auto& q : grains) if (!q.active) { g = &q; break; }
        if (!g) { g = &grains[0]; for (auto& q : grains) if (q.len - q.pos < g->len - g->pos) g = &q; }
        g->active = true; g->len = len; g->pos = 0;
        g->src = (pB > 0.f && rng.uni() < pB) ? 1 : 0;
        int jit = spray > 0 ? (int)(rng.uni() * spray * (float)len) : 0;
        g->read = T - N - jit; g->nontrivial = g->src == 1 || jit != 0;
    }

    // ------------------------------------------------------------------------------------------------ spectral frame
    void bandSetup(int nBands) {
        if (nBands == bandCache) return; bandCache = nBands;
        double lo = 80.0, hi = std::min(14000.0, 0.45 * sr); bandCentre.assign((size_t)nBands, 0); bandOf.assign((size_t)nb, 0);
        auto& edge = edgeBuf; edge.assign((size_t)nBands + 1, 0.0); for (int b = 0; b <= nBands; ++b) edge[(size_t)b] = lo * std::pow(hi / lo, (double)b / nBands);
        for (int b = 0; b < nBands; ++b) bandCentre[(size_t)b] = (float)(std::sqrt(edge[(size_t)b] * edge[(size_t)b + 1]) / binHz);
        for (int k = 0; k < nb; ++k) { double f = k * (double)binHz; int b = 0; while (b < nBands - 1 && f >= edge[(size_t)b + 1]) ++b; bandOf[(size_t)k] = b; }
        interpB0.assign((size_t)nb, 0); interpFr.assign((size_t)nb, 0);
        for (int k = 0; k < nb; ++k) {
            float fk = (float)k; int b0 = 0; while (b0 < nBands - 1 && bandCentre[(size_t)b0 + 1] <= fk) ++b0;
            float fr = 0; if (fk <= bandCentre[0]) { b0 = 0; fr = 0; } else if (b0 >= nBands - 1) { b0 = nBands - 1; fr = 0; }
            else { float c0 = std::log(std::max(bandCentre[(size_t)b0], 1e-3f)), c1 = std::log(bandCentre[(size_t)b0 + 1]); fr = clampf((std::log(std::max(fk, 1e-3f)) - c0) / (c1 - c0), 0, 1); }
            interpB0[(size_t)k] = b0; interpFr[(size_t)k] = fr;
        }
        bandSum.assign((size_t)nBands, 0); bandCnt.assign((size_t)nBands, 0); for (int k = 0; k < nb; ++k) bandCnt[(size_t)bandOf[(size_t)k]] += 1;
    }
    void bandEnvelope(const float* mag, float* envOut, int nBands) {
        std::fill(bandSum.begin(), bandSum.end(), 0.0);
        for (int k = 0; k < nb; ++k) bandSum[(size_t)bandOf[(size_t)k]] += (double)mag[k] * mag[k];
        bandAmp.resize((size_t)nBands); for (int b = 0; b < nBands; ++b) bandAmp[(size_t)b] = bandCnt[(size_t)b] > 0 ? (float)std::sqrt(bandSum[(size_t)b] / bandCnt[(size_t)b]) : 0.f;
        for (int k = 0; k < nb; ++k) { int b0 = interpB0[(size_t)k]; float fr = interpFr[(size_t)k]; float v0 = bandAmp[(size_t)b0], v1 = b0 + 1 < nBands ? bandAmp[(size_t)b0 + 1] : v0; envOut[k] = v0 + fr * (v1 - v0); }
    }
    void peakEnvelope(const float* mag, float* envOut, int W) { runningMax(mag, tmpMax.data(), nb, W, vg, vh); runningMean(tmpMax.data(), envOut, nb, W, pre); }
    int widthFor(float f0) const { if (f0 <= 0) return 2; float S = f0 / binHz; return std::max(2, std::min(40, (int)std::ceil(S * 0.55f))); }

    /// Pitch from a normalised autocorrelation r(l)/r(0) / rw(l). Returns 0 when unvoiced.
    float pitchFrom(const std::vector<cpx>& r, bool imag) {
        auto at = [&](int l) { float v = imag ? r[(size_t)l].imag() : r[(size_t)l].real(); return v; };
        float r0 = at(0); if (r0 <= 1e-12f) return 0;
        int lmin = std::max(2, (int)(sr / 1000.0)), lmax = std::min(N / 3, (int)(sr / 70.0));
        float best = 0; int bl = -1; auto& nr = nrBuf; std::fill(nr.begin(), nr.end(), 0.f);
        for (int l = lmin - 1; l <= lmax + 1; ++l) nr[(size_t)l] = at(l) / r0 / std::max(rw[(size_t)l], 1e-3f);
        for (int l = lmin; l <= lmax; ++l) if (nr[(size_t)l] > best && nr[(size_t)l] >= nr[(size_t)l - 1] && nr[(size_t)l] >= nr[(size_t)l + 1]) { best = nr[(size_t)l]; bl = l; }
        if (bl < 0 || best < 0.5f) return 0;
        for (int l = lmin; l <= lmax; ++l) if (nr[(size_t)l] >= 0.9f * best && nr[(size_t)l] >= nr[(size_t)l - 1] && nr[(size_t)l] >= nr[(size_t)l + 1]) { bl = l; break; }  // shortest strong lag: avoids octave-down errors
        float y0 = nr[(size_t)bl - 1], y1 = nr[(size_t)bl], y2 = nr[(size_t)bl + 1], den = y0 - 2 * y1 + y2;
        float d = std::abs(den) > 1e-9f ? clampf(0.5f * (y0 - y2) / den, -0.5f, 0.5f) : 0.f;
        return (float)(sr / ((double)bl + d));
    }

    void frame(float knob, int i) {
        const float* p = params;
        float m = effectiveMorph(knob, i);
        // transient keeper: frame energy of A against its recent average
        // gather + analysis (A and B packed into one complex FFT per channel)
        float EA[2], EB[2];
        for (int c = 0; c < 2; ++c) {
            auto& C = ch[c]; auto& X = spec[c]; X.resize((size_t)N);
            int start = (int)((T + 1) & (N - 1));
            for (int j = 0; j < N; ++j) { int idx = (start + j) & (N - 1); X[(size_t)j] = cpx(win[(size_t)j] * C.inA[(size_t)idx], win[(size_t)j] * C.inB[(size_t)idx]); }
            fft.run(X.data(), false);
        }
        for (int c = 0; c < 2; ++c) {
            auto& X = spec[c]; auto& xa = XA[c]; auto& xb = XB[c]; xa.resize((size_t)nb); xb.resize((size_t)nb);
            for (int k = 0; k < nb; ++k) { cpx zk = X[(size_t)k], zn = std::conj(X[(size_t)((N - k) & (N - 1))]); xa[(size_t)k] = 0.5f * (zk + zn); xb[(size_t)k] = cpx(0, -0.5f) * (zk - zn); }
            double ea = 0, eb = 0; for (int k = 0; k < nb; ++k) { ea += std::norm(xa[(size_t)k]); eb += std::norm(xb[(size_t)k]); } EA[c] = (float)ea; EB[c] = (float)eb;
        }
        float Ef = EA[0] + EA[1]; float ratio = Es > 1e-12f ? Ef / Es : (Ef > 1e-9f ? 16.f : 0.f); Es = 0.85f * Es + 0.15f * Ef;
        float tr = clampf((std::log2(std::max(ratio, 1e-6f)) - 0.6f) / 1.4f, 0, 1); trHold = std::max(tr, trHold * 0.6f);
        m *= 1.f - p[P_TRANS] * 0.01f * trHold;
        meterMorph.store(m);
        bool active = m > 0.f || forceWet;
        int st = style == ST_GRAIN ? ST_SMOOTH : style;   // Grain runs in the time domain; the spectral path keeps idling on Smooth
        float y = p[P_SHAPE] * 0.01f, ramp = clampf(m / 0.2f, 0, 1);
        // pitch (mid of both channels, A in real part, B in imaginary part)
        for (int k = 0; k < N; ++k) { int kk = k <= N / 2 ? k : N - k; float pa = std::norm(XA[0][(size_t)kk]) + std::norm(XA[1][(size_t)kk]), pb = std::norm(XB[0][(size_t)kk]) + std::norm(XB[1][(size_t)kk]); acf[(size_t)k] = cpx(pa, pb); }
        fft.run(acf.data(), true);
        float fa = pitchFrom(acf, false), fb = pitchFrom(acf, true);
        f0A = fa; f0B = fb; meterF0A.store(fa); meterF0B.store(fb);
        if (fa > 0 && fb > 0) { float lr = std::log2(fb / fa); while (lr > 1.f) lr -= 1.f; while (lr < -1.f) lr += 1.f; logRatio = std::abs(lr - logRatio) > 0.08f ? lr : 0.6f * logRatio + 0.4f * lr; }
        else logRatio *= 0.95f;
        int nBands = 8 + (int)std::lround(y * 32.f); if (st == ST_TALK) bandSetup(nBands);
        float s = p[P_SMOOTH] * 0.01f; float alpha = s <= 0 ? 0.f : (float)std::exp(-(double)hop / ((0.01 + 0.39 * s * s) * sr)), alphaA = alpha * ramp;
        float fs = p[P_FORMANT] * ramp; float fr = std::pow(2.f, fs / 12.f);
        float mE = m, mF = m; if (st == ST_SMOOTH) leadCurves(m, y, mE, mF);
        int dir = st == ST_TALK ? (int)p[P_TALK_DIR] : (st == ST_SWAP ? (int)p[P_SWAP_DIR] : 0);
        float e = swapPitchCurve(m, y); float lratio = logRatio; float rA = std::exp2(lratio * e), rB = std::exp2(lratio * (e - 1.f));
        bool pv = st == ST_SWAP && dir == 0 && std::abs(lratio) > 1e-3f && m > 0.f && m < 1.f;
        int WA = widthFor(fa), WB = widthFor(fb);
        for (int c = 0; c < 2; ++c) {
            auto& C = ch[c]; auto& xa = XA[c]; auto& xb = XB[c];
            magA.resize((size_t)nb); magB.resize((size_t)nb); envA.resize((size_t)nb); envB.resize((size_t)nb); fA.resize((size_t)nb); fB.resize((size_t)nb);
            env.resize((size_t)nb); fine.resize((size_t)nb); ym.resize((size_t)nb); ph.resize((size_t)nb); tmpMax.resize((size_t)nb);
            for (int k = 0; k < nb; ++k) { magA[(size_t)k] = std::abs(xa[(size_t)k]); magB[(size_t)k] = std::abs(xb[(size_t)k]); }
            if (st == ST_TALK) { bandEnvelope(magA.data(), envA.data(), nBands); bandEnvelope(magB.data(), envB.data(), nBands); }
            else { peakEnvelope(magA.data(), envA.data(), WA); peakEnvelope(magB.data(), envB.data(), WB); }
            const float fl = 1e-9f; float pkEA = 0, pkEB = 0;
            for (int k = 0; k < nb; ++k) {
                size_t K = (size_t)k; fA[K] = envA[K] > fl ? magA[K] / envA[K] : 0.f; fB[K] = envB[K] > fl ? magB[K] / envB[K] : 0.f;
                C.envAs[K] = flushTiny(alphaA * C.envAs[K] + (1 - alphaA) * envA[K]); C.envBs[K] = flushTiny(alpha * C.envBs[K] + (1 - alpha) * envB[K]); C.fBs[K] = flushTiny(alpha * C.fBs[K] + (1 - alpha) * fB[K]);
                pkEA = std::max(pkEA, C.envAs[K]); pkEB = std::max(pkEB, C.envBs[K]);
            }
            // envelope floors 60 dB under each frame's peak: two sounds with no shared frequencies still meet in the middle
            float flA = std::max(pkEA * 1e-3f, 1e-12f), flB = std::max(pkEB * 1e-3f, 1e-12f);
            // phases for Swap's pitch glide (instantaneous frequency per bin)
            float phAdv = 2.f * (float)kPi * (float)hop / (float)N;
            if (pv) { omA.resize((size_t)nb); omB.resize((size_t)nb); }
            for (int k = 0; k < nb; ++k) {
                size_t K = (size_t)k; float pa = std::arg(xa[K]), pb = std::arg(xb[K]);
                if (pv) { omA[K] = 2.f * (float)kPi * k / N + princarg(pa - C.phAprev[K] - phAdv * k) / hop; omB[K] = 2.f * (float)kPi * k / N + princarg(pb - C.phBprev[K] - phAdv * k) / hop; }
                C.phAprev[K] = pa; C.phBprev[K] = pb;
            }
            float tgt;
            C.eAs = flushTiny(alphaA * C.eAs + (1 - alphaA) * std::sqrt(EA[c])); C.eBs = flushTiny(alpha * C.eBs + (1 - alpha) * std::sqrt(EB[c]));
            float eA = alphaA <= 0.f ? EA[c] : C.eAs * C.eAs, eB = alpha <= 0.f ? EB[c] : C.eBs * C.eBs;   // smoothed frame energies set the loudness target, so Smoothness slurs loudness too
            auto geo = [](float x, float yv, float t) { if (t <= 0.f) return x; if (t >= 1.f) return yv; return std::exp((1 - t) * std::log(std::max(x, 1e-30f)) + t * std::log(std::max(yv, 1e-30f))); };
            auto geoEnv = [&](float x, float yv, float t) { if (t <= 0.f) return x; if (t >= 1.f) return yv; return geo(std::max(x, flA), std::max(yv, flB), t); };
            for (int k = 0; k < nb; ++k) {
                size_t K = (size_t)k; float ea = C.envAs[K], eb = C.envBs[K], fa_ = fA[K], fb_ = C.fBs[K]; float en, fi, wp;
                if (st == ST_SMOOTH) { en = geoEnv(ea, eb, mE); fi = (1 - mF) * fa_ + mF * fb_; wp = mF; }
                else if (dir == 0) { en = ea; fi = (1 - m) * fa_ + m * fb_; wp = m; }
                else { en = geoEnv(ea, eb, m); fi = fa_; wp = 0; }
                env[K] = en; fine[K] = fi;
                cpx mixc = wp <= 0.f ? xa[K] : (wp >= 1.f ? xb[K] : (1 - wp) * xa[K] + wp * xb[K]);
                float am = std::abs(mixc); ph[K] = am > 1e-20f ? mixc / am : cpx(1, 0);
            }
            if (st == ST_SWAP && dir == 0) {     // pitch glide: warp both fine structures to the in-between pitch, then blend them
                for (int k = 0; k < nb; ++k) {
                    size_t K = (size_t)k;
                    float ka = (float)k / rA, kb = (float)k / rB; float wa = interp(fA, ka), wb = interp(C.fBs, kb);
                    float ca = (1 - m) * wa, cb = m * wb; fine[K] = ca + cb;
                    if (pv) { float om = ca >= cb ? omA[(size_t)std::min(nb - 1, (int)std::lround(ka))] * rA : omB[(size_t)std::min(nb - 1, (int)std::lround(kb))] * rB;
                              C.phOut[K] = princarg(C.phOut[K] + om * (float)hop); ph[K] = cpx(std::cos(C.phOut[K]), std::sin(C.phOut[K])); }
                    else C.phOut[K] = std::arg(ph[K]);
                }
                tgt = eA;
            } else {
                tgt = st == ST_SMOOTH ? geo(eA, eB, mE) : (dir == 0 ? eA : geo(eA, eB, m));
                for (int k = 0; k < nb; ++k) C.phOut[(size_t)k] = std::arg(ph[(size_t)k]);   // keeps Swap's glide continuous if it starts later
            }
            // formant shift of the envelope, then magnitude = envelope x fine detail
            double ey = 0;
            for (int k = 0; k < nb; ++k) { float en = fr == 1.f ? env[(size_t)k] : interp(env, (float)k / fr); float v = en * fine[(size_t)k]; ym[(size_t)k] = v; ey += (double)v * v; }
            float scale = ey > 1e-20 ? clampf((float)std::sqrt((double)tgt / ey), 0.f, 16.f) : 0.f;
            if (!(scale == scale)) scale = 0.f;
            auto& Y = outY[c]; Y.resize((size_t)nb);
            for (int k = 0; k < nb; ++k) Y[(size_t)k] = (ym[(size_t)k] * scale) * ph[(size_t)k];
            Y[0] = cpx(Y[0].real(), 0); Y[(size_t)nb - 1] = cpx(Y[(size_t)nb - 1].real(), 0);
        }
        // synthesis: L in the real part, R in the imaginary part of one inverse FFT
        for (int k = 0; k < N; ++k) {
            cpx yl, yr; if (k <= N / 2) { yl = outY[0][(size_t)k]; yr = outY[1][(size_t)k]; } else { yl = std::conj(outY[0][(size_t)(N - k)]); yr = std::conj(outY[1][(size_t)(N - k)]); }
            outSpec[(size_t)k] = yl + cpx(0, 1) * yr;
        }
        fft.run(outSpec.data(), true);
        const float norm = 1.f / ((float)N * 2.f);   // 1/N for the FFT, 1/2 for Hann overlap-add at hop N/4
        const int mask2 = 2 * N - 1;
        for (int j = 0; j < N; ++j) {
            size_t idx = (size_t)((T + 1 + j) & mask2); float wv = win[(size_t)j] * norm;
            ch[0].acc[idx] += flushTiny(outSpec[(size_t)j].real() * wv); ch[1].acc[idx] += flushTiny(outSpec[(size_t)j].imag() * wv);
            if (active) ch[0].tch[idx] = 1;
        }
    }
    float interp(const std::vector<float>& v, float x) const { if (x <= 0) return v[0]; int i0 = (int)x; if (i0 >= nb - 1) return 0.f; float f = x - (float)i0; return v[(size_t)i0] + f * (v[(size_t)i0 + 1] - v[(size_t)i0]); }

    double sr = 48000; int N = 1024, hop = 256, nb = 513, H = 1 << 16, gMaxLen = 12000; float binHz = 46.875f;
    FFT fft; std::vector<float> hann, win, rw;
    Chan ch[2]; std::vector<cpx> z, acf, outSpec, spec[2], XA[2], XB[2], outY[2];
    std::vector<float> magA, magB, envA, envB, fA, fB, env, fine, ym, tmpMax, omA, omB, vg, vh; std::vector<cpx> ph; std::vector<double> pre;
    std::vector<float> bandCentre, interpFr, bandAmp, nrBuf; std::vector<double> edgeBuf; std::vector<int> bandOf, interpB0; std::vector<double> bandSum; std::vector<int> bandCnt; int bandCache = -1;
    Grain grains[4]; int grainTimer = 0;
    int64_t T = 0, blockStartT = 0, lastSc = 0; int hopCount = 0; bool first = true;
    Ramp morphR, mixR, gainR, cmpR, styleR, scR;
    const float* params = nullptr; HostInfo hostI; int style = 0, bSrc = 0; float gB = 1;
    float folA = 0, folB = 0, gateA = 0, fastA = 0, slowA = 0, scFol = 0, limG = 1, Es = 0, trHold = 0, logRatio = 0, f0A = 0, f0B = 0;
    double freeBeats = 0; Rng rng; float noiseL = 0, noiseR = 0, padLp1[2] = {0, 0}, padLp2[2] = {0, 0}; Saw saws[6];
};
} // namespace mx
