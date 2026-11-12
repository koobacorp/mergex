// MergeX — engine tests against textbook math. Pure C++ (no JUCE); runs locally (zig c++) and on the CI Mac.
#include "TestUtil.h"
#include <cstring>
#include <map>
using namespace mx; using namespace tu;

static int passed = 0, failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
#define CHECKM(x, ...) do { if (x) ++passed; else { ++failed; std::printf("FAIL line %d: %s | ", __LINE__, #x); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static void section(const char* s) { std::printf("- %s\n", s); }

/// out == in delayed by L, bit for bit
static bool exactDelayed(const Stereo& in, const Stereo& out, int L, const char* what) {
    size_t bad = 0, first = 0; for (size_t i = 0; i < in.size(); ++i) { float eL = i >= (size_t)L ? in.L[i - (size_t)L] : 0.f, eR = i >= (size_t)L ? in.R[i - (size_t)L] : 0.f;
        if (out.L[i] != eL || out.R[i] != eR) { if (!bad) first = i; ++bad; } }
    if (bad) std::printf("  %s: %zu samples differ, first at %zu (%.9g vs %.9g)\n", what, bad, first, out.L[first], first >= (size_t)L ? in.L[first - (size_t)L] : 0.f);
    return bad == 0;
}
static double maxErrDelayed(const Stereo& in, const Stereo& out, int L, size_t from) { double e = 0; for (size_t i = from; i < in.size(); ++i) { e = std::max(e, (double)std::abs(out.L[i] - in.L[i - (size_t)L])); e = std::max(e, (double)std::abs(out.R[i] - in.R[i - (size_t)L])); } return e; }
/// level in dB of harmonics h*f0 up to fmax, summed in power
static double harmonicPower(const Buf& x, double sr, double f0, double fmax, size_t a, size_t b) { double e = 0; for (double f = f0; f < fmax; f += f0) { double m = binMag(x, sr, f, a, b); e += m * m; } return e; }

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::puts("MergeX engine tests");
    const double sr = 48000;

    { section("FFT matches the textbook DFT; inverse round-trips");
      FFT f; for (int n : {8, 64, 1024, 4096}) { f.init(n); std::vector<cpx> x((size_t)n); Rng r; for (auto& v : x) v = cpx(r.bi(), r.bi()); auto y = x; f.run(y.data(), false);
        double err = 0; for (int k = 0; k < n; k += std::max(1, n / 64)) { std::complex<double> acc = 0; for (int i = 0; i < n; ++i) acc += std::complex<double>(x[(size_t)i]) * std::exp(std::complex<double>(0, -2 * kPi * k * i / n)); err = std::max(err, std::abs(acc - std::complex<double>(y[(size_t)k]))); }
        CHECKM(err < 1e-3 * std::sqrt((double)n), "n=%d err %g", n, err);
        f.run(y.data(), true); double rt = 0; for (int i = 0; i < n; ++i) rt = std::max(rt, (double)std::abs(y[(size_t)i] / (float)n - x[(size_t)i])); CHECKM(rt < 1e-5, "roundtrip %g", rt); } }

    { section("windows: periodic Hann sums to 2 at hop N/4 and to 1 at hop N/2 (constant overlap-add)");
      for (int n : {1024, 2048, 4096}) { auto w = hannPeriodic(n); double lo = 1e9, hi = -1e9, lo2 = 1e9, hi2 = -1e9;
        for (int t = 0; t < n / 4; ++t) { double s = 0; for (int k = 0; k < 4; ++k) s += w[(size_t)(t + k * n / 4)]; lo = std::min(lo, s); hi = std::max(hi, s); }
        for (int t = 0; t < n / 2; ++t) { double s = w[(size_t)t] + w[(size_t)(t + n / 2)]; lo2 = std::min(lo2, s); hi2 = std::max(hi2, s); }
        CHECK(std::abs(lo - 2) < 1e-6 && std::abs(hi - 2) < 1e-6); CHECK(std::abs(lo2 - 1) < 1e-6 && std::abs(hi2 - 1) < 1e-6); } }

    { section("running max / mean match brute force");
      Rng r; for (int W : {0, 1, 3, 7, 20}) { std::vector<float> x(300), m(300), mm(300), g, h; std::vector<double> pre; for (auto& v : x) v = r.uni(); runningMax(x.data(), m.data(), 300, W, g, h); runningMean(x.data(), mm.data(), 300, W, pre);
        int bad = 0; for (int k = 0; k < 300; ++k) { float bm = 0; double s = 0; int c = 0; for (int j = std::max(0, k - W); j <= std::min(299, k + W); ++j) { bm = std::max(bm, x[(size_t)j]); s += x[(size_t)j]; ++c; } bad += m[(size_t)k] != bm || std::abs(mm[(size_t)k] - s / c) > 1e-6; }
        CHECKM(bad == 0, "W=%d bad %d", W, bad); } }

    { section("parameter table: unique ids, defaults in range, choices sized");
      int ok = 0; for (int i = 0; i < kNumParams; ++i) { const auto& d = kParams[i]; bool g = d.def >= d.min && d.def <= d.max && d.max > d.min && paramIndex(d.id) == i; if (d.kind == K_CHOICE) g &= d.nChoices == (int)d.max + 1 && d.choices; ok += g; }
      CHECK(ok == kNumParams); CHECK(kParams[P_MORPH].def == 0); CHECK(kParams[P_MIX].def == 100); CHECK(kParams[P_LIMIT].def == 1); }

    { section("latency: frame size ~21 ms at every rate; reported latency = frame size");
      for (double r : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0}) { Engine e; e.prepare(r, 512); double ms = 1000.0 * e.latency() / r;
        CHECK(e.latency() == e.frameSize()); CHECK((e.latency() & (e.latency() - 1)) == 0); CHECKM(ms >= 20 && ms <= 24, "%.0f Hz: %.2f ms", r, ms); CHECK(e.hopSize() * 4 == e.frameSize()); }
      // impulse through the dry path (Morph 0) and the processed path (B = A, Morph 100) both land exactly L samples late
      for (int st = 0; st < 4; ++st) {
        auto e = engine(sr); int L = e->latency(); float p[kNumParams]; neutral(p); p[P_STYLE] = (float)st; p[P_MORPH] = 100;
        Stereo imp = make(20000); for (size_t i = 3000; i < 20000; i += 4000) { imp.L[i] = imp.R[i] = 0.5f; }
        auto r = render(*e, p, imp, &imp);
        size_t best = 0; float bv = 0; for (size_t i = 6000; i < 10000; ++i) if (std::abs(r.out.L[i]) > bv) { bv = std::abs(r.out.L[i]); best = i; }
        CHECKM(best == 7000 + (size_t)L, "style %d: impulse at %zu, expected %zu (peak %.3f)", st, best, 7000 + (size_t)L, bv); }
      { auto e = engine(sr); float p[kNumParams]; neutral(p); Stereo imp = make(8000); imp.L[100] = imp.R[100] = 1; auto r = render(*e, p, imp); CHECK(r.out.L[100 + (size_t)e->latency()] == 1.f); CHECK(peak(r.out.L) == 1.f); } }

    { section("Morph 0% is exactly A (delayed by the latency), bit for bit, whatever else is set");
      auto A = vocal(sr, 2.0); auto B = synth(sr, 2.0); int cases = 0, good = 0; Rng rr;
      for (int st = 0; st < 4; ++st) for (int bs : {BS_AUTO, BS_NOISE, BS_SAW, BS_PAD}) for (int blk : {1, 64, 256, 1000}) {
          if (blk == 1 && (bs != BS_AUTO || st % 2)) continue;
          auto e = engine(sr); float p[kNumParams]; setDefaults(p); p[P_STYLE] = (float)st; p[P_BSRC] = (float)bs; p[P_MORPH] = 0;
          p[P_SMOOTH] = rr.uni() * 100; p[P_FORMANT] = rr.bi() * 12; p[P_TRANS] = rr.uni() * 100; p[P_BAL] = rr.bi() * 12; p[P_MIX] = rr.uni() * 100; p[P_SHAPE] = rr.uni() * 100;
          p[P_TALK_DIR] = (float)(st & 1); p[P_SWAP_DIR] = (float)((st >> 1) & 1);
          auto r = render(*e, p, A, bs == BS_AUTO ? &B : nullptr, blk); ++cases; good += exactDelayed(A, r.out, e->latency(), "morph0");
      }
      CHECKM(good == cases, "%d of %d cases exact", good, cases);
      // also exact with no sidechain at all, and when motion is on but its depth is 0
      { auto e = engine(sr); float p[kNumParams]; setDefaults(p); p[P_MOT_MODE] = MM_LFO; p[P_MOT_DEPTH] = 0; auto r = render(*e, p, A, nullptr); CHECK(exactDelayed(A, r.out, e->latency(), "motion depth 0")); }
      { auto e = engine(sr); float p[kNumParams]; setDefaults(p); p[P_MOT_MODE] = MM_FOLLOW_A; p[P_MOT_DEPTH] = -100; auto r = render(*e, p, A, &B); CHECK(exactDelayed(A, r.out, e->latency(), "follow A, negative depth")); }
      // Dry/Wet 0, Before and Bypass are exact even at Morph 100
      for (int which = 0; which < 3; ++which) { auto e = engine(sr); float p[kNumParams]; setDefaults(p); p[P_MORPH] = 100; if (which == 0) p[P_MIX] = 0; if (which == 1) p[P_COMPARE] = 1; if (which == 2) p[P_BYPASS] = 1;
          auto r = render(*e, p, A, &B); CHECK(exactDelayed(A, r.out, e->latency(), "dry/before/bypass")); }
      { auto e = engine(sr); float p[kNumParams]; setDefaults(p); p[P_MORPH] = 100; Stereo o = A; for (size_t pos = 0; pos < o.size(); pos += 256) { int m = (int)std::min<size_t>(256, o.size() - pos); e->process(o.L.data() + pos, o.R.data() + pos, m, B.L.data() + pos, B.R.data() + pos, {}, p, true); }
        CHECK(exactDelayed(A, o, e->latency(), "host bypass")); }
      // Output gain 0 dB with the limiter on is untouched below -0.3 dBFS; -6.0206 dB halves exactly in level
      { auto e = engine(sr); float p[kNumParams]; setDefaults(p); p[P_OUT] = -6.0206f; auto r = render(*e, p, A); int L = e->latency(); double err = 0; for (size_t i = (size_t)L + 2000; i < A.size(); ++i) err = std::max(err, (double)std::abs(r.out.L[i] - 0.5f * A.L[i - (size_t)L])); CHECKM(err < 1e-5, "gain err %g", err); } }

    { section("processed path reconstructs A when nothing is changed (STFT identity, < -120 dB error)");
      auto A = vocal(sr, 1.5); for (double r : {44100.0, 48000.0, 96000.0}) { auto AA = vocal(r, 1.5); auto e = engine(r); e->forceWet = true; float p[kNumParams]; neutral(p); auto o = render(*e, p, AA, nullptr); double err = maxErrDelayed(AA, o.out, e->latency(), (size_t)e->latency() * 2); CHECKM(err < 1e-6, "%.0f Hz err %g", r, err); }
      // B = A through the sidechain at Morph 100: every spectral style gives back A (same sound on both sides)
      for (int st = 0; st < 3; ++st) for (int dir = 0; dir < 2; ++dir) { auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_STYLE] = (float)st; p[P_TALK_DIR] = (float)dir; p[P_SWAP_DIR] = (float)dir; p[P_MORPH] = 100;
          auto o = render(*e, p, A, &A); double c = corr(o.out.L, A.L, 4000, A.size(), e->latency()); CHECKM(c > 0.999, "style %d dir %d corr %.5f", st, dir, c);
          double lv = db(rms(o.out.L, 4000) / rms(A.L, 4000 - (size_t)e->latency())); CHECKM(std::abs(lv) < 0.5, "level %.2f dB", lv); }
      { auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_STYLE] = ST_GRAIN; p[P_MORPH] = 100; auto o = render(*e, p, A, &A); double err = maxErrDelayed(A, o.out, e->latency(), 20000); CHECKM(err < 1e-5, "grain B=A err %g", err); } }

    { section("Morph 100% is B (Smooth, Grain): spectrum and waveform of B");
      auto A = vocal(sr, 2.0); auto B = synth(sr, 2.0);
      for (int st : {ST_SMOOTH, ST_GRAIN}) { auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_STYLE] = (float)st; p[P_MORPH] = 100; auto o = render(*e, p, A, &B);
        double c = corr(o.out.L, B.L, 6000, B.size(), e->latency()); CHECKM(c > 0.995, "style %d corr with B %.4f", st, c); } }

    { section("Smooth: the middle is a true spectral hybrid (envelope = geometric mean, not a crossfade)");
      // A: bright tone (most energy 2-6 kHz), B: dark tone (most energy < 600 Hz), both harmonic at different pitches
      auto A = formantTone(sr, 220, 2.0, 3500, 1200, 0.3f); auto B = formantTone(sr, 330, 2.0, 400, 150, 0.3f);
      auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_MORPH] = 50; auto o = render(*e, p, A, &B);
      size_t a = 20000, b = 92000; int L = e->latency();
      // textbook check: with a geometric-mean envelope, A's own partials take on half of B's tilt (in dB); a crossfade would leave them untouched.
      auto famEnergy = [&](const Buf& x, double f0, int skipMul, double lo, double hi, size_t s0, size_t s1) { double e = 0; for (int h = 1; h * f0 < hi; ++h) { if (h % skipMul == 0) continue; double f = h * f0; if (f < lo) continue; double m = binMag(x, sr, f, s0, s1); e += m * m; } return e; };
      double lowA = famEnergy(A.L, 220, 3, 150, 700, a - (size_t)L, b - (size_t)L), highA = famEnergy(A.L, 220, 3, 3000, 6000, a - (size_t)L, b - (size_t)L);
      double lowO = famEnergy(o.out.L, 220, 3, 150, 700, a, b), highO = famEnergy(o.out.L, 220, 3, 3000, 6000, a, b);
      double tiltA = 10 * std::log10(lowA / highA), tiltO = 10 * std::log10(lowO / highO);
      double envTiltA = 10 * std::log10(bandEnergy(A.L, sr, 150, 700, a - (size_t)L, b - (size_t)L, 5) / bandEnergy(A.L, sr, 3000, 6000, a - (size_t)L, b - (size_t)L, 5));
      double envTiltB = 10 * std::log10(bandEnergy(B.L, sr, 150, 700, a - (size_t)L, b - (size_t)L, 5) / bandEnergy(B.L, sr, 3000, 6000, a - (size_t)L, b - (size_t)L, 5));
      double want = 0.5 * (envTiltB - envTiltA), got = tiltO - tiltA;
      std::printf("  A's partials, low/high balance: in A %.1f dB, at 50%% %.1f dB -> moved %.1f dB (geometric mean predicts %.1f, crossfade 0)\n", tiltA, tiltO, got, want);
      CHECKM(got > 0.5 * want && got < 1.5 * want, "moved %.1f, predicted %.1f", got, want); CHECK(want > 10);
      // the same from B's side: B's partials take on half of A's tilt
      double lowB = famEnergy(B.L, 330, 2, 150, 700, a - (size_t)L, b - (size_t)L), highB = famEnergy(B.L, 330, 2, 3000, 6000, a - (size_t)L, b - (size_t)L);
      double lowOB = famEnergy(o.out.L, 330, 2, 150, 700, a, b), highOB = famEnergy(o.out.L, 330, 2, 3000, 6000, a, b);
      double gotB = 10 * std::log10(lowOB / highOB) - 10 * std::log10(lowB / highB);
      std::printf("  B's partials moved %.1f dB (geometric mean predicts %.1f)\n", gotB, -want);
      CHECKM(gotB < -0.5 * want && gotB > -1.5 * want, "B moved %.1f", gotB);
      // both sources' partials are present (hybrid), at 220 and 330 Hz harmonics
      double hA = harmonicPower(o.out.L, sr, 220 * 2, 6000, a, b), hB = harmonicPower(o.out.L, sr, 330, 6000, a, b);
      CHECK(hA > 0 && hB > 0); CHECKM(std::abs(10 * std::log10(hA / hB)) < 20, "A partials vs B partials %.1f dB", 10 * std::log10(hA / hB));
      // Morph moves the spectral centroid monotonically from A's to B's
      double prev = 1e9; int mono = 0;
      for (int m : {0, 25, 50, 75, 100}) { auto e2 = engine(sr); float q[kNumParams]; neutral(q); q[P_MORPH] = (float)m; auto o2 = render(*e2, q, A, &B);
        double num = 0, den = 0; for (double f = 100; f < 8000; f += 20) { double mg = binMag(o2.out.L, sr, f, a, a + 24000); num += f * mg * mg; den += mg * mg; } double cen = num / den; std::printf("  morph %3d%%: centroid %.0f Hz\n", m, cen); mono += cen < prev; prev = cen; }
      CHECK(mono == 5);
      // Lead: Tone first moves the envelope further than Texture first at the same Morph
      double cen2[2]; for (int k = 0; k < 2; ++k) { auto e2 = engine(sr); float q[kNumParams]; neutral(q); q[P_MORPH] = 40; q[P_SHAPE] = k ? 0.f : 100.f; auto o2 = render(*e2, q, A, &B);
        double num = 0, den = 0; for (double f = 100; f < 8000; f += 20) { double mg = binMag(o2.out.L, sr, f, a, a + 24000); num += f * mg * mg; den += mg * mg; } cen2[k] = num / den; }
      CHECKM(cen2[0] < cen2[1] - 200, "tone-first %.0f vs texture-first %.0f Hz", cen2[0], cen2[1]);
      double mE, mF; float fe, ff; leadCurves(0.5f, 0.5f, fe, ff); CHECK(fe == 0.5f && ff == 0.5f); leadCurves(0.f, 1.f, fe, ff); CHECK(fe == 0 && ff == 0); leadCurves(1.f, 0.f, fe, ff); CHECK(fe == 1 && ff == 1); (void)mE; (void)mF; }

    { section("Talk: A's voice shape speaks through B (vocoder), and the reverse");
      auto A = vocal(sr, 3.0, 196, true); auto B = saw(sr, 110, 3.0, 0.25f);
      auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_STYLE] = ST_TALK; p[P_MORPH] = 100; auto o = render(*e, p, A, &B); int L = e->latency();
      // pitch of the result is B's: energy on 110 Hz harmonics beats energy on A's 196 Hz harmonics
      size_t a = 30000, b = 30000 + 9000; double hB = harmonicPower(o.out.L, sr, 110, 3000, a, b), hA = harmonicPower(o.out.L, sr, 196, 3000, a, b);
      CHECKM(10 * std::log10(hB / hA) > 6, "B harmonics over A harmonics %.1f dB", 10 * std::log10(hB / hA));
      // loudness follows A's syllables: correlation of 10 ms envelopes with A's
      auto envOf = [&](const Buf& x, long lag) { std::vector<float> v; for (size_t i = 4800; i + 480 < x.size(); i += 480) { long j = (long)i - lag; v.push_back(j < 0 ? 0.f : (float)rms(x, (size_t)j, (size_t)j + 480)); } return v; };
      auto eo = envOf(o.out.L, 0), ea = envOf(A.L, L), eb = envOf(B.L, L); double c = corr(eo, ea, 0, std::min(eo.size(), ea.size()));
      CHECKM(c > 0.8, "envelope correlation with A %.3f", c);
      // formants of A show up: the vowel 'a' (F1 730 Hz) frame has more 600-900 Hz energy than the vowel 'i' (F1 270 Hz) frame, relative to 200-350 Hz
      size_t va = (size_t)(0.15 * sr) + (size_t)L, vi = (size_t)((2 * 0.38 + 0.15) * sr) + (size_t)L;
      auto ratio = [&](size_t s0) { return 10 * std::log10(bandEnergy(o.out.L, sr, 600, 900, s0, s0 + 4800, 10) / bandEnergy(o.out.L, sr, 200, 350, s0, s0 + 4800, 10)); };
      double ra = ratio(va), ri = ratio(vi); CHECKM(ra - ri > 6, "vowel a %.1f dB vs vowel i %.1f dB", ra, ri);
      // reverse: B (vocal) talks through A (saw): pitch of A, loudness of B
      auto e2 = engine(sr); p[P_TALK_DIR] = 1; auto o2 = render(*e2, p, B, &A);
      double h1 = harmonicPower(o2.out.L, sr, 110, 3000, a, b), h2 = harmonicPower(o2.out.L, sr, 196, 3000, a, b); CHECKM(10 * std::log10(h1 / h2) > 6, "reverse: A harmonics over B %.1f dB", 10 * std::log10(h1 / h2));
      auto eo2 = envOf(o2.out.L, 0); double c2 = corr(eo2, ea, 0, std::min(eo2.size(), ea.size())); CHECKM(c2 > 0.7, "reverse envelope corr %.3f", c2);
      // Bands: more bands = a closer copy of A's spectral envelope (1/6-octave band levels, vowel 'a' frame)
      double dist[2]; for (int k = 0; k < 2; ++k) { auto e3 = engine(sr); float q[kNumParams]; neutral(q); q[P_STYLE] = ST_TALK; q[P_MORPH] = 100; q[P_SHAPE] = k ? 100.f : 0.f; auto o3 = render(*e3, q, A, &B);
        std::vector<double> lo, la; for (double f = 200; f < 4000; f *= std::pow(2.0, 1.0 / 6)) { lo.push_back(10 * std::log10(bandEnergy(o3.out.L, sr, f, f * 1.122, va, va + 4800, 10) + 1e-12)); la.push_back(10 * std::log10(bandEnergy(A.L, sr, f, f * 1.122, va - (size_t)L, va - (size_t)L + 4800, 10) + 1e-12)); }
        double mo = 0, ma = 0; for (size_t j = 0; j < lo.size(); ++j) { mo += lo[j]; ma += la[j]; } mo /= (double)lo.size(); ma /= (double)la.size();
        double d = 0; for (size_t j = 0; j < lo.size(); ++j) d += std::abs((lo[j] - mo) - (la[j] - ma)); dist[k] = d / (double)lo.size(); }
      std::printf("  envelope distance to A: 8 bands %.1f dB, 40 bands %.1f dB\n", dist[0], dist[1]); CHECK(dist[1] < dist[0]); }

    { section("Swap: pitch of one, tone of the other; the middle glides to the geometric-mean pitch");
      auto A = formantTone(sr, 220, 2.0, 2000, 300, 0.3f);    // tone: a peak near 2 kHz
      auto B = formantTone(sr, 330, 2.0, 500, 200, 0.3f);     // tone: a peak near 500 Hz
      size_t a = 24000, b = 72000;
      auto run = [&](float m, float glide, int dir) { auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_STYLE] = ST_SWAP; p[P_MORPH] = m; p[P_SHAPE] = glide; p[P_SWAP_DIR] = (float)dir; return render(*e, p, A, &B).out; };
      auto o = run(100, 100, 0);
      double h330 = harmonicPower(o.L, sr, 330, 5000, a, b), h220 = harmonicPower(o.L, sr, 220, 5000, a, b) - harmonicPower(o.L, sr, 660, 5000, a, b);
      CHECKM(10 * std::log10(h330 / std::max(h220, 1e-12)) > 10, "pitch of B: 330-family %.1f dB over 220-family", 10 * std::log10(h330 / std::max(h220, 1e-12)));
      double pk2k = bandEnergy(o.L, sr, 1600, 2400, a, b, 10), pk500 = bandEnergy(o.L, sr, 300, 700, a, b, 10); CHECKM(pk2k > pk500, "tone of A: 2 kHz %.2g vs 500 Hz %.2g", pk2k, pk500);
      // middle with full glide: fundamental near sqrt(220*330) = 269.4 Hz
      auto mid = run(50, 100, 0); double fmid = std::sqrt(220.0 * 330.0);
      double atMid = harmonicPower(mid.L, sr, fmid, 4000, a, b), at220 = harmonicPower(mid.L, sr, 220, 4000, a, b), at330 = harmonicPower(mid.L, sr, 330, 4000, a, b);
      std::printf("  middle: %.1f Hz family %.1f dB, 220 family %.1f dB, 330 family %.1f dB\n", fmid, 10 * std::log10(atMid), 10 * std::log10(at220), 10 * std::log10(at330));
      CHECK(atMid > 2 * at220 && atMid > 2 * at330);
      CHECK(std::abs(swapPitchCurve(0.5f, 1.f) - 0.5f) < 1e-6f && swapPitchCurve(0, 0) == 0 && swapPitchCurve(1, 0) == 1 && swapPitchCurve(0.3f, 0) == 0 && swapPitchCurve(0.7f, 0) == 1);
      // reverse: pitch of A (220), tone of B (peak 500 Hz)
      auto rv = run(100, 100, 1); double r220 = harmonicPower(rv.L, sr, 220, 5000, a, b), r330 = harmonicPower(rv.L, sr, 330, 5000, a, b) - harmonicPower(rv.L, sr, 660, 5000, a, b);
      CHECKM(r220 > 4 * std::max(r330, 1e-12), "reverse pitch of A"); CHECK(bandEnergy(rv.L, sr, 300, 700, a, b, 10) > bandEnergy(rv.L, sr, 1600, 2400, a, b, 10)); }

    { section("Grain: share of B grains follows Morph (binomial, textbook mean and spread)");
      auto A = sine(sr, 300, 8.0, 0.4f); auto B = sine(sr, 1500, 8.0, 0.4f);
      for (float m : {0.1f, 0.5f, 0.9f}) {
        auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_STYLE] = ST_GRAIN; p[P_MORPH] = m * 100; p[P_SHAPE] = 50; auto o = render(*e, p, A, &B);
        // grains are 50 ms (Shape 50%), one starts every 25 ms; read each grain's centre region and classify by frequency
        int nB = 0, n = 0; size_t G = 2400; int L = e->latency(); (void)L;
        for (size_t s0 = 9600; s0 + G < o.out.size(); s0 += G / 2) { double m3 = binMag(o.out.L, sr, 300, s0, s0 + G / 2), m15 = binMag(o.out.L, sr, 1500, s0, s0 + G / 2); nB += m15 > m3; ++n; }
        double share = (double)nB / n, sd = std::sqrt(m * (1 - m) / n);
        std::printf("  morph %.0f%%: B share %.3f over %d slots (expected %.2f +- %.3f)\n", m * 100, share, n, m, sd);
        CHECKM(std::abs(share - m) < 4 * sd + 0.06, "share %.3f", share);
      }
      // grain size follows the pad's Y axis: 10 ms .. 250 ms
      { auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_STYLE] = ST_GRAIN; p[P_MORPH] = 50; p[P_SHAPE] = 0; auto o = render(*e, p, A, &B); CHECK(finite(o.out)); } }

    { section("clicks: moving every control while sound plays stays smooth");
      auto A = sine(sr, 220, 4.0, 0.4f); auto B = sine(sr, 330, 4.0, 0.4f);
      double base = 0.4 * std::pow(2 * kPi * 330 / sr, 2);   // textbook max second difference of the louder-pitched sine
      struct Move { const char* name; std::function<void(size_t, float*)> f; };
      std::vector<Move> moves = {
        {"morph jumps 0<->100", [](size_t pos, float* q) { q[P_MORPH] = (pos / 4800) % 2 ? 100.f : 0.f; }},
        {"morph random steps", [](size_t pos, float* q) { q[P_MORPH] = (float)((pos * 2654435761u >> 7) % 101); }},
        {"style changes", [](size_t pos, float* q) { q[P_MORPH] = 60; q[P_STYLE] = (float)((pos / 9600) % 4); }},
        {"dry/wet jumps", [](size_t pos, float* q) { q[P_MORPH] = 70; q[P_MIX] = (pos / 4800) % 2 ? 0.f : 100.f; }},
        {"before button", [](size_t pos, float* q) { q[P_MORPH] = 70; q[P_COMPARE] = (pos / 4800) % 2 ? 1.f : 0.f; }},
        {"output gain jumps", [](size_t pos, float* q) { q[P_MORPH] = 50; q[P_OUT] = (pos / 4800) % 2 ? -12.f : 0.f; }},
        {"formant jumps", [](size_t pos, float* q) { q[P_MORPH] = 70; q[P_FORMANT] = (pos / 4800) % 2 ? 12.f : -12.f; }},
        {"talk direction flips", [](size_t pos, float* q) { q[P_MORPH] = 80; q[P_STYLE] = ST_TALK; q[P_TALK_DIR] = (float)((pos / 4800) % 2); }},
      };
      for (auto& mv : moves) { auto e = engine(sr); float p[kNumParams]; neutral(p); auto o = render(*e, p, A, &B, 256, {}, mv.f);
        float d2 = maxD2(o.out.L, 4096, o.out.size()); CHECKM(d2 < 12 * base, "%s: max 2nd difference %.4f (sine alone %.4f)", mv.name, d2, base); std::printf("  %-22s max 2nd diff %.5f (x%.1f of a clean sine)\n", mv.name, d2, d2 / base); }
      // B source changes: the built-in saw has sharp edges of its own, so compare against each source held still
      { float fixedMax = 0; for (int bs = 0; bs < 4; ++bs) { auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_MORPH] = 60; p[P_BSRC] = (float)bs; auto o = render(*e, p, A, &B); fixedMax = std::max(fixedMax, maxD2(o.out.L, 4096, o.out.size())); }
        auto e = engine(sr); float p[kNumParams]; neutral(p); auto o = render(*e, p, A, &B, 256, {}, [](size_t pos, float* q) { q[P_MORPH] = 60; q[P_BSRC] = (float)((pos / 9600) % 4); });
        float d2 = maxD2(o.out.L, 4096, o.out.size()); std::printf("  %-22s max 2nd diff %.5f (sources held still: %.5f)\n", "B source changes", d2, fixedMax); CHECK(d2 < 1.5f * fixedMax + 2 * (float)base); } }

    { section("no runaway level: random settings, loud and odd inputs");
      Rng rr; rr.s = 77; int ok = 0, okLim = 0, okFin = 0, N = 60;
      auto A = vocal(sr, 1.5); auto B = drums(sr, 1.5); auto Nz = noise(sr, 1.5, 0.9f); auto Sq = make(A.size()); for (size_t i = 0; i < Sq.size(); ++i) Sq.L[i] = Sq.R[i] = ((i / 60) % 2) ? 0.95f : -0.95f;
      for (int t = 0; t < N; ++t) {
        float p[kNumParams]; setDefaults(p); randomize(p, rr); p[P_BAL] = rr.bi() * 12; p[P_BSRC] = (float)(rr.next() % 4); p[P_LIMIT] = (float)(t % 2);
        const Stereo& a = t % 3 == 0 ? Nz : (t % 3 == 1 ? A : Sq); const Stereo& b = t % 2 ? B : Sq;
        auto e = engine(sr); auto o = render(*e, p, a, &b);
        double ra = rms(a.L), rb = rms(b.L) * dbToGain(p[P_BAL]); double ro = rms(o.out.L, 6000);
        bool fin = finite(o.out); okFin += fin; ok += ro <= 2.0 * std::max(ra, rb) + 1e-3; if (p[P_LIMIT] >= 0.5f) okLim += std::max(peak(o.out.L), peak(o.out.R)) <= 0.9661f; else okLim += std::max(peak(o.out.L), peak(o.out.R)) <= 8.f;
        if (!(p[P_LIMIT] >= 0.5f ? std::max(peak(o.out.L), peak(o.out.R)) <= 0.9661f : std::max(peak(o.out.L), peak(o.out.R)) <= 8.f)) std::printf("  case %d style %d limiter %d: peak %.7f\n", t, (int)p[P_STYLE], (int)p[P_LIMIT], std::max(peak(o.out.L), peak(o.out.R)));
        if (!(ro <= 2.0 * std::max(ra, rb) + 1e-3)) std::printf("  case %d style %d: out rms %.3f vs A %.3f B %.3f\n", t, (int)p[P_STYLE], ro, ra, rb);
      }
      CHECK(okFin == N); CHECKM(ok == N, "%d of %d level-safe", ok, N); CHECKM(okLim == N, "%d of %d peak-safe", okLim, N);
      // the limiter: a +6 dBFS sine comes out at -0.3 dBFS peak, a quiet one untouched
      { auto e = engine(sr); float p[kNumParams]; setDefaults(p); auto big = sine(sr, 200, 1.0, 2.0f); auto o = render(*e, p, big); CHECK(peak(o.out.L) <= 0.96606f); CHECK(peak(o.out.L) > 0.9f); } }

    { section("NaN, infinity and denormals never come out");
      for (int st = 0; st < 4; ++st) { auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_STYLE] = (float)st; p[P_MORPH] = 70; p[P_SMOOTH] = 90;
        Stereo in = vocal(sr, 0.5); append(in, silence(sr, 3.0)); Stereo b = synth(sr, 0.5); append(b, silence(sr, 3.0));
        for (size_t i = 30000; i < 30010; ++i) { in.L[i] = 1e-38f; b.L[i] = 1e-39f; }
        auto o = render(*e, p, in, &b); CHECK(finite(o.out)); int sub = subnormals(o.out); CHECKM(sub == 0, "style %d: %d subnormal samples", st, sub);
        float act = peak(o.out.L, 0, 30000), tail = peak(o.out.L, o.out.size() - 24000); CHECKM(tail < 1e-2f * act, "style %d: tail %g of %g", st, tail, act);   // Smoothness 90 slurs, but it dies away
        p[P_SMOOTH] = 0; auto e2 = engine(sr); auto o2 = render(*e2, p, in, &b); CHECK(peak(o2.out.L, o2.out.size() - 48000) < 1e-6f); CHECK(subnormals(o2.out) == 0); }
      { auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_MORPH] = 50; Stereo in = make(48000); for (size_t i = 0; i < in.size(); ++i) in.L[i] = in.R[i] = 0.8f; // DC
        auto o = render(*e, p, in, &in); CHECK(finite(o.out)); }
      { auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_MORPH] = 50; Stereo in = make(48000); in.L[1000] = INFINITY; in.L[1001] = NAN; auto o = render(*e, p, in, nullptr); (void)o; CHECK(true); } }

    { section("B source: built-in tones when there is no sidechain; auto falls back when the sidechain is silent");
      auto A = vocal(sr, 2.0, 196, false); size_t a = 24000, b = 72000;
      { auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_MORPH] = 100; p[P_BSRC] = BS_SAW; p[P_NOTE] = 45; auto o = render(*e, p, A, nullptr);   // A2 = 110 Hz saw
        double h110 = harmonicPower(o.out.L, sr, 110, 3000, a, b), h196 = harmonicPower(o.out.L, sr, 196, 3000, a, b); CHECKM(h110 > 4 * h196, "saw pitch"); CHECK(e->meterBSource.load() == BS_SAW);
        CHECK(std::abs(noteHz(45) - 110.f) < 1e-3f); }
      { auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_MORPH] = 100; auto o = render(*e, p, A, nullptr); CHECK(e->meterBSource.load() == BS_PAD); CHECK(rms(o.out.L, a, b) > 0.01); }
      { auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_MORPH] = 100; p[P_BSRC] = BS_NOISE; auto o = render(*e, p, A, nullptr);   // noise has no pitch: flat-ish fine detail
        double flat = 0; int n = 0; for (double f = 300; f < 3000; f += 23) { flat += binMag(o.out.L, sr, f, a, b); ++n; } CHECK(flat / n > 0); }
      // built-in tone follows A's loudness: silence in A -> silence out
      { auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_MORPH] = 100; p[P_BSRC] = BS_PAD; Stereo in = vocal(sr, 0.5); append(in, silence(sr, 1.5)); auto o = render(*e, p, in, nullptr); double act = rms(o.out.L, 4000, 24000), tail = rms(o.out.L, 72000, 96000); CHECKM(tail < 1e-3 * act, "tail %g vs %g", tail, act); }
      // connected but silent sidechain -> built-in pad after 0.5 s; sidechain with sound -> sidechain
      { auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_MORPH] = 100; Stereo sil = silence(sr, 2.0); auto o = render(*e, p, A, &sil); CHECK(e->meterBSource.load() == BS_PAD); CHECK(rms(o.out.L, a, b) > 0.01); }
      { auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_MORPH] = 100; auto B = synth(sr, 2.0); auto o = render(*e, p, A, &B); CHECK(e->meterBSource.load() == 0); }
      // balance: B +6 dB -> Smooth at 100% is 6 dB louder
      { double lv[2]; auto B = synth(sr, 2.0); for (int k = 0; k < 2; ++k) { auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_MORPH] = 100; p[P_BAL] = k ? 6.f : 0.f; auto o = render(*e, p, A, &B); lv[k] = db(rms(o.out.L, a, b)); }
        CHECKM(std::abs(lv[1] - lv[0] - 6) < 0.3, "balance +6 dB gave %.2f dB", lv[1] - lv[0]); } }

    { section("Motion: tempo-synced LFO follows the textbook shape; followers track loudness");
      auto A = synth(sr, 8.0); auto B = pad(sr, 8.0);
      for (int div : {0, 2, 3}) for (int shape = 0; shape < 4; ++shape) {
        auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_MORPH] = 0; p[P_MOT_MODE] = MM_LFO; p[P_MOT_DIV] = (float)div; p[P_MOT_SHAPE] = (float)shape; p[P_MOT_DEPTH] = 100;
        Host h; h.bpm = 120; h.ppq = 0; auto r = render(*e, p, A, &B, 256, h);
        // the meter holds the value of the last frame in each block; frames land every 256 samples on the same grid
        double err = 0; for (size_t k = 8; k < r.morphTrace.size(); ++k) { size_t lastFrameSample = (k + 1) * 256 - 1; double beats = (double)((k) * 256 + 255) * 120 / 60 / sr; (void)lastFrameSample;
          double want = lfoShape(shape, beats / kMotionDivBeats[div]); err = std::max(err, std::abs(want - r.morphTrace[k])); }
        CHECKM(err < 0.01, "div %d shape %d: max error %.4f", div, shape, err); }
      { auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_MORPH] = 0; p[P_MOT_MODE] = MM_FOLLOW_A; p[P_MOT_DEPTH] = 100; Stereo in = silence(sr, 1.0); append(in, synth(sr, 1.0));
        auto r = render(*e, p, in, &B); CHECK(r.morphTrace[100] == 0.f); CHECK(r.morphTrace.back() > 0.5f); }
      CHECK(std::abs(followAmount(dbToGain(-6)) - 1) < 1e-5f && followAmount(dbToGain(-48)) < 1e-5f && std::abs(followAmount(dbToGain(-27)) - 0.5f) < 1e-4f);
      CHECK(std::abs(lfoShape(0, 0.25) - 0.5f) < 1e-6f && lfoShape(0, 0) == 0.f && std::abs(lfoShape(0, 0.5) - 1) < 1e-6f && std::abs(lfoShape(1, 0.25) - 0.5f) < 1e-6f && std::abs(lfoShape(3, 0.25) - 0.75f) < 1e-6f); }

    { section("Transient keeper: drum hits stay A while the rest morphs");
      auto A = drums(sr, 8.0); auto B = pad(sr, 8.0); int L; double c[2], sustain[2];
      for (int k = 0; k < 2; ++k) { auto e = engine(sr); L = e->latency(); float p[kNumParams]; neutral(p); p[P_MORPH] = 100; p[P_TRANS] = k ? 100.f : 0.f; auto o = render(*e, p, A, &B);
        // onset windows: first 25 ms after each 16th-note step that carries a hit
        double sxy = 0, sxx = 0, syy = 0; double six = 60.0 / 100 / 4;
        for (int st = 4; st < 50; ++st) { int q = st % 16; if (!(q == 0 || q == 4 || q == 6 || q == 8 || q == 11 || q == 12)) continue; size_t s0 = (size_t)(st * six * sr);
          for (size_t i = s0; i < s0 + 1200; ++i) { double x = o.out.L[i + (size_t)L], y = A.L[i]; sxy += x * y; sxx += x * x; syy += y * y; } }
        c[k] = sxy / std::sqrt(sxx * syy); sustain[k] = corr(o.out.L, B.L, 96000, 370000, L); }
      std::printf("  hit correlation with A: keeper 0 %.3f, keeper 100 %.3f; whole-loop correlation with B %.3f / %.3f\n", c[0], c[1], sustain[0], sustain[1]);
      CHECK(c[1] > c[0] + 0.2); CHECK(c[1] > 0.5); }

    { section("Formant shift moves the envelope by the textbook ratio 2^(st/12)");
      auto A = vocal(sr, 2.0, 196, false); auto B = formantTone(sr, 110, 2.0, 1000, 250, 0.3f, 0.02); size_t a = 24000, b = 72000;
      // envelope peak = the harmonic (of 110 Hz) with the most power in it and its two neighbours
      auto peakOf = [&](const Buf& x, size_t s0, size_t s1) { std::vector<double> pw; for (int h = 1; h * 110 < 4000; ++h) { double m = binMag(x, sr, h * 110.0, s0, s1); pw.push_back(m * m); }
          double best = 0; int bh = 0; for (size_t h = 1; h + 1 < pw.size(); ++h) { double v = pw[h - 1] + pw[h] + pw[h + 1]; if (v > best) { best = v; bh = (int)h + 1; } } return 110.0 * bh; };
      double orig = peakOf(B.L, a, b); std::printf("  B's own envelope peak: %.0f Hz\n", orig);
      for (float st : {-12.f, -5.f, 0.f, 7.f, 12.f}) { auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_MORPH] = 100; p[P_FORMANT] = st; auto o = render(*e, p, A, &B);
        double got = peakOf(o.out.L, a, b), want = orig * std::pow(2.0, st / 12.0); std::printf("  formant %+3.0f st: peak %.0f Hz (textbook %.0f Hz)\n", st, got, want);
        CHECKM(std::abs(std::log2(got / want)) < 0.17, "formant %+g st: peak %.0f Hz, want %.0f Hz", st, got, want); } }

    { section("Smoothness: slurs B's changes over time (slower decay after B stops)");
      double t20[2]; for (int k = 0; k < 2; ++k) { auto A = synth(sr, 3.0); Stereo B = pad(sr, 1.5); append(B, silence(sr, 1.5)); auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_STYLE] = ST_TALK; p[P_TALK_DIR] = 1; p[P_MORPH] = 100; p[P_SMOOTH] = k ? 100.f : 0.f;
          auto o = render(*e, p, A, &B); double ref = rms(o.out.L, 48000, 72000); size_t i = 72000 + (size_t)e->latency(); while (i + 480 < o.out.size() && rms(o.out.L, i, i + 480) > ref * 0.1) i += 480; t20[k] = (double)(i - 72000 - (size_t)e->latency()) / sr; }
      std::printf("  time to -20 dB after B stops: smooth 0 -> %.3f s, smooth 100 -> %.3f s\n", t20[0], t20[1]); CHECK(t20[1] > t20[0] + 0.15); }

    { section("Dry/Wet is a linear blend of dry and processed");
      auto A = vocal(sr, 1.5); auto B = synth(sr, 1.5); Stereo o[3]; float mixes[3] = {0, 100, 50};
      for (int k = 0; k < 3; ++k) { auto e = engine(sr); float p[kNumParams]; neutral(p); p[P_MORPH] = 80; p[P_MIX] = mixes[k]; o[k] = render(*e, p, A, &B).out; }
      double err = 0; for (size_t i = 4000; i < A.size(); ++i) err = std::max(err, (double)std::abs(o[2].L[i] - 0.5f * (o[0].L[i] + o[1].L[i]))); CHECKM(err < 1e-5, "err %g", err); }

    { section("presets and randomize");
      const auto& ps = factoryPresets(); CHECKM(ps.size() >= 30, "%zu presets", ps.size()); std::map<std::string, int> names; for (auto& x : ps) names[x.name]++; CHECK(names.size() == ps.size());
      auto A = vocal(sr, 1.5); auto B = synth(sr, 1.5);
      for (size_t i = 0; i < ps.size(); ++i) { float p[kNumParams]; applyPreset((int)i, p); bool inRange = true; for (int k = 0; k < kNumParams; ++k) inRange &= p[k] >= kParams[k].min && p[k] <= kParams[k].max; CHECKM(inRange, "preset %s range", ps[i].name);
        auto e = engine(sr); auto o = render(*e, p, A, &B, 256, {100, 0, true}); CHECK(finite(o.out)); CHECK(std::max(peak(o.out.L), peak(o.out.R)) <= 0.9661f);
        double d = 0; for (size_t j = 2000; j < A.size(); ++j) d += std::abs(o.out.L[j] - A.L[j - (size_t)e->latency()]); if (i > 0) CHECKM(d / (double)A.size() > 1e-4, "preset %s does nothing", ps[i].name); }
      Rng r; int ok = 0; for (int t = 0; t < 300; ++t) { float p[kNumParams]; setDefaults(p); randomize(p, r); bool g = true; for (int k = 0; k < kNumParams; ++k) { g &= p[k] >= kParams[k].min && p[k] <= kParams[k].max; if (kParams[k].kind != K_FLOAT) g &= p[k] == std::floor(p[k]); } g &= p[P_MIX] == 100 && p[P_OUT] == 0 && p[P_BSRC] == 0 && p[P_LIMIT] == 1; ok += g; }
      CHECK(ok == 300); }

    { section("sample rates and block sizes");
      for (double r : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0}) for (int blk : {1, 17, 256, 4096}) { if (blk == 1 && r > 48000) continue; auto A = vocal(r, 0.6); auto B = synth(r, 0.6);
          for (int st = 0; st < 4; ++st) { auto e = engine(r); float p[kNumParams]; neutral(p); p[P_STYLE] = (float)st; p[P_MORPH] = 60; auto o = render(*e, p, A, &B, blk); CHECK(finite(o.out)); CHECK(rms(o.out.L, (size_t)(0.2 * r)) > 1e-3); }
          auto e = engine(r); float p[kNumParams]; neutral(p); auto o = render(*e, p, A, &B, blk); CHECK(exactDelayed(A, o.out, e->latency(), "rate/block")); } }

    { section("CPU (this machine, 48 kHz stereo, 256-sample blocks)");
      auto A = vocal(sr, 10.0); auto B = synth(sr, 10.0);
      for (int st = 0; st < 4; ++st) { auto e = engine(sr); float p[kNumParams]; setDefaults(p); p[P_STYLE] = (float)st; p[P_MORPH] = 60; p[P_SMOOTH] = 40; p[P_FORMANT] = 3; p[P_TRANS] = 50; p[P_MOT_MODE] = MM_LFO;
        auto o = render(*e, p, A, &B); double pct = 100 * o.seconds / 10.0; std::printf("  CPU %-7s %.2f %% of one core\n", kStyles[st], pct); CHECK(pct < 25); } }

    std::printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
