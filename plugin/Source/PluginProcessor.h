#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "dsp/TB303Model.h"
#include "SeqModel.h"

// The front-panel mods (FRONT_MODS in parts.js) plus the TM3 filter trim are automatable parameters;
// every other part is set on the Circuit page or by presets.
struct ModInfo { const char* part; const char* name; const char* desc; };
const std::vector<ModInfo>& frontMods();
bool isModPart(int part);
bool isFixedPart(int part);
double partTolerance(int part);          // spread between real units (tolerance() in functions.js)
std::vector<double> seriesValues(int part); // E24 / E12 values inside the part's dial range, or empty
double snapReal(int part, double v);

// MIDI CC 1.. follow the voice dials, then the mods (MIDI_MAP in midi.js)
struct CcTarget { juce::String paramId, name; };
const std::vector<CcTarget>& ccMap();

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

    // ---- sequencer model (message thread; lock modelLock around changes) ----
    juce::CriticalSection modelLock;
    tb303::SeqModel seq;
    void publish();                          // call after any change to seq
    std::vector<int> chainOffs;              // of the published sequence
    std::atomic<int> currentStep { -1 };     // index into the published sequence
    std::atomic<bool> seqRunning { false };
    std::atomic<int> modelVersion { 0 };     // bumped by publish(), so editors redraw

    // ---- parts (message thread) ----
    double partValue(int part) const;
    void setPart(int part, double v);
    juce::String randomiseParts(bool wild);  // New unit / Messed up
    juce::String undoParts();
    bool canUndoParts() const { return !partsUndo.empty(); }
    void restoreParts();
    tb303::Parts allParts() const;
    tb303::Knobs allKnobs() const;
    double knobValue(int k) const;

    // ---- microtuning (message thread): cents for C..B, every octave; part of the sound (tuning.js) ----
    using Tune = std::array<double, 12>;
    Tune tune() const;
    void setTune(const Tune& t);             // rounds to 0.1 c, clamps to ±100
    int tuneRoot = 9;                        // the temperaments' root (A), kept with the plug-in's state
    void pushTuneUndo(); bool undoTune(); bool canUndoTune() const { return !tuneUndo.empty(); }

    // ---- presets (web app formats) ----
    void applySound(const juce::var& sound);
    void applyPreset(const juce::var& preset); // honours Sound only
    void applyProject(const juce::var& project);
    juce::var soundSnapshot() const;
    juce::var projectSnapshot() const;
    juce::String importFile(const juce::File& f);
    void exportProject(const juce::File& f) const;
    void exportAll(const juce::File& f) const;
    juce::String exportFavourites(const juce::File& f) const;

    // user presets, favourites and Sound only: one file shared by every instance
    juce::StringArray userPresetNames() const;
    juce::var userPreset(const juce::String& name) const;
    // factory presets: factory-presets.json (the web app's Factory list), embedded; a saved preset of the same name wins
    static juce::StringArray factoryPresetNames();
    static juce::var factoryPreset(const juce::String& name);
    juce::var anyPreset(const juce::String& name) const;
    void savePreset(const juce::String& name);
    void removeUserPreset(const juce::String& name);
    bool isFavourite(const juce::String& name) const;
    void setFavourite(const juce::String& name, bool on);
    bool soundOnly() const;
    void setSoundOnly(bool on);
    static juce::File userPresetFile();
    juce::String lastPresetName;

    // ---- monitor ----
    static constexpr int SCOPE = 4096;
    float scope[SCOPE] {};
    std::atomic<int> scopeWrite { 0 };

    static juce::String formatPart(double v, const juce::String& unit);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged(const juce::String& id, float newValue) override;
    void handleAsyncUpdate() override;
    void pushAllParams(bool instant);

    std::unique_ptr<tb303::Engine> engine;
    double fs = 48000;

    std::array<std::atomic<float>*, tb303::K::COUNT> knobParams {};
    std::array<float, tb303::K::COUNT> lastKnob {};
    std::vector<std::pair<int, std::atomic<float>*>> modParams;
    std::vector<float> lastMod;
    std::atomic<float>*pWave, *pRat, *pLex, *pDly, *pDeclick, *pDeclickMs, *pSeq;
    float lastFade = -1;

    // parts that are not parameters
    mutable juce::SpinLock partsLock;
    tb303::Parts extraParts {};
    std::atomic<int> partsVersion { 0 };
    int partsSeen = -1;
    tb303::Parts sentParts {};
    std::vector<tb303::Parts> partsUndo;

    mutable juce::SpinLock tuneLock;
    Tune tuneCents {};
    std::atomic<int> tuneVersion { 0 };
    int tuneSeen = -1;
    std::vector<Tune> tuneUndo;

    // published sequence
    mutable juce::SpinLock patternLock;
    std::vector<tb303::Step> pubPattern;
    double pubGate = 0.5, pubShuffle = 0;
    std::atomic<int> patternVersion { 0 };
    int patternSeen = -1;
    std::vector<tb303::Step> audioPattern;

    // MIDI
    std::vector<int> held;
    bool midiAccent = false, hostSynced = false, lastDeclick = true;
    std::array<std::atomic<float>, 32> ccPending;

    juce::var userData; // { presets: {name: preset}, fav: [names], soundOnly: bool }
    void loadUserData();
    void saveUserData() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TB303Processor)
};
