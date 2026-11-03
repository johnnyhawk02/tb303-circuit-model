#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "dsp/TB303Model.h"

// The front-panel mods (FRONT_MODS in parts.js) plus the TM3 filter trim are automatable parameters;
// every other part keeps its stock value unless a web-app preset changes it.
struct ModInfo { const char* part; const char* name; const char* desc; };
const std::vector<ModInfo>& frontMods();

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

    // --- pattern (message thread side) ---
    std::vector<tb303::Step> getPattern() const;
    void setPattern(const std::vector<tb303::Step>& p);
    double getShuffle() const { return shuffle.load(); }
    std::atomic<int> currentStep { -1 };
    std::atomic<bool> seqRunning { false };

    // --- presets: web-app JSON (sound, pattern or project) ---
    void applySound(const juce::var& sound);       // knobs, parts, wave, fx; anything missing goes back to stock
    void applyPatternJson(const juce::var& x);     // pattern, gate, shuffle
    void applyPreset(const juce::var& preset);     // {kind:'sound'|'pattern'} or a project
    juce::var soundSnapshot() const;
    juce::var projectSnapshot() const;
    juce::String importFile(const juce::File& f);  // returns a message for the user
    void exportFile(const juce::File& f) const;

    // user presets: imported from the web app's favourites or project files, kept in a file shared by every instance
    juce::StringArray userPresetNames() const;
    juce::var userPreset(const juce::String& name) const;
    void addUserPreset(const juce::String& name, const juce::var& preset);
    void removeUserPreset(const juce::String& name);
    static juce::File userPresetFile();

    double partValue(int part) const; // current value (message thread)
    static juce::String formatPart(double v, const juce::String& unit);
    juce::String lastPresetName;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged(const juce::String& id, float newValue) override;
    void handleAsyncUpdate() override;
    void pushAllParams(bool instant);
    double knobValue(int k) const;

    std::unique_ptr<tb303::Engine> engine;
    double fs = 48000;

    // raw parameter pointers, read on the audio thread
    std::array<std::atomic<float>*, tb303::K::COUNT> knobParams {};
    std::array<float, tb303::K::COUNT> lastKnob {};
    std::vector<std::pair<int, std::atomic<float>*>> modParams; // part index, t 0..1
    std::vector<float> lastMod;
    std::atomic<float>*pWave, *pRat, *pLex, *pDly, *pDeclick, *pSeq, *pRun, *pTempo, *pGate;

    // parts that are not parameters (set by presets)
    mutable juce::SpinLock partsLock;
    tb303::Parts extraParts {};
    std::atomic<int> partsVersion { 0 };
    int partsSeen = -1;
    tb303::Parts sentParts {};

    // pattern shared with the audio thread
    mutable juce::SpinLock patternLock;
    std::vector<tb303::Step> pattern;
    std::atomic<double> shuffle { 0 };
    std::atomic<int> patternVersion { 0 };
    int patternSeen = -1;
    std::vector<tb303::Step> audioPattern;

    // MIDI: mono note stack, legato = slide
    std::vector<int> held;
    bool midiAccent = false;
    // host sync
    bool hostSynced = false, lastDeclick = true;

    juce::var userPresets; // object: name -> preset
    void loadUserPresets();
    void saveUserPresets() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TB303Processor)
};
