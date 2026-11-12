#include "PluginProcessor.h"
#include "PluginEditor.h"
using namespace mx;

static juce::String noteName(float v) { static const char* n[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}; int m = (int)std::lround(v); return juce::String(n[(m % 12 + 12) % 12]) + juce::String(m / 12 - 1); }

/// "A2", "C#3" or a plain MIDI number -> MIDI note
static float parseNote(const juce::String& text) {
    auto t = text.trim().toUpperCase(); if (t.isEmpty()) return 45.f;
    if (t[0] < 'A' || t[0] > 'G') return t.getFloatValue();
    static const int base[] = {9, 11, 0, 2, 4, 5, 7}; int n = base[t[0] - 'A']; int i = 1;
    if (i < t.length() && t[i] == '#') { ++n; ++i; } else if (i < t.length() && t[i] == 'B') { --n; ++i; }
    return (float)(n + 12 * (t.substring(i).getIntValue() + 1));
}

juce::AudioProcessorValueTreeState::ParameterLayout MergeXProcessor::layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (int i = 0; i < kNumParams; ++i) {
        const auto& d = kParams[i]; juce::ParameterID id { d.id, 1 };
        if (d.kind == K_BOOL) l.add(std::make_unique<juce::AudioParameterBool>(id, d.name, d.def >= 0.5f));
        else if (d.kind == K_CHOICE) { juce::StringArray c; for (int k = 0; k < d.nChoices; ++k) c.add(d.choices[k]); l.add(std::make_unique<juce::AudioParameterChoice>(id, d.name, c, (int)d.def)); }
        else {
            juce::NormalisableRange<float> r(d.min, d.max); if (i == P_NOTE) r.interval = 1.f;
            auto unit = juce::String(d.unit); bool note = i == P_NOTE;
            auto toText = [unit, note](float v, int) { if (note) return noteName(v); int dec = unit == "%" ? 0 : 1; juce::String s = juce::String(v, dec); if (unit == "dB" || unit == "st") { if (v > 0.04f) s = "+" + s; } return s + (unit.isEmpty() ? "" : " " + unit); };
            auto fromText = [note](const juce::String& t) { return note ? parseNote(t) : t.retainCharacters("-0123456789.").getFloatValue(); };
            l.add(std::make_unique<juce::AudioParameterFloat>(id, d.name, r, d.def, juce::AudioParameterFloatAttributes().withLabel(note ? "" : d.unit).withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
        }
    }
    return l;
}

MergeXProcessor::MergeXProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true).withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "MERGEX", layout()) {
    for (int i = 0; i < kNumParams; ++i) { raw.push_back(apvts.getRawParameterValue(kParams[i].id)); params.push_back(apvts.getParameter(kParams[i].id)); }
    eng.prepare(48000, 512); setLatencySamples(eng.latency());
}

bool MergeXProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    if (in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono()) return false;
    if (out == juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::mono()) return false;
    if (l.inputBuses.size() > 1) { auto sc = l.getChannelSet(true, 1); if (!sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false; }
    return true;
}

void MergeXProcessor::prepareToPlay(double sr, int bs) {
    eng.prepare(sr, bs); scratch.setSize(2, std::max(bs, 32), false, false, true); setLatencySamples(eng.latency());
}

void MergeXProcessor::run(juce::AudioBuffer<float>& buffer, bool bypassed) {
    juce::ScopedNoDenormals nd;
    int n = buffer.getNumSamples(); if (n <= 0) return;
    auto mainIn = getBusBuffer(buffer, true, 0); auto mainOut = getBusBuffer(buffer, false, 0);
    int inCh = mainIn.getNumChannels(), outCh = mainOut.getNumChannels();
    if (outCh == 0) return;
    if (scratch.getNumSamples() < n) scratch.setSize(2, n, false, false, true);   // only if the host breaks its promise
    for (int c = 0; c < 2; ++c) { if (inCh > 0) scratch.copyFrom(c, 0, mainIn, std::min(c, inCh - 1), 0, n); else scratch.clear(c, 0, n); }
    const float* scL = nullptr; const float* scR = nullptr; bool conn = false;
    if (getBusCount(true) > 1 && getBus(true, 1)->isEnabled()) { auto sc = getBusBuffer(buffer, true, 1); if (sc.getNumChannels() > 0) { scL = sc.getReadPointer(0); scR = sc.getReadPointer(std::min(1, sc.getNumChannels() - 1)); conn = true; } }
    scConnected.store(conn);
    for (int i = 0; i < kNumParams; ++i) { // snap host round-trip noise (1e-7) to whole values so 0 dB is exactly unity
        float v = raw[(size_t)i]->load(), r = std::round(v); vals[i] = std::abs(v - r) <= 1e-5f * std::max(1.f, kParams[i].max - kParams[i].min) ? r : v; }
    HostInfo hi;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) { if (auto b = pos->getBpm()) hi.bpm = *b; if (auto q = pos->getPpqPosition()) { hi.ppq = *q; hi.hasPpq = true; } hi.playing = pos->getIsPlaying(); }
    eng.process(scratch.getWritePointer(0), scratch.getWritePointer(1), n, scL, scR, hi, vals, bypassed);
    if (outCh == 1) { auto* d = mainOut.getWritePointer(0); auto* a = scratch.getReadPointer(0); auto* b = scratch.getReadPointer(1); if (inCh == 1) std::memcpy(d, a, sizeof(float) * (size_t)n); else for (int i = 0; i < n; ++i) d[i] = 0.5f * (a[i] + b[i]); }
    else { mainOut.copyFrom(0, 0, scratch, 0, 0, n); mainOut.copyFrom(1, 0, scratch, 1, 0, n); for (int c = 2; c < outCh; ++c) mainOut.clear(c, 0, n); }
}
void MergeXProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer& m) { m.clear(); run(b, false); }
// bypass keeps the latency (the dry signal stays delayed by the same amount), so FL's delay compensation stays lined up
void MergeXProcessor::processBlockBypassed(juce::AudioBuffer<float>& b, juce::MidiBuffer& m) { m.clear(); run(b, true); }

void MergeXProcessor::applyValues(const float* v) {
    for (int i = 0; i < kNumParams; ++i) { if (i == P_BYPASS || i == P_COMPARE) continue;
        auto* p = params[(size_t)i]; float n = p->convertTo0to1(v[i]); if (std::abs(p->getValue() - n) > 1e-7f) { p->beginChangeGesture(); p->setValueNotifyingHost(n); p->endChangeGesture(); } }
}
void MergeXProcessor::loadFactory(int i) {
    const auto& ps = factoryPresets(); if (i < 0 || i >= (int)ps.size()) return;
    float v[kNumParams]; applyPreset(i, v); applyValues(v);
    currentProgram = i; currentName = ps[(size_t)i].name; updateHostDisplay(ChangeDetails().withProgramChanged(true));
}
void MergeXProcessor::setCurrentProgram(int i) { loadFactory(i); }
const juce::String MergeXProcessor::getProgramName(int i) { const auto& ps = factoryPresets(); return i >= 0 && i < (int)ps.size() ? juce::String(ps[(size_t)i].name) : juce::String(); }
void MergeXProcessor::randomize() { float v[kNumParams]; readValues(v); rr.s = (uint32_t)rng.nextInt() | 1u; mx::randomize(v, rr); applyValues(v); currentName = "Random roll"; }

void MergeXProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto st = apvts.copyState(); st.setProperty("preset", currentName, nullptr); st.setProperty("program", currentProgram, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary(*xml, dest);
}
void MergeXProcessor::setStateInformation(const void* data, int size) {
    auto xml = getXmlFromBinary(data, size); if (!xml) return; auto st = juce::ValueTree::fromXml(*xml); if (!st.hasType(apvts.state.getType())) return;
    apvts.replaceState(st);
    if (st.hasProperty("preset")) currentName = st.getProperty("preset").toString(); currentProgram = (int)st.getProperty("program", 0);
    // JUCE skips params whose stored value matches its cached one, so a switch the host left at e.g. 0.81 would keep reporting 0.81.
    for (int i = 0; i < kNumParams; ++i) { auto* prm = param(i); float want = prm->convertTo0to1(raw[(size_t)i]->load()); if (std::abs(prm->getValue() - want) > 1e-6f) prm->setValueNotifyingHost(want); }
}

juce::File MergeXProcessor::userPresetDir() { return juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("MergeX Presets"); }
bool MergeXProcessor::saveUserPreset(const juce::String& name) {
    auto clean = juce::File::createLegalFileName(name.trim()); if (clean.isEmpty()) return false;
    auto dir = userPresetDir(); if (!dir.createDirectory()) return false;
    currentName = clean; juce::MemoryBlock mb; getStateInformation(mb); auto xml = getXmlFromBinary(mb.getData(), (int)mb.getSize()); if (!xml) return false;
    return xml->writeTo(dir.getChildFile(clean + ".mxpreset"));
}
bool MergeXProcessor::loadUserPreset(const juce::File& f) {
    auto xml = juce::XmlDocument::parse(f); if (!xml) return false; juce::MemoryBlock mb; copyXmlToBinary(*xml, mb); setStateInformation(mb.getData(), (int)mb.getSize());
    currentName = f.getFileNameWithoutExtension(); return true;
}
juce::Array<juce::File> MergeXProcessor::userPresets() const { auto a = userPresetDir().findChildFiles(juce::File::findFiles, false, "*.mxpreset"); a.sort(); return a; }

juce::AudioProcessorEditor* MergeXProcessor::createEditor() { return new MergeXEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MergeXProcessor(); }
