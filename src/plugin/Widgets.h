// MergeX — palette, look and feel, and parameter-bound widgets. Original palette: deep indigo night,
// aqua for A, coral for B, violet where they meet. Every control carries its own label.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

namespace ui {
namespace col {
const juce::Colour bg{0xff090a14}, bg2{0xff0e1020}, panel{0xff141729}, panelHi{0xff1b1f38}, edge{0xff2c3263}, text{0xfff3f5ff}, dim{0xffaab1d9},
    a{0xff35f2d2}, b{0xffff5f8f}, mid{0xffa27dff}, ink{0xff070810}, knob{0xff1a1d33}, track{0xff0a0c19}, off{0xff4a5080};
}
inline juce::Font font(float h, bool bold = false) { return juce::Font(juce::FontOptions(h, bold ? juce::Font::bold : juce::Font::plain)); }
/// colour along the A -> B morph line
inline juce::Colour morphColour(float t) { t = juce::jlimit(0.f, 1.f, t); return t < 0.5f ? col::a.interpolatedWith(col::mid, t * 2) : col::mid.interpolatedWith(col::b, t * 2 - 1); }
inline void glow(juce::Graphics& g, juce::Point<float> c, float r, juce::Colour cl, float alpha = 0.5f) {
    juce::ColourGradient gr(cl.withAlpha(alpha), c.x, c.y, cl.withAlpha(0.f), c.x + r, c.y, true); g.setGradientFill(gr); g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
}

class Look : public juce::LookAndFeel_V4 {
public:
    Look() {
        setColour(juce::ResizableWindow::backgroundColourId, col::bg);
        setColour(juce::TextButton::buttonColourId, col::panelHi); setColour(juce::TextButton::buttonOnColourId, col::mid);
        setColour(juce::TextButton::textColourOffId, col::text); setColour(juce::TextButton::textColourOnId, col::ink);
        setColour(juce::ComboBox::backgroundColourId, col::bg2); setColour(juce::ComboBox::textColourId, col::text); setColour(juce::ComboBox::outlineColourId, col::edge); setColour(juce::ComboBox::arrowColourId, col::a);
        setColour(juce::PopupMenu::backgroundColourId, col::panel); setColour(juce::PopupMenu::textColourId, col::text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, col::mid); setColour(juce::PopupMenu::highlightedTextColourId, col::ink); setColour(juce::PopupMenu::headerTextColourId, col::a);
        setColour(juce::Label::textColourId, col::text);
        setColour(juce::TooltipWindow::backgroundColourId, col::text); setColour(juce::TooltipWindow::textColourId, col::ink); setColour(juce::TooltipWindow::outlineColourId, col::mid);
        setColour(juce::TextEditor::backgroundColourId, col::bg2); setColour(juce::TextEditor::textColourId, col::text); setColour(juce::TextEditor::outlineColourId, col::edge); setColour(juce::TextEditor::focusedOutlineColourId, col::a);
        setColour(juce::AlertWindow::backgroundColourId, col::panel); setColour(juce::AlertWindow::textColourId, col::text); setColour(juce::AlertWindow::outlineColourId, col::mid);
        setColour(juce::CaretComponent::caretColourId, col::a);
    }
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override {
        auto r = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(2); float d = std::min(r.getWidth(), r.getHeight()); auto c = r.withSizeKeepingCentre(d, d);
        bool hero = s.getProperties()["hero"]; bool bip = s.getProperties()["bipolar"];
        float thick = hero ? d * 0.06f : juce::jlimit(3.f, 6.f, d * 0.08f); float rad = d * 0.5f - thick * 0.5f - (hero ? d * 0.04f : 0.f); float ang = a0 + pos * (a1 - a0);
        juce::Path track; track.addCentredArc(c.getCentreX(), c.getCentreY(), rad, rad, 0, a0, a1, true);
        g.setColour(col::track); g.strokePath(track, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (hero) {   // A -> B gradient arc with a soft glow
            const int segs = 64; for (int k = 0; k < segs; ++k) { float t0 = (float)k / segs, t1 = (float)(k + 1) / segs; if (t0 > pos) break; float e = std::min(t1, pos);
                juce::Path seg; seg.addCentredArc(c.getCentreX(), c.getCentreY(), rad, rad, 0, a0 + t0 * (a1 - a0), a0 + e * (a1 - a0), true);
                g.setColour(morphColour(t0).withAlpha(0.25f)); g.strokePath(seg, juce::PathStrokeType(thick * 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
                g.setColour(morphColour(t0)); g.strokePath(seg, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::butt)); }
        } else {
            float from = bip ? (a0 + a1) * 0.5f : a0; juce::Path arc; arc.addCentredArc(c.getCentreX(), c.getCentreY(), rad, rad, 0, std::min(from, ang), std::max(from, ang), true);
            juce::Colour ac = s.isEnabled() ? juce::Colour((juce::uint32)(int)s.getProperties().getWithDefault("accent", (int)col::mid.getARGB())) : col::off;
            g.setColour(ac); g.strokePath(arc, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        auto cap = c.reduced(thick * (hero ? 2.6f : 1.9f) + (hero ? d * 0.04f : 0.f));
        g.setColour(juce::Colours::black.withAlpha(0.45f)); g.fillEllipse(cap.translated(0, hero ? 4.f : 2.f));
        juce::ColourGradient grad(juce::Colour(0xff272b4a), cap.getCentreX(), cap.getY(), col::knob, cap.getCentreX(), cap.getBottom(), false); g.setGradientFill(grad); g.fillEllipse(cap);
        g.setColour(col::edge); g.drawEllipse(cap, 1.2f);
        float pr = cap.getWidth() * 0.5f; juce::Path ptr; float pw = hero ? 5.f : 3.4f; ptr.addRoundedRectangle(-pw * 0.5f, -pr + (hero ? 8.f : 3.f), pw, pr * (hero ? 0.28f : 0.5f), pw * 0.5f);
        g.setColour(hero ? morphColour(pos) : col::text); g.fillPath(ptr, juce::AffineTransform::rotation(ang).translated(cap.getCentreX(), cap.getCentreY()));
        if (hero) { g.setColour(col::text); g.setFont(font(cap.getHeight() * 0.24f, true)); g.drawText(juce::String(juce::roundToInt(pos * 100)) + "%", cap.toNearestInt(), juce::Justification::centred); }
    }
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override {
        auto r = b.getLocalBounds().toFloat().reduced(1); bool on = b.getToggleState(); float rad = std::min(8.f, r.getHeight() * 0.5f);
        juce::Colour accent((juce::uint32)(int)b.getProperties().getWithDefault("accent", (int)col::mid.getARGB()));
        auto fill = on ? accent : col::panelHi; if (over) fill = fill.brighter(0.1f); if (down) fill = fill.darker(0.1f);
        if (on) { g.setColour(accent.withAlpha(0.25f)); g.fillRoundedRectangle(r.expanded(1.5f), rad + 1.5f); }
        g.setColour(fill); g.fillRoundedRectangle(r, rad); g.setColour(on ? accent.brighter(0.3f) : col::edge); g.drawRoundedRectangle(r, rad, 1.f);
    }
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override {
        g.setFont(font(juce::jlimit(10.f, 15.f, b.getHeight() * 0.48f), true)); g.setColour(b.getToggleState() ? col::ink : col::text);
        g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(4, 1), juce::Justification::centred, 1, 0.85f);
    }
    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override {
        auto r = juce::Rectangle<float>(0, 0, (float)w, (float)h).reduced(0.5f); g.setColour(col::bg2); g.fillRoundedRectangle(r, 6); g.setColour(col::edge); g.drawRoundedRectangle(r, 6, 1);
        juce::Path p; float cx = (float)w - 11, cy = (float)h * 0.5f; p.addTriangle(cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3); g.setColour(col::a); g.fillPath(p);
    }
    juce::Font getComboBoxFont(juce::ComboBox& c) override { return font(juce::jlimit(10.f, 14.f, c.getHeight() * 0.55f)); }
    void positionComboBoxText(juce::ComboBox& c, juce::Label& l) override { l.setBounds(4, 0, c.getWidth() - 20, c.getHeight()); l.setFont(getComboBoxFont(c)); }
    juce::Font getPopupMenuFont() override { return font(15); }
};

/// Knob with its name above and its value below, both always visible.
class Knob : public juce::Component {
public:
    Knob(MergeXProcessor& p, int index, const juce::String& label = {}, juce::Colour accent = col::mid) : idx(index), name(label.isEmpty() ? juce::String(mx::kParams[index].name) : label) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        s.setPopupDisplayEnabled(true, true, nullptr); s.setDoubleClickReturnValue(true, mx::kParams[index].def);
        const auto& d = mx::kParams[index]; if (d.min < 0 && d.max > 0) s.getProperties().set("bipolar", true);
        s.getProperties().set("accent", (int)accent.getARGB());
        addAndMakeVisible(s); att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, d.id, s);
        s.onValueChange = [this] { repaint(); };
        s.setTooltip(juce::String(d.name) + " - drag up/down, double-click to reset");
        setName(d.name);
    }
    void setLabel(const juce::String& l) { if (l != name) { name = l; repaint(); } }
    void resized() override { auto r = getLocalBounds(); r.removeFromTop(labelH()); r.removeFromBottom(valH()); s.setBounds(r); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds(); g.setColour(isEnabled() ? col::text : col::dim); g.setFont(font((float)labelH() * 0.82f, true));
        g.drawFittedText(name, r.removeFromTop(labelH()), juce::Justification::centred, 1, 0.8f);
        g.setColour(col::dim); g.setFont(font((float)valH() * 0.85f)); g.drawFittedText(s.getTextFromValue(s.getValue()), r.removeFromBottom(valH()), juce::Justification::centred, 1);
    }
    juce::Slider s; int idx;
private:
    int labelH() const { return juce::jlimit(12, 18, getHeight() / 6); }
    int valH() const { return juce::jlimit(11, 16, getHeight() / 7); }
    juce::String name; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

/// On/off pill bound to a bool parameter.
class Toggle : public juce::TextButton {
public:
    Toggle(MergeXProcessor& p, int index, const juce::String& label = {}, juce::Colour accent = col::mid) : juce::TextButton(label.isEmpty() ? juce::String(mx::kParams[index].name) : label) {
        setClickingTogglesState(true); getProperties().set("accent", (int)accent.getARGB());
        att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, mx::kParams[index].id, *this); setTooltip(mx::kParams[index].name); setName(mx::kParams[index].name);
    }
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
};

/// Row of buttons for a choice parameter.
class Segmented : public juce::Component {
public:
    Segmented(MergeXProcessor& p, int index, juce::Colour accent = col::mid) : param(p.param(index)), n(mx::kParams[index].nChoices) {
        setName(mx::kParams[index].name);
        for (int k = 0; k < n; ++k) { auto* b = btn.add(new juce::TextButton(mx::kParams[index].choices[k])); b->setClickingTogglesState(false); b->getProperties().set("accent", (int)accent.getARGB()); addAndMakeVisible(b);
            b->setTooltip(juce::String(mx::kParams[index].name) + ": " + mx::kParams[index].choices[k]);
            b->onClick = [this, k] { if (pa) pa->setValueAsCompleteGesture((float)k); }; }
        pa = std::make_unique<juce::ParameterAttachment>(*param, [this](float v) { int c = (int)std::lround(v); for (int k = 0; k < btn.size(); ++k) btn[k]->setToggleState(k == c, juce::dontSendNotification); });
        pa->sendInitialUpdate();
    }
    void resized() override { auto r = getLocalBounds(); int w = r.getWidth() / std::max(1, n); for (int k = 0; k < n; ++k) btn[k]->setBounds(k == n - 1 ? r : r.removeFromLeft(w).withTrimmedRight(4)); }
    juce::OwnedArray<juce::TextButton> btn;
private:
    juce::RangedAudioParameter* param; int n; std::unique_ptr<juce::ParameterAttachment> pa;
};

/// Combo for a choice parameter, with its label above.
class Choice : public juce::Component {
public:
    Choice(MergeXProcessor& p, int index, const juce::String& label = {}) : name(label.isEmpty() ? juce::String(mx::kParams[index].name) : label) {
        const auto& d = mx::kParams[index]; for (int k = 0; k < d.nChoices; ++k) box.addItem(d.choices[k], k + 1);
        addAndMakeVisible(box); att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts, d.id, box); box.setTooltip(d.name); setName(d.name);
    }
    void resized() override { auto r = getLocalBounds(); r.removeFromTop(labelH()); box.setBounds(r.withSizeKeepingCentre(r.getWidth(), std::min(r.getHeight(), 26))); }
    void paint(juce::Graphics& g) override { g.setColour(col::text); g.setFont(font((float)labelH() * 0.82f, true)); g.drawFittedText(name, getLocalBounds().removeFromTop(labelH()), juce::Justification::centred, 1); }
    juce::ComboBox box;
private:
    int labelH() const { return juce::jlimit(12, 17, getHeight() / 3); }
    juce::String name; std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
};
} // namespace ui
