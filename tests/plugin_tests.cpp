// MergeX — JUCE-level tests: identity, parameters, buses (main + sidechain, mono), latency reported to the host,
// Morph 0% exact through the processor, sidechain morphing, bypass, state, presets, randomize, editor + screenshots, CPU.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <chrono>
#include <cstdio>
using namespace mx;

static int passed = 0, failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
#define CHECKM(x, ...) do { if (x) ++passed; else { ++failed; std::printf("FAIL line %d: %s | ", __LINE__, #x); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

using Sig = std::vector<std::vector<float>>;
struct Head : juce::AudioPlayHead {
    bool playing = true; double bpm = 120, ppq = 0;
    juce::Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setIsPlaying(playing); p.setBpm(bpm); p.setPpqPosition(ppq); return p; }
};
struct Rig {
    MergeXProcessor p; Head head; double sr; int bs;
    Rig(double rate = 48000, int block = 256, bool sidechain = true) : sr(rate), bs(block) {
        auto l = p.getBusesLayout(); l.inputBuses.getReference(1) = sidechain ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled(); p.setBusesLayout(l);
        p.setPlayHead(&head); p.setRateAndBufferSizeDetails(sr, bs); p.prepareToPlay(sr, bs);
    }
    Sig run(const Sig& in, const Sig* sc = nullptr, double* secs = nullptr, bool bypassed = false) {
        size_t n = in[0].size(); int nCh = std::max(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()); int outCh = p.getMainBusNumOutputChannels();
        Sig out((size_t)outCh, std::vector<float>(n)); juce::AudioBuffer<float> b(nCh, bs); juce::MidiBuffer m; double t = 0;
        for (size_t pos = 0; pos < n; pos += (size_t)bs) {
            int k = (int)std::min<size_t>((size_t)bs, n - pos); b.setSize(nCh, k, false, false, true); b.clear();
            for (int c = 0; c < p.getMainBusNumInputChannels(); ++c) b.copyFrom(c, 0, in[(size_t)std::min<int>(c, (int)in.size() - 1)].data() + pos, k);
            if (sc && p.getBusCount(true) > 1 && p.getBus(true, 1)->isEnabled()) { int off = p.getMainBusNumInputChannels(); for (int c = 0; c < p.getBus(true, 1)->getNumberOfChannels(); ++c) b.copyFrom(off + c, 0, (*sc)[(size_t)std::min<int>(c, (int)sc->size() - 1)].data() + pos, k); }
            auto t0 = std::chrono::steady_clock::now(); if (bypassed) p.processBlockBypassed(b, m); else p.processBlock(b, m); t += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            for (int c = 0; c < outCh; ++c) std::memcpy(out[(size_t)c].data() + pos, b.getReadPointer(c), sizeof(float) * (size_t)k);
            head.ppq += k * head.bpm / 60.0 / sr;
        }
        if (secs) *secs = t; return out;
    }
};
static Sig tone(double sr, double sec, double f = 220, float a = 0.3f) { size_t n = (size_t)(sr * sec); Sig v(2, std::vector<float>(n)); for (size_t i = 0; i < n; ++i) v[0][i] = v[1][i] = a * (float)std::sin(2 * juce::MathConstants<double>::pi * f * (double)i / sr); return v; }
static Sig voice(double sr, double sec) { auto v = tone(sr, sec, 196, 0.2f); for (size_t i = 0; i < v[0].size(); ++i) { double t = (double)i / sr; float e = (float)(0.5 + 0.5 * std::sin(2 * juce::MathConstants<double>::pi * 2.5 * t)); float h = 0.08f * (float)std::sin(2 * juce::MathConstants<double>::pi * 588 * t); v[0][i] = (v[0][i] + h) * e; v[1][i] = v[0][i] * 0.95f; } return v; }
static double rms(const std::vector<float>& x, size_t a = 0) { double s = 0; for (size_t i = a; i < x.size(); ++i) s += (double)x[i] * x[i]; return std::sqrt(s / (double)std::max<size_t>(1, x.size() - a)); }
static double mag(const std::vector<float>& x, double sr, double f, size_t a, size_t b) { std::complex<double> acc = 0; for (size_t i = a; i < b; ++i) acc += (double)x[i] * std::exp(std::complex<double>(0, -2 * juce::MathConstants<double>::pi * f * (double)i / sr)); return 2 * std::abs(acc) / (double)(b - a); }
static bool finite(const Sig& o) { for (auto& c : o) for (float v : c) if (!std::isfinite(v)) return false; return true; }
static bool exactDelayed(const Sig& in, const Sig& out, int L) { for (size_t c = 0; c < out.size(); ++c) for (size_t i = 0; i < in[0].size(); ++i) { float want = i >= (size_t)L ? in[std::min(c, in.size() - 1)][i - (size_t)L] : 0.f; if (out[c][i] != want) { std::printf("  differs at ch %zu sample %zu: %.9g vs %.9g\n", c, i, out[c][i], want); return false; } } return true; }
static void savePng(juce::Component& c, const juce::String& name) {
    auto img = c.createComponentSnapshot(c.getLocalBounds(), true, 1.0f); auto f = juce::File::getCurrentWorkingDirectory().getChildFile(name); f.deleteFile();
    juce::FileOutputStream s(f); bool ok = s.openedOk() && juce::PNGImageFormat().writeImageToStream(img, s); CHECK(ok); std::printf("  screenshot %s %dx%d\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0); juce::ScopedJuceInitialiser_GUI gui;
    std::puts("MergeX plugin tests");

    { std::puts("- identity and parameters");
      Rig r; CHECK(r.p.getName() == "MergeX"); CHECK(!r.p.acceptsMidi() && !r.p.producesMidi() && !r.p.isMidiEffect()); CHECK(r.p.getTailLengthSeconds() > 0);
      CHECK((int)r.p.getParameters().size() == kNumParams); CHECK(r.p.getBypassParameter() == r.p.param(P_BYPASS));
      int okRange = 0, okText = 0;
      for (int i = 0; i < kNumParams; ++i) { auto* prm = r.p.param(i); const auto& d = kParams[i];
        okRange += prm->paramID == d.id && std::abs(prm->convertFrom0to1(0) - d.min) < 1e-4f && std::abs(prm->convertFrom0to1(1) - d.max) < 1e-4f && std::abs(r.p.value(i) - d.def) < 1e-4f;
        okText += prm->getCurrentValueAsText().isNotEmpty(); }
      CHECK(okRange == kNumParams); CHECK(okText == kNumParams);
      // text -> value round trips for every float parameter (incl. note names like "A2")
      int rt = 0, nf = 0; for (int i = 0; i < kNumParams; ++i) if (kParams[i].kind == K_FLOAT) { ++nf; auto* prm = r.p.param(i); for (float n : {0.f, 0.37f, 1.f}) { auto t = prm->getText(n, 64); float back = prm->getValueForText(t); rt += std::abs(prm->convertFrom0to1(back) - prm->convertFrom0to1(n)) <= 0.51f * std::max(0.02f * (kParams[i].max - kParams[i].min), 1.f); } }
      CHECKM(rt == nf * 3, "%d of %d text round trips", rt, nf * 3);
      CHECK(r.p.param(P_NOTE)->getText(r.p.param(P_NOTE)->convertTo0to1(45), 16) == "A2");
      juce::Random rnd(3); int rv = 0; for (int i = 0; i < kNumParams; ++i) { auto* prm = r.p.param(i); float n = rnd.nextFloat(); prm->setValueNotifyingHost(n); rv += std::abs(prm->convertTo0to1(r.p.value(i)) - prm->convertTo0to1(prm->convertFrom0to1(n))) < 1e-4f; }
      CHECK(rv == kNumParams); std::printf("  %d parameters\n", kNumParams); }

    { std::puts("- latency is reported to the host at every sample rate");
      for (double sr : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0}) { Rig r(sr, 512); CHECK(r.p.getLatencySamples() == r.p.engine().latency()); double ms = 1000.0 * r.p.getLatencySamples() / sr; CHECKM(ms > 20 && ms < 24, "%.0f Hz: %.2f ms", sr, ms); std::printf("  %6.0f Hz: %d samples = %.1f ms\n", sr, r.p.getLatencySamples(), ms); } }

    { std::puts("- buses: stereo main + stereo sidechain, mono, sidechain off");
      Rig r; CHECK(r.p.getBusCount(true) == 2 && r.p.getBusCount(false) == 1); CHECK(r.p.getBus(true, 1)->getName() == "Sidechain"); CHECK(r.p.getTotalNumInputChannels() == 4);
      auto l = r.p.getBusesLayout(); CHECK(r.p.checkBusesLayoutSupported(l));
      auto m = l; m.inputBuses.getReference(0) = juce::AudioChannelSet::mono(); m.outputBuses.getReference(0) = juce::AudioChannelSet::mono(); CHECK(r.p.checkBusesLayoutSupported(m));
      auto ms = l; ms.inputBuses.getReference(0) = juce::AudioChannelSet::mono(); CHECK(r.p.checkBusesLayoutSupported(ms));
      auto bad = l; bad.outputBuses.getReference(0) = juce::AudioChannelSet::mono(); CHECK(!r.p.checkBusesLayoutSupported(bad));
      auto q = l; q.outputBuses.getReference(0) = juce::AudioChannelSet::quadraphonic(); CHECK(!r.p.checkBusesLayoutSupported(q));
      auto sc = l; sc.inputBuses.getReference(1) = juce::AudioChannelSet::mono(); CHECK(r.p.checkBusesLayoutSupported(sc)); sc.inputBuses.getReference(1) = juce::AudioChannelSet::disabled(); CHECK(r.p.checkBusesLayoutSupported(sc));
      auto sq = l; sq.inputBuses.getReference(1) = juce::AudioChannelSet::quadraphonic(); CHECK(!r.p.checkBusesLayoutSupported(sq));
      MergeXProcessor mp; CHECK(mp.setBusesLayout(m)); mp.prepareToPlay(48000, 256); juce::AudioBuffer<float> b(std::max(mp.getTotalNumInputChannels(), mp.getTotalNumOutputChannels()), 256); juce::MidiBuffer mid;
      for (int k = 0; k < 8; ++k) { for (int i = 0; i < 256; ++i) b.setSample(0, i, 0.3f * std::sin((k * 256 + i) * 0.05f)); mp.processBlock(b, mid); }
      bool fin = true; for (int i = 0; i < 256; ++i) fin &= std::isfinite(b.getSample(0, i)); CHECK(fin); CHECK(b.getMagnitude(0, 0, 256) > 0.05f); }

    { std::puts("- Morph 0% is exactly A (latency-delayed) through the processor; bypass/Before too");
      auto in = voice(48000, 1.0); auto sc = tone(48000, 1.0, 330, 0.3f);
      { Rig a; auto o = a.run(in, &sc); CHECK(exactDelayed(in, o, a.p.getLatencySamples())); }
      { Rig a(48000, 256, false); a.p.setValue(P_STYLE, 2); a.p.setValue(P_SMOOTH, 70); auto o = a.run(in); CHECK(exactDelayed(in, o, a.p.getLatencySamples())); }
      { Rig a; a.p.setValue(P_MORPH, 100); auto o = a.run(in, &sc, nullptr, true); CHECK(exactDelayed(in, o, a.p.getLatencySamples())); }
      { Rig a; a.p.setValue(P_MORPH, 100); a.p.setValue(P_BYPASS, 1); auto o = a.run(in, &sc); CHECK(exactDelayed(in, o, a.p.getLatencySamples())); }
      { Rig a; a.p.setValue(P_MORPH, 100); a.p.setValue(P_COMPARE, 1); auto o = a.run(in, &sc); CHECK(exactDelayed(in, o, a.p.getLatencySamples())); }
      { Rig a; Sig sil(2, std::vector<float>(48000, 0.f)); auto o = a.run(sil, &sil); CHECK(rms(o[0]) == 0 && rms(o[1]) == 0); } }

    { std::puts("- the sidechain bus feeds B: Morph 100% Smooth turns A into B");
      auto in = tone(48000, 1.5, 220, 0.3f); auto sc = tone(48000, 1.5, 660, 0.3f);
      Rig a; a.p.setValue(P_MORPH, 100); auto o = a.run(in, &sc); double m220 = mag(o[0], 48000, 220, 24000, 72000), m660 = mag(o[0], 48000, 660, 24000, 72000);
      CHECKM(m660 > 0.2 && m220 < 0.01, "660 Hz %.3f, 220 Hz %.3f", m660, m220); CHECK(a.p.engine().meterBSource.load() == 0); CHECK(a.p.sidechainConnected());
      // mono sidechain works too
      Rig mo; auto l = mo.p.getBusesLayout(); l.inputBuses.getReference(1) = juce::AudioChannelSet::mono(); CHECK(mo.p.setBusesLayout(l)); mo.p.prepareToPlay(48000, 256); mo.p.setValue(P_MORPH, 100);
      Sig sc1(1, sc[0]); auto o2 = mo.run(in, &sc1); CHECK(mag(o2[1], 48000, 660, 24000, 72000) > 0.2);
      // no sidechain sound -> built-in pad, and the editor can say so
      Rig n; n.p.setValue(P_MORPH, 100); Sig sil(2, std::vector<float>(in[0].size(), 0.f)); auto o3 = n.run(in, &sil); CHECK(n.p.engine().meterBSource.load() == BS_PAD); CHECK(rms(o3[0], 24000) > 0.01); CHECK(finite(o3)); }

    { std::puts("- state: everything survives save/load; switches restore exactly");
      Rig a; a.p.setValue(P_STYLE, 2); a.p.setValue(P_MORPH, 61); a.p.setValue(P_FORMANT, -3.5f); a.p.setValue(P_NOTE, 52); a.p.setValue(P_LIMIT, 0); a.p.loadFactory(5); a.p.setValue(P_MIX, 77);
      juce::MemoryBlock mb; a.p.getStateInformation(mb); Rig b; b.p.setStateInformation(mb.getData(), (int)mb.getSize());
      int same = 0; for (int i = 0; i < kNumParams; ++i) same += std::abs(a.p.value(i) - b.p.value(i)) < 1e-4f; CHECK(same == kNumParams); CHECK(b.p.presetName() == a.p.presetName());
      { Rig c; juce::MemoryBlock m0; c.p.param(P_LIMIT)->setValueNotifyingHost(1.f); c.p.getStateInformation(m0); c.p.param(P_LIMIT)->setValueNotifyingHost(0.81f);
        c.p.setStateInformation(m0.getData(), (int)m0.getSize()); CHECKM(std::abs(c.p.param(P_LIMIT)->getValue() - 1.f) < 1e-4f, "switch restored as %f", c.p.param(P_LIMIT)->getValue()); }
      b.p.setStateInformation("junk", 4); CHECK(std::abs(b.p.value(P_MIX) - 77) < 1e-3f); }

    { std::puts("- factory presets as host programs; randomize");
      Rig r; CHECK(r.p.getNumPrograms() == (int)factoryPresets().size()); CHECK(r.p.getNumPrograms() >= 30);
      auto in = voice(48000, 0.5); auto sc = tone(48000, 0.5, 147, 0.2f);
      for (int i = 0; i < r.p.getNumPrograms(); ++i) { r.p.setCurrentProgram(i); CHECK(r.p.getCurrentProgram() == i); CHECK(r.p.getProgramName(i) == factoryPresets()[(size_t)i].name);
        float v[kNumParams]; applyPreset(i, v); bool ok = true; for (int k = 0; k < kNumParams; ++k) if (k != P_BYPASS && k != P_COMPARE) ok &= std::abs(r.p.value(k) - v[k]) < 1e-3f * std::max(1.f, kParams[k].max - kParams[k].min); CHECKM(ok, "preset %d", i);
        auto o = r.run(in, &sc); CHECK(finite(o)); }
      Rig q; float before[kNumParams]; q.p.readValues(before); int changed = 0; bool kept = true;
      for (int k = 0; k < 20; ++k) { q.p.randomize(); float now[kNumParams]; q.p.readValues(now); for (int i = 0; i < kNumParams; ++i) { if (i == P_BSRC || i == P_MIX || i == P_OUT || i == P_LIMIT || i == P_BYPASS || i == P_COMPARE || i == P_NOTE || i == P_BAL) kept &= now[i] == before[i]; else changed += now[i] != before[i]; } }
      CHECK(kept); CHECK(changed > 60); }

    { std::puts("- sample rates and block sizes, incl. a short host block");
      for (double sr : {44100.0, 48000.0, 96000.0, 192000.0}) for (int bs : {32, 128, 512, 2048}) { Rig r(sr, bs); r.p.loadFactory(10); auto o = r.run(voice(sr, 0.4), nullptr); CHECK(finite(o)); }
      Rig r(48000, 512); juce::AudioBuffer<float> b(4, 17); b.clear(); juce::MidiBuffer m; r.p.processBlock(b, m); CHECK(true); }

    { std::puts("- editor: builds, resizes, every control labelled and inside the window, screenshots");
      Rig r; auto in = voice(48000, 1.0); auto sc = tone(48000, 1.0, 147, 0.25f);
      r.p.setValue(P_MORPH, 35); r.run(in, &sc);
      std::unique_ptr<juce::AudioProcessorEditor> ed(r.p.createEditor()); auto* e = dynamic_cast<MergeXEditor*>(ed.get()); CHECK(e != nullptr);
      CHECK(ed->getWidth() == MergeXEditor::kW && ed->getHeight() == MergeXEditor::kH); CHECK(ed->isResizable());
      auto* c = ed->getConstrainer(); CHECK(c->getMinimumWidth() < MergeXEditor::kW && c->getMaximumWidth() >= 2 * MergeXEditor::kW - 1);
      e->refreshNow(); savePng(*ed, "MergeX-ui-1-smooth.png");
      r.p.loadFactory(10); r.run(in, &sc); e->refreshNow(); savePng(*ed, "MergeX-ui-2-talk.png");
      r.p.loadFactory(18); r.p.setValue(P_MOT_MODE, MM_LFO); r.run(in, &sc); e->refreshNow(); savePng(*ed, "MergeX-ui-3-swap-motion.png");
      r.p.loadFactory(22); Rig r2(48000, 256, false); (void)r2; e->refreshNow(); ed->setSize(MergeXEditor::kW * 6 / 10, MergeXEditor::kH * 6 / 10); e->refreshNow(); savePng(*ed, "MergeX-ui-4-grain-small-window.png");
      ed->setSize(MergeXEditor::kW * 3 / 2, MergeXEditor::kH * 3 / 2); e->refreshNow(); CHECK(ed->getWidth() == MergeXEditor::kW * 3 / 2);
      ed->setSize(MergeXEditor::kW, MergeXEditor::kH); e->refreshNow(); int outside = 0, unnamed = 0;
      std::function<void(juce::Component&, juce::Rectangle<int>, int)> walk = [&](juce::Component& comp, juce::Rectangle<int> area, int depth) { for (auto* ch : comp.getChildren()) { if (!ch->isVisible()) continue;
          if (!area.contains(ch->getBounds())) { ++outside; std::printf("  outside: %s %s in %s\n", ch->getName().toRawUTF8(), ch->getBounds().toString().toRawUTF8(), area.toString().toRawUTF8()); }
          if (depth == 1 && ch->getName().isEmpty() && dynamic_cast<juce::TextButton*>(ch) == nullptr) { ++unnamed; std::printf("  unnamed control at %s\n", ch->getBounds().toString().toRawUTF8()); }
          walk(*ch, ch->getLocalBounds(), depth + 1); } };
      walk(*ed, ed->getLocalBounds(), 0); CHECKM(outside == 0, "%d components outside their parent", outside); CHECKM(unnamed == 0, "%d unlabelled controls", unnamed);
      // the XY pad moves Morph and Shape
      auto* pad = [&]() -> XYPad* { std::function<XYPad*(juce::Component&)> f = [&](juce::Component& cc) -> XYPad* { for (auto* ch : cc.getChildren()) { if (auto* x = dynamic_cast<XYPad*>(ch)) return x; if (auto* y = f(*ch)) return y; } return nullptr; }; return f(*ed); }();
      CHECK(pad != nullptr); if (pad) { auto a = pad->area(); pad->setFromMouse({a.getX() + a.getWidth() * 0.75f, a.getY() + a.getHeight() * 0.25f}); CHECK(std::abs(r.p.value(P_MORPH) - 75) < 0.5f); CHECK(std::abs(r.p.value(P_SHAPE) - 75) < 0.5f); }
      ed.reset(); CHECK(true); }

    { std::puts("- CPU (48 kHz, 256-sample blocks, this CI machine)");
      auto in = voice(48000, 10.0); auto sc = tone(48000, 10.0, 147, 0.25f);
      auto cpu = [&](const char* name, std::function<void(MergeXProcessor&)> set) { Rig r; set(r.p); double t = 0; r.run(in, &sc, &t); double pct = 100.0 * t / 10.0; std::printf("  CPU %-32s %6.2f %%\n", name, pct); return pct; };
      double a = cpu("defaults (Morph 0)", [](MergeXProcessor&) {});
      double b = cpu("Smooth 60% + motion", [](MergeXProcessor& p) { p.setValue(P_MORPH, 60); p.setValue(P_MOT_MODE, MM_LFO); p.setValue(P_SMOOTH, 40); });
      double c = cpu("Talk 100%", [](MergeXProcessor& p) { p.loadFactory(10); });
      double d = cpu("Swap 50% glide", [](MergeXProcessor& p) { p.loadFactory(18); });
      double g = cpu("Grain 50%", [](MergeXProcessor& p) { p.loadFactory(22); });
      CHECK(a < 15 && b < 15 && c < 15 && d < 15 && g < 15); }

    std::printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
