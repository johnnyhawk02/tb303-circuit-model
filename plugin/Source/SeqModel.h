#pragma once
#include <juce_core/juce_core.h>
#include "dsp/TB303Model.h"

// The sequencer's data as the web app keeps it: four banks (A-D) of twelve pattern slots, a chain of slots
// played in a row, the copy/paste clipboard, pattern undo, and the pattern functions (Generate, Refill,
// Alter, Duplicate steps, Shuffle notes, Join chain). Lives on the message thread; the processor publishes
// the playing sequence to the audio thread.
namespace tb303 {

struct Slot {
    std::vector<Step> pattern;
    double shuffle = 0, gate = 0.5;
    bool autoFilled = false; juce::String name; // filled by Generate/Refill and not edited since
};

struct GenResult { std::vector<Step> pattern; juce::String src; juce::StringArray did; };

class SeqModel {
public:
    SeqModel();

    std::array<Slot, 48> slots;
    int selG = 0, selN = 0;        // bank A-D, slot 1-12
    juce::StringArray chain;        // slot keys like "A1"
    std::vector<Step> tail;         // steps removed by shortening, brought back by lengthening
    int genDens = 0, genBars = 1;   // Generate settings (index into GEN_DENS, bars 1-4)

    static juce::String key(int g, int n) { return juce::String::charToString((juce::juce_wchar)('A' + g)) + juce::String(n + 1); }
    static int indexOf(const juce::String& k);
    juce::String selKey() const { return key(selG, selN); }
    Slot& cur() { return slots[(size_t)(selG * 12 + selN)]; }
    const Slot& cur() const { return slots[(size_t)(selG * 12 + selN)]; }

    // the sequence the engine plays: the chain joined, or the selected slot; offs = start of each chained slot
    std::vector<Step> sequence(std::vector<int>& offs) const;
    // a step of the joined sequence as (chain index, step within that slot)
    std::pair<int, int> chainPos(int i, const std::vector<int>& offs) const;

    void select(int g, int n);
    void edited();                  // the current slot was changed by hand: drop its auto tag
    void toggleChain(int g, int n);

    // undo for pattern functions (30 deep), per slot
    void pushUndo();
    juce::String undo();
    bool canUndo() const { return !undoStack.empty(); }

    // clipboard, shared by every instance in the host
    static std::unique_ptr<Slot>& clip();
    void copy();
    bool paste();
    void clear();
    void setLength(int n);
    void extendTo(int n);
    juce::String duplicate();
    juce::String shuffleNotes();
    juce::String generate();
    int refill(bool force);
    juce::String alter();
    juce::String joinChain();

    GenResult generatePattern(int len, double dens);
    static const std::vector<double>& densities();
    static juce::String densityText(int i);

    juce::var toVar() const;                  // web app 'banks' object
    void fromVar(const juce::var& banks, const juce::var& sel, const juce::var& chain);
    static std::vector<Step> patternFromVar(const juce::var& arr);
    static juce::var patternToVar(const std::vector<Step>& p);

private:
    struct Undo { juce::String k; std::vector<Step> pattern; };
    std::vector<Undo> undoStack;
    juce::Random rng;
    struct Source { juce::String name; std::vector<Step> pattern; };
    static const std::vector<Source>& sources();
    int rint(int a, int b) { return a + rng.nextInt(b - a + 1); }
    juce::String move(int which, std::vector<Step>& P);
};

// preset auto-namer (namer.js): alien splices, messed-up desserts, cosmology, Greek-ish, s to z
juce::String autoName(const juce::StringArray& taken);

} // namespace tb303
