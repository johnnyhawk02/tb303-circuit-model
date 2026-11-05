// Drives the plugin's processor like a host would: transport playing at 128 BPM, then MIDI notes.
// Writes host_test.wav and checks that notes land on the 16th-note grid, then the page's messages and the saved state.
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

    // messages from the page, and the state the host saves
    auto check = [&](bool c, const char* what) { if (!c) { std::printf("FAILED: %s\n", what); ok = false; } };
    auto msg = [](const char* json) { return juce::JSON::parse(juce::String(json)); };
    proc.handleMessage(msg(R"({"type":"knob","name":"cutoff","value":0.42})"));
    proc.handleMessage(msg(R"({"type":"knob","name":"revtype","value":0.6666666666666666})"));
    proc.handleMessage(msg(R"({"type":"component","name":"R52","value":150000})"));   // a mod (host parameter)
    proc.handleMessage(msg(R"({"type":"component","name":"C33","value":1.2e-8})"));   // a part that is not a parameter
    proc.handleMessage(msg(R"({"type":"fx","name":"flg","value":true})"));
    proc.handleMessage(msg(R"({"type":"wave","value":"square"})"));
    proc.handleMessage(msg(R"({"type":"tune","value":[0,0,0,0,0,0,0,0,0,-13.7,0,0]})"));
    proc.handleMessage(msg(R"({"type":"pattern","value":[{"code":33,"gate":true,"accent":true,"slide":false},{"code":45,"gate":true,"accent":false,"slide":true},{"code":21,"gate":false,"accent":false,"slide":false}]})"));
    proc.handleMessage(msg(R"({"type":"store","key":"tb303.banks","value":"{\"A1\":{}}"})"));
    check(std::fabs(proc.knobValue(tb303::K::cutoff) - 0.42) < 1e-6, "knob");
    check(std::fabs(proc.knobValue(tb303::K::revtype) - 2.0 / 3) < 1e-9, "stepped knob");
    check(std::fabs(proc.partValue(tb303::P::R52) / 150000 - 1) < 1e-5, "mod part");
    check(proc.pattern().size() == 3 && proc.pattern()[1].slide, "pattern");
    juce::Array<juce::var> changes; proc.collectChanges(changes);
    check(changes.isEmpty(), "the page's own changes are not sent back");
    proc.apvts.getParameter("cutoff")->setValueNotifyingHost(0.9f); // host automation
    proc.collectChanges(changes);
    check(changes.size() == 1 && changes[0].getProperty("id", {}).toString() == "cutoff", "automation goes to the page");
    juce::MemoryBlock state; proc.getStateInformation(state);
    TB303Processor other; other.setStateInformation(state.getData(), (int)state.getSize());
    check(std::fabs(other.partValue(tb303::P::C33) / 1.2e-8 - 1) < 1e-9 && std::fabs(other.partValue(tb303::P::R52) / 150000 - 1) < 1e-5, "state keeps parts");
    check(other.pattern().size() == 3 && other.pattern()[0].accent && !other.pattern()[2].gate, "state keeps the pattern");
    check(other.projectStore()["tb303.banks"] == "{\"A1\":{}}", "state keeps the page's project keys");
    const auto boot = other.bootScript();
    const auto bootJson = juce::JSON::parse(boot.fromFirstOccurrenceOf("=", false, false).upToLastOccurrenceOf(";</script>", false, false));
    const auto sess = juce::JSON::parse(bootJson.getProperty("store", {}).getProperty("tb303.session", {}).toString());
    check(std::fabs((double)sess.getProperty("parts", {}).getProperty("C33", 0) / 1.2e-8 - 1) < 1e-9, "boot session has the parts");
    check((bool)sess.getProperty("fx", {}).getProperty("flg", false) && sess.getProperty("wave", {}).toString() == "square"
          && std::fabs((double)sess.getProperty("tune", {})[9] + 13.7) < 1e-9, "boot session has the sound");
    check((double)sess.getProperty("knobs", {}).getProperty("resonance", 0) == 0.82, "untouched dials keep their exact default");
    if (!ok) std::printf("%s\n", boot.substring(0, 1500).toRawUTF8());
    std::printf(ok ? "OK\n" : "FAILED\n");
    return ok ? 0 : 1;
}
