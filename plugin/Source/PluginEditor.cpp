#include "PluginEditor.h"
#include "PartsMeta.h"

using namespace tb303;

static juce::String U(const char* s) { return juce::String::fromUTF8(s); }
static bool partChanged(int k, double v) { const double d = parts()[k].v; return std::fabs(v - d) > (d != 0 ? 1e-6 * std::fabs(d) : 1e-9); }
static juce::String fmtPart(int k, double v) { return TB303Processor::formatPart(v, U(parts()[k].unit)); }
static double sig3(double v) { return v == 0 ? 0 : juce::String(v, 3, true).getDoubleValue(); } // toPrecision(3)
static double tToPart(int k, double t) { return sig3(partFromT(k, juce::jlimit(0.0, 1.0, t))); }

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
    setColour(juce::TextEditor::focusedOutlineColourId, amber);
    setColour(juce::ResizableWindow::backgroundColourId, bg);
    setColour(juce::ScrollBar::thumbColourId, juce::Colour(0xff4a4a4a));
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
    if (!b.isEnabled()) c = juce::Colour(0xff303030);
    else { if (over) c = c.brighter(0.12f); if (down) c = c.darker(0.15f); }
    g.setColour(c); g.fillRoundedRectangle(r, 3.0f);
    g.setColour(edge); g.drawRoundedRectangle(r, 3.0f, 1.0f);
}

void LookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    g.setFont(getTextButtonFont(b, b.getHeight()));
    g.setColour(b.getToggleState() ? juce::Colours::black : (b.isEnabled() ? text : dim.withAlpha(0.6f)));
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(4, 0), juce::Justification::centred, 1);
}

Dial::Dial() {
    setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    setMouseDragSensitivity(220);
    setPopupDisplayEnabled(false, false, nullptr);
}

Knob::Knob(juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& n, bool sm)
    : name(n), param(s.getParameter(paramId)), small(sm) {
    addAndMakeVisible(slider);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(s, paramId, slider);
    slider.setDoubleClickReturnValue(true, param->convertFrom0to1(param->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };
}
void Knob::resized() {
    auto r = getLocalBounds();
    r.removeFromTop((small ? 14 : 16) + (desc.isNotEmpty() ? 13 : 0)); r.removeFromBottom(small ? 14 : 16);
    slider.setBounds(r.withSizeKeepingCentre(juce::jmin(r.getWidth(), r.getHeight()), juce::jmin(r.getWidth(), r.getHeight())));
}
void Knob::paint(juce::Graphics& g) {
    auto r = getLocalBounds();
    g.setColour(text); g.setFont(juce::FontOptions(small ? 11.0f : 12.5f, juce::Font::bold));
    g.drawFittedText(name, r.removeFromTop(small ? 14 : 16), juce::Justification::centred, 1);
    if (desc.isNotEmpty()) { g.setColour(dim); g.setFont(juce::FontOptions(10.0f)); g.drawFittedText(desc, r.removeFromTop(13), juce::Justification::centred, 1); }
    g.setColour(changed && !changed() ? dim : cyan); g.setFont(juce::FontOptions(small ? 10.5f : 11.5f));
    g.drawFittedText(valueText ? valueText() : param->getCurrentValueAsText(), r.removeFromBottom(small ? 14 : 16), juce::Justification::centred, 1);
}

Toggle::Toggle(juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& label) : juce::TextButton(label) {
    setClickingTogglesState(true);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, paramId, *this);
}

void Section::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    g.setColour(panel); g.fillRoundedRectangle(r, 4);
    g.setColour(panelHead); g.fillRoundedRectangle(r.removeFromTop(24), 4);
    g.setColour(edge); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4, 1);
    g.setColour(lit ? amber : juce::Colour(0xff555555)); g.fillEllipse(9, 8, 8, 8);
    g.setColour(text); g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    const int tw = juce::GlyphArrangement::getStringWidthInt(juce::FontOptions(13.0f, juce::Font::bold), title);
    g.drawText(title, 24, 0, tw + 4, 24, juce::Justification::centredLeft);
    if (sub.isNotEmpty()) { g.setColour(dim); g.setFont(juce::FontOptions(11.5f)); g.drawText(sub, 24 + tw + 12, 0, 320, 24, juce::Justification::centredLeft); }
}
void Section::resized() {
    auto head = getLocalBounds().removeFromTop(24).reduced(4, 3);
    if (headerButton) headerButton->setBounds(head.removeFromRight(50));
    for (auto& it : headerItems) { head.removeFromRight(6); it.c->setBounds(head.removeFromRight(it.w)); }
    auto r = getLocalBounds().reduced(6, 0); r.removeFromTop(bodyTop); r.removeFromBottom(4);
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
int SeqGrid::cols() const { return juce::jmax(32, (int)proc.seq.cur().pattern.size()); }
int SeqGrid::rowAt(float y) const { return juce::jlimit(0, 18, (int)(y / rowH)); }
int SeqGrid::colAt(float x) const { return x < headW ? -1 : juce::jlimit(0, cols() - 1, (int)((x - headW) / colW)); }

void SeqGrid::paint(juce::Graphics& g) {
    const auto& pattern = proc.seq.cur().pattern;
    const int n = (int)pattern.size(), C = cols();
    colW = (getWidth() - headW) / C; rowH = getHeight() / 19.0f;
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
        for (int c = 0; c < C; ++c) {
            const float x = headW + c * colW;
            auto cellR = juce::Rectangle<float>(x + 0.5f, y + 0.5f, colW - 1, rowH - 1);
            if (row == 0) {
                g.setColour(c == playing ? amber.withAlpha(0.35f) : juce::Colour(0xff262626)); g.fillRect(cellR);
                if (c % 8 == 0) { g.setColour(c < n ? dim : dim.withAlpha(0.4f)); g.drawText(juce::String(c + 1), cellR.withWidth(30).reduced(2, 0), juce::Justification::centredLeft); }
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
    // bar lines, the end of the pattern and section gaps
    g.setColour(juce::Colour(0xff555555));
    for (int c = 4; c < C; c += 4) g.fillRect(headW + c * colW - 0.5f, 0.0f, 1.0f, (float)getHeight());
    if (n < C) { g.setColour(amber.withAlpha(0.6f)); g.fillRect(headW + n * colW - 1.0f, 0.0f, 2.0f, (float)getHeight()); }
    g.setColour(juce::Colour(0xff555555));
    for (int row : { 1, 5, 17 }) g.fillRect(0.0f, row * rowH - 0.5f, (float)getWidth(), 1.0f);
}

// applyCell / paintCell in sequencer.js
bool SeqGrid::apply(int col, int row, bool first) {
    if (col < 0 || row < 1) return false;
    {
        const juce::ScopedLock sl(proc.modelLock);
        auto& P = proc.seq.cur().pattern;
        if (col >= (int)P.size()) {
            if (!first) return false;
            proc.seq.extendTo(col + 1); // a dimmed step past the end: extend, then press the real cell
            if (col >= (int)P.size()) return false;
        }
        Step& s = P[(size_t)col];
        const Step before = s;
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
        } else if (paintRow == 5) {
            if (paintOn) { if (row >= 5 && row <= 16) { s.code = clampCode(24 + (16 - row) + 12 * o); s.gate = true; } else if (row <= 4) return false; else s.gate = true; }
            else s.gate = false;
        } else if (paintRow == 1) { if (row > 4) return false; s.code = clampCode(24 + k + 12 * paintOct); }
        else { if (row != paintRow) return false; (paintRow == 17 ? s.slide : s.accent) = paintOn; }
        if (!first && before.code == s.code && before.gate == s.gate && before.accent == s.accent && before.slide == s.slide) return false;
        proc.seq.edited();
    }
    if (onEdit) onEdit();
    repaint();
    return true;
}

void SeqGrid::mouseDown(const juce::MouseEvent& e) {
    const int c = colAt((float)e.x), r = rowAt((float)e.y);
    painting = true; lastCol = c; lastRow = r;
    apply(c, r, true);
}
void SeqGrid::mouseDrag(const juce::MouseEvent& e) {
    if (!painting) return;
    const int c = colAt((float)e.x), r = rowAt((float)e.y);
    if (c == lastCol && r == lastRow) return;
    lastCol = c; lastRow = r;
    apply(c, r, false);
}

void SlotButton::paintButton(juce::Graphics& g, bool over, bool down) {
    auto r = getLocalBounds().toFloat().reduced(0.5f, 6.5f).translated(0, 2);
    const bool on = getToggleState();
    auto c = on ? amber : juce::Colour(0xff3c3c3c);
    if (over) c = c.brighter(0.12f);
    if (down) c = c.darker(0.15f);
    g.setColour(c); g.fillRoundedRectangle(r, 3.0f);
    g.setColour(playing ? cyan : edge); g.drawRoundedRectangle(r, 3.0f, playing ? 2.0f : 1.0f);
    g.setColour(on ? juce::Colours::black : used ? cyan : ui::text);
    g.setFont(juce::FontOptions(12.5f, used ? juce::Font::bold : juce::Font::plain));
    g.drawText(getButtonText(), r, juce::Justification::centred);
    if (chainNo > 0) {
        auto b = juce::Rectangle<float>(r.getRight() - 11, 0, 13, 11);
        g.setColour(cyan); g.fillRoundedRectangle(b, 2);
        g.setColour(juce::Colour(0xff161616)); g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText(juce::String(chainNo), b, juce::Justification::centred);
    }
}
// Cmd-click (Ctrl-click) or right-click adds the slot to the chain or takes it out
void SlotButton::mouseDown(const juce::MouseEvent& e) {
    alt = e.mods.isPopupMenu() || e.mods.isCommandDown();
    if (alt) { if (onRightClick) onRightClick(); return; }
    juce::TextButton::mouseDown(e);
}
void SlotButton::mouseUp(const juce::MouseEvent& e) { if (alt) { alt = false; return; } juce::TextButton::mouseUp(e); }

// ---------- parts ----------
PartKnob::PartKnob(TB303Processor& p, int k) : part(k), proc(p) {
    slider.setRange(0, 1);
    slider.setMouseDragSensitivity(180);
    const bool fixed = isFixedPart(k);
    slider.setEnabled(!fixed);
    if (!seriesValues(k).empty()) slider.snap = [k](double t) { return partToT(k, snapReal(k, tToPart(k, t))); };
    slider.setDoubleClickReturnValue(true, partToT(k, parts()[k].v));
    slider.onValueChange = [this] {
        const double v = tToPart(part, slider.getValue());
        if (std::fabs(v - proc.partValue(part)) > 1e-12 * std::fabs(v)) { proc.setPart(part, v); if (onChange) onChange(part); }
        repaint();
    };
    slider.onDragStart = [this] { if (onHover) onHover(part); };
    addAndMakeVisible(slider);
    refresh();
}
void PartKnob::refresh() {
    if (!slider.isMouseButtonDown()) slider.setValue(isFixedPart(part) ? 0.5 : partToT(part, proc.partValue(part)), juce::dontSendNotification);
    repaint();
}
void PartKnob::resized() { auto r = getLocalBounds(); r.removeFromTop(14); r.removeFromBottom(14); slider.setBounds(r.withSizeKeepingCentre(r.getHeight(), r.getHeight())); }
void PartKnob::paint(juce::Graphics& g) {
    auto r = getLocalBounds();
    const auto& m = PARTS_META[part];
    g.setColour(text); g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    auto top = r.removeFromTop(14);
    g.drawFittedText(m.id, top, juce::Justification::centred, 1);
    if (juce::String(m.mod).isNotEmpty()) { g.setColour(amber); g.fillEllipse((float)top.getRight() - 7, 4, 5, 5); }
    const double v = proc.partValue(part);
    g.setColour(partChanged(part, v) ? cyan : dim); g.setFont(juce::FontOptions(10.5f));
    g.drawFittedText(isFixedPart(part) ? fmtPart(part, v) + " (taper)" : fmtPart(part, v), r.removeFromBottom(14), juce::Justification::centred, 1);
}
// click the value to reset the part
void PartKnob::mouseUp(const juce::MouseEvent& e) {
    if (e.y > getHeight() - 14 && !isFixedPart(part)) { proc.setPart(part, parts()[part].v); refresh(); if (onChange) onChange(part); }
}

static juce::String srcText(const juce::String& s) {
    if (s == "schematic") return "from the 1982 sheet";
    if (s == "td3") return "read from the TD-3 sheet";
    if (s == "rat") return "standard RAT value";
    if (s == "lex") return "from Lexicon documentation";
    if (s == "estimate") return "estimate";
    return s;
}

CircuitPage::CircuitPage(TB303Processor& p) : proc(p) {
    tabs.add("Mods");
    for (int k = 0; k < PARTS_META_COUNT; ++k) if (juce::String(PARTS_META[k].block).isNotEmpty()) tabs.addIfNotAlreadyThere(PARTS_META[k].block);
    for (auto& t : tabs) {
        auto* b = tabBtns.add(new juce::TextButton(t));
        b->onClick = [this, t] { showBlock(t); };
        addAndMakeVisible(b);
    }
    legend.setText(U("Drag a knob to change the part (resistors move in E24 steps, capacitors in E12; Shift for in-between). Double-click or click its value to reset. ● commonly modded."), juce::dontSendNotification);
    legend.setColour(juce::Label::textColourId, dim); legend.setFont(juce::FontOptions(11.5f));
    addAndMakeVisible(legend);
    restore.onClick = [this] { proc.restoreParts(); refresh(); if (say) say("Every part back to stock."); };
    addAndMakeVisible(restore);
    view.setViewedComponent(&holder, false); view.setScrollBarsShown(true, false);
    addAndMakeVisible(view);
    detail.setColour(juce::Label::textColourId, text); detail.setFont(juce::FontOptions(12.0f));
    detail.setJustificationType(juce::Justification::topLeft);
    detail.setText("Hover a part to see what it does.", juce::dontSendNotification);
    addAndMakeVisible(detail);
    showBlock("Mods");
}
std::vector<int> CircuitPage::keysOf(const juce::String& t) const {
    std::vector<int> out;
    for (int k = 0; k < PARTS_META_COUNT; ++k)
        if (t == "Mods" ? juce::String(PARTS_META[k].mod).isNotEmpty() : t == PARTS_META[k].block) out.push_back(k);
    return out;
}
void CircuitPage::showBlock(const juce::String& b) {
    tab = tabs.contains(b) ? b : "Mods";
    knobs.clear();
    for (int k : keysOf(tab)) {
        auto* pk = knobs.add(new PartKnob(proc, k));
        pk->onHover = [this](int part) { showDetail(part); };
        pk->onChange = [this](int) { refresh(); };
        holder.addAndMakeVisible(pk);
    }
    resized(); refresh();
}
void CircuitPage::showDetail(int k) {
    detailPart = k;
    const auto& m = PARTS_META[k]; const double t = partTolerance(k);
    juce::String s; s << m.id << U(" · ") << m.note << " (" << srcText(m.source) << "). Stock " << fmtPart(k, parts()[k].v);
    if (t > 0) s << U(", real units ±") << juce::String(t * 100, 1, false).trimCharactersAtEnd("0").trimCharactersAtEnd(".") << "%";
    if (!seriesValues(k).empty()) s << ", moves in " << (U(parts()[k].unit) == U("Ω") ? "E24" : "E12") << " steps (Shift for in-between)";
    s << ".";
    if (juce::String(m.mod).isNotEmpty()) s << U("\n● ") << m.mod;
    detail.setText(s, juce::dontSendNotification);
}
void CircuitPage::refresh() {
    for (int i = 0; i < tabBtns.size(); ++i) {
        int n = 0; for (int k : keysOf(tabs[i])) if (partChanged(k, proc.partValue(k))) ++n;
        tabBtns[i]->setButtonText(n ? tabs[i] + U(" ●") + juce::String(n) : tabs[i]);
        tabBtns[i]->setToggleState(tabs[i] == tab, juce::dontSendNotification);
    }
    for (auto* k : knobs) k->refresh();
}
void CircuitPage::resized() {
    auto r = getLocalBounds();
    auto t = r.removeFromTop(24);
    const int w = juce::jmin(96, t.getWidth() / juce::jmax(1, tabBtns.size()));
    for (auto* b : tabBtns) { b->setBounds(t.removeFromLeft(w - 3)); t.removeFromLeft(3); }
    r.removeFromTop(6);
    auto l = r.removeFromTop(22); restore.setBounds(l.removeFromRight(130)); legend.setBounds(l);
    detail.setBounds(r.removeFromBottom(40));
    view.setBounds(r);
    const int cw = 84, ch = 78, per = juce::jmax(1, (r.getWidth() - 12) / cw);
    for (int i = 0; i < knobs.size(); ++i) knobs[i]->setBounds((i % per) * cw, (i / per) * ch + 4, cw - 4, ch - 6);
    holder.setSize(r.getWidth() - 10, ((knobs.size() + per - 1) / per) * ch + 8);
}
void CircuitPage::paint(juce::Graphics& g) { g.setColour(edge); g.fillRect(detail.getBounds().withHeight(1).translated(0, -2)); }

// ---------- board ----------
static const int CELL_W = 88, CELL_H = 76, PAD = 14, HEAD = 22;
static int boardCols(const juce::String& b) {
    static const std::map<juce::String, int> m { { "CV", 3 }, { "Slide", 2 }, { "VCO", 6 }, { "Square", 3 }, { "VCF", 5 }, { "Accent", 3 }, { "Env", 3 }, { "VCA", 7 }, { "RAT", 5 }, { "224", 4 }, { "Noise", 4 } };
    auto it = m.find(b); return it == m.end() ? 4 : it->second;
}
static juce::String boardBlockName(const juce::String& b) {
    static const std::map<juce::String, const char*> m { { "CV", "PITCH CV DAC" }, { "Slide", "SLIDE" }, { "VCO", "VCO" }, { "Square", "SQUARE SHAPER" }, { "VCF", "VCF · DIODE LADDER" },
        { "Env", "FILTER ENVELOPE" }, { "Accent", "ACCENT SWEEP" }, { "VCA", "VCA · OUTPUT" }, { "RAT", "PROCO RAT" }, { "224", "LEXICON 224" }, { "Noise", "NOISE" } };
    auto it = m.find(b); return it == m.end() ? b : U(it->second);
}
void BoardView::layout(int) {
    cells.clear(); boards.clear();
    const float W = 1180;
    auto shelf = [&](const juce::String& title, juce::Colour col, std::initializer_list<const char*> blocks, juce::Point<float> at) {
        Board bd; bd.title = U(title.toRawUTF8()); bd.col = col;
        float x = PAD, y = PAD + 26, rowH = 0, maxX = 0;
        const bool solo = blocks.size() == 1;
        for (auto* b : blocks) {
            std::vector<int> keys; for (int k = 0; k < PARTS_META_COUNT; ++k) if (juce::String(PARTS_META[k].block) == b) keys.push_back(k);
            if (keys.empty()) continue;
            const int cols = juce::jmin((int)keys.size(), boardCols(b)), rows = ((int)keys.size() + cols - 1) / cols;
            const float bw = cols * CELL_W + 12.0f, bh = rows * CELL_H + HEAD + 8.0f;
            if (x + bw > W - PAD) { x = PAD; y += rowH + 12; rowH = 0; }
            if (!solo) bd.blocks.push_back({ boardBlockName(b), juce::Rectangle<float>(x, y, bw, bh) + at });
            for (size_t i = 0; i < keys.size(); ++i)
                cells.push_back({ keys[i], juce::Point<float>(x + 6 + (i % cols) * CELL_W + CELL_W / 2.0f, y + HEAD + (i / cols) * CELL_H + CELL_H / 2.0f) + at });
            maxX = juce::jmax(maxX, x + bw); x += bw + 12; rowH = juce::jmax(rowH, bh);
        }
        bd.r = juce::Rectangle<float>(at.x, at.y, maxX + PAD, y + rowH + PAD);
        boards.push_back(bd);
        return bd.r;
    };
    const auto main = shelf(U("TB-303 · MAIN BOARD"), juce::Colour(0xff1f5a34), { "CV", "Slide", "VCO", "Square", "VCF", "Accent", "Env", "VCA" }, {});
    const auto rat = shelf("PROCO RAT", juce::Colour(0xff23485e), { "RAT" }, { 0, main.getBottom() + 18 });
    shelf(U("LEXICON 224 · DIGITAL"), juce::Colour(0xff2b2b33), { "224" }, { rat.getRight() + 18, main.getBottom() + 18 });
    float BW = 0, H = 0; for (auto& b : boards) { BW = juce::jmax(BW, b.r.getRight()); H = juce::jmax(H, b.r.getBottom()); }
    const float s = getWidth() > 0 ? getWidth() / BW : 1;
    setSize(getWidth(), (int)std::ceil(H * s));
}

static const juce::Colour BAND[10] = { juce::Colour(0xff111111), juce::Colour(0xff7a3b12), juce::Colour(0xffdd2222), juce::Colour(0xfff07c1a), juce::Colour(0xfff2d21b),
    juce::Colour(0xff2a9a3a), juce::Colour(0xff2361d6), juce::Colour(0xff8a3fc8), juce::Colour(0xff888888), juce::Colour(0xfff4f4f4) };
static const juce::Colour GOLD(0xffc9a43a), SILVER(0xffc8c8c8);

void BoardView::drawPart(juce::Graphics& g, int k, juce::Point<float> c) {
    const auto& m = PARTS_META[k];
    const double v = proc.partValue(k);
    const bool chg = partChanged(k, v);
    const float cx = c.x, cy = c.y;
    const juce::String id = m.id, unit = U(parts()[k].unit), block = m.block;
    if (k == sel) { g.setColour(juce::Colours::white.withAlpha(0.1f)); g.fillRoundedRectangle(cx - CELL_W / 2.0f + 2, cy - CELL_H / 2.0f + 2, CELL_W - 4.0f, CELL_H - 4.0f, 4); g.setColour(amber); g.drawRoundedRectangle(cx - CELL_W / 2.0f + 2, cy - CELL_H / 2.0f + 2, CELL_W - 4.0f, CELL_H - 4.0f, 4, 1.5f); }
    if (chg) { g.setColour(cyan.withAlpha(0.22f)); g.fillEllipse(cx - 26, cy - 18, 52, 36); }
    auto padsLead = [&] {
        g.setColour(juce::Colour(0xffb9b9b9)); g.drawLine(cx - 34, cy, cx + 34, cy, 1.6f);
        for (float px : { cx - 34, cx + 34 }) { g.setColour(juce::Colour(0xffd8b24a)); g.fillEllipse(px - 4, cy - 4, 8, 8); g.setColour(juce::Colour(0xff6b5413)); g.drawEllipse(px - 4, cy - 4, 8, 8, 1); }
    };
    if (id.startsWith("VR")) {
        static const std::map<juce::String, int> potKnob { { "VR2", K::tuning }, { "VR4", K::resonance }, { "VR5", K::envmod }, { "VR6", K::decay }, { "VR7", K::accent } };
        auto it = potKnob.find(id);
        const double t = it != potKnob.end() ? proc.knobValue(it->second) : partToT(k, v);
        const float a = (float)((-150 + 300 * t) * juce::MathConstants<double>::pi / 180);
        padsLead();
        g.setColour(juce::Colour(0xff3a3a3a)); g.fillRoundedRectangle(cx - 17, cy - 17, 34, 34, 3); g.setColour(juce::Colour(0xff111111)); g.drawRoundedRectangle(cx - 17, cy - 17, 34, 34, 3, 1);
        g.setColour(juce::Colour(0xff9a9a9a)); g.fillEllipse(cx - 11, cy - 11, 22, 22); g.setColour(juce::Colour(0xff333333)); g.drawEllipse(cx - 11, cy - 11, 22, 22, 1);
        g.setColour(juce::Colour(0xff111111)); g.drawLine(cx, cy, cx + std::sin(a) * 10, cy - std::cos(a) * 10, 2.4f);
    } else if (block == "224") {
        g.setColour(juce::Colour(0xffc8c8c8));
        for (int i = 0; i < 6; ++i) { g.fillRect(cx - 18 + i * 7, cy - 14, 3.0f, 4.0f); g.fillRect(cx - 18 + i * 7, cy + 10, 3.0f, 4.0f); }
        g.setColour(juce::Colour(0xff151515)); g.fillRoundedRectangle(cx - 22, cy - 10, 44, 20, 2);
        g.setColour(juce::Colour(0xff333333)); g.fillEllipse(cx - 18.5f, cy - 2.5f, 5, 5);
    } else if (unit == U("Ω")) {
        // colour bands: 4 for an E24 value (gold ±5%), 5 for an in-between value (brown ±1%)
        const double nom = snapReal(k, v); const bool e24 = std::fabs(v / nom - 1) <= partTolerance(k) + 1e-6;
        const double x = e24 ? nom : v; const int digits = e24 ? 2 : 3;
        const int ex = (int)std::floor(std::log10(x)) - (digits - 1); const int mm = (int)std::llround(x / std::pow(10.0, ex));
        juce::String ds = juce::String(mm).paddedLeft('0', digits);
        std::vector<juce::Colour> cols;
        for (int i = 0; i < ds.length(); ++i) cols.push_back(BAND[juce::jlimit(0, 9, (int)(ds[i] - '0'))]);
        cols.push_back(ex >= 0 ? BAND[juce::jmin(9, ex)] : ex == -1 ? GOLD : SILVER);
        cols.push_back(e24 ? GOLD : BAND[1]);
        padsLead();
        const float w = 40, x0 = cx - w / 2;
        g.setColour(e24 ? juce::Colour(0xffd8c391) : juce::Colour(0xff6f9fd6)); g.fillRoundedRectangle(x0, cy - 7, w, 14, 6);
        for (size_t i = 0; i < cols.size(); ++i) { g.setColour(cols[i]); g.fillRect(i < cols.size() - 1 ? x0 + 7 + i * 5.5f : x0 + w - 9, cy - 7, 3.0f, 14.0f); }
        g.setColour(e24 ? juce::Colour(0xff6d5a33) : juce::Colour(0xff2c4f7a)); g.drawRoundedRectangle(x0, cy - 7, w, 14, 6, 0.8f);
    } else if (unit == "F") {
        padsLead();
        const double d = parts()[k].v;
        if (d >= 1e-6) {
            g.setColour(juce::Colour(0xff22407a)); g.fillEllipse(cx - 14, cy - 14, 28, 28);
            juce::Path p; p.addCentredArc(cx, cy, 14, 14, 0, juce::MathConstants<float>::pi * 0.25f, juce::MathConstants<float>::pi * 0.75f, true);
            g.setColour(juce::Colour(0xffc9d6ee)); g.strokePath(p, juce::PathStrokeType(4));
            g.setColour(juce::Colour(0xff8fa6cf)); g.drawLine(cx - 6, cy, cx + 6, cy, 1); g.drawLine(cx, cy - 6, cx, cy + 6, 1);
        } else if (d < 1e-9) { g.setColour(juce::Colour(0xffd2752a)); g.fillEllipse(cx - 10, cy - 10, 20, 20); }
        else { g.setColour(juce::Colour(0xffd4a52a)); g.fillRoundedRectangle(cx - 13, cy - 10, 26, 20, 3); }
    } else {
        padsLead();
        g.setColour(juce::Colour(0xff2f6fc0)); g.fillRoundedRectangle(cx - 10, cy - 10, 20, 20, 2);
        g.setColour(juce::Colour(0xfff3f3f3)); g.fillEllipse(cx - 6.5f, cy - 6.5f, 13, 13);
        g.setColour(juce::Colour(0xff555555)); g.drawLine(cx - 4, cy, cx + 4, cy, 1.5f); g.drawLine(cx, cy - 4, cx, cy + 4, 1.5f);
    }
    const juce::Colour silk(0xffe9efe6);
    g.setColour(silk); g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText(id.replace("RAT_", "").replace("LEX_", ""), juce::Rectangle<float>(cx - 44, cy - 34, 88, 13), juce::Justification::centred);
    g.setColour(chg ? cyan : silk.withAlpha(0.85f)); g.setFont(juce::FontOptions(10.0f, chg ? juce::Font::bold : juce::Font::plain));
    g.drawText(fmtPart(k, v), juce::Rectangle<float>(cx - 44, cy + 22, 88, 13), juce::Justification::centred);
}

void BoardView::paint(juce::Graphics& g) {
    if (boards.empty()) layout(getWidth());
    float BW = 0; for (auto& b : boards) BW = juce::jmax(BW, b.r.getRight());
    const float s = getWidth() / BW;
    g.addTransform(juce::AffineTransform::scale(s));
    const juce::Colour silk(0xffe9efe6);
    for (auto& b : boards) {
        g.setColour(b.col); g.fillRoundedRectangle(b.r, 10);
        g.setColour(juce::Colour(0xff0c2416)); g.drawRoundedRectangle(b.r, 10, 2);
        for (auto p : { b.r.getTopLeft() + juce::Point<float>(10, 10), b.r.getTopRight() + juce::Point<float>(-10, 10), b.r.getBottomLeft() + juce::Point<float>(10, -10), b.r.getBottomRight() + juce::Point<float>(-10, -10) }) {
            g.setColour(juce::Colour(0xff141414)); g.fillEllipse(p.x - 5, p.y - 5, 10, 10); g.setColour(GOLD); g.drawEllipse(p.x - 5, p.y - 5, 10, 10, 2.5f);
        }
        g.setColour(silk); g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(b.title, juce::Rectangle<float>(b.r.getX() + PAD + 10, b.r.getY() + PAD, 600, 16), juce::Justification::centredLeft);
        for (auto& [name, r] : b.blocks) {
            juce::Path p; p.addRoundedRectangle(r, 6);
            juce::Path dashed; const float dl[] = { 5, 4 }; juce::PathStrokeType(1.2f).createDashedStroke(dashed, p, dl, 2);
            g.setColour(silk.withAlpha(0.55f)); g.fillPath(dashed);
            g.setColour(silk); g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            g.drawText(name, juce::Rectangle<float>(r.getX() + 9, r.getY() + 4, r.getWidth() - 12, 13), juce::Justification::centredLeft);
        }
    }
    // traces between neighbours in a row
    g.setColour(GOLD.withAlpha(0.55f));
    for (size_t i = 1; i < cells.size(); ++i)
        if (cells[i].c.y == cells[i - 1].c.y && std::abs(cells[i].c.x - cells[i - 1].c.x - CELL_W) < 0.5f && juce::String(PARTS_META[cells[i].part].block) != "224"
            && juce::String(PARTS_META[cells[i].part].block) == PARTS_META[cells[i - 1].part].block)
            g.drawLine(cells[i].c.x - CELL_W + 34, cells[i].c.y, cells[i].c.x - 34, cells[i].c.y, 3);
    for (auto& c : cells) drawPart(g, c.part, c.c);
}
void BoardView::mouseDown(const juce::MouseEvent& e) {
    float BW = 0; for (auto& b : boards) BW = juce::jmax(BW, b.r.getRight());
    const auto p = e.position / (getWidth() / BW);
    for (auto& c : cells)
        if (std::abs(p.x - c.c.x) < CELL_W / 2.0f && std::abs(p.y - c.c.y) < CELL_H / 2.0f) { sel = c.part; repaint(); if (onPick) onPick(c.part); return; }
}

void Scope::paint(juce::Graphics& g) {
    const float W = (float)getWidth(), H = (float)getHeight();
    g.setColour(juce::Colour(0xff161616)); g.fillRoundedRectangle(getLocalBounds().toFloat(), 4);
    g.setColour(juce::Colour(0xff2c2c2c));
    for (int i = 1; i < 10; ++i) g.drawVerticalLine((int)(i * W / 10), 0, H);
    for (int i = 1; i < 4; ++i) g.drawHorizontalLine((int)(i * H / 4), 0, W);
    // the latest 2048 samples, starting at a rising zero crossing
    const int N = TB303Processor::SCOPE, w = proc.scopeWrite.load(), span = 1024;
    int start = (w - 2048 + N) & (N - 1);
    for (int i = 1; i < 1024; ++i) {
        const int a = (start + i - 1) & (N - 1), b = (start + i) & (N - 1);
        if (proc.scope[a] < 0 && proc.scope[b] >= 0) { start = b; break; }
    }
    juce::Path p;
    for (int i = 0; i < span; ++i) {
        const float x = i / (float)(span - 1) * W, y = H / 2 - juce::jlimit(-1.1f, 1.1f, proc.scope[(start + i) & (N - 1)]) * H * 0.45f;
        if (i) p.lineTo(x, y); else p.startNewSubPath(x, y);
    }
    g.setColour(cyan); g.strokePath(p, juce::PathStrokeType(1.6f));
}
} // namespace ui

// ---------------------------------------------------------------------------------------------------
static juce::String knobName(const char* id) { return tb303::knobs()[(size_t)tb303::knobIndex(id)].name; }
static const char* TAB_NAMES[] = { "Sequencer", "Mods", "FX", "Circuit", "Board", "Monitor", "MIDI", "About" };

TB303Editor::TB303Editor(TB303Processor& p) : juce::AudioProcessorEditor(p), proc(p), grid(p), circuit(p), board(p), scope(p) {
    setLookAndFeel(&lnf);
    auto& s = proc.apvts;

    // ---- top bar: preset browser, file, functions ----
    seqBtn = std::make_unique<ui::Toggle>(s, "seq", "Sequencer");
    seqBtn->setTooltip("Plays the pattern with the host's transport. Off: MIDI notes only.");
    setPresetName(proc.lastPresetName);
    presetBtn.onClick = [this] { showPresetMenu(); };
    prevBtn.onClick = [this] { stepPreset(-1); };
    nextBtn.onClick = [this] { stepPreset(1); };
    favBtn.onClick = [this] {
        const auto n = proc.lastPresetName;
        if (n.isEmpty() || !proc.userPresetNames().contains(n)) { say("Load or save a preset first, then star it."); return; }
        proc.setFavourite(n, !proc.isFavourite(n));
        say(proc.isFavourite(n) ? U("★ ") + n + " is a favourite." : n + " is no longer a favourite.");
        setPresetName(n);
    };
    saveBtn.onClick = [this] { savePresetDialog(proc.lastPresetName.isNotEmpty() ? proc.lastPresetName : tb303::autoName(proc.userPresetNames())); };
    delBtn.onClick = [this] {
        const auto n = proc.lastPresetName;
        if (n.isEmpty() || !proc.userPresetNames().contains(n)) { say("Pick a saved preset to delete."); return; }
        juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::QuestionIcon).withTitle("Delete preset")
            .withMessage("Delete " + n + "?").withButton("Delete").withButton("Cancel").withAssociatedComponent(this),
            [this, n](int r) { if (r == 1) { proc.removeUserPreset(n); proc.lastPresetName = {}; setPresetName({}); say("Deleted " + n + "."); } });
    };
    fileBtn.onClick = [this] { showFileMenu(); };
    fnBtn.onClick = [this] { showFunctionsMenu(); };
    search.setTextToShowWhenEmpty("Search", ui::dim);
    search.setFont(juce::FontOptions(13.0f));
    search.onReturnKey = [this] { showPresetMenu(); };
    for (auto* c : std::initializer_list<juce::Component*> { seqBtn.get(), &presetBtn, &prevBtn, &nextBtn, &favBtn, &saveBtn, &delBtn, &fileBtn, &fnBtn, &search }) addAndMakeVisible(c);

    // ---- voice ----
    waveBtn = std::make_unique<juce::TextButton>("Saw");
    waveBtn->setClickingTogglesState(true);
    waveAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, "wave", *waveBtn);
    waveBtn->onStateChange = [this] { waveBtn->setButtonText(waveBtn->getToggleState() ? "Square" : "Saw"); };
    waveBtn->setButtonText(waveBtn->getToggleState() ? "Square" : "Saw");
    struct LabelledBox : juce::Component {
        juce::String name; std::vector<juce::Component*> cs;
        void resized() override { auto r = getLocalBounds(); r.removeFromTop(20); const int h = cs.size() > 1 ? 22 : 26; for (auto* c : cs) { c->setBounds(r.removeFromTop(h)); r.removeFromTop(4); } }
        void paint(juce::Graphics& g) override { g.setColour(ui::text); g.setFont(juce::FontOptions(12.5f, juce::Font::bold)); g.drawText(name, 0, 0, getWidth(), 16, juce::Justification::centred); }
    };
    {
        auto wb = std::make_unique<LabelledBox>(); wb->name = "Waveform"; wb->cs = { waveBtn.get() }; wb->addAndMakeVisible(*waveBtn);
        voice.add(wb.get(), 84); owned.push_back(std::move(wb));
    }
    for (auto* id : { "tuning", "cutoff", "resonance", "envmod", "decay", "accent", "volume" }) knobs.push_back(std::make_unique<ui::Knob>(s, id, knobName(id)));
    auto kv = [&s](const char* id) { return s.getRawParameterValue(id)->load(); };
    knobs[0]->valueText = [kv] { const double st = (kv("tuning") - 0.5) * 24; return (st >= 0 ? "+" : "") + juce::String(st, 1) + " st"; };
    knobs[1]->valueText = [this, kv] {
        const double g73 = 1 / proc.partValue(P::R73), g72 = 1 / proc.partValue(P::R72), g63 = 1 / proc.partValue(P::R63), g71 = 1 / proc.partValue(P::R71);
        return juce::String(g73 / (g73 + g72 + g63 + g71) * proc.partValue(P::V_CUT) * kv("cutoff") * 1000, 1) + " mV"; };
    knobs[2]->valueText = [this, kv] { return "k = " + juce::String(proc.partValue(P::A_Q21) * kv("resonance"), 1); };
    knobs[4]->valueText = [this, kv] { return TB303Processor::formatPart(proc.partValue(P::R138) + proc.partValue(P::VR6) * audioTaper(kv("decay")), U("Ω")); };
    for (auto& k : knobs) voice.add(k.get(), 86);
    {
        auto trim = std::make_unique<ui::Knob>(s, "m_I_REF", "Filter trim", true);
        trim->valueText = [this] { const double o = std::log2(proc.partValue(P::I_REF) / parts()[P::I_REF].v) * 12; return (o >= 0 ? "+" : "") + juce::String(o, 1) + " st"; };
        trim->changed = [this] { return partChanged(P::I_REF, proc.partValue(P::I_REF)); };
        voice.add(trim.get(), 80); knobs.push_back(std::move(trim));
    }
    {
        auto cb = std::make_unique<LabelledBox>(); cb->name = "Components";
        cb->cs = { &newUnit, &messedUp, &partsUndo };
        for (auto* b : cb->cs) cb->addAndMakeVisible(b);
        newUnit.setTooltip("New machine: every part lands somewhere inside its real tolerance, like another unit off the line");
        messedUp.setTooltip("Every part bent well outside spec, circuit-bending style. Tuning parts are left alone so it stays in tune");
        newUnit.onClick = [this] { say(proc.randomiseParts(false)); refreshAll(); };
        messedUp.onClick = [this] { say(proc.randomiseParts(true)); refreshAll(); };
        partsUndo.onClick = [this] { say(proc.undoParts()); refreshAll(); };
        voice.add(cb.get(), 96); owned.push_back(std::move(cb));
    }
    addAndMakeVisible(voice);

    for (int i = 0; i < NTABS; ++i) {
        tabBtn[i].setButtonText(TAB_NAMES[i]);
        tabBtn[i].onClick = [this, i] { showTab(i); };
        addAndMakeVisible(tabBtn[i]);
    }

    // ---- sequencer ----
    for (int g = 0; g < 4; ++g) {
        bankBtn[g].setButtonText(juce::String::charToString((juce::juce_wchar)('A' + g)));
        bankBtn[g].onClick = [this, g] { seqDo([this, g] { proc.seq.select(g, proc.seq.selN); return juce::String(); }); };
        seqPage.addAndMakeVisible(bankBtn[g]);
    }
    for (int n = 0; n < 12; ++n) {
        slotBtn[n].setButtonText(juce::String(n + 1));
        slotBtn[n].onClick = [this, n] { seqDo([this, n] { proc.seq.select(proc.seq.selG, n); return juce::String(); }); };
        slotBtn[n].onRightClick = [this, n] { seqDo([this, n] { proc.seq.toggleChain(proc.seq.selG, n); return juce::String(); }); };
        slotBtn[n].setTooltip("Click to edit. Cmd-click (Ctrl-click) or right-click to add it to the chain");
        seqPage.addAndMakeVisible(slotBtn[n]);
    }
    chainLabel.setText("Chain", juce::dontSendNotification); chainLabel.setColour(juce::Label::textColourId, ui::dim); chainLabel.setFont(juce::FontOptions(11.5f));
    chainList.setFont(juce::FontOptions(12.5f, juce::Font::bold));
    chainClear.onClick = [this] { seqDo([this] { proc.seq.chain.clear(); return juce::String(); }); };
    copyBtn.onClick = [this] { proc.seq.copy(); refreshBank(); say("Copied " + proc.seq.selKey() + "."); };
    pasteBtn.onClick = [this] { seqDo([this] { return proc.seq.paste() ? "Pasted into " + proc.seq.selKey() + "." : juce::String(); }); };
    clearBtn.onClick = [this] { seqDo([this] { proc.seq.clear(); return juce::String(); }); };
    refillBtn.setTooltip("Give every slot you have not edited a new generated pattern");
    refillBtn.onClick = [this] { seqDo([this] { const int n = proc.seq.refill(true); return "Refilled " + juce::String(n) + " slot" + (n == 1 ? "" : "s") + " you had not edited."; }); };
    for (auto* c : std::initializer_list<juce::Component*> { &chainLabel, &chainList, &chainClear, &copyBtn, &pasteBtn, &clearBtn, &refillBtn }) seqPage.addAndMakeVisible(c);

    auto stepper = [this](Stepper& st, const juce::String& name, const juce::String& dn, const juce::String& up, std::function<void(int)> fn) {
        st.name.setText(name, juce::dontSendNotification); st.name.setColour(juce::Label::textColourId, ui::dim); st.name.setFont(juce::FontOptions(11.5f));
        st.value.setJustificationType(juce::Justification::centred); st.value.setColour(juce::Label::textColourId, ui::cyan); st.value.setFont(juce::FontOptions(12.5f));
        st.down.setButtonText(dn); st.up.setButtonText(up);
        st.down.onClick = [fn] { fn(-1); }; st.up.onClick = [fn] { fn(1); };
        for (auto* c : std::initializer_list<juce::Component*> { &st.name, &st.value, &st.down, &st.up }) seqPage.addAndMakeVisible(c);
    };
    stepper(stLen, "Length", U("◀"), U("▶"), [this](int d) { seqDo([this, d] { proc.seq.setLength((int)proc.seq.cur().pattern.size() + d); return juce::String(); }); });
    stepper(stGate, "Step len", U("◀"), U("▶"), [this](int d) { seqDo([this, d] { auto& g = proc.seq.cur().gate; g = juce::jlimit(0.1, 0.95, std::round((g + d * 0.05) * 100) / 100); return juce::String(); }); });
    stepper(stShift, "Shift", U("◀"), U("▶"), [this](int d) { seqDo([this, d] {
        auto& P = proc.seq.cur().pattern;
        if (P.size() > 1) { if (d > 0) std::rotate(P.rbegin(), P.rbegin() + 1, P.rend()); else std::rotate(P.begin(), P.begin() + 1, P.end()); }
        proc.seq.edited(); return juce::String(); }); });
    stepper(stTr, "Transpose", U("▼"), U("▲"), [this](int d) { seqDo([this, d] {
        auto& P = proc.seq.cur().pattern;
        for (auto& st : P) if (st.code + d < 0 || st.code + d > 59) return juce::String();
        for (auto& st : P) st.code += d;
        proc.seq.edited(); return juce::String(); }); });
    stepper(stDens, "Density", U("◀"), U("▶"), [this](int d) { proc.seq.genDens = juce::jlimit(0, (int)SeqModel::densities().size() - 1, proc.seq.genDens + d); refreshSeqSide(); });
    stepper(stBars, "Bars", U("◀"), U("▶"), [this](int d) { proc.seq.genBars = juce::jlimit(1, 4, proc.seq.genBars + d); refreshSeqSide(); });
    stShift.value.setText("step", juce::dontSendNotification); stShift.value.setColour(juce::Label::textColourId, ui::dim);
    stTr.value.setText("semi", juce::dontSendNotification); stTr.value.setColour(juce::Label::textColourId, ui::dim);
    dupBtn.setTooltip("Repeat the pattern until it fills 16 steps, or 32 if it is already 16 or more");
    shufBtn.setTooltip("Move the steps around at random: notes keep their accents and slides, rests move too");
    genBtn.setTooltip("New pattern made from a random factory pattern, looped, reversed, doubled or shuffled, at the density and bars set below");
    dupBtn.onClick = [this] { seqDo([this] { return proc.seq.duplicate(); }); };
    shufBtn.onClick = [this] { seqDo([this] { return proc.seq.shuffleNotes(); }); };
    genBtn.onClick = [this] { seqDo([this] { return proc.seq.generate(); }); };
    genBtn.setColour(juce::TextButton::buttonColourId, ui::amber);
    syncLabel.setFont(juce::FontOptions(11.5f)); syncLabel.setColour(juce::Label::textColourId, ui::dim);
    syncLabel.setJustificationType(juce::Justification::topLeft);
    grid.onEdit = [this] { proc.publish(); lastModel = proc.modelVersion; refreshSeqSide(); refreshBank(); };
    for (auto* c : std::initializer_list<juce::Component*> { &dupBtn, &shufBtn, &genBtn, &syncLabel, &grid }) seqPage.addAndMakeVisible(c);
    addChildComponent(seqPage);

    // ---- mods ----
    for (auto& m : frontMods()) {
        if (juce::String(m.part) == "I_REF") continue;
        auto k = std::make_unique<ui::Knob>(s, juce::String("m_") + m.part, m.name);
        k->desc = m.desc;
        const int pi = partIndex(m.part);
        k->valueText = [this, pi] { return fmtPart(pi, proc.partValue(pi)); };
        k->changed = [this, pi] { return partChanged(pi, proc.partValue(pi)); };
        if (!seriesValues(pi).empty()) k->slider.snap = [pi](double t) { return partToT(pi, snapReal(pi, tToPart(pi, t))); };
        k->setTooltip(juce::String(m.part) + ": " + PARTS_META[pi].mod);
        mods.add(k.get(), 90); owned.push_back(std::move(k));
    }
    mods.sub = "the parts people change most. Shift for in-between values, double-click to reset";
    modsPage.addAndMakeVisible(mods);
    addChildComponent(modsPage);

    // ---- fx ----
    auto addKnobs = [&](ui::Section& sec, std::initializer_list<const char*> ids, int w = 66, std::vector<ui::Knob*>* keep = nullptr) {
        for (auto* id : ids) {
            auto k = std::make_unique<ui::Knob>(s, id, knobName(id), true);
            if (keep) keep->push_back(k.get());
            sec.add(k.get(), w); owned.push_back(std::move(k));
        }
    };
    auto onBtn = [&](ui::Section& sec, const char* id) { auto b = std::make_unique<ui::Toggle>(s, id, "On"); sec.setHeaderButton(b.get()); owned.push_back(std::move(b)); };
    auto setParam = [this](const char* id, float plain) { if (auto* p = proc.apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(plain)); };
    onBtn(rat, "fx_rat"); addKnobs(rat, { "dist", "filter", "ratvol" }, 70);
    {
        auto* kd = dynamic_cast<ui::Knob*>(owned[owned.size() - 3].get()), *kf = dynamic_cast<ui::Knob*>(owned[owned.size() - 2].get());
        kd->valueText = [this, kv] { return U("gain ×") + juce::String(juce::roundToInt(1 + (proc.partValue(P::RAT_RD) * audioTaper(kv("dist")) + 1) * (1 / proc.partValue(P::RAT_RG1) + 1 / proc.partValue(P::RAT_RG2)))); };
        kf->valueText = [this, kv] { return TB303Processor::formatPart(std::round(1 / (2 * juce::MathConstants<double>::pi * (proc.partValue(P::RAT_RF) + 100e3 * kv("filter")) * proc.partValue(P::RAT_CFLT))), "Hz"); };
    }
    onBtn(dly, "fx_dly"); dly.sub = "tempo synced, tone on the repeats"; addKnobs(dly, { "dlytime", "dlyfb", "dlylo", "dlyhi", "dlymix" }, 70);
    const char* dm[2] = { "Ping-pong", "Mono" };
    for (int i = 0; i < 2; ++i) { dlyMode[i].setButtonText(dm[i]); dlyMode[i].onClick = [setParam, i] { setParam("dlypp", i == 0 ? 1.0f : 0.0f); }; dly.addHeaderItem(&dlyMode[1 - i], 74); }
    cmp.sub = "ducks the delay and 224 returns, keyed by the synth"; addKnobs(cmp, { "cmpthr", "cmprng", "cmpatk", "cmphold", "cmprel", "cmpmix" }, 70);
    const char* cm[3] = { "Peak", "ISP", "RMS" };
    for (int i = 0; i < 3; ++i) { cmpMode[i].setButtonText(cm[i]); cmpMode[i].onClick = [setParam, i] { setParam("cmpmode", (float)i); }; cmp.addHeaderItem(&cmpMode[2 - i], 50); }
    onBtn(lex, "fx_lex");
    lex.bodyTop = 62;
    addKnobs(lex, { "lexbass", "lexmid", "lexxover", "lextreble", "lexdepth", "lexdiff", "lexpre", "lexme", "lexpitch", "lexmix" }, 74, &lexKnobs);
    for (int i = 0; i < 9; ++i) {
        const auto& P = lexPrograms()[(size_t)i];
        lexProg[i].setButtonText(juce::String(P.n) + " " + P.shortName);
        lexProg[i].setTooltip(P.name);
        lexProg[i].onClick = [setParam, i] { setParam("lexprog", (float)i); };
        lex.addAndMakeVisible(lexProg[i]);
    }
    lexLcd.setColour(juce::Label::backgroundColourId, juce::Colour(0xff10180f)); lexLcd.setColour(juce::Label::textColourId, juce::Colour(0xff9df07a));
    lexLcd.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::bold)); lexLcd.setJustificationType(juce::Justification::centredLeft);
    lex.addHeaderItem(&lexLcd, 260);
    lexNote.setColour(juce::Label::textColourId, ui::dim); lexNote.setFont(juce::FontOptions(11.0f)); lexNote.setJustificationType(juce::Justification::topLeft);
    fxPage.addAndMakeVisible(lexNote);
    for (auto* c : { &rat, &dly, &cmp, &lex }) fxPage.addAndMakeVisible(c);
    addChildComponent(fxPage);

    // ---- circuit, board ----
    circuit.say = [this](const juce::String& m) { say(m); };
    addChildComponent(circuit);
    boardView.setViewedComponent(&board, false); boardView.setScrollBarsShown(true, false);
    boardInfo.setColour(juce::Label::textColourId, ui::dim); boardInfo.setFont(juce::FontOptions(12.0f));
    boardInfo.setText("Every part at its current value. Changed parts glow; click one for details. (A stylised board, not the real layout.)", juce::dontSendNotification);
    board.onPick = [this](int k) {
        const auto& m = PARTS_META[k]; const double v = proc.partValue(k), t = partTolerance(k);
        juce::String sTxt; sTxt << m.id << "  " << fmtPart(k, v) << U(" · ") << m.note << ". Stock " << fmtPart(k, parts()[k].v);
        if (t > 0) sTxt << U(", ±") << juce::String(t * 100, 1).trimCharactersAtEnd("0").trimCharactersAtEnd(".") << "%";
        boardInfo.setText(sTxt + ".", juce::dontSendNotification);
        boardEdit.setVisible(!isFixedPart(k));
    };
    boardEdit.onClick = [this] {
        if (board.sel < 0) return;
        circuit.showBlock(PARTS_META[board.sel].block); circuit.showDetail(board.sel); showTab(3);
    };
    boardEdit.setVisible(false);
    for (auto* c : std::initializer_list<juce::Component*> { &boardView, &boardInfo, &boardEdit }) boardPage.addAndMakeVisible(c);
    addChildComponent(boardPage);

    // ---- monitor ----
    dcBtn = std::make_unique<ui::Toggle>(s, "declick", "De-click");
    dcFade = std::make_unique<ui::Knob>(s, "declickms", "Fade", true);
    dcText.setText("Eases the level over a few ms around each retrigger, where the real 303 clicks. Adds 3 ms of latency (reported to the host). Off is the original sound.", juce::dontSendNotification);
    dcText.setColour(juce::Label::textColourId, ui::dim); dcText.setFont(juce::FontOptions(12.0f)); dcText.setJustificationType(juce::Justification::topLeft);
    readouts.setColour(juce::Label::textColourId, ui::dim); readoutVals.setColour(juce::Label::textColourId, ui::cyan);
    for (auto* l : { &readouts, &readoutVals }) { l->setFont(juce::FontOptions(12.5f)); l->setJustificationType(juce::Justification::topLeft); }
    for (auto* c : std::initializer_list<juce::Component*> { dcBtn.get(), dcFade.get(), &dcText, &scope, &readouts, &readoutVals }) monitorPage.addAndMakeVisible(c);
    addChildComponent(monitorPage);

    // ---- MIDI ----
    midiText.setText("Notes play the voice on any channel: velocity 100 or more is an accent, and a note played while another is held slides (legato). "
                     "Front-panel dials follow MIDI CC:", juce::dontSendNotification);
    midiText.setColour(juce::Label::textColourId, ui::dim); midiText.setFont(juce::FontOptions(12.5f)); midiText.setJustificationType(juce::Justification::topLeft);
    {
        juce::String a, b; const auto& map = ccMap();
        for (size_t i = 0; i < map.size(); ++i) { (i < 10 ? a : b) << "CC " << (int)i + 1 << "    " << map[i].name << "\n"; }
        midiMap.setText(a, juce::dontSendNotification); midiMapR.setText(b, juce::dontSendNotification);
        for (auto* l : { &midiMap, &midiMapR }) { l->setFont(juce::FontOptions(13.0f)); l->setJustificationType(juce::Justification::topLeft); l->setColour(juce::Label::textColourId, ui::text); }
    }
    for (auto* c : std::initializer_list<juce::Component*> { &midiText, &midiMap, &midiMapR }) midiPage.addAndMakeVisible(c);
    addChildComponent(midiPage);

    // ---- about ----
    aboutText.setText(U(
        "Source. Roland main board schematic, Feb 19 1982, with unclear values read from the TD-3 clone sheet. Parts on neither sheet are estimates. Runs at 2x oversampling, sample for sample the same engine as the web app.\n\n"
        "Pitch and VCO. A 6-bit R-2R ladder from 5.333 V gives 1/12 V per step. An exponential current charges C33 from 5.5 V to 12 V, then resets, smoothed with a polyBLEP.\n\n"
        "VCF. Four coupled ladder stages with a tanh law per transistor pair, 18n bottom cap and 33n above, solved implicitly. Full feedback (k = 17) self-oscillates.\n\n"
        "Envelopes and VCA. C62 decays through R138 + VR6 (200 ms to 2.5 s). Accent shorts VR6 and charges the sweep cap. The OTA VCA gets resonance compensation through C76/R100, and normal notes fade in through R_SOFT.\n\n"
        "Lexicon 224. Its own 20 kHz clock with 12-bit gain-ranging converters, 16-bit words and a shared 32K-word delay memory. The core is Dattorro's published Griesinger-style plate; the hall, chamber, room and chorus variants are estimates.\n\n"
        "RAT. LM308 stage with gain-bandwidth and slew limits from its 30p cap, 1N914 diodes to ground, then the 3.3n filter.\n\n"
        "Plug-in. The sequencer follows the host's transport and tempo (no Play button or tempo dial here). Presets, favourites and Sound only are shared by every copy of the plug-in and "
        "use the web app's file format, so files move both ways through File > Import and Export."), juce::dontSendNotification);
    aboutText.setColour(juce::Label::textColourId, ui::text); aboutText.setFont(juce::FontOptions(12.5f)); aboutText.setJustificationType(juce::Justification::topLeft);
    aboutPage.addAndMakeVisible(aboutText);
    addChildComponent(aboutPage);

    setSize(1120, 720);
    showTab(0);
    refreshAll();
    startTimerHz(30);
}

TB303Editor::~TB303Editor() { stopTimer(); setLookAndFeel(nullptr); }

// ---------------------------------------------------------------------------------------------------
void TB303Editor::seqDo(const std::function<juce::String()>& fn) {
    juce::String msg;
    { const juce::ScopedLock sl(proc.modelLock); msg = fn(); }
    seqChanged();
    if (msg.isNotEmpty()) say(msg);
}
void TB303Editor::seqChanged() {
    proc.publish(); lastModel = proc.modelVersion;
    refreshSeqSide(); refreshBank(); grid.repaint();
}
void TB303Editor::refreshSeqSide() {
    const auto& c = proc.seq.cur();
    stLen.value.setText(juce::String((int)c.pattern.size()), juce::dontSendNotification);
    stGate.value.setText(juce::String(juce::roundToInt(c.gate * 100)) + "%", juce::dontSendNotification);
    stDens.value.setText(SeqModel::densityText(proc.seq.genDens), juce::dontSendNotification);
    stBars.value.setText(juce::String(proc.seq.genBars), juce::dontSendNotification);
}
void TB303Editor::refreshBank() {
    auto& sq = proc.seq;
    for (int g = 0; g < 4; ++g) bankBtn[g].setToggleState(g == sq.selG, juce::dontSendNotification);
    // the slot playing now, when a chain plays
    juce::String playingSlot;
    const int st = proc.currentStep.load();
    if (st >= 0 && sq.chain.size() >= 2) { const auto [ci, i] = sq.chainPos(st, proc.chainOffs); juce::ignoreUnused(i); if (ci >= 0 && ci < sq.chain.size()) playingSlot = sq.chain[ci]; }
    for (int n = 0; n < 12; ++n) {
        auto& b = slotBtn[n];
        const auto k = SeqModel::key(sq.selG, n);
        const auto& sl = sq.slots[(size_t)(sq.selG * 12 + n)];
        bool used = false; for (auto& s : sl.pattern) used |= s.gate;
        const int pos = sq.chain.indexOf(k);
        const bool changed = b.getToggleState() != (n == sq.selN) || b.used != used || b.chainNo != (pos >= 0 && sq.chain.size() > 1 ? pos + 1 : 0) || b.playing != (k == playingSlot);
        b.setToggleState(n == sq.selN, juce::dontSendNotification);
        b.used = used; b.chainNo = pos >= 0 && sq.chain.size() > 1 ? pos + 1 : 0; b.playing = k == playingSlot;
        b.setTooltip(sl.autoFilled && sl.name.isNotEmpty() ? sl.name.fromLastOccurrenceOf(" / ", false, false) : juce::String("Click to edit. Cmd-click or right-click to chain"));
        if (changed) b.repaint();
    }
    pasteBtn.setEnabled(SeqModel::clip() != nullptr);
    const bool ch = sq.chain.size() >= 2;
    chainLabel.setVisible(ch); chainList.setVisible(ch); chainClear.setVisible(ch);
    if (ch) {
        juce::String s;
        for (int i = 0; i < sq.chain.size(); ++i) { if (i) s << U(" › "); s << (sq.chain[i] == playingSlot ? "[" + sq.chain[i] + "]" : sq.chain[i]); }
        chainList.setText(s, juce::dontSendNotification);
        chainList.setColour(juce::Label::textColourId, ui::cyan);
    }
}

void TB303Editor::refreshAll() {
    refreshSeqSide(); refreshBank(); grid.repaint();
    circuit.refresh();
    partsUndo.setEnabled(proc.canUndoParts());
    setPresetName(proc.lastPresetName);
    for (auto& k : knobs) k->refresh();
    repaint();
}

void TB303Editor::showTab(int t) {
    tab = t;
    for (int i = 0; i < NTABS; ++i) tabBtn[i].setToggleState(i == t, juce::dontSendNotification);
    juce::Component* pages[NTABS] = { &seqPage, &modsPage, &fxPage, &circuit, &boardPage, &monitorPage, &midiPage, &aboutPage };
    for (int i = 0; i < NTABS; ++i) pages[i]->setVisible(i == t);
    if (t == 3) circuit.refresh();
    if (t == 4) { board.layout(board.getWidth()); board.repaint(); }
}

// ---------------------------------------------------------------------------------------------------
juce::StringArray TB303Editor::visiblePresets() const {
    juce::StringArray out;
    const auto q = search.getText().trim();
    for (auto& n : proc.userPresetNames())
        if ((!favOnly || proc.isFavourite(n)) && (q.isEmpty() || n.containsIgnoreCase(q))) out.add(n);
    return out;
}
void TB303Editor::setPresetName(const juce::String& n) {
    presetBtn.setButtonText(n.isEmpty() ? U("Presets ▾") : n + U("  ▾"));
    const bool fav = n.isNotEmpty() && proc.isFavourite(n);
    favBtn.setButtonText(fav ? U("★") : U("☆"));
    favBtn.setToggleState(fav, juce::dontSendNotification);
    favBtn.setTooltip("Star the current preset");
}
void TB303Editor::loadPreset(const juce::String& n) {
    const auto x = proc.userPreset(n);
    if (!x.isObject()) return;
    proc.applyPreset(x);
    proc.lastPresetName = n;
    setPresetName(n);
    say("Loaded " + n + (proc.soundOnly() ? " (sound only)." : "."));
    refreshAll();
}
void TB303Editor::stepPreset(int d) {
    const auto list = visiblePresets();
    if (list.isEmpty()) { say(favOnly ? "No favourites match." : "No presets yet: save one, or import your web app favourites (File > Import)."); return; }
    int i = list.indexOf(proc.lastPresetName);
    i = i < 0 ? (d > 0 ? 0 : list.size() - 1) : (i + d + list.size()) % list.size();
    loadPreset(list[i]);
}
void TB303Editor::showPresetMenu() {
    juce::PopupMenu m;
    const auto list = visiblePresets();
    const auto q = search.getText().trim();
    m.addSectionHeader(q.isNotEmpty() ? "Presets matching \"" + q + "\"" : favOnly ? U("★ Favourites") : juce::String("My presets"));
    if (list.isEmpty()) m.addItem(proc.userPresetNames().isEmpty() ? "None yet: save one, or File > Import your web app favourites" : "Nothing matches", false, false, [] {});
    for (auto& n : list) {
        juce::PopupMenu::Item it((proc.isFavourite(n) ? U("★ ") : juce::String()) + n);
        it.setTicked(n == proc.lastPresetName);
        it.setAction([this, n] { loadPreset(n); });
        m.addItem(it);
    }
    m.addSeparator();
    m.addItem(U("Show ★ favourites only"), true, favOnly, [this] { favOnly = !favOnly; showPresetMenu(); });
    m.addItem("Sound only (keep my patterns)", true, proc.soundOnly(), [this] {
        proc.setSoundOnly(!proc.soundOnly());
        say(proc.soundOnly() ? "Sound only: loading a preset changes only the sound and effects, and keeps your patterns." : "Loading a preset now brings its patterns too.");
    });
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetBtn).withMaximumNumColumns(3));
}
// save with a suggested name from the auto-namer; "Another name" rolls a new one
void TB303Editor::savePresetDialog(const juce::String& suggestion) {
    auto* w = new juce::AlertWindow("Save preset", "Saves the sound, all 48 pattern slots and the chain.", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor("name", suggestion, "Name");
    w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton(U("🎲 Another name"), 2);
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
        const auto n = w->getTextEditorContents("name").trim().substring(0, 40);
        if (r == 2) { juce::MessageManager::callAsync([sp = juce::Component::SafePointer<TB303Editor>(this)] { if (sp) sp->savePresetDialog(tb303::autoName(sp->proc.userPresetNames())); }); return; }
        if (r != 1 || n.isEmpty()) return;
        const bool existed = proc.userPresetNames().contains(n);
        proc.savePreset(n); setPresetName(n);
        say((existed ? "Updated " : "Saved ") + n + ".");
    }), true);
}
void TB303Editor::showFileMenu() {
    juce::PopupMenu m;
    auto downloads = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Downloads");
    auto save = [this, downloads](const juce::String& title, const juce::String& file, std::function<juce::String(const juce::File&)> fn) {
        chooser = std::make_unique<juce::FileChooser>(title, downloads.getChildFile(file), "*.json");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, fn](const juce::FileChooser& fc) {
            auto f = fc.getResult(); if (f == juce::File()) return;
            say(fn(f.withFileExtension("json")));
        });
    };
    m.addItem("Import file...", [this, downloads] {
        chooser = std::make_unique<juce::FileChooser>("Import a preset, project, favourites or Export all file from the web app", downloads, "*.json");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            const auto f = fc.getResult(); if (f == juce::File()) return;
            say(proc.importFile(f)); refreshAll();
        });
    });
    m.addItem("Export preset file...", [this, save] {
        const auto name = proc.lastPresetName.isNotEmpty() ? proc.lastPresetName : juce::String("tb303-plugin");
        save("Export the current sound and patterns", juce::File::createLegalFileName(name) + ".json", [this](const juce::File& f) { proc.exportProject(f); return "Exported " + f.getFileName() + ". Import it in the web app's preset menu."; });
    });
    m.addItem("Export favourites...", [this, save] { save("Export starred presets", "tb303-favourites.json", [this](const juce::File& f) { return proc.exportFavourites(f); }); });
    m.addItem("Export all...", [this, save] { save("Export every preset and the current sound", "tb303-presets.json", [this](const juce::File& f) { proc.exportAll(f); return "Exported every preset to " + f.getFileName() + "."; }); });
    m.addSeparator();
    m.addItem("Show presets file", [] { TB303Processor::userPresetFile().revealToUser(); });
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&fileBtn));
}
void TB303Editor::showFunctionsMenu() {
    juce::PopupMenu m;
    m.addItem("Join chain and copy", [this] { seqDo([this] { return proc.seq.joinChain(); }); });
    m.addItem("Alter pattern", [this] { seqDo([this] { return proc.seq.alter(); }); });
    m.addItem("Undo", proc.seq.canUndo(), false, [this] { seqDo([this] { return proc.seq.undo(); }); });
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&fnBtn));
}

// ---------------------------------------------------------------------------------------------------
void TB303Editor::timerCallback() {
    if (proc.modelVersion != lastModel) { lastModel = proc.modelVersion; refreshAll(); }
    const int st = proc.currentStep.load();
    if (st != lastStep) {
        lastStep = st;
        int at = st;
        if (st >= 0 && proc.seq.chain.size() >= 2) {
            const auto [ci, i] = proc.seq.chainPos(st, proc.chainOffs);
            at = ci >= 0 && ci < proc.seq.chain.size() && proc.seq.chain[ci] == proc.seq.selKey() ? i : -1;
        }
        if (at != grid.playing) { grid.playing = at; grid.repaint(); }
        refreshBank();
    }
    const bool seqOn = proc.apvts.getRawParameterValue("seq")->load() > 0.5f;
    syncLabel.setText(!seqOn ? "Sequencer off: MIDI notes play the voice."
        : proc.seqRunning ? "Playing with the host. MIDI notes play the voice too."
                          : "Starts with the host's transport, at its tempo. MIDI notes play the voice too (velocity 100+ = accent, legato = slide).",
        juce::dontSendNotification);
    if (status.isNotEmpty() && juce::Time::getMillisecondCounter() > statusUntil) { status = {}; repaint(0, getHeight() - 24, getWidth(), 24); }

    // parts and dials: redraw what shows them
    const auto P = proc.allParts(); const auto Kn = proc.allKnobs();
    const bool partsMoved = P != lastParts, knobsMoved = Kn != lastKnobs;
    if (partsMoved || knobsMoved) {
        lastParts = P; lastKnobs = Kn;
        preview.c = P; preview.knobs = Kn; preview.derive();
        for (auto& k : knobs) k->refresh();
        partsUndo.setEnabled(proc.canUndoParts());
        if (tab == 1) for (auto& c : mods.getChildren()) c->repaint();
        if (tab == 2) for (auto* c : { &rat, &dly, &cmp, &lex }) for (auto* ch : c->getChildren()) ch->repaint();
        if (tab == 3) circuit.refresh();
        if (tab == 4) board.repaint();
        // Lexicon: program buttons, LCD, chorus names, note
        const auto q = lexParams(Kn); const auto& LP = lexPrograms()[(size_t)q.prog];
        const bool chorus = LP.alg == ALG_CHORUS;
        for (int i = 0; i < 9; ++i) lexProg[i].setToggleState(i == q.prog, juce::dontSendNotification);
        lexLcd.setText(" P" + juce::String(LP.n) + " " + juce::String(LP.name).toUpperCase(), juce::dontSendNotification);
        static const char* chorusNames[5] = { "Pair 1", "Pair 2", "Pair 3", "Pair 4", "Wander" };
        static const char* ids[5] = { "lexbass", "lexmid", "lexxover", "lextreble", "lexdepth" };
        for (int i = 0; i < 5; ++i) lexKnobs[(size_t)i]->setLabel(chorus ? juce::String(chorusNames[i]) : knobName(ids[i]));
        const auto mu = preview.lex.memoryUse();
        juce::String note = chorus ? juce::String("Eight voices, four a side, each delay wandering at random. The first four knobs are the voice-pair levels; Diffusion acts on pairs 3 and 4.")
                                   : U("Predelay ") + juce::String((int)LP.pre0) + U("–") + juce::String((int)LP.pre1) + " ms on this program.";
        note << " " << juce::String(P[P::LEX_FS] / 1000, 1) << " kHz, " << juce::roundToInt(P[P::LEX_BITS]) << "-bit converters, " << mu.words << " of " << mu.of << " words.";
        if (!chorus && mu.scale < LP.size - 1e-3) note << " Shrunk to " << juce::roundToInt(mu.scale / LP.size * 100) << "% to fit.";
        note << " Program names, control ranges and converter specs follow Lexicon; the algorithms are estimates built on Dattorro's plate.";
        lexNote.setText(note, juce::dontSendNotification);
        const int pp = Kn[K::dlypp] >= 0.5 ? 0 : 1;
        for (int i = 0; i < 2; ++i) dlyMode[i].setToggleState(i == pp, juce::dontSendNotification);
        for (int i = 0; i < 3; ++i) cmpMode[i].setToggleState(i == (int)jsRound(Kn[K::cmpmode] * 2), juce::dontSendNotification);
        // Monitor readouts (knobs.js readouts())
        const double vb = preview.cutoffBase(), envTop = preview.envTopBase();
        const double rd = P[P::R138] + P[P::VR6] * std::pow(Kn[K::decay], 2.74);
        const double vco = P[P::F_TUNE] * std::pow(2.0, (Kn[K::tuning] - 0.5) * 2) * std::pow(2.0, dacVoltage(33, P[P::VREF], P[P::R_DAC], P[P::R2_DAC]) - P[P::CV_TUNE]);
        juce::String a, b;
        auto row = [&](const juce::String& n, const juce::String& v) { a << n << "\n"; b << v << "\n"; };
        row("VCO at A (code 33)", TB303Processor::formatPart(vco, "Hz"));
        row("Ramp current at A", TB303Processor::formatPart(P[P::F_TUNE] * P[P::C33] * (P[P::V_HI] - P[P::V_LO]), "A"));
        row("VCF peak, knob only", TB303Processor::formatPart(std::round(preview.vcfResonantHz(vb)), "Hz"));
        row("VCF peak, env top", TB303Processor::formatPart(std::round(std::min(preview.vcfResonantHz(envTop), 99999.0)), "Hz"));
        row("Feedback k / k_crit", juce::String(P[P::A_Q21] * Kn[K::resonance], 1) + " / 17.0");
        row("Env decay to 10%", juce::String(juce::roundToInt(2.3026 * rd * P[P::C62] * 1000)) + " ms");
        row("Accent decay to 10%", juce::String(juce::roundToInt(2.3026 * P[P::R138] * P[P::C62] * 1000)) + " ms");
        row("Slide time constant", juce::String(P[P::R93] * P[P::C35] * 1000, 1) + " ms");
        row("Square HPF corner", TB303Processor::formatPart(std::round(10 / (2 * juce::MathConstants<double>::pi * P[P::C10] * P[P::R_SQB])) / 10, "Hz"));
        readouts.setText(a, juce::dontSendNotification); readoutVals.setText(b, juce::dontSendNotification);
    }
    const bool cmpLit = Kn[0] >= 0 && (proc.apvts.getRawParameterValue("fx_lex")->load() > 0.5f || proc.apvts.getRawParameterValue("fx_dly")->load() > 0.5f);
    if (cmp.lit != cmpLit) { cmp.lit = cmpLit; cmp.repaint(); }
    for (auto [sec, id] : { std::pair<ui::Section*, const char*> { &rat, "fx_rat" }, { &dly, "fx_dly" }, { &lex, "fx_lex" } }) {
        const bool on = proc.apvts.getRawParameterValue(id)->load() > 0.5f;
        if (sec->lit != on) { sec->lit = on; sec->repaint(); }
    }
    if (tab == 5) scope.repaint();
}

void TB303Editor::paint(juce::Graphics& g) {
    g.fillAll(ui::bg);
    g.setColour(juce::Colour(0xff262626)); g.fillRect(0, 0, getWidth(), 44);
    g.setColour(ui::edge); g.drawHorizontalLine(44, 0, (float)getWidth());
    g.setColour(ui::text); g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    g.drawText("303 circuit model", 12, 0, 150, 44, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff262626)); g.fillRect(0, getHeight() - 24, getWidth(), 24);
    g.setColour(status.isNotEmpty() ? ui::text : ui::dim); g.setFont(juce::FontOptions(12.0f));
    g.drawText(status.isNotEmpty() ? status : juce::String("Component-level model of the 1982 main board, same engine as the web app"),
               12, getHeight() - 22, getWidth() - 24, 20, juce::Justification::centredLeft);
}

void TB303Editor::resized() {
    auto r = getLocalBounds();
    auto top = r.removeFromTop(44).reduced(8, 8);
    top.removeFromLeft(150);
    prevBtn.setBounds(top.removeFromLeft(26)); top.removeFromLeft(3);
    presetBtn.setBounds(top.removeFromLeft(230)); top.removeFromLeft(3);
    nextBtn.setBounds(top.removeFromLeft(26)); top.removeFromLeft(6);
    favBtn.setBounds(top.removeFromLeft(30)); top.removeFromLeft(6);
    search.setBounds(top.removeFromLeft(120)); top.removeFromLeft(6);
    saveBtn.setBounds(top.removeFromLeft(56)); top.removeFromLeft(4);
    delBtn.setBounds(top.removeFromLeft(60)); top.removeFromLeft(10);
    fileBtn.setBounds(top.removeFromLeft(56)); top.removeFromLeft(4);
    fnBtn.setBounds(top.removeFromLeft(86));
    seqBtn->setBounds(top.removeFromRight(96));

    r.removeFromBottom(24);
    r = r.reduced(10, 8);
    voice.setBounds(r.removeFromTop(124));
    r.removeFromTop(8);
    auto tabs = r.removeFromTop(26);
    for (auto& b : tabBtn) { b.setBounds(tabs.removeFromLeft(92)); tabs.removeFromLeft(4); }
    r.removeFromTop(8);
    for (auto* pg : std::initializer_list<juce::Component*> { &seqPage, &modsPage, &fxPage, &circuit, &boardPage, &monitorPage, &midiPage, &aboutPage }) pg->setBounds(r);

    // sequencer page
    {
        auto pg = seqPage.getLocalBounds();
        auto bank = pg.removeFromTop(32);
        for (auto& b : bankBtn) { b.setBounds(bank.removeFromLeft(28).withTrimmedTop(6)); bank.removeFromLeft(2); }
        bank.removeFromLeft(10);
        for (auto& b : slotBtn) { b.setBounds(bank.removeFromLeft(32)); bank.removeFromLeft(2); }
        refillBtn.setBounds(bank.removeFromRight(64).withTrimmedTop(6)); bank.removeFromRight(4);
        clearBtn.setBounds(bank.removeFromRight(56).withTrimmedTop(6)); bank.removeFromRight(4);
        pasteBtn.setBounds(bank.removeFromRight(56).withTrimmedTop(6)); bank.removeFromRight(4);
        copyBtn.setBounds(bank.removeFromRight(56).withTrimmedTop(6)); bank.removeFromRight(12);
        bank.removeFromLeft(10); bank.removeFromTop(6);
        chainLabel.setBounds(bank.removeFromLeft(42)); chainClear.setBounds(bank.removeFromRight(24)); chainList.setBounds(bank);
        pg.removeFromTop(8);
        auto side = pg.removeFromLeft(178);
        pg.removeFromLeft(10);
        grid.setBounds(pg);
        auto stepRow = [&](Stepper& st) {
            auto rr = side.removeFromTop(24);
            st.name.setBounds(rr.removeFromLeft(70)); st.up.setBounds(rr.removeFromRight(26)); st.down.setBounds(rr.removeFromLeft(26)); st.value.setBounds(rr);
            side.removeFromTop(5);
        };
        auto btnRow = [&](juce::Component& b) { b.setBounds(side.removeFromTop(24)); side.removeFromTop(5); };
        stepRow(stLen); btnRow(dupBtn); btnRow(shufBtn); stepRow(stGate); stepRow(stShift); stepRow(stTr);
        side.removeFromTop(4); btnRow(genBtn); stepRow(stDens); stepRow(stBars);
        syncLabel.setBounds(side.reduced(0, 2));
    }
    mods.setBounds(modsPage.getLocalBounds().removeFromTop(140));
    {
        auto pg = fxPage.getLocalBounds();
        auto r1 = pg.removeFromTop(100);
        rat.setBounds(r1.removeFromLeft(240)); r1.removeFromLeft(8); dly.setBounds(r1.removeFromLeft(560));
        pg.removeFromTop(8);
        cmp.setBounds(pg.removeFromTop(100).removeFromLeft(560));
        pg.removeFromTop(8);
        lex.setBounds(pg.removeFromTop(140).removeFromLeft(800));
        lexNote.setBounds(pg.reduced(4, 2).removeFromLeft(800));
        auto pr = juce::Rectangle<int>(8, 28, lex.getWidth() - 16, 26);
        const int w = pr.getWidth() / 9;
        for (auto& b : lexProg) { b.setBounds(pr.removeFromLeft(w).reduced(2, 0)); }
    }
    {
        auto pg = boardPage.getLocalBounds();
        auto info = pg.removeFromBottom(30);
        boardEdit.setBounds(info.removeFromRight(60).reduced(0, 3)); boardInfo.setBounds(info);
        boardView.setBounds(pg);
        board.setSize(pg.getWidth() - 10, 10); board.layout(board.getWidth());
    }
    {
        auto pg = monitorPage.getLocalBounds();
        auto dc = pg.removeFromTop(64);
        dcBtn->setBounds(dc.removeFromLeft(90).withSizeKeepingCentre(90, 26)); dc.removeFromLeft(10);
        dcFade->setBounds(dc.removeFromRight(80)); dc.removeFromRight(10);
        dcText.setBounds(dc);
        pg.removeFromTop(8);
        auto ro = pg.removeFromRight(360);
        scope.setBounds(pg.reduced(0, 0).removeFromTop(juce::jmin(pg.getHeight(), 300)));
        ro.removeFromLeft(16);
        readouts.setBounds(ro.removeFromLeft(170)); readoutVals.setBounds(ro);
    }
    {
        auto pg = midiPage.getLocalBounds();
        midiText.setBounds(pg.removeFromTop(44));
        midiMap.setBounds(pg.removeFromLeft(300)); midiMapR.setBounds(pg.removeFromLeft(300));
    }
    aboutText.setBounds(aboutPage.getLocalBounds().reduced(4));
}
