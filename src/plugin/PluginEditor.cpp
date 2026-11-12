#include "PluginEditor.h"
using namespace mx; using namespace ui;

static const char* kStyleDesc[] = {"blend tone and texture", "one sound speaks through the other", "pitch of one, tone of the other", "shuffle tiny pieces of both"};

// ===================================================================================================================== style tiles
StyleTiles::StyleTiles(MergeXProcessor& p) {
    setName("Style");
    pa = std::make_unique<juce::ParameterAttachment>(*p.param(P_STYLE), [this](float v) { current = (int)std::lround(v); repaint(); }); pa->sendInitialUpdate();
}
juce::Rectangle<int> StyleTiles::tile(int k) const { int w = getWidth() / 4; return juce::Rectangle<int>(k * w, 0, k == 3 ? getWidth() - 3 * w : w, getHeight()).reduced(4, 0); }
void StyleTiles::paint(juce::Graphics& g) {
    for (int k = 0; k < 4; ++k) {
        auto r = tile(k).toFloat(); bool on = k == current; juce::Colour acc = morphColour((float)k / 3.f);
        if (on) { g.setColour(acc.withAlpha(0.22f)); g.fillRoundedRectangle(r.expanded(2), 12); }
        g.setColour(on ? col::panelHi.brighter(0.08f) : (k == hover ? col::panelHi : col::bg2)); g.fillRoundedRectangle(r, 10);
        g.setColour(on ? acc : col::edge); g.drawRoundedRectangle(r, 10, on ? 2.f : 1.f);
        auto t = r.reduced(12, 6).toNearestInt();
        g.setColour(on ? acc : col::text); g.setFont(font(19, true)); g.drawText(kStyles[k], t.removeFromTop(t.getHeight() / 2 + 3), juce::Justification::bottomLeft);
        g.setColour(on ? col::text : col::dim); g.setFont(font(12)); g.drawFittedText(kStyleDesc[k], t, juce::Justification::topLeft, 1, 0.8f);
    }
}
void StyleTiles::mouseDown(const juce::MouseEvent& e) { for (int k = 0; k < 4; ++k) if (tile(k).contains(e.getPosition())) pa->setValueAsCompleteGesture((float)k); }
void StyleTiles::mouseMove(const juce::MouseEvent& e) { int h = -1; for (int k = 0; k < 4; ++k) if (tile(k).contains(e.getPosition())) h = k; if (h != hover) { hover = h; repaint(); } }

// ===================================================================================================================== XY pad
XYPad::XYPad(MergeXProcessor& p) : proc(p) { setName("XY Pad"); setTooltip("Drag: left/right = Morph from A to B, up/down = the style's shape control. Double-click to centre the shape."); }
void XYPad::setFromMouse(juce::Point<float> pos) {
    auto a = area(); float x = juce::jlimit(0.f, 1.f, (pos.x - a.getX()) / a.getWidth()), y = juce::jlimit(0.f, 1.f, 1.f - (pos.y - a.getY()) / a.getHeight());
    proc.param(P_MORPH)->setValueNotifyingHost(x); proc.param(P_SHAPE)->setValueNotifyingHost(y);
}
void XYPad::mouseDown(const juce::MouseEvent& e) { dragging = true; proc.param(P_MORPH)->beginChangeGesture(); proc.param(P_SHAPE)->beginChangeGesture(); setFromMouse(e.position); }
void XYPad::mouseDrag(const juce::MouseEvent& e) { setFromMouse(e.position); }
void XYPad::mouseUp(const juce::MouseEvent&) { if (dragging) { proc.param(P_MORPH)->endChangeGesture(); proc.param(P_SHAPE)->endChangeGesture(); } dragging = false; }
void XYPad::mouseDoubleClick(const juce::MouseEvent&) { proc.setValue(P_SHAPE, 50); }
void XYPad::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat(); auto a = area();
    juce::ColourGradient bgG(col::a.withAlpha(0.16f), b.getX(), b.getCentreY(), col::b.withAlpha(0.16f), b.getRight(), b.getCentreY(), false); bgG.addColour(0.5, col::mid.withAlpha(0.08f));
    g.setColour(col::bg2); g.fillRoundedRectangle(b, 14); g.setGradientFill(bgG); g.fillRoundedRectangle(b, 14);
    g.setColour(col::edge); g.drawRoundedRectangle(b.reduced(0.5f), 14, 1.2f);
    g.setColour(col::edge.withAlpha(0.55f)); for (int k = 1; k < 4; ++k) { float x = a.getX() + a.getWidth() * k / 4.f, y = a.getY() + a.getHeight() * k / 4.f; g.drawLine(x, a.getY(), x, a.getBottom(), 1.f); g.drawLine(a.getX(), y, a.getRight(), y, 1.f); }
    g.setFont(font(15, true)); g.setColour(col::a); g.drawText("A", a.withWidth(30).withTrimmedTop(a.getHeight() - 26).toNearestInt(), juce::Justification::centredLeft);
    g.setColour(col::b); g.drawText("B", a.withTrimmedLeft(a.getWidth() - 30).withTrimmedTop(a.getHeight() - 26).toNearestInt(), juce::Justification::centredRight);
    float mk = proc.value(P_MORPH) * 0.01f, y = proc.value(P_SHAPE) * 0.01f, me = proc.engine().meterMorph.load();
    auto at = [&](float xx, float yy) { return juce::Point<float>(a.getX() + xx * a.getWidth(), a.getBottom() - yy * a.getHeight()); };
    auto live = at(me, y), set = at(mk, y);
    glow(g, live, 46, morphColour(me), 0.55f); g.setColour(morphColour(me)); g.fillEllipse(juce::Rectangle<float>(14, 14).withCentre(live));
    g.setColour(col::text); g.drawEllipse(juce::Rectangle<float>(26, 26).withCentre(set), 2.f);
    g.setColour(col::dim); g.setFont(font(12)); g.drawText("Morph " + juce::String(juce::roundToInt(mk * 100)) + "%   " + shapeShort((int)proc.value(P_STYLE)) + " " + juce::String(juce::roundToInt(y * 100)) + "%",
                                                             a.withHeight(18).toNearestInt(), juce::Justification::centredLeft);
}

// ===================================================================================================================== level bar
void LevelBar::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat(); auto cap = r.removeFromLeft(18);
    g.setColour(colour); g.setFont(font(15, true)); g.drawText(name, cap.toNearestInt(), juce::Justification::centredLeft);
    auto bar = r.withSizeKeepingCentre(r.getWidth(), 8); g.setColour(col::track); g.fillRoundedRectangle(bar, 4);
    g.setColour(colour); g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * level), 4);
}

// ===================================================================================================================== editor
MergeXEditor::MergeXEditor(MergeXProcessor& p) : juce::AudioProcessorEditor(p), proc(p) {
    setLookAndFeel(&look); addAndMakeVisible(content); content.setInterceptsMouseClicks(false, true);
    for (auto* b : {&prevBtn, &nextBtn, &presetBtn, &saveBtn, &randomBtn}) content.addAndMakeVisible(b);
    prevBtn.setTooltip("Previous preset"); nextBtn.setTooltip("Next preset"); presetBtn.setTooltip("Pick a preset"); saveBtn.setTooltip("Save your own preset");
    randomBtn.setTooltip("Roll a new morph (leaves B source, Dry/Wet and Output alone)");
    prevBtn.onClick = [this] { stepPreset(-1); }; nextBtn.onClick = [this] { stepPreset(1); }; presetBtn.onClick = [this] { showPresetMenu(); }; saveBtn.onClick = [this] { savePreset(); };
    randomBtn.onClick = [this] { proc.randomize(); };

    auto add = [&](auto& ptr, auto* c) { ptr.reset(c); content.addAndMakeVisible(*c); };
    add(styles, new StyleTiles(p)); add(pad, new XYPad(p));
    add(morph, new Knob(p, P_MORPH, "MORPH")); morph->s.getProperties().set("hero", true); morph->s.setTooltip("Morph: 0% = only A, 100% = B's character, in between = a real mix of both");
    add(talkDir, new Segmented(p, P_TALK_DIR)); add(swapDir, new Segmented(p, P_SWAP_DIR));
    add(bSource, new Choice(p, P_BSRC, "B Source")); add(note, new Knob(p, P_NOTE, "Tone Note", col::b)); add(balance, new Knob(p, P_BAL, "A/B Balance", col::b));
    note->s.setTooltip("Pitch of the built-in Saw/Pad"); balance->s.setTooltip("How loud B is against A going into the morph");
    add(levelA, new LevelBar("A", col::a)); add(levelB, new LevelBar("B", col::b));
    add(motMode, new Segmented(p, P_MOT_MODE, col::a)); add(motRate, new Choice(p, P_MOT_DIV, "Rate")); add(motShape, new Choice(p, P_MOT_SHAPE, "Shape")); add(motDepth, new Knob(p, P_MOT_DEPTH, "Depth", col::a));
    motDepth->s.setTooltip("How far Motion pushes Morph (negative pulls it back toward A)");
    add(smooth, new Knob(p, P_SMOOTH, "Smoothness")); add(formant, new Knob(p, P_FORMANT, "Formant")); add(trans, new Knob(p, P_TRANS, "Transient Keep"));
    smooth->s.setTooltip("Low = fast and detailed, high = slurry and blurred"); formant->s.setTooltip("Shifts the voice/tone shape up or down (not in Grain)"); trans->s.setTooltip("Keeps drum hits and attacks from A punchy");
    add(mix, new Knob(p, P_MIX, "Dry/Wet")); add(outGain, new Knob(p, P_OUT, "Output"));
    add(limit, new Toggle(p, P_LIMIT, "Limiter")); limit->setTooltip("Safety limiter: catches peaks above -0.3 dB so nothing clips");
    add(before, new Toggle(p, P_COMPARE, "BEFORE", col::a)); before->setTooltip("Hear only A (your original) for a quick before/after check");

    setResizable(true, true); setResizeLimits(kW * 6 / 10, kH * 6 / 10, kW * 2, kH * 2); getConstrainer()->setFixedAspectRatio((double)kW / kH);
    setSize(kW, kH); timerCallback(); startTimerHz(30);
}
MergeXEditor::~MergeXEditor() { stopTimer(); setLookAndFeel(nullptr); }

void MergeXEditor::card(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, juce::Colour accent) {
    auto f = r.toFloat(); g.setColour(juce::Colours::black.withAlpha(0.35f)); g.fillRoundedRectangle(f.translated(0, 3), 14);
    juce::ColourGradient gr(col::panelHi, 0, f.getY(), col::panel, 0, f.getBottom(), false); g.setGradientFill(gr); g.fillRoundedRectangle(f, 14);
    g.setColour(accent); g.drawRoundedRectangle(f.reduced(0.5f), 14, 1.2f);
    if (title.isNotEmpty()) { g.setColour(col::dim); g.setFont(font(13, true)); g.drawText(title, r.reduced(14, 0).withHeight(28), juce::Justification::centredLeft); }
}
void MergeXEditor::paint(juce::Graphics& g) {
    float s = getWidth() / (float)kW; g.addTransform(juce::AffineTransform::scale(s));
    juce::ColourGradient bg(col::bg2, 0, 0, col::bg, 0, (float)kH, false); g.setGradientFill(bg); g.fillRect(0, 0, kW, kH);
    glow(g, {300, 300}, 380, col::a, 0.06f); glow(g, {760, 300}, 380, col::b, 0.06f);
    // wordmark
    g.setFont(font(30, true)); g.setColour(col::a); g.drawText("Merge", 22, 10, 100, 40, juce::Justification::centredLeft);
    g.setColour(col::b); g.drawText("X", 108, 10, 30, 40, juce::Justification::centredLeft);
    g.setColour(col::dim); g.setFont(font(12)); g.drawText("sound morph", 24, 40, 140, 14, juce::Justification::centredLeft);
    card(g, {16, 68, 220, 400}, "SOURCES");
    card(g, {248, 68, 736, 400}, {}, col::mid.withAlpha(0.6f));
    card(g, {16, 480, 400, 146}, "MOTION");
    card(g, {428, 480, 292, 146}, "SHAPE");
    card(g, {732, 480, 252, 146}, "OUTPUT");
    // source status
    g.setColour(col::dim); g.setFont(font(12)); g.drawFittedText(bStatus, juce::Rectangle<int>(30, 168, 192, 44), juce::Justification::topLeft, 3);
    g.setColour(col::dim); g.drawFittedText("A = this track\nB = sidechain input (or a built-in tone)", juce::Rectangle<int>(30, 418, 192, 40), juce::Justification::topLeft, 3);
    // hero area hints
    g.setColour(col::dim); g.setFont(font(12)); g.drawFittedText(styleHint, juce::Rectangle<int>(262, 138, 708, 26), juce::Justification::centred, 1);
    g.setColour(col::a); g.setFont(font(14, true)); g.drawText("A", 268, 424, 30, 20, juce::Justification::centred);
    g.setColour(col::b); g.drawText("B", 512, 424, 30, 20, juce::Justification::centred);
    // pad side info
    int st = (int)proc.value(P_STYLE);
    g.setColour(col::dim); g.setFont(font(12, true)); g.drawText("PAD UP/DOWN", 862, 190, 116, 16, juce::Justification::centredLeft);
    g.setColour(col::text); g.setFont(font(20, true)); g.drawText(shapeShort(st), 862, 208, 116, 26, juce::Justification::centredLeft);
    g.setColour(col::dim); g.setFont(font(12)); g.drawFittedText(shapeLabel(st), juce::Rectangle<int>(862, 236, 116, 48), juce::Justification::topLeft, 3);
    g.setColour(col::text); g.setFont(font(16, true)); g.drawText(juce::String(juce::roundToInt(proc.value(P_SHAPE))) + "%", 862, 284, 116, 22, juce::Justification::centredLeft);
    g.setColour(col::dim); g.setFont(font(12, true)); g.drawText("PAD LEFT/RIGHT", 862, 330, 116, 16, juce::Justification::centredLeft);
    g.setColour(col::text); g.setFont(font(20, true)); g.drawText("Morph", 862, 348, 116, 26, juce::Justification::centredLeft);
    g.setColour(col::dim); g.setFont(font(12)); g.drawFittedText("A on the left,\nB on the right", juce::Rectangle<int>(862, 376, 116, 32), juce::Justification::topLeft, 2);
    // limiter readout
    if (grDb < -0.1f) { g.setColour(col::b); g.setFont(font(12)); g.drawText("limiting " + juce::String(grDb, 1) + " dB", 856, 596, 120, 18, juce::Justification::centred); }
}
void MergeXEditor::resized() {
    content.setBounds(0, 0, kW, kH); float s = getWidth() / (float)kW; content.setTransform(juce::AffineTransform::scale(s));
    prevBtn.setBounds(330, 16, 34, 34); presetBtn.setBounds(368, 14, 300, 38); nextBtn.setBounds(672, 16, 34, 34); saveBtn.setBounds(712, 18, 60, 30);
    randomBtn.setBounds(786, 18, 104, 30); before->setBounds(898, 16, 86, 34);
    // sources
    levelA->setBounds(30, 100, 192, 22); levelB->setBounds(30, 136, 192, 22);
    bSource->setBounds(30, 214, 192, 50); note->setBounds(30, 278, 92, 116); balance->setBounds(130, 278, 92, 116);
    // hero
    styles->setBounds(258, 78, 716, 56);
    talkDir->setBounds(424, 140, 384, 26); swapDir->setBounds(424, 140, 384, 26);
    morph->setBounds(262, 168, 280, 290);
    pad->setBounds(566, 176, 284, 284);
    // bottom
    motMode->setBounds(30, 510, 372, 28); motRate->setBounds(30, 548, 116, 64); motShape->setBounds(154, 548, 116, 64); motDepth->setBounds(286, 542, 110, 80);
    smooth->setBounds(440, 506, 90, 114); formant->setBounds(530, 506, 90, 114); trans->setBounds(620, 506, 90, 114);
    mix->setBounds(742, 506, 80, 108); outGain->setBounds(822, 506, 80, 108); limit->setBounds(904, 524, 72, 52);
}
void MergeXEditor::timerCallback() {
    presetBtn.setButtonText(proc.presetName() + "  v");
    int st = (int)proc.value(P_STYLE); talkDir->setVisible(st == ST_TALK); swapDir->setVisible(st == ST_SWAP);
    juce::String hint = st == ST_SMOOTH ? "Pad up = B's tone arrives first, pad down = B's texture arrives first"
                      : (st == ST_GRAIN ? "Pad up = bigger grains, Smoothness = more scatter. Formant is not used in Grain" : "");
    formant->setEnabled(st != ST_GRAIN); formant->setAlpha(st == ST_GRAIN ? 0.45f : 1.f);
    bool built = (int)proc.value(P_BSRC) != BS_AUTO; note->setAlpha((built && (int)proc.value(P_BSRC) != BS_NOISE) || (!built && proc.engine().meterBSource.load() == BS_PAD) ? 1.f : 0.45f);
    bool motion = (int)proc.value(P_MOT_MODE) != MM_OFF, lfo = (int)proc.value(P_MOT_MODE) == MM_LFO;
    motRate->setAlpha(lfo ? 1.f : 0.45f); motShape->setAlpha(lfo ? 1.f : 0.45f); motDepth->setAlpha(motion ? 1.f : 0.45f);
    auto& e = proc.engine(); levelA->set(e.meterA.load()); levelB->set(e.meterB.load());
    int bs = e.meterBSource.load(); juce::String s;
    if (bs == 0) s = "B: sidechain input"; else { s = juce::String("B: built-in ") + (bs == BS_NOISE ? "Noise" : (bs == BS_SAW ? "Saw" : "Pad")); if ((int)proc.value(P_BSRC) == BS_AUTO) s << (proc.sidechainConnected() ? " (sidechain is silent)" : " (no sidechain)"); }
    float gr = 20.f * std::log10(std::max(e.meterGR.load(), 1e-4f));
    juce::String key = s + "|" + hint + "|" + juce::String(gr, 1) + "|" + juce::String(st) + "|" + juce::String(juce::roundToInt(proc.value(P_SHAPE)));
    if (key != lastKey) { bStatus = s; styleHint = hint; grDb = gr; lastKey = key; repaint(); }
    pad->repaint();
}
void MergeXEditor::stepPreset(int d) { int n = (int)factoryPresets().size(); proc.loadFactory((proc.getCurrentProgram() + d + n) % n); }
void MergeXEditor::showPresetMenu() {
    juce::PopupMenu menu; const auto& ps = factoryPresets(); juce::StringArray cats; for (auto& p : ps) cats.addIfNotAlreadyThere(p.category);
    for (auto& c : cats) { juce::PopupMenu sm; for (size_t i = 0; i < ps.size(); ++i) if (c == ps[i].category) sm.addItem(ps[i].name, true, (int)i == proc.getCurrentProgram() && proc.presetName() == ps[i].name, [this, i] { proc.loadFactory((int)i); }); menu.addSubMenu(c, sm); }
    auto user = proc.userPresets(); juce::PopupMenu um; for (auto& f : user) um.addItem(f.getFileNameWithoutExtension(), [this, f] { proc.loadUserPreset(f); });
    if (user.isEmpty()) um.addItem("(none saved yet)", false, false, [] {});
    menu.addSeparator(); menu.addSubMenu("My presets", um); menu.addItem("Save current as...", [this] { savePreset(); });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetBtn).withMinimumWidth(presetBtn.getWidth()));
}
void MergeXEditor::savePreset() {
    auto* w = new juce::AlertWindow("Save preset", "Name your preset. It goes in Music/MergeX Presets.", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor("name", proc.presetName()); w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<MergeXEditor> safe(this);
    w->enterModalState(true, juce::ModalCallbackFunction::create([safe, w](int r) { if (r == 1 && safe) safe->proc.saveUserPreset(w->getTextEditorContents("name")); }), true);
}
