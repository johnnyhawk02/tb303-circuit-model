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

// a dial with its name above and the parameter's own text below
class Knob : public juce::Component {
public:
    Knob(juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& name, bool small = false);
    void resized() override;
    void paint(juce::Graphics&) override;
    void refresh();
    std::function<juce::String()> valueText; // overrides the parameter's own text
private:
    juce::Slider slider;
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
    juce::Component* headerButton = nullptr; // optional on/off switch in the header
    void setHeaderButton(juce::Component* b) { headerButton = b; addAndMakeVisible(b); }
private:
    juce::String title;
    struct Item { juce::Component* c; int w; };
    std::vector<Item> items;
};

// piano-roll sequencer, like the web app's: octave lanes, twelve key rows, slide and accent
class SeqGrid : public juce::Component {
public:
    explicit SeqGrid(TB303Processor& p) : proc(p) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { painting = false; }
    std::vector<tb303::Step> pattern;
    int playing = -1;
    std::function<void()> onEdit;
private:
    TB303Processor& proc;
    static constexpr int SHOW = 32;
    int rowAt(float y) const; int colAt(float x) const;
    void apply(int col, int row, bool first);
    float headW = 34, rowH = 16, colW = 24;
    bool painting = false; int paintRow = -1; bool paintOn = false; int paintOct = 0; int lastCol = -1;
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
    void showPresetMenu();
    void say(const juce::String& s) { status = s; statusUntil = juce::Time::getMillisecondCounter() + 6000; repaint(); }
    void setLength(int n);

    TB303Processor& proc;
    ui::LookAndFeel lnf;

    // top bar
    juce::TextButton presetBtn, importBtn { "Import" }, exportBtn { "Export" };
    std::unique_ptr<ui::Toggle> playBtn, seqBtn, declickBtn;
    std::unique_ptr<ui::Knob> tempoKnob;
    // voice
    ui::Section voice { "TB-303 Voice" };
    std::unique_ptr<juce::TextButton> waveBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> waveAtt;
    std::vector<std::unique_ptr<ui::Knob>> knobs;
    // tabs
    juce::TextButton tabBtn[3];
    int tab = 0;
    // sequencer tab
    juce::Component seqPage;
    ui::SeqGrid grid;
    juce::TextButton lenDown { "<" }, lenUp { ">" }, clearBtn { "Clear" }, patBtn { "Patterns" }, shiftL { "<" }, shiftR { ">" }, trDown { "-" }, trUp { "+" };
    std::unique_ptr<ui::Knob> gateKnob;
    juce::Label lenLabel, syncLabel, shiftName, trName;
    // mods + fx tabs
    juce::Component modsPage, fxPage;
    ui::Section mods { "Mods" }, rat { "ProCo RAT" }, dly { "Delay" }, cmp { "Compactor (ducks the delay and 224 returns)" }, lex { "Lexicon 224" };
    std::vector<std::unique_ptr<juce::Component>> owned;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::String status; juce::uint32 statusUntil = 0;
    int lastStep = -2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TB303Editor)
};
