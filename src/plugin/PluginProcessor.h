// MergeX — JUCE processor: host parameters from the engine table, main + sidechain buses, latency, presets, state.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"
#include "Presets.h"

class MergeXProcessor : public juce::AudioProcessor {
public:
    MergeXProcessor();
    ~MergeXProcessor() override = default;

    void prepareToPlay(double sr, int bs) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter("bypass"); }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "MergeX"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }

    int getNumPrograms() override { return (int)mx::factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int i) override;
    const juce::String getProgramName(int i) override;
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    // helpers for the editor and the tests
    juce::RangedAudioParameter* param(int i) const { return params[(size_t)i]; }
    float value(int i) const { return raw[(size_t)i]->load(); }
    void setValue(int i, float plain) { auto* p = params[(size_t)i]; p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(plain)); p->endChangeGesture(); }
    void readValues(float* out) const { for (int i = 0; i < mx::kNumParams; ++i) out[i] = raw[(size_t)i]->load(); }
    void applyValues(const float* v);

    juce::String presetName() const { return currentName; }
    void loadFactory(int i);
    static juce::File userPresetDir();
    bool saveUserPreset(const juce::String& name);
    bool loadUserPreset(const juce::File& f);
    juce::Array<juce::File> userPresets() const;
    void randomize();
    bool sidechainConnected() const { return scConnected.load(); }

    mx::Engine& engine() { return eng; }
    juce::Random rng;

private:
    mx::Engine eng; int currentProgram = 0; juce::String currentName = "Init";
    std::vector<std::atomic<float>*> raw; std::vector<juce::RangedAudioParameter*> params;
    juce::AudioBuffer<float> scratch; float vals[mx::kNumParams] = {};
    std::atomic<bool> scConnected {false};
    mx::Rng rr;
    void run(juce::AudioBuffer<float>& buf, bool bypassed);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MergeXProcessor)
};
