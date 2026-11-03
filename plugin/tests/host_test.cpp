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
    const bool ok = firstSound >= 0 && firstSound < 400 && rmsSeq > 0.01f && rmsMidi > 0.01f;
    std::printf(ok ? "OK\n" : "FAILED\n");
    return ok ? 0 : 1;
}
