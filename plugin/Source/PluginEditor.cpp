#include "PluginEditor.h"

using namespace tb303;

namespace ui {

LookAndFeel::LookAndFeel() {
    setColour(juce::PopupMenu::backgroundColourId, panelHead);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, amber);
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour(juce::PopupMenu::headerTextColourId, dim);
    setColour(juce::Label::textColourId, text);
    setColour(juce::TextButton::textColourOffId, text);
    setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    setColour(juce::AlertWindow::backgroundColourId, panel);
    setColour(juce::AlertWindow::textColourId, text);
    setColour(juce::TextEditor::backgroundColourId, cellAlt);
    setColour(juce::TextEditor::textColourId, text);
    setColour(juce::TextEditor::outlineColourId, edge);
    setColour(juce::ResizableWindow::backgroundColourId, bg);
}

void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) {
    const float r = juce::jmin(w, h) * 0.5f - 3.0f, cx = x + w * 0.5f, cy = y + h * 0.5f;
    const float ang = a0 + pos * (a1 - a0), th = juce::jmax(2.0f, r * 0.16f);
    juce::Path track; track.addCentredArc(cx, cy, r, r, 0, a0, a1, true);
    g.setColour(juce::Colour(0xff444444)); g.strokePath(track, juce::PathStrokeType(th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path val; val.addCentredArc(cx, cy, r, r, 0, a0, ang, true);
    g.setColour(s.isEnabled() ? cyan : dim); g.strokePath(val, juce::PathStrokeType(th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(juce::Colour(0xff1b1b1b)); g.fillEllipse(cx - r * 0.72f, cy - r * 0.72f, r * 1.44f, r * 1.44f);
    juce::Path ptr; ptr.addRoundedRectangle(-th * 0.45f, -r * 0.72f, th * 0.9f, r * 0.55f, th * 0.4f);
    g.setColour(juce::Colour(0xffe8e8e8)); g.fillPath(ptr, juce::AffineTransform::rotation(ang).translated(cx, cy));
}

void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    const bool on = b.getToggleState();
    auto c = on ? amber : juce::Colour(0xff3c3c3c);
    if (over) c = c.brighter(0.12f);
    if (down) c = c.darker(0.15f);
    g.setColour(c); g.fillRoundedRectangle(r, 3.0f);
    g.setColour(edge); g.drawRoundedRectangle(r, 3.0f, 1.0f);
}

void LookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    g.setFont(getTextButtonFont(b, b.getHeight()));
    g.setColour(b.getToggleState() ? juce::Colours::black : (b.isEnabled() ? text : dim));
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(4, 0), juce::Justification::centred, 1);
}

Knob::Knob(juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& n, bool sm)
    : name(n), param(s.getParameter(paramId)), small(sm) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity(220);
    slider.setVelocityModeParameters(1.0, 1, 0.0, false, juce::ModifierKeys::shiftModifier); // Shift for fine
    addAndMakeVisible(slider);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(s, paramId, slider);
    slider.setDoubleClickReturnValue(true, param->convertFrom0to1(param->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };
    slider.setPopupDisplayEnabled(false, false, nullptr);
}
void Knob::resized() {
    auto r = getLocalBounds();
    r.removeFromTop(small ? 14 : 16); r.removeFromBottom(small ? 14 : 16);
    slider.setBounds(r.withSizeKeepingCentre(juce::jmin(r.getWidth(), r.getHeight()), juce::jmin(r.getWidth(), r.getHeight())));
}
void Knob::paint(juce::Graphics& g) {
    auto r = getLocalBounds();
    g.setColour(text); g.setFont(juce::FontOptions(small ? 11.0f : 12.5f, juce::Font::bold));
    g.drawFittedText(name, r.removeFromTop(small ? 14 : 16), juce::Justification::centred, 1);
    g.setColour(cyan); g.setFont(juce::FontOptions(small ? 10.5f : 11.5f));
    g.drawFittedText(valueText ? valueText() : param->getCurrentValueAsText(), r.removeFromBottom(small ? 14 : 16), juce::Justification::centred, 1);
}
void Knob::refresh() { repaint(); }

Toggle::Toggle(juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& label) : juce::TextButton(label) {
    setClickingTogglesState(true);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, paramId, *this);
}

void Section::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    g.setColour(panel); g.fillRoundedRectangle(r, 4);
    g.setColour(panelHead); g.fillRoundedRectangle(r.removeFromTop(24), 4);
    g.setColour(edge); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4, 1);
    g.setColour(amber); g.fillEllipse(9, 8, 8, 8);
    g.setColour(text); g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawText(title, 24, 0, getWidth() - 100, 24, juce::Justification::centredLeft);
}
void Section::resized() {
    if (headerButton) headerButton->setBounds(getWidth() - 56, 3, 50, 18);
    auto r = getLocalBounds().reduced(6, 0); r.removeFromTop(28); r.removeFromBottom(4);
    for (auto& it : items) it.c->setBounds(r.removeFromLeft(it.w).reduced(2, 0));
}

// ---------- sequencer grid ----------
static const int OCT_ROWS[4] = { 2, 1, -1, -2 };
static const char* const NOTE[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static int splitO(int c) { return (int)std::floor((c - 24) / 12.0); }
static int splitK(int c) { return c - 24 - 12 * splitO(c); }
static int clampCode(int c) { return juce::jlimit(0, 59, c); }
static bool isBlack(int k) { return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
// rows: 0 header, 1-4 octave lanes, 5-16 keys B..C, 17 slide, 18 accent
int SeqGrid::rowAt(float y) const { return juce::jlimit(0, 18, (int)(y / rowH)); }
int SeqGrid::colAt(float x) const { return x < headW ? -1 : juce::jlimit(0, SHOW - 1, (int)((x - headW) / colW)); }

void SeqGrid::paint(juce::Graphics& g) {
    colW = (getWidth() - headW) / SHOW; rowH = getHeight() / 19.0f;
    const int n = (int)pattern.size();
    g.fillAll(edge);
    g.setFont(juce::FontOptions(10.5f));
    for (int row = 0; row < 19; ++row) {
        const float y = row * rowH;
        juce::String lab; bool blk = false;
        if (row == 0) lab = "STEP";
        else if (row <= 4) lab = (OCT_ROWS[row - 1] > 0 ? "+" : "-") + juce::String(std::abs(OCT_ROWS[row - 1]));
        else if (row <= 16) { const int k = 16 - row; lab = NOTE[k]; blk = isBlack(k); }
        else lab = row == 17 ? "SLIDE" : "ACC";
        g.setColour(blk ? juce::Colour(0xff1c1c1c) : juce::Colour(0xff303030));
        g.fillRect(0.0f, y + 0.5f, headW - 1, rowH - 1);
        g.setColour(dim); g.drawText(lab, juce::Rectangle<float>(3, y, headW - 4, rowH), juce::Justification::centredLeft);
        for (int c = 0; c < SHOW; ++c) {
            const float x = headW + c * colW;
            auto cellR = juce::Rectangle<float>(x + 0.5f, y + 0.5f, colW - 1, rowH - 1);
            if (row == 0) {
                g.setColour(c == playing ? amber.withAlpha(0.35f) : juce::Colour(0xff262626)); g.fillRect(cellR);
                if (c % 8 == 0) { g.setColour(c < n ? dim : dim.withAlpha(0.4f)); g.drawText(juce::String(c + 1), cellR.reduced(2, 0), juce::Justification::centredLeft); }
                continue;
            }
            const bool ghost = c >= n;
            g.setColour(ghost ? juce::Colour(0xff1a1a1a) : (blk ? cellAlt : cell)); g.fillRect(cellR);
            if (c == playing && !ghost) { g.setColour(juce::Colours::white.withAlpha(0.06f)); g.fillRect(cellR); }
            if (ghost) continue;
            const Step& s = pattern[(size_t)c];
            bool on = false; juce::Colour col = amber;
            if (row <= 4) on = s.gate && splitO(s.code) == OCT_ROWS[row - 1];
            else if (row <= 16) on = s.gate && splitK(s.code) == 16 - row;
            else { on = row == 17 ? s.slide : s.accent; col = cyan; }
            if (on) { g.setColour(col); g.fillRect(cellR.reduced(1, 1.5f)); }
            else if (row > 4 && row <= 16 && !s.gate && splitK(s.code) == 16 - row) { g.setColour(amber.withAlpha(0.25f)); g.drawRect(cellR.reduced(1, 1.5f), 1.0f); }
        }
    }
    // bar lines and section gaps
    g.setColour(juce::Colour(0xff555555));
    for (int c = 4; c < SHOW; c += 4) g.fillRect(headW + c * colW - 0.5f, 0.0f, 1.0f, (float)getHeight());
    for (int row : { 1, 5, 17 }) g.fillRect(0.0f, row * rowH - 0.5f, (float)getWidth(), 1.0f);
}

void SeqGrid::apply(int col, int row, bool first) {
    if (col < 0 || row < 1) return;
    auto& P = pattern;
    if (col >= (int)P.size()) {
        if (!first) return;
        while ((int)P.size() <= col) P.push_back({ 24, false, false, false });
    }
    Step& s = P[(size_t)col];
    const int o = splitO(s.code), k = splitK(s.code);
    if (first) {
        if (row >= 5 && row <= 16) {
            const int v = 16 - row;
            if (k == v && s.gate) s.gate = false; else { s.code = clampCode(24 + v + 12 * o); s.gate = true; }
            paintRow = 5; paintOn = s.gate;
        } else if (row <= 4) {
            const int v = OCT_ROWS[row - 1];
            s.code = clampCode(24 + k + 12 * (o == v ? 0 : v));
            paintRow = 1; paintOct = splitO(s.code);
        } else {
            bool& b = row == 17 ? s.slide : s.accent; b = !b;
            paintRow = row; paintOn = b;
        }
    } else {
        if (paintRow == 5) {
            if (paintOn) { if (row >= 5 && row <= 16) { s.code = clampCode(24 + (16 - row) + 12 * o); s.gate = true; } else if (row > 16) s.gate = true; }
            else s.gate = false;
        } else if (paintRow == 1) { if (row <= 4) s.code = clampCode(24 + k + 12 * paintOct); }
        else { (paintRow == 17 ? s.slide : s.accent) = paintOn; }
    }
    if (onEdit) onEdit();
    repaint();
}

void SeqGrid::mouseDown(const juce::MouseEvent& e) {
    const int c = colAt((float)e.x), r = rowAt((float)e.y);
    painting = true; lastCol = c;
    apply(c, r, true);
}
void SeqGrid::mouseDrag(const juce::MouseEvent& e) {
    if (!painting) return;
    const int c = colAt((float)e.x), r = rowAt((float)e.y);
    if (c == lastCol && paintRow != 5) return;
    lastCol = c;
    apply(c, r, false);
}
} // namespace ui

// ---------------------------------------------------------------------------------------------------
// The web app's former factory sounds and patterns (SOUND_BUILTIN / PATTERN_BUILTIN in state.js)
static const char* BUILTIN_SOUNDS[][2] = {
    { "Stock 303", R"({"knobs":{},"parts":{}})" },
    { "Devil Fish style", R"({"knobs":{"resonance":0.9,"envmod":0.7,"decay":0.2,"accent":0.9},"parts":{"A_Q21":21,"R138":22000,"R52":680000,"R_SOFT":1000,"C_ACC":4.7e-7,"R_IN":47000,"R72":120000,"V_CUT":4.5}})" },
    { "Acid in the hall", R"({"knobs":{"cutoff":0.28,"resonance":0.85,"envmod":0.65,"decay":0.25,"lexprog":0.25,"lexmid":0.36,"lexbass":0.4,"lexpre":0.3,"lexmix":0.45},"fx":{"rat":true,"lex":true},"parts":{}})" },
    { "Long drone", R"({"knobs":{"cutoff":0.25,"resonance":0.75,"envmod":0.4,"decay":0.9},"wave":"square","parts":{"C35":1e-6,"R52":4.7e6,"R_SOFT":22000}})" },
};
static juce::var builtinPattern(int i) {
    static const int rows[3][16][4] = {
        { { 21, 1, 0, 1 }, { 21, 0, 0, 1 }, { 33, 0, 1, 1 }, { 31, 0, 0, 1 }, { 21, 1, 0, 1 }, { 21, 0, 0, 0 }, { 24, 0, 1, 1 }, { 26, 0, 0, 1 },
          { 21, 0, 0, 1 }, { 33, 1, 0, 1 }, { 28, 0, 1, 1 }, { 24, 0, 0, 1 }, { 21, 1, 1, 1 }, { 19, 0, 0, 1 }, { 21, 0, 0, 0 }, { 36, 1, 0, 1 } },
        { { 21, 1, 0, 1 }, { 33, 0, 0, 1 }, { 21, 0, 0, 1 }, { 33, 1, 0, 1 }, { 21, 0, 0, 1 }, { 33, 0, 1, 1 }, { 31, 0, 0, 1 }, { 21, 0, 0, 1 },
          { 24, 1, 0, 1 }, { 36, 0, 0, 1 }, { 24, 0, 0, 1 }, { 36, 1, 0, 1 }, { 19, 0, 0, 1 }, { 31, 0, 1, 1 }, { 33, 0, 0, 1 }, { 21, 1, 0, 1 } },
        { { 21, 1, 1, 1 }, { 28, 0, 1, 1 }, { 33, 0, 0, 1 }, { 21, 0, 0, 0 }, { 21, 0, 1, 1 }, { 24, 1, 1, 1 }, { 26, 0, 0, 1 }, { 21, 0, 0, 0 },
          { 21, 1, 0, 1 }, { 21, 0, 0, 0 }, { 33, 0, 1, 1 }, { 31, 1, 1, 1 }, { 28, 0, 0, 1 }, { 21, 0, 0, 0 }, { 16, 0, 1, 1 }, { 21, 1, 0, 1 } },
    };
    juce::Array<juce::var> steps;
    for (auto& r : rows[i]) {
        auto* o = new juce::DynamicObject();
        o->setProperty("code", r[0]); o->setProperty("accent", r[1] != 0); o->setProperty("slide", r[2] != 0); o->setProperty("gate", r[3] != 0);
        steps.add(juce::var(o));
    }
    auto* x = new juce::DynamicObject(); x->setProperty("pattern", steps);
    return juce::var(x);
}
static juce::String knobName(const char* id) { return tb303::knobs()[(size_t)tb303::knobIndex(id)].name; }
static const char* PATTERN_NAMES[3] = { "Example line", "Octave bounce", "Slow squelch" };

TB303Editor::TB303Editor(TB303Processor& p) : juce::AudioProcessorEditor(p), proc(p), grid(p) {
    setLookAndFeel(&lnf);
    auto& s = proc.apvts;

    // top bar
    playBtn = std::make_unique<ui::Toggle>(s, "run", "Play");
    playBtn->setTooltip("Runs the sequencer on its own clock while the host is stopped");
    seqBtn = std::make_unique<ui::Toggle>(s, "seq", "Sequencer");
    declickBtn = std::make_unique<ui::Toggle>(s, "declick", "De-click");
    tempoKnob = std::make_unique<ui::Knob>(s, "tempo", "Tempo", true);
    for (auto* c : std::initializer_list<juce::Component*> { playBtn.get(), seqBtn.get(), declickBtn.get(), tempoKnob.get(), &presetBtn, &importBtn, &exportBtn }) addAndMakeVisible(c);
    presetBtn.setButtonText(proc.lastPresetName.isEmpty() ? "Presets" : proc.lastPresetName);
    presetBtn.onClick = [this] { showPresetMenu(); };
    importBtn.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Import a preset, project or favourites file from the web app", juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Downloads"), "*.json");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            const auto f = fc.getResult(); if (f == juce::File()) return;
            say(proc.importFile(f));
            if (proc.lastPresetName.isNotEmpty()) presetBtn.setButtonText(proc.lastPresetName);
        });
    };
    exportBtn.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Export as a web app project", juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Downloads/tb303-plugin.json"), "*.json");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
            auto f = fc.getResult(); if (f == juce::File()) return;
            proc.exportFile(f.withFileExtension("json"));
            say("Exported " + f.getFileName() + ". Import it in the web app's Preset menu.");
        });
    };

    // voice panel
    waveBtn = std::make_unique<juce::TextButton>("Saw");
    waveBtn->setClickingTogglesState(true);
    waveAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, "wave", *waveBtn);
    waveBtn->onStateChange = [this] { waveBtn->setButtonText(waveBtn->getToggleState() ? "Square" : "Saw"); };
    waveBtn->setButtonText(waveBtn->getToggleState() ? "Square" : "Saw");
    struct WaveBox : juce::Component {
        juce::TextButton* b = nullptr;
        void resized() override { b->setBounds(getLocalBounds().withSizeKeepingCentre(getWidth() - 6, 26)); }
        void paint(juce::Graphics& g) override { g.setColour(ui::text); g.setFont(juce::FontOptions(12.5f, juce::Font::bold)); g.drawText("Waveform", 0, 0, getWidth(), 16, juce::Justification::centred); }
    };
    auto wb = std::make_unique<WaveBox>(); wb->b = waveBtn.get(); wb->addAndMakeVisible(*waveBtn);
    voice.add(wb.get(), 84); owned.push_back(std::move(wb));
    for (auto* id : { "tuning", "cutoff", "resonance", "envmod", "decay", "accent", "volume" }) {
        knobs.push_back(std::make_unique<ui::Knob>(s, id, knobName(id)));
    }
    // readouts in circuit terms, as on the web page
    auto kv = [&s](const char* id) { return s.getRawParameterValue(id)->load(); };
    knobs[0]->valueText = [kv] { const double st = (kv("tuning") - 0.5) * 24; return (st >= 0 ? "+" : "") + juce::String(st, 1) + " st"; };
    knobs[1]->valueText = [this, kv] {
        const double g73 = 1 / proc.partValue(P::R73), g72 = 1 / proc.partValue(P::R72), g63 = 1 / proc.partValue(P::R63), g71 = 1 / proc.partValue(P::R71);
        const double v = g73 / (g73 + g72 + g63 + g71) * proc.partValue(P::V_CUT) * kv("cutoff");
        return juce::String(v * 1000, 1) + " mV"; };
    knobs[2]->valueText = [this, kv] { return "k = " + juce::String(proc.partValue(P::A_Q21) * kv("resonance"), 1); };
    knobs[4]->valueText = [this, kv] { return TB303Processor::formatPart(proc.partValue(P::R138) + proc.partValue(P::VR6) * audioTaper(kv("decay")), juce::String::fromUTF8("Ω")); };
    for (auto& k : knobs) voice.add(k.get(), 86);
    {
        auto trim = std::make_unique<ui::Knob>(s, "m_I_REF", "Filter trim", true);
        voice.add(trim.get(), 86); knobs.push_back(std::move(trim));
    }
    addAndMakeVisible(voice);

    // tabs
    const char* tabNames[3] = { "Sequencer", "Mods", "FX" };
    for (int i = 0; i < 3; ++i) {
        tabBtn[i].setButtonText(tabNames[i]);
        tabBtn[i].onClick = [this, i] { showTab(i); };
        addAndMakeVisible(tabBtn[i]);
    }

    // sequencer page
    grid.pattern = proc.getPattern();
    grid.onEdit = [this] { proc.setPattern(grid.pattern); lenLabel.setText(juce::String((int)grid.pattern.size()) + " steps", juce::dontSendNotification); };
    gateKnob = std::make_unique<ui::Knob>(s, "gate", "Step len", true);
    lenLabel.setJustificationType(juce::Justification::centred);
    lenLabel.setColour(juce::Label::textColourId, ui::cyan);
    lenLabel.setText(juce::String((int)grid.pattern.size()) + " steps", juce::dontSendNotification);
    syncLabel.setFont(juce::FontOptions(11.5f)); syncLabel.setColour(juce::Label::textColourId, ui::dim);
    syncLabel.setJustificationType(juce::Justification::topLeft);
    lenDown.onClick = [this] { setLength((int)grid.pattern.size() - 1); };
    lenUp.onClick = [this] { setLength((int)grid.pattern.size() + 1); };
    shiftL.onClick = [this] { auto& P = grid.pattern; if (P.size() > 1) std::rotate(P.begin(), P.begin() + 1, P.end()); grid.onEdit(); grid.repaint(); };
    shiftR.onClick = [this] { auto& P = grid.pattern; if (P.size() > 1) std::rotate(P.rbegin(), P.rbegin() + 1, P.rend()); grid.onEdit(); grid.repaint(); };
    auto transpose = [this](int d) {
        for (auto& st : grid.pattern) if (st.code + d < 0 || st.code + d > 59) return;
        for (auto& st : grid.pattern) st.code += d;
        grid.onEdit(); grid.repaint();
    };
    trDown.onClick = [transpose] { transpose(-1); };
    trUp.onClick = [transpose] { transpose(1); };
    clearBtn.onClick = [this] { for (auto& st : grid.pattern) st = { 24, false, false, false }; grid.onEdit(); grid.repaint(); };
    patBtn.onClick = [this] {
        juce::PopupMenu m;
        for (int i = 0; i < 3; ++i) m.addItem(PATTERN_NAMES[i], [this, i] { proc.applyPatternJson(builtinPattern(i)); grid.pattern = proc.getPattern(); grid.onEdit(); grid.repaint(); });
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&patBtn));
    };
    for (auto* l : { &shiftName, &trName }) { l->setFont(juce::FontOptions(11.5f)); l->setColour(juce::Label::textColourId, ui::dim); l->setJustificationType(juce::Justification::centred); }
    shiftName.setText("Shift", juce::dontSendNotification); trName.setText("Transpose", juce::dontSendNotification);
    for (auto* c : std::initializer_list<juce::Component*> { &shiftName, &trName, &grid, &lenDown, &lenUp, &lenLabel, &shiftL, &shiftR, &trDown, &trUp, &clearBtn, &patBtn, gateKnob.get(), &syncLabel })
        seqPage.addAndMakeVisible(c);
    addChildComponent(seqPage);

    // mods page
    for (auto& m : frontMods()) {
        if (juce::String(m.part) == "I_REF") continue;
        auto k = std::make_unique<ui::Knob>(s, juce::String("m_") + m.part, m.name);
        mods.add(k.get(), 79); owned.push_back(std::move(k));
    }
    modsPage.addAndMakeVisible(mods);
    addChildComponent(modsPage);

    // fx page
    auto addKnobs = [&](ui::Section& sec, std::initializer_list<const char*> ids, int w = 70) {
        for (auto* id : ids) { auto k = std::make_unique<ui::Knob>(s, id, knobName(id), true); sec.add(k.get(), w); owned.push_back(std::move(k)); }
    };
    auto onBtn = [&](ui::Section& sec, const char* id) { auto b = std::make_unique<ui::Toggle>(s, id, "On"); sec.setHeaderButton(b.get()); owned.push_back(std::move(b)); };
    onBtn(rat, "fx_rat"); addKnobs(rat, { "dist", "filter", "ratvol" });
    onBtn(dly, "fx_dly"); addKnobs(dly, { "dlytime", "dlyfb", "dlylo", "dlyhi", "dlymix" });
    { auto b = std::make_unique<ui::Toggle>(s, "dlypp", "Ping-pong"); auto box = std::make_unique<juce::Component>();
      struct Box : juce::Component { juce::Component* c = nullptr; void resized() override { c->setBounds(getLocalBounds().withSizeKeepingCentre(getWidth() - 4, 24)); } };
      auto bx = std::make_unique<Box>(); bx->c = b.get(); bx->addAndMakeVisible(*b); dly.add(bx.get(), 80); owned.push_back(std::move(b)); owned.push_back(std::move(bx)); }
    addKnobs(cmp, { "cmpthr", "cmprng", "cmpatk", "cmphold", "cmprel", "cmpmix", "cmpmode" });
    onBtn(lex, "fx_lex"); addKnobs(lex, { "lexprog", "lexbass", "lexmid", "lexxover", "lextreble", "lexdepth", "lexdiff", "lexpre", "lexme", "lexpitch", "lexmix" }, 80);
    for (auto* c : { &rat, &dly, &cmp, &lex }) fxPage.addAndMakeVisible(c);
    addChildComponent(fxPage);

    setSize(1000, 600);
    showTab(0);
    startTimerHz(30);
}

TB303Editor::~TB303Editor() { stopTimer(); setLookAndFeel(nullptr); }

void TB303Editor::setLength(int n) {
    n = juce::jlimit(1, 32, n);
    auto& P = grid.pattern;
    while ((int)P.size() < n) P.push_back({ 24, false, false, false });
    P.resize((size_t)n);
    grid.onEdit(); grid.repaint();
}

void TB303Editor::showTab(int t) {
    tab = t;
    for (int i = 0; i < 3; ++i) tabBtn[i].setToggleState(i == t, juce::dontSendNotification);
    seqPage.setVisible(t == 0); modsPage.setVisible(t == 1); fxPage.setVisible(t == 2);
}

void TB303Editor::showPresetMenu() {
    juce::PopupMenu m, stock, pats, mine, del;
    for (int i = 0; i < 4; ++i) {
        const juce::String name = BUILTIN_SOUNDS[i][0];
        stock.addItem(name, [this, i, name] { proc.applySound(juce::JSON::parse(BUILTIN_SOUNDS[i][1])); proc.lastPresetName = name; presetBtn.setButtonText(name); });
    }
    for (int i = 0; i < 3; ++i) pats.addItem(PATTERN_NAMES[i], [this, i] { proc.applyPatternJson(builtinPattern(i)); grid.pattern = proc.getPattern(); grid.repaint(); });
    for (auto& n : proc.userPresetNames()) {
        mine.addItem(n, [this, n] { proc.applyPreset(proc.userPreset(n)); proc.lastPresetName = n; presetBtn.setButtonText(n); grid.pattern = proc.getPattern(); grid.repaint(); });
        del.addItem(n, [this, n] { proc.removeUserPreset(n); say("Deleted " + n + "."); });
    }
    m.addSectionHeader("My presets");
    if (mine.getNumItems()) for (juce::PopupMenu::MenuItemIterator it(mine); it.next();) m.addItem(it.getItem());
    else m.addItem("Import your web app favourites to fill this list", false, false, [] {});
    m.addSeparator();
    m.addSubMenu("Stock sounds", stock);
    m.addSubMenu("Patterns", pats);
    m.addSeparator();
    m.addItem("Save current as preset...", [this] {
        auto* w = new juce::AlertWindow("Save preset", "Name for this sound and pattern:", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor("name", proc.lastPresetName, "Name");
        w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
            const auto n = w->getTextEditorContents("name").trim().substring(0, 40);
            if (r == 1 && n.isNotEmpty()) { proc.addUserPreset(n, proc.projectSnapshot()); proc.lastPresetName = n; presetBtn.setButtonText(n); say("Saved " + n + "."); }
        }), true);
    });
    m.addSubMenu("Delete preset", del, del.getNumItems() > 0);
    m.addItem("Import web app file...", [this] { importBtn.triggerClick(); });
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetBtn));
}

void TB303Editor::timerCallback() {
    // pick up pattern changes made elsewhere (preset loads, host state restore)
    const auto p = proc.getPattern();
    bool same = p.size() == grid.pattern.size();
    for (size_t i = 0; same && i < p.size(); ++i)
        same = p[i].code == grid.pattern[i].code && p[i].gate == grid.pattern[i].gate && p[i].accent == grid.pattern[i].accent && p[i].slide == grid.pattern[i].slide;
    if (!same) { grid.pattern = p; lenLabel.setText(juce::String((int)p.size()) + " steps", juce::dontSendNotification); grid.repaint(); }
    const int st = proc.currentStep.load();
    if (st != lastStep) { lastStep = st; grid.playing = st; grid.repaint(); }
    const bool seqOn = proc.apvts.getRawParameterValue("seq")->load() > 0.5f;
    syncLabel.setText(!seqOn ? "Sequencer off: MIDI notes play the voice."
        : proc.seqRunning ? "Playing. Follows the host's transport; Play runs it while the host is stopped."
                          : "Starts with the host's transport, or press Play. MIDI notes play the voice too (velocity 100+ = accent, legato = slide).",
        juce::dontSendNotification);
    if (status.isNotEmpty() && juce::Time::getMillisecondCounter() > statusUntil) { status = {}; repaint(); }
    for (auto& k : knobs) k->refresh();
}

void TB303Editor::paint(juce::Graphics& g) {
    g.fillAll(ui::bg);
    g.setColour(juce::Colour(0xff262626)); g.fillRect(0, 0, getWidth(), 44);
    g.setColour(ui::edge); g.drawHorizontalLine(44, 0, (float)getWidth());
    g.setColour(ui::text); g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    g.drawText("TB303 Circuit Model", 12, 0, 200, 44, juce::Justification::centredLeft);
    g.setColour(ui::dim); g.setFont(juce::FontOptions(12.0f));
    g.drawText(status.isNotEmpty() ? status : juce::String("Component-level model of the 1982 main board, same engine as the web app"),
               12, getHeight() - 22, getWidth() - 24, 20, juce::Justification::centredLeft);
}

void TB303Editor::resized() {
    auto r = getLocalBounds();
    auto top = r.removeFromTop(44).reduced(8, 7);
    top.removeFromLeft(190);
    playBtn->setBounds(top.removeFromLeft(56)); top.removeFromLeft(6);
    tempoKnob->setBounds(top.removeFromLeft(70).withY(0).withHeight(44)); top.removeFromLeft(6);
    presetBtn.setBounds(top.removeFromLeft(230)); top.removeFromLeft(6);
    importBtn.setBounds(top.removeFromLeft(66)); top.removeFromLeft(4);
    exportBtn.setBounds(top.removeFromLeft(66));
    declickBtn->setBounds(top.removeFromRight(76)); top.removeFromRight(6);
    seqBtn->setBounds(top.removeFromRight(90));

    r.removeFromBottom(24);
    r = r.reduced(10, 8);
    voice.setBounds(r.removeFromTop(124));
    r.removeFromTop(8);
    auto tabs = r.removeFromTop(26);
    for (auto& b : tabBtn) { b.setBounds(tabs.removeFromLeft(96)); tabs.removeFromLeft(4); }
    r.removeFromTop(6);
    seqPage.setBounds(r); modsPage.setBounds(r); fxPage.setBounds(r);

    // sequencer page
    {
        auto pg = seqPage.getLocalBounds();
        auto side = pg.removeFromLeft(170);
        pg.removeFromLeft(10);
        grid.setBounds(pg);
        auto row = [&](juce::Component& a, juce::Component* mid, juce::Component& b) {
            auto rr = side.removeFromTop(26); a.setBounds(rr.removeFromLeft(30)); b.setBounds(rr.removeFromRight(30));
            if (mid) mid->setBounds(rr.reduced(4, 0)); side.removeFromTop(6);
        };
        row(lenDown, &lenLabel, lenUp);
        row(shiftL, &shiftName, shiftR);
        row(trDown, &trName, trUp);
        auto rr = side.removeFromTop(26); clearBtn.setBounds(rr.removeFromLeft(80)); rr.removeFromLeft(10); patBtn.setBounds(rr); side.removeFromTop(6);
        gateKnob->setBounds(side.removeFromTop(78).withSizeKeepingCentre(80, 78));
        syncLabel.setBounds(side.reduced(0, 4));
    }
    mods.setBounds(modsPage.getLocalBounds().removeFromTop(124));
    {
        auto pg = fxPage.getLocalBounds();
        auto r1 = pg.removeFromTop(104);
        rat.setBounds(r1.removeFromLeft(230)); r1.removeFromLeft(8); dly.setBounds(r1.removeFromLeft(450));
        pg.removeFromTop(8);
        cmp.setBounds(pg.removeFromTop(104).removeFromLeft(510));
        pg.removeFromTop(8);
        lex.setBounds(pg.removeFromTop(104).removeFromLeft(900));
    }
}
