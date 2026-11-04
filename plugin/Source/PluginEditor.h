#pragma once
#include "PluginProcessor.h"

namespace ui {
// The Shyoy LCD look: one pane of yellow-green backlit LCD glass in a black bezel, one ink for the segments,
// a faint ghost of the unlit ones, the offset shadow LCD type casts on its backplane, and a pixel font (VT323).
// Buttons are outlined boxes that invert when on; dials are rings of segments lit up to the value.
const juce::Colour glass { 0xffc3d83a }, ink { 0xff0a0f04 }, caseCol { 0xff161815 }, bezel { 0xff0b0c0a };
const juce::Colour text = ink, dim = ink.withAlpha(0.74f), amber = ink, cyan = ink, edge = ink.withAlpha(0.45f),
    ghost = ink.withAlpha(0.13f), tint = ink.withAlpha(0.08f), shadow = ink.withAlpha(0.17f),
    cell = ghost, cellAlt = ink.withAlpha(0.2f), bg = glass, panel = juce::Colours::transparentBlack, panelHead = juce::Colours::transparentBlack;

juce::FontOptions lcd(float h);   // VT323 at a size that matches the old layout's text
// text with the LCD's backplane shadow
void lcdText(juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification, juce::Colour = ink, bool shadowed = true);

class LookAndFeel : public juce::LookAndFeel_V4 {
public:
    LookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return lcd(juce::jmin(13.5f, h * 0.62f)); }
    juce::Font getLabelFont(juce::Label& l) override { return l.getFont(); }
    void drawLabel(juce::Graphics&, juce::Label&) override;
    juce::Font getPopupMenuFont() override { return lcd(14.0f); }
    juce::Font getAlertWindowMessageFont() override { return lcd(14.0f); }
    juce::Font getAlertWindowTitleFont() override { return lcd(17.0f); }
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;
    void fillTextEditorBackground(juce::Graphics&, int, int, juce::TextEditor&) override {}
    void drawTextEditorOutline(juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
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

// Tuning page (tuning.js): 12 upright segmented faders, one per note, ±100 cents in every octave,
// temperaments laid out from a root, Drift, Reset and Undo
class TuningPage : public juce::Component {
public:
    explicit TuningPage(TB303Processor& p);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void refresh();
    std::function<void(const juce::String&)> say;
private:
    TB303Processor& proc;
    juce::TextButton sys[4], reset { "Reset" }, undo { "Undo" }, root[12];
    juce::Label rowLab, rootLab, note;
    juce::Rectangle<int> faders;
    juce::String sysOn;                     // the temperament as laid out, until a fader moves
    int drag = -1; float dragY = 0; double dragFrom = 0;
    juce::uint32 lastUndo = 0;
    int colAt(juce::Point<int>) const;
    juce::Rectangle<float> barOf(int i) const;
    void apply(const juce::String& id);
    void set(int i, double c);
    void undoPoint(bool force);
};

// the FX routing block diagram (the web app's FX page routing strip): blocks filled when the unit is on; click one to switch it
class RouteView : public juce::Component {
public:
    std::function<float(const char*)> get;      // a parameter's plain value
    std::function<void(const char*)> toggle;    // switch an fx_* parameter
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;
private:
    struct Hit { juce::Rectangle<float> r; const char* id; };
    std::vector<Hit> hits;
    juce::AffineTransform tf;
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
    void say(const juce::String& s) { status = s; statusUntil = juce::Time::getMillisecondCounter() + 9000; repaint(0, getHeight() - 28, getWidth(), 28); }
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
    static constexpr int NTABS = 9;
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
    ui::Section mods { "Mods" }, fold { "Folder" }, rat { "Drive" }, dly { "Delay" }, cmp { "Compactor" }, lex { "Reverb" }, route { "Routing" };
    ui::RouteView routeView; juce::TextButton orderMenuBtn, retMenuBtn;
    juce::TextButton dlyMode[2];
    // the reverb unit's program (224 P1-P9, plate sizes, spring tanks, Conv IRs), also a dropdown
    juce::TextButton progMenuBtn; juce::StringArray progNames; int progSel = 0;
    // the drive pedal and the reverb unit are picked from dropdown menus (as the web app's FX page)
    juce::TextButton drvMenuBtn, revMenuBtn;
    void showTypeMenu(juce::TextButton& btn, const char* param, std::initializer_list<const char*> names, int current);
    std::vector<ui::Knob*> drvKnobs[3]; // the drive slot: only the selected pedal's dials show
    int shownDrv = -1;
    std::vector<ui::Knob*> revKnobs[4];
    int shownRev = -1;
    juce::Label lexNote;
    std::vector<ui::Knob*> lexKnobs;
    // options pages
    ui::TuningPage tuning;
    ui::CircuitPage circuit;
    juce::Component boardPage; juce::Viewport boardView; ui::BoardView board; juce::Label boardInfo; juce::TextButton boardEdit { "Edit" };
    juce::Component monitorPage; ui::Scope scope; juce::Label readouts, readoutVals, dcText;
    std::unique_ptr<ui::Toggle> dcBtn; std::unique_ptr<ui::Knob> dcFade;
    juce::Component midiPage; juce::Label midiText, midiMap, midiMapR;
    juce::Component aboutPage; juce::Label aboutText;
    std::vector<std::unique_ptr<juce::Component>> owned;

    juce::Image glassImg; // the LCD pane, drawn once per size
    tb303::Voice preview { 44100 };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String status; juce::uint32 statusUntil = 0;
    int lastStep = -2, lastModel = -1;
    tb303::Parts lastParts {};
    tb303::Knobs lastKnobs {};
    TB303Processor::Tune lastTune {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TB303Editor)
};
