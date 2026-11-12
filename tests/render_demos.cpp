// MergeX — renders demo WAVs from sources synthesised here (vocal-like, synth, drums, pad): no outside audio.
#include "TestUtil.h"
using namespace mx; using namespace tu;
int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "."; const double sr = 48000, bpm = 100, sec = 9.6;   // 4 bars at 100 BPM
    auto vox = vocal(sr, sec, 196, true), syn = synth(sr, sec, bpm), drm = drums(sr, sec, bpm), pd = pad(sr, sec);
    writeWav(dir + "/MergeX-demo-00a-source-vocal.wav", vox, sr); writeWav(dir + "/MergeX-demo-00b-source-synth.wav", syn, sr);
    writeWav(dir + "/MergeX-demo-00c-source-drums.wav", drm, sr); writeWav(dir + "/MergeX-demo-00d-source-pad.wav", pd, sr);
    struct Demo { const char* file; const Stereo* a; const Stereo* b; std::function<void(float*)> set; std::function<void(size_t, float*)> move; };
    size_t n = vox.size();
    std::vector<Demo> demos = {
        {"01-vocal-to-synth-morph-sweep", &vox, &syn, [](float* p) { p[P_STYLE] = ST_SMOOTH; }, [n](size_t pos, float* p) { p[P_MORPH] = std::min(100.f, 100.f * (float)pos / (float)(n * 0.85)); }},
        {"02-drums-to-pad-transient-keep", &drm, &pd, [](float* p) { p[P_STYLE] = ST_SMOOTH; p[P_MORPH] = 75; p[P_TRANS] = 85; p[P_SMOOTH] = 25; }, nullptr},
        {"03-style-smooth-50", &vox, &syn, [](float* p) { p[P_STYLE] = ST_SMOOTH; p[P_MORPH] = 50; }, nullptr},
        {"04-style-talk-vocal-through-synth", &vox, &syn, [](float* p) { p[P_STYLE] = ST_TALK; p[P_MORPH] = 100; p[P_SHAPE] = 60; p[P_SMOOTH] = 8; }, nullptr},
        {"05-style-swap-synth-pitch-vocal-tone", &vox, &syn, [](float* p) { p[P_STYLE] = ST_SWAP; p[P_MORPH] = 100; p[P_SHAPE] = 100; }, nullptr},
        {"06-style-swap-glide-sweep", &vox, &pd, [](float* p) { p[P_STYLE] = ST_SWAP; p[P_SHAPE] = 100; }, [n](size_t pos, float* p) { p[P_MORPH] = std::min(100.f, 100.f * (float)pos / (float)(n * 0.85)); }},
        {"07-style-grain-shuffle-50", &vox, &syn, [](float* p) { p[P_STYLE] = ST_GRAIN; p[P_MORPH] = 50; p[P_SHAPE] = 45; }, nullptr},
        {"08-motion-lfo-1-bar", &drm, &syn, [](float* p) { p[P_STYLE] = ST_SMOOTH; p[P_MORPH] = 0; p[P_MOT_MODE] = MM_LFO; p[P_MOT_DIV] = 2; p[P_MOT_DEPTH] = 100; p[P_TRANS] = 60; }, nullptr},
        {"09-no-sidechain-built-in-pad", &vox, nullptr, [](float* p) { p[P_STYLE] = ST_SMOOTH; p[P_MORPH] = 60; p[P_BSRC] = BS_PAD; p[P_SMOOTH] = 50; }, nullptr},
        {"10-drums-talk-through-pad", &drm, &pd, [](float* p) { p[P_STYLE] = ST_TALK; p[P_MORPH] = 100; p[P_SHAPE] = 50; }, nullptr},
    };
    for (auto& d : demos) {
        float p[kNumParams]; setDefaults(p); d.set(p); auto e = engine(sr);
        Stereo a = *d.a; append(a, silence(sr, 1.0)); Stereo b; if (d.b) { b = *d.b; append(b, silence(sr, 1.0)); }
        auto r = render(*e, p, a, d.b ? &b : nullptr, 256, {bpm, 0, true}, d.move);
        // drop the latency so the demo starts on the beat
        Stereo o = make(r.out.size() - (size_t)e->latency()); for (size_t i = 0; i < o.size(); ++i) { o.L[i] = r.out.L[i + (size_t)e->latency()]; o.R[i] = r.out.R[i + (size_t)e->latency()]; }
        std::string fn = dir + "/MergeX-demo-" + d.file + ".wav"; writeWav(fn, o, sr);
        std::printf("%s  peak %.3f  rms %.1f dB  finite %d\n", fn.c_str(), std::max(peak(o.L), peak(o.R)), db(rms(o.L)), (int)finite(o));
    }
}
