// MergeX — editor. The Morph knob and the XY pad are the heroes; source, motion, shaping and output controls are smaller.
#pragma once
#include "Widgets.h"

/// Style picker: four tiles, name + one line saying what it does.
class StyleTiles : public juce::Component {
public:
    explicit StyleTiles(MergeXProcessor& p);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { hover = -1; repaint(); }
    juce::Rectangle<int> tile(int k) const;
private:
    std::unique_ptr<juce::ParameterAttachment> pa; int current = 0, hover = -1;
};

/// XY pad: X = Morph (A left, B right), Y = the style's shape control. A glowing dot shows where the morph really is right now.
class XYPad : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit XYPad(MergeXProcessor& p);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override; void mouseDrag(const juce::MouseEvent&) override; void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void setFromMouse(juce::Point<float> pos);
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced(10); }
private:
    MergeXProcessor& proc; bool dragging = false;
};

/// Little level bar with a caption, for the A and B inputs.
class LevelBar : public juce::Component {
public:
    LevelBar(const juce::String& n, juce::Colour c) : name(n), colour(c) {}
    void set(float lin) { float d = juce::jlimit(0.f, 1.f, (20.f * std::log10(std::max(lin, 1e-6f)) + 60.f) / 60.f); if (std::abs(d - level) > 0.004f) { level = d; repaint(); } }
    void paint(juce::Graphics&) override;
    juce::String name; juce::Colour colour; float level = 0;
};

class MergeXEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    static constexpr int kW = 1000, kH = 640;
    explicit MergeXEditor(MergeXProcessor&);
    ~MergeXEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void refreshNow() { timerCallback(); }
private:
    void timerCallback() override;
    void showPresetMenu(); void stepPreset(int d); void savePreset();
    void card(juce::Graphics&, juce::Rectangle<int> r, const juce::String& title, juce::Colour accent = ui::col::edge);
    MergeXProcessor& proc; ui::Look look; juce::Component content; juce::TooltipWindow tips { this, 600 };
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, presetBtn { "Init" }, saveBtn { "Save" }, randomBtn { "Randomize" };
    std::unique_ptr<ui::Knob> morph, note, balance, motDepth, smooth, formant, trans, mix, outGain;
    std::unique_ptr<ui::Toggle> before, limit;
    std::unique_ptr<ui::Segmented> talkDir, swapDir, motMode;
    std::unique_ptr<ui::Choice> bSource, motRate, motShape;
    std::unique_ptr<StyleTiles> styles; std::unique_ptr<XYPad> pad;
    std::unique_ptr<LevelBar> levelA, levelB;
    juce::String bStatus, styleHint, lastKey; float grDb = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MergeXEditor)
};
