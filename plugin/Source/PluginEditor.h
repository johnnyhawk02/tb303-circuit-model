#pragma once
#include "PluginProcessor.h"

namespace ui {
// the web app's dark DAW theme: grey panels, amber = on, cyan = values
const juce::Colour bg { 0xff1f1f1f }, panel { 0xff2b2b2b }, panelHead { 0xff353535 }, edge { 0xff151515 },
    text { 0xffc9c9c9 }, dim { 0xff8a8a8a }, amber { 0xfff6a623 }, cyan { 0xff5fd3ea }, cell { 0xff2a2a2a }, cellAlt { 0xff232323 };

class LookAndFeel : public juce::LookAndFeel_V4 {
public:
    LookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return juce::FontOptions(juce::jmin(13.0f, h * 0.6f)); }
};

// a rotary slider that can snap (E24 / E12 part values; Shift moves freely)
class Dial : public juce::Slider {
public:
    Dial();
    std::function<double(double)> snap;
    double snapValue(double v, DragMode) override { return snap && !juce::ModifierKeys::currentModifiers.isShiftDown() ? snap(v) : v; }
};

// a dial with its name above (and an optional description) and its value below
class Knob : public juce::Component, public juce::SettableTooltipClient {
public:
    Knob(juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& name, bool small = false);
    void resized() override;
    void paint(juce::Graphics&) override;
    void refresh() { repaint(); }
    void setLabel(const juce::String& n) { if (n != name) { name = n; repaint(); } }
    std::function<juce::String()> valueText; // overrides the parameter's own text
    std::function<bool()> changed;           // value drawn in cyan when true, grey otherwise
    juce::String desc;
    Dial slider;
private:
    juce::String name;
    juce::RangedAudioParameter* param;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    bool small;
};

class Toggle : public juce::TextButton {
public:
    Toggle(juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& label);
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
};

// a titled panel holding a row of controls
class Section : public juce::Component {
public:
    explicit Section(const juce::String& title) : title(title) {}
    void paint(juce::Graphics&) override;
    void resized() override;
    void add(juce::Component* c, int width = 66) { items.push_back({ c, width }); addAndMakeVisible(c); }
    void setHeaderButton(juce::Component* b) { headerButton = b; addAndMakeVisible(b); }
    void addHeaderItem(juce::Component* c, int width) { headerItems.push_back({ c, width }); addAndMakeVisible(c); }
    juce::String title, sub;
    bool lit = true;
    int bodyTop = 28;
private:
    juce::Component* headerButton = nullptr;
    struct Item { juce::Component* c; int w; };
    std::vector<Item> items, headerItems;
};

// piano-roll sequencer, like the web app's: octave lanes, twelve key rows, slide and accent.
// Edits the processor's selected slot directly (message thread).
class SeqGrid : public juce::Component {
public:
    explicit SeqGrid(TB303Processor& p) : proc(p) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { painting = false; }
    int playing = -1;
    std::function<void()> onEdit;
private:
    TB303Processor& proc;
    int cols() const;
    int rowAt(float y) const; int colAt(float x) const;
    bool apply(int col, int row, bool first);
    float headW = 34, rowH = 16, colW = 24;
    bool painting = false; int paintRow = -1; bool paintOn = false; int paintOct = 0; int lastCol = -1, lastRow = -1;
};

// a pattern slot button: used, chain position, playing
class SlotButton : public juce::TextButton {
public:
    using juce::TextButton::TextButton;
    void paintButton(juce::Graphics&, bool over, bool down) override;
    bool used = false, playing = false; int chainNo = 0;
    std::function<void()> onRightClick;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
private:
    bool alt = false;
};

// one part: small dial, reference above, value below (cyan when changed from stock)
class PartKnob : public juce::Component {
public:
    PartKnob(TB303Processor& p, int part);
    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseEnter(const juce::MouseEvent&) override { if (onHover) onHover(part); }
    void mouseUp(const juce::MouseEvent& e) override;
    void refresh();
    std::function<void(int)> onHover, onChange;
    const int part;
private:
    TB303Processor& proc;
    Dial slider;
};

// the parts list (Options > Circuit in the web app)
class CircuitPage : public juce::Component {
public:
    explicit CircuitPage(TB303Processor& p);
    void resized() override;
    void paint(juce::Graphics&) override;
    void refresh();
    void showBlock(const juce::String& b);
    void showDetail(int part);
    std::function<void(const juce::String&)> say;
private:
    TB303Processor& proc;
    juce::StringArray tabs;
    juce::OwnedArray<juce::TextButton> tabBtns;
    juce::String tab = "Mods";
    juce::OwnedArray<PartKnob> knobs;
    juce::Component holder;
    juce::Viewport view;
    juce::TextButton restore { "Restore all parts" };
    juce::Label detail, legend;
    int detailPart = -1;
    std::vector<int> keysOf(const juce::String& t) const;
};

// a stylised PCB with every part at its live value (board.js)
class BoardView : public juce::Component {
public:
    explicit BoardView(TB303Processor& p) : proc(p) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void layout(int width);
    int sel = -1;
    std::function<void(int)> onPick;
private:
    TB303Processor& proc;
    struct Cell { int part; juce::Point<float> c; };
    struct Board { juce::String title; juce::Colour col; juce::Rectangle<float> r; std::vector<std::pair<juce::String, juce::Rectangle<float>>> blocks; };
    std::vector<Cell> cells; std::vector<Board> boards;
    void drawPart(juce::Graphics&, int k, juce::Point<float> c);
};

class Scope : public juce::Component {
public:
    explicit Scope(TB303Processor& p) : proc(p) {}
    void paint(juce::Graphics&) override;
private:
    TB303Processor& proc;
};
} // namespace ui

class TB303Editor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit TB303Editor(TB303Processor&);
    ~TB303Editor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showTab(int t);
    void say(const juce::String& s) { status = s; statusUntil = juce::Time::getMillisecondCounter() + 9000; repaint(0, getHeight() - 24, getWidth(), 24); }
    void seqChanged();                         // after any sequencer edit: publish and redraw
    void seqDo(const std::function<juce::String()>& fn); // run a model change under the lock
    void refreshSeqSide();
    void refreshBank();
    void refreshAll();

    // presets
    void showPresetMenu();
    void loadPreset(const juce::String& name);
    void stepPreset(int d);
    void savePresetDialog(const juce::String& suggestion);
    void showFileMenu();
    void showFunctionsMenu();
    juce::StringArray visiblePresets() const;
    void setPresetName(const juce::String& n);

    TB303Processor& proc;
    ui::LookAndFeel lnf;

    // top bar
    juce::TextButton presetBtn, prevBtn { "<" }, nextBtn { ">" }, favBtn, saveBtn { "Save" }, delBtn { "Delete" }, fileBtn { "File" }, fnBtn { "Functions" };
    juce::TextEditor search;
    bool favOnly = false;
    std::unique_ptr<ui::Toggle> seqBtn;
    // voice
    ui::Section voice { "TB-303 Voice" };
    std::unique_ptr<juce::TextButton> waveBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> waveAtt;
    std::vector<std::unique_ptr<ui::Knob>> knobs;
    juce::TextButton newUnit { "New unit" }, messedUp { "Messed up" }, partsUndo { "Undo" };
    // tabs
    static constexpr int NTABS = 8;
    juce::TextButton tabBtn[NTABS];
    int tab = 0;
    // sequencer
    juce::Component seqPage;
    ui::SeqGrid grid;
    juce::TextButton bankBtn[4];
    ui::SlotButton slotBtn[12];
    juce::Label chainLabel, chainList;
    juce::TextButton chainClear { "x" }, copyBtn { "Copy" }, pasteBtn { "Paste" }, clearBtn { "Clear" }, refillBtn { "Refill" };
    struct Stepper { juce::Label name, value; juce::TextButton down, up; };
    Stepper stLen, stGate, stShift, stTr, stDens, stBars;
    juce::TextButton dupBtn { "Duplicate steps" }, shufBtn { "Shuffle notes" }, genBtn { "Generate" };
    juce::Label syncLabel;
    // mods + fx
    juce::Component modsPage, fxPage;
    ui::Section mods { "Mods" }, rat { "ProCo RAT" }, dly { "Delay" }, cmp { "Compactor" }, lex { "Lexicon 224" };
    juce::TextButton dlyMode[2], cmpMode[3], lexProg[9];
    juce::Label lexLcd, lexNote;
    std::vector<ui::Knob*> lexKnobs;
    // options pages
    ui::CircuitPage circuit;
    juce::Component boardPage; juce::Viewport boardView; ui::BoardView board; juce::Label boardInfo; juce::TextButton boardEdit { "Edit" };
    juce::Component monitorPage; ui::Scope scope; juce::Label readouts, readoutVals, dcText;
    std::unique_ptr<ui::Toggle> dcBtn; std::unique_ptr<ui::Knob> dcFade;
    juce::Component midiPage; juce::Label midiText, midiMap, midiMapR;
    juce::Component aboutPage; juce::Label aboutText;
    std::vector<std::unique_ptr<juce::Component>> owned;

    tb303::Voice preview { 44100 };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String status; juce::uint32 statusUntil = 0;
    int lastStep = -2, lastModel = -1;
    tb303::Parts lastParts {};
    tb303::Knobs lastKnobs {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TB303Editor)
};
