// Drives the plugin's processor like a host would: transport playing at 128 BPM, then MIDI notes.
// Writes host_test.wav and checks that notes land on the 16th-note grid.
#include "../Source/PluginProcessor.h"

struct FakePlayHead : juce::AudioPlayHead {
    double ppq = 0, bpm = 128; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p; p.setBpm(bpm); p.setIsPlaying(playing); p.setPpqPosition(ppq); return p;
    }
};

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    TB303Processor proc;
    FakePlayHead ph; proc.setPlayHead(&ph);
    const double fs = 48000; const int B = 480;
    proc.setPlayConfigDetails(0, 2, fs, B);
    proc.prepareToPlay(fs, B);
    juce::AudioBuffer<float> all(2, (int)fs * 6), buf(2, B);
    juce::MidiBuffer midi;
    int firstSound = -1;
    for (int pos = 0; pos < all.getNumSamples(); pos += B) {
        midi.clear();
        if (pos >= fs * 4) { // last 2 s: host stopped, MIDI notes instead
            ph.playing = false;
            const int at = pos - (int)(fs * 4);
            if (at == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 45, (juce::uint8)110), 10);
            if (at == 24000) midi.addEvent(juce::MidiMessage::noteOn(1, 57, (juce::uint8)80), 0);   // legato: slides up an octave
            if (at == 48000) { midi.addEvent(juce::MidiMessage::noteOff(1, 57), 0); }
            if (at == 72000) midi.addEvent(juce::MidiMessage::noteOff(1, 45), 0);
        }
        buf.clear();
        proc.processBlock(buf, midi);
        for (int c = 0; c < 2; ++c) all.copyFrom(c, pos, buf, c, 0, B);
        ph.ppq += B / (fs * 60 / ph.bpm);
        if (firstSound < 0) for (int i = 0; i < B; ++i) if (std::fabs(buf.getSample(0, i)) > 1e-3f) { firstSound = pos + i; break; }
    }
    const float rmsSeq = all.getRMSLevel(0, 0, (int)fs * 4), rmsMidi = all.getRMSLevel(0, (int)fs * 4, (int)fs * 2);
    std::printf("first sound at %d samples, seq rms %.1f dB, midi rms %.1f dB\n", firstSound, 20 * std::log10(rmsSeq), 20 * std::log10(rmsMidi));
    juce::WavAudioFormat wav;
    auto f = juce::File::getCurrentWorkingDirectory().getChildFile("host_test.wav");
    f.deleteFile();
    if (auto w = std::unique_ptr<juce::AudioFormatWriter>(wav.createWriterFor(new juce::FileOutputStream(f), fs, 2, 24, {}, 0))) w->writeFromAudioSampleBuffer(all, 0, all.getNumSamples());
    bool ok = firstSound >= 0 && firstSound < 400 && rmsSeq > 0.01f && rmsMidi > 0.01f;

    // sequencer model, presets and state, like the web app
    auto check = [&](bool c, const char* what) { if (!c) { std::printf("FAILED: %s\n", what); ok = false; } };
    int filled = 0; for (auto& sl : proc.seq.slots) for (auto& st : sl.pattern) if (st.gate) { filled++; break; }
    check(filled == 48, "every slot starts filled (A1 example, the rest generated)");
    {
        const juce::ScopedLock sl(proc.modelLock);
        proc.seq.select(1, 2);
        const auto msg = proc.seq.generate();
        std::printf("generate: %s\n", msg.toRawUTF8());
        check(!proc.seq.cur().pattern.empty() && proc.seq.canUndo(), "generate");
        proc.seq.toggleChain(1, 5);
        check(proc.seq.chain.size() == 2 && proc.seq.chain[0] == "B3" && proc.seq.chain[1] == "B6", "chain B3 > B6");
        std::printf("alter: %s\njoin: %s\n", proc.seq.alter().toRawUTF8(), proc.seq.joinChain().toRawUTF8());
    }
    proc.publish();
    const auto parts = proc.randomiseParts(true);
    std::printf("parts: %s\n", parts.toRawUTF8());
    check(proc.canUndoParts(), "messed up");
    const double r52 = proc.partValue(tb303::P::R52), c35 = proc.partValue(tb303::P::C35);
    juce::MemoryBlock state; proc.getStateInformation(state);
    TB303Processor other; other.setStateInformation(state.getData(), (int)state.getSize());
    check(other.seq.chain.joinIntoString(",") == "B3,B6" && other.seq.selKey() == "B3", "state keeps chain and selection");
    check(std::fabs(other.partValue(tb303::P::R52) / r52 - 1) < 1e-6 && std::fabs(other.partValue(tb303::P::C35) / c35 - 1) < 1e-6, "state keeps parts");
    bool same = true; for (int i = 0; i < 48; ++i) { const auto& a = proc.seq.slots[(size_t)i].pattern; const auto& b = other.seq.slots[(size_t)i].pattern;
        same &= a.size() == b.size(); for (size_t j = 0; same && j < a.size(); ++j) same &= a[j].code == b[j].code && a[j].gate == b[j].gate && a[j].accent == b[j].accent && a[j].slide == b[j].slide; }
    check(same, "state keeps all 48 slots");
    // web app project format both ways
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("tb303-host-test.json");
    proc.exportProject(tmp);
    TB303Processor third; const auto msg = third.importFile(tmp);
    std::printf("import: %s\n", msg.toRawUTF8());
    check(third.seq.chain.joinIntoString(",") == "B3,B6" && std::fabs(third.partValue(tb303::P::R52) / r52 - 1) < 1e-3, "export / import project");
    tmp.deleteFile();
    third.removeUserPreset("tb303-host-test");
    std::printf("auto names: %s, %s\n", tb303::autoName({}).toRawUTF8(), tb303::autoName({}).toRawUTF8());
    std::printf(ok ? "OK\n" : "FAILED\n");
    return ok ? 0 : 1;
}
