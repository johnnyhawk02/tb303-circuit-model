#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "dsp/TB303Model.h"

// The plug-in is the web app: its editor shows the app's own page (Resources/ui.html, made by build.js from src/)
// in a web view, and the page drives this processor with the same messages it sends the web app's AudioWorklet
// (TB303Engine.message in tb303-model.js). The engine is the C++ port (dsp/TB303Model.h). The host plays and sets
// the tempo: the sequencer follows the host transport, and MIDI notes play the voice.

// The voice dials, FX dials, the front-panel mods and the FX switches are host parameters (automation, MIDI CC);
// every other part, the tuning and the pattern are kept here and saved with the host's project.
struct ModInfo { const char* part; const char* name; const char* desc; };
const std::vector<ModInfo>& frontMods();
bool isModPart(int part);

// MIDI CC 1.. follow the voice dials, then the mods (MIDI_MAP in midi.js)
struct CcTarget { juce::String paramId, name; };
const std::vector<CcTarget>& ccMap();

// localStorage of the plug-in's page. This project's keys (the session, pattern banks, ...) are kept by each
// plug-in instance and saved with the host project; the rest (presets, favourites, the screen look) are shared by
// every instance and kept in one file: ~/Library/Application Support/TB303 Circuit Model/web-store.json.
bool isProjectKey(const juce::String& key);
class SharedStore : private juce::Timer {
public:
    static SharedStore& get();
    std::map<juce::String, juce::String> all();
    void set(const juce::String& key, const juce::var& value); // a void var removes the key
    void flush();
    static juce::File file();
private:
    SharedStore();
    void timerCallback() override { flush(); }
    juce::CriticalSection lock;
    std::map<juce::String, juce::String> data;
    bool dirty = false;
};

class TB303Processor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater {
public:
    TB303Processor();
    ~TB303Processor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- the page (message thread) ----
    void handleMessage(const juce::var& m);   // a message from the page: knob, component, wave, fx, tune, pattern, groove, declick, store
    juce::String bootScript();                // window.TB303_BOOT for the page: its localStorage, with the sound as the processor has it
    // dials the page did not move itself (host automation, MIDI CC), as { type: 'set', what, id, value } messages
    void collectChanges(juce::Array<juce::var>& out);
    void endGestures();                       // ends host gestures for dials the page stopped moving

    std::atomic<int> currentStep { -1 };      // index into the pattern the page sent; -1 when the host is stopped
    std::atomic<double> hostBpm { 128 };
    std::atomic<int> stateVersion { 0 };      // bumped when the host loads a state, so an open page reloads
    double sampleRateNow() const { return fs; }

    // state kept for the page and the engine (also used by the tests)
    double knobValue(int k) const;            // 0..1 as the page has it
    double partValue(int part) const;
    std::vector<tb303::Step> pattern() const;
    std::map<juce::String, juce::String> projectStore() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged(const juce::String& id, float newValue) override;
    void handleAsyncUpdate() override;
    void pushAllParams(bool instant);
    void setKnobFromPage(int k, double v);
    void setPartFromPage(int part, double v);
    void setBoolParam(const juce::String& id, bool on);
    void touch(juce::RangedAudioParameter* p);

    std::unique_ptr<tb303::Engine> engine;
    double fs = 48000;

    std::array<std::atomic<float>*, tb303::K::COUNT> knobParams {};
    std::array<float, tb303::K::COUNT> lastKnob {};
    std::vector<std::pair<int, std::atomic<float>*>> modParams;
    std::vector<float> lastMod;
    std::atomic<float>*pWave, *pRat, *pLex, *pDly, *pFold, *pFlg, *pDeclick, *pDeclickMs, *pSeq;
    float lastFade = -1;

    // what the page last saw of each parameter (raw value), so changes from elsewhere go back to it
    std::array<float, tb303::K::COUNT> seenKnob {};
    std::vector<float> seenMod;
    float seenWave = 0; std::array<float, 5> seenFx {};
    void markAllSeen();
    // host gestures: a parameter the page moves stays "touched" until it rests for a moment
    std::map<juce::RangedAudioParameter*, juce::uint32> gestures;

    // parts that are not parameters
    mutable juce::SpinLock partsLock;
    tb303::Parts extraParts {};
    std::atomic<int> partsVersion { 0 };
    int partsSeen = -1;
    tb303::Parts sentParts {};

    mutable juce::SpinLock tuneLock;
    std::array<double, 12> tuneCents {};
    std::atomic<int> tuneVersion { 0 };
    int tuneSeen = -1;

    // the pattern from the page (chained slots joined), with its groove
    mutable juce::SpinLock patternLock;
    std::vector<tb303::Step> pubPattern;
    double pubGate = 0.5, pubShuffle = 0;
    std::atomic<int> patternVersion { 0 };
    int patternSeen = -1;
    std::vector<tb303::Step> audioPattern;

    // this project's localStorage keys
    mutable juce::CriticalSection storeLock;
    std::map<juce::String, juce::String> projStore;

    // MIDI
    std::vector<int> held;
    bool midiAccent = false, hostSynced = false, lastDeclick = true;
    std::array<std::atomic<float>, 32> ccPending;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TB303Processor)
};
