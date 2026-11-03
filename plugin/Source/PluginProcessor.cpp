#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace tb303;

const std::vector<ModInfo>& frontMods() {
    static const std::vector<ModInfo> m = {
        { "R_SOFT", "Soft attack", "fade-in on normal notes" },
        { "C35", "Slide time", "how long glides take" },
        { "R138", "Short decay", "fastest and accent decay" },
        { "R52", "Note length", "how long the VCA rings" },
        { "C_ACC", "Accent sweep", "how accents build up" },
        { "R72", "Env depth", "size of the filter sweep" },
        { "V_CUT", "Cutoff range", "how far the cutoff reaches" },
        { "A_Q21", "Reso limit", "up to self-oscillation" },
        { "R_IN", "Drive", "push the filter harder" },
        { "C18", "Ladder cap", "resonance character" },
        { "WOW", "Accent wow", "swell on accents at high reso" },
        { "NOISE", "Noise", "hiss of the old parts" },
        { "I_REF", "Filter trim", "TM3: cutoff calibration" },
    };
    return m;
}

static bool isModPart(int part) {
    for (auto& m : frontMods()) if (partIndex(m.part) == part) return true;
    return false;
}
// parts the web app never lets you change (pot values that only set a taper)
static bool isFixedPart(int part) { return part == P::VR2 || part == P::VR4 || part == P::VR5 || part == P::VR7; }

static const std::vector<Step>& examplePattern() {
    static const std::vector<Step> p = [] {
        const int rows[16][4] = { { 21, 1, 0, 1 }, { 21, 0, 0, 1 }, { 33, 0, 1, 1 }, { 31, 0, 0, 1 }, { 21, 1, 0, 1 }, { 21, 0, 0, 0 }, { 24, 0, 1, 1 }, { 26, 0, 0, 1 },
                                  { 21, 0, 0, 1 }, { 33, 1, 0, 1 }, { 28, 0, 1, 1 }, { 24, 0, 0, 1 }, { 21, 1, 1, 1 }, { 19, 0, 0, 1 }, { 21, 0, 0, 0 }, { 36, 1, 0, 1 } };
        std::vector<Step> v;
        for (auto& r : rows) v.push_back({ r[0], r[3] != 0, r[1] != 0, r[2] != 0 });
        return v;
    }();
    return p;
}

juce::String TB303Processor::formatPart(double v, const juce::String& unit) {
    auto sig = [](double x, int digits) {
        if (x == 0) return juce::String("0");
        const int mag = (int)std::floor(std::log10(std::fabs(x)));
        const int dec = juce::jmax(0, digits - 1 - mag);
        // 3 significant figures (toPrecision); String(x, 0) would print every digit
        const double q = std::pow(10.0, dec), rounded = std::round(x * q) / q;
        juce::String s = dec == 0 ? juce::String((juce::int64)std::llround(rounded)) : juce::String(rounded, dec);
        if (s.containsChar('.')) { s = s.trimCharactersAtEnd("0"); s = s.trimCharactersAtEnd("."); }
        return s;
    };
    if (unit == "×" || unit == "Hz" || unit == "V") return sig(v, 4) + " " + unit;
    static const std::pair<double, const char*> pfx[] = { { 1e6, "M" }, { 1e3, "k" }, { 1, "" }, { 1e-3, "m" }, { 1e-6, "µ" }, { 1e-9, "n" }, { 1e-12, "p" } };
    for (auto& [m, p] : pfx) if (std::fabs(v) >= m * 0.9999) return sig(v / m, 3) + " " + juce::String::fromUTF8(p) + unit;
    return juce::String(v, 2, true) + " " + unit;
}

static juce::String groupName(const char* g) {
    const juce::String s(g);
    if (s == "rat") return "RAT ";
    if (s == "lex") return "224 ";
    if (s == "dly") return "Delay ";
    if (s == "cmp") return "Compactor ";
    return {};
}

juce::AudioProcessorValueTreeState::ParameterLayout TB303Processor::createLayout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout L;
    const auto pct = [](float v, int) { return String(juce::roundToInt(v * 100)) + "%"; };
    for (int k = 0; k < K::COUNT; ++k) {
        const auto& info = knobs()[k];
        const String id = info.id, name = groupName(info.group) + info.name;
        if (k == K::lexprog) {
            StringArray names; for (auto& p : lexPrograms()) names.add("P" + String(p.n) + " " + p.shortName);
            L.add(std::make_unique<AudioParameterChoice>(ParameterID { id, 1 }, name, names, 1));
        } else if (k == K::dlytime) {
            StringArray names; for (auto* n : DLY_NAMES) names.add(n);
            L.add(std::make_unique<AudioParameterChoice>(ParameterID { id, 1 }, name, names, 7));
        } else if (k == K::cmpmode) {
            L.add(std::make_unique<AudioParameterChoice>(ParameterID { id, 1 }, name, StringArray { "Peak", "ISP", "RMS" }, 0));
        } else if (k == K::dlypp) {
            L.add(std::make_unique<AudioParameterBool>(ParameterID { id, 1 }, name, true));
        } else {
            std::function<String(float, int)> txt = pct;
            switch (k) {
            case K::lexbass: case K::lexmid: txt = [](float v, int) { return String(0.6 * std::pow(70 / 0.6, v), 1) + " s"; }; break;
            case K::lexxover: case K::lextreble: txt = [](float v, int) { return String(juce::roundToInt(100 * std::pow(109.0, v))) + " Hz"; }; break;
            case K::lexdepth: txt = [](float v, int) { return String(juce::roundToInt(v * 71)); }; break;
            case K::lexdiff: txt = [](float v, int) { return String(juce::roundToInt(v * 63)); }; break;
            case K::lexme: txt = [](float v, int) { return v < 0.03f ? String("Off") : String(17 - juce::jlimit(1, 16, (int)std::ceil(v * 16))); }; break;
            case K::lexpitch: txt = [](float v, int) { return String(1 + juce::roundToInt(v * 15)); }; break;
            case K::dlyfb: txt = [](float v, int) { return String(juce::roundToInt(95 * v)) + "%"; }; break;
            case K::dlylo: txt = [](float v, int) { return String(juce::roundToInt(dlyLo(v))) + " Hz"; }; break;
            case K::dlyhi: txt = [](float v, int) { return String(dlyHi(v) / 1000, 1) + " kHz"; }; break;
            case K::cmpthr: txt = [](float v, int) { return String(cmpThrDb(v), 1) + " dB"; }; break;
            case K::cmprng: txt = [](float v, int) { return String(juce::roundToInt(200 * v)) + "%"; }; break;
            case K::cmpatk: txt = [](float v, int) { return String(cmpAtkMs(v), 1) + " ms"; }; break;
            case K::cmphold: txt = [](float v, int) { return String(juce::roundToInt(cmpHoldMs(v))) + " ms"; }; break;
            case K::cmprel: txt = [](float v, int) { return String(juce::roundToInt(cmpRelMs(v))) + " ms"; }; break;
            default: break;
            }
            L.add(std::make_unique<AudioParameterFloat>(ParameterID { id, 1 }, name, NormalisableRange<float>(0, 1), (float)info.def,
                AudioParameterFloatAttributes().withStringFromValueFunction(txt)));
        }
    }
    for (auto& m : frontMods()) {
        const int pi = partIndex(m.part);
        const String unit = String::fromUTF8(parts()[pi].unit);
        L.add(std::make_unique<AudioParameterFloat>(ParameterID { String("m_") + m.part, 1 }, String("Mod ") + m.name, NormalisableRange<float>(0, 1),
            (float)partToT(pi, parts()[pi].v),
            AudioParameterFloatAttributes().withStringFromValueFunction([pi, unit](float t, int) { return formatPart(partFromT(pi, t), unit); })));
    }
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "wave", 1 }, "Waveform", StringArray { "Saw", "Square" }, 0));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "fx_rat", 1 }, "RAT on", false));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "fx_dly", 1 }, "Delay on", false));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "fx_lex", 1 }, "224 on", false));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "declick", 1 }, "De-click", true));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "seq", 1 }, "Sequencer", true));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "run", 1 }, "Play (free run)", false));
    L.add(std::make_unique<AudioParameterFloat>(ParameterID { "tempo", 1 }, "Tempo (free run)", NormalisableRange<float>(60, 200, 1), 128.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return String(juce::roundToInt(v)) + " BPM"; })));
    L.add(std::make_unique<AudioParameterFloat>(ParameterID { "gate", 1 }, "Gate length", NormalisableRange<float>(0.1f, 0.95f, 0.01f), 0.5f,
        AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return String(juce::roundToInt(v * 100)) + "%"; })));
    return L;
}

TB303Processor::TB303Processor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "TB303", createLayout()) {
    for (int k = 0; k < K::COUNT; ++k) knobParams[(size_t)k] = apvts.getRawParameterValue(knobs()[k].id);
    for (auto& m : frontMods()) modParams.push_back({ partIndex(m.part), apvts.getRawParameterValue(juce::String("m_") + m.part) });
    lastMod.assign(modParams.size(), -1.0f);
    pWave = apvts.getRawParameterValue("wave"); pRat = apvts.getRawParameterValue("fx_rat"); pLex = apvts.getRawParameterValue("fx_lex");
    pDly = apvts.getRawParameterValue("fx_dly"); pDeclick = apvts.getRawParameterValue("declick"); pSeq = apvts.getRawParameterValue("seq");
    pRun = apvts.getRawParameterValue("run"); pTempo = apvts.getRawParameterValue("tempo"); pGate = apvts.getRawParameterValue("gate");
    apvts.addParameterListener("declick", this);
    for (int i = 0; i < P::COUNT; ++i) extraParts[(size_t)i] = parts()[i].v;
    pattern = examplePattern();
    pattern.reserve(64);
    audioPattern.reserve(64);
    held.reserve(128);
    loadUserPresets();
}

TB303Processor::~TB303Processor() { cancelPendingUpdate(); apvts.removeParameterListener("declick", this); }

bool TB303Processor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

double TB303Processor::knobValue(int k) const {
    const double raw = knobParams[(size_t)k]->load();
    if (k == K::lexprog) return raw / 8.0;
    if (k == K::dlytime) return raw / 10.0;
    if (k == K::cmpmode) return raw / 2.0;
    return raw;
}

void TB303Processor::prepareToPlay(double sampleRate, int) {
    fs = sampleRate;
    engine = std::make_unique<Engine>(sampleRate, juce::Random::getSystemRandom().nextInt() | 1);
    lastKnob.fill(-1.0f);
    std::fill(lastMod.begin(), lastMod.end(), -1.0f);
    partsSeen = -1; patternSeen = -1; sentParts.fill(-1.0);
    held.clear(); hostSynced = false;
    engine->dc.setOn(*pDeclick > 0.5f);
    lastDeclick = *pDeclick > 0.5f;
    pushAllParams(true);
    setLatencySamples(engine->dc.latency());
}

void TB303Processor::parameterChanged(const juce::String& id, float) {
    if (id == "declick") triggerAsyncUpdate();
}

// de-click delays the output 3 ms; tell the host so it can compensate
void TB303Processor::handleAsyncUpdate() {
    const int D = juce::jmax(1, (int)jsRound(3e-3 * fs));
    setLatencySamples(*pDeclick > 0.5f ? D : 0);
}

// parameters and preset parts into the engine; instant on (re)start, otherwise through the slew
void TB303Processor::pushAllParams(bool instant) {
    auto& e = *engine;
    auto setK = [&](int k, double v) { if (instant) e.v.setKnob(k, v); else e.slew.set(true, k, v); };
    auto setP = [&](int p, double v) { if (instant) e.v.setComponent(p, v); else e.slew.set(false, p, v); };
    for (int k = 0; k < K::COUNT; ++k) {
        const float raw = knobParams[(size_t)k]->load();
        if (raw != lastKnob[(size_t)k]) { lastKnob[(size_t)k] = raw; setK(k, knobValue(k)); }
    }
    for (size_t i = 0; i < modParams.size(); ++i) {
        const float t = modParams[i].second->load();
        if (t != lastMod[i]) { lastMod[i] = t; setP(modParams[i].first, partFromT(modParams[i].first, t)); }
    }
    const int pv = partsVersion.load();
    if (pv != partsSeen) {
        const juce::SpinLock::ScopedTryLockType lock(partsLock);
        if (lock.isLocked()) {
            partsSeen = pv;
            for (int p = 0; p < P::COUNT; ++p)
                if (!isModPart(p) && extraParts[(size_t)p] != sentParts[(size_t)p]) { sentParts[(size_t)p] = extraParts[(size_t)p]; setP(p, extraParts[(size_t)p]); }
        }
    }
    e.v.square = *pWave > 0.5f;
    e.v.fxRat = *pRat > 0.5f; e.v.fxLex = *pLex > 0.5f; e.v.fxDly = *pDly > 0.5f;
    const bool dc = *pDeclick > 0.5f;
    if (dc != lastDeclick) { lastDeclick = dc; e.dc.setOn(dc); }
    const int patv = patternVersion.load();
    if (patv != patternSeen) {
        const juce::SpinLock::ScopedTryLockType lock(patternLock);
        if (lock.isLocked()) { patternSeen = patv; audioPattern.assign(pattern.begin(), pattern.end()); e.pattern.assign(pattern.begin(), pattern.end()); }
    }
    e.shuffle = shuffle.load();
    e.gateLen = *pGate;
}

void TB303Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const int N = buffer.getNumSamples();
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear(ch, 0, N);
    if (!engine || N == 0) { buffer.clear(); return; }
    float* L = buffer.getWritePointer(0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    auto& e = *engine;

    pushAllParams(false);

    // transport
    bool hostPlay = false; double ppq = 0, bpm = *pTempo;
    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            if (auto b = pos->getBpm()) bpm = juce::jlimit(20.0, 400.0, *b);
            if (auto q = pos->getPpqPosition(); q && pos->getIsPlaying()) { hostPlay = true; ppq = *q; }
        }
    }
    if (bpm != e.bpm) e.setBpm(bpm);
    const bool seq = *pSeq > 0.5f;

    struct Ev { int pos; int type; int code; bool accent, slide; int step; }; // type 0 = off, 1 = on, 2 = rest
    Ev evs[512]; int nev = 0;
    auto add = [&](Ev ev) { if (nev < 512) evs[nev++] = ev; };

    int syncStep = -1;
    if (seq && hostPlay) {
        if (e.playing) e.setPlaying(false);
        hostSynced = true;
        const int n = (int)audioPattern.size();
        if (n > 0) {
            const double spq = fs * 60.0 / bpm, ppq1 = ppq + N / spq, sh = e.shuffle, gl = e.gateLen;
            const int64_t i0 = (int64_t)std::floor(ppq * 4) - 1, i1 = (int64_t)std::floor(ppq1 * 4);
            for (int64_t i = i0; i <= i1; ++i) {
                const bool odd = (i & 1) != 0;
                const double start = i / 4.0 + (odd ? sh / 4 : 0), len = (odd ? 1 - sh : 1 + sh) / 4;
                const int idx = (int)(((i % n) + n) % n);
                const Step& s = audioPattern[(size_t)idx]; const Step& prev = audioPattern[(size_t)((idx - 1 + n) % n)];
                if (start >= ppq && start < ppq1) {
                    const int pos = juce::jlimit(0, N - 1, (int)((start - ppq) * spq));
                    if (s.gate) add({ pos, 1, s.code, s.accent, prev.gate && prev.slide, idx });
                    else add({ pos, 2, 0, false, false, idx });
                }
                if (s.gate && !s.slide) {
                    const double off = start + len * gl;
                    if (off >= ppq && off < ppq1) add({ juce::jlimit(0, N - 1, (int)((off - ppq) * spq)), 0, 0, false, false, -1 });
                }
                if (start < ppq1) syncStep = idx;
            }
        }
    } else {
        if (hostSynced) { hostSynced = false; e.noteOff(); }
        const bool free = seq && *pRun > 0.5f;
        if (free != e.playing) e.setPlaying(free);
    }

    // MIDI: mono, last note priority; a note played while another is held slides (legato)
    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        const int pos = juce::jlimit(0, N - 1, meta.samplePosition);
        if (m.isNoteOn()) {
            const int note = m.getNoteNumber();
            const bool accent = m.getVelocity() >= 100;
            const bool slide = !held.empty();
            held.erase(std::remove(held.begin(), held.end(), note), held.end());
            if (held.size() < 127) held.push_back(note);
            if (!slide) midiAccent = accent;
            add({ pos, 3, note - 12, accent, slide, -1 });
        } else if (m.isNoteOff()) {
            const int note = m.getNoteNumber();
            const bool wasLast = !held.empty() && held.back() == note;
            held.erase(std::remove(held.begin(), held.end(), note), held.end());
            if (held.empty()) add({ pos, 0, 0, false, false, -2 });
            else if (wasLast) add({ pos, 3, held.back() - 12, midiAccent, true, -1 });
        } else if (m.isAllNotesOff() || m.isAllSoundOff()) {
            held.clear(); add({ pos, 0, 0, false, false, -2 });
        }
    }
    std::stable_sort(evs, evs + nev, [](const Ev& a, const Ev& b) { return a.pos != b.pos ? a.pos < b.pos : (a.type == 0) > (b.type == 0); });

    int i = 0;
    for (int k = 0; k < nev; ++k) {
        const Ev& ev = evs[k];
        if (ev.pos > i) { e.render(L, R, i, ev.pos); i = ev.pos; }
        if (ev.type == 1 || ev.type == 3) e.noteOn(ev.code, ev.accent, ev.slide);
        else e.noteOff();
    }
    if (i < N) e.render(L, R, i, N);
    if (!R && buffer.getNumChannels() > 1) buffer.copyFrom(1, 0, buffer, 0, 0, N);

    if (hostSynced) currentStep = syncStep;
    else currentStep = e.playing ? e.step : -1;
    seqRunning = hostSynced || e.playing;
}

// ---------------------------------------------------------------------------------------------------
std::vector<Step> TB303Processor::getPattern() const {
    const juce::SpinLock::ScopedLockType lock(patternLock);
    return pattern;
}
void TB303Processor::setPattern(const std::vector<Step>& p) {
    {
        const juce::SpinLock::ScopedLockType lock(patternLock);
        pattern.assign(p.begin(), p.begin() + (long)std::min<size_t>(p.size(), 64));
        if (pattern.empty()) pattern.push_back({ 24, false, false, false });
    }
    ++patternVersion;
}

double TB303Processor::partValue(int part) const {
    for (auto& m : modParams) if (m.first == part) return partFromT(part, m.second->load());
    const juce::SpinLock::ScopedLockType lock(partsLock);
    return extraParts[(size_t)part];
}

// ---------------------------------------------------------------------------------------------------
// web-app JSON. Same rules as cleanSettings() in state.js.
static void setParam(juce::AudioProcessorValueTreeState& s, const juce::String& id, float plain) {
    if (auto* p = s.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(plain));
}

void TB303Processor::applySound(const juce::var& x) {
    const auto* kn = x.getProperty("knobs", {}).getDynamicObject();
    for (int k = 0; k < K::COUNT; ++k) {
        double v = knobs()[k].def;
        if (kn) { const auto j = kn->getProperty(knobs()[k].id); if ((j.isDouble() || j.isInt() || j.isInt64()) && (double)j >= 0 && (double)j <= 1) v = j; }
        float plain = (float)v;
        if (k == K::lexprog) plain = (float)jsRound(v * 8);
        else if (k == K::dlytime) plain = (float)dlyDivIndex(v);
        else if (k == K::cmpmode) plain = (float)jsRound(v * 2);
        else if (k == K::dlypp) plain = v >= 0.5 ? 1.0f : 0.0f;
        setParam(apvts, knobs()[k].id, plain);
    }
    const auto* pa = x.getProperty("parts", {}).getDynamicObject();
    {
        const juce::SpinLock::ScopedLockType lock(partsLock);
        for (int p = 0; p < P::COUNT; ++p) {
            double v = parts()[p].v;
            if (pa && !isFixedPart(p)) {
                const auto j = pa->getProperty(parts()[p].id);
                if ((j.isDouble() || j.isInt() || j.isInt64()) && std::isfinite((double)j) && (double)j >= 0) v = j;
                if (p == P::NOISE) v = std::min(v, 0.15);
            }
            extraParts[(size_t)p] = v;
        }
    }
    ++partsVersion;
    for (auto& m : frontMods()) { const int p = partIndex(m.part); setParam(apvts, juce::String("m_") + m.part, (float)partToT(p, extraParts[(size_t)p])); }
    setParam(apvts, "wave", x.getProperty("wave", "saw").toString() == "square" ? 1.0f : 0.0f);
    const auto fx = x.getProperty("fx", {});
    setParam(apvts, "fx_rat", (bool)fx.getProperty("rat", false) ? 1.0f : 0.0f);
    setParam(apvts, "fx_lex", (bool)fx.getProperty("lex", false) ? 1.0f : 0.0f);
    setParam(apvts, "fx_dly", (bool)fx.getProperty("dly", false) ? 1.0f : 0.0f);
}

void TB303Processor::applyPatternJson(const juce::var& x) {
    const auto* arr = x.getProperty("pattern", {}).getArray();
    if (arr && !arr->isEmpty() && arr->size() <= 64) {
        std::vector<Step> p;
        for (auto& st : *arr) p.push_back({ juce::jlimit(0, 59, (int)st.getProperty("code", 24)), (bool)st.getProperty("gate", false),
                                            (bool)st.getProperty("accent", false), (bool)st.getProperty("slide", false) });
        setPattern(p);
    }
    const auto g = x.getProperty("gate", {});
    if (g.isDouble() || g.isInt()) setParam(apvts, "gate", juce::jlimit(0.1f, 0.95f, (float)(double)g));
    const auto sh = x.getProperty("shuffle", {});
    if (sh.isDouble() || sh.isInt()) shuffle = juce::jlimit(0.0, 0.5, (double)sh);
}

void TB303Processor::applyPreset(const juce::var& x) {
    const auto kind = x.getProperty("kind", "").toString();
    if (kind == "pattern") { applyPatternJson(x.getProperty("pattern", {})); return; }
    if (kind == "sound") { applySound(x.getProperty("sound", {})); return; }
    applySound(x.getProperty("sound", {}));
    // a project: the pattern in its selected bank slot (or the first one there is)
    const auto banks = x.getProperty("banks", {});
    if (auto* b = banks.getDynamicObject()) {
        const auto sel = x.getProperty("sel", {});
        const juce::String key = sel.getProperty("g", "A").toString() + juce::String((int)sel.getProperty("n", 0) + 1);
        juce::var slot = b->getProperty(key);
        if (!slot.isObject()) for (auto& kv : b->getProperties()) if (kv.value.isObject()) { slot = kv.value; break; }
        if (slot.isObject()) applyPatternJson(slot);
    }
}

juce::var TB303Processor::soundSnapshot() const {
    auto* s = new juce::DynamicObject();
    auto* kn = new juce::DynamicObject();
    for (int k = 0; k < K::COUNT; ++k) kn->setProperty(knobs()[k].id, knobValue(k));
    s->setProperty("knobs", juce::var(kn));
    auto* pa = new juce::DynamicObject();
    for (int p = 0; p < P::COUNT; ++p) {
        if (isFixedPart(p)) continue;
        const double v = partValue(p), d = parts()[p].v;
        if (std::fabs(v - d) > (d != 0 ? 1e-4 * std::fabs(d) : 1e-9)) pa->setProperty(parts()[p].id, juce::String(v, 3, true).getDoubleValue());
    }
    s->setProperty("parts", juce::var(pa));
    s->setProperty("wave", *pWave > 0.5f ? "square" : "saw");
    auto* fx = new juce::DynamicObject();
    fx->setProperty("rat", *pRat > 0.5f); fx->setProperty("lex", *pLex > 0.5f); fx->setProperty("dly", *pDly > 0.5f);
    s->setProperty("fx", juce::var(fx));
    return juce::var(s);
}

juce::var TB303Processor::projectSnapshot() const {
    auto* pr = new juce::DynamicObject();
    pr->setProperty("sound", soundSnapshot());
    pr->setProperty("bpm", juce::roundToInt(pTempo->load()));
    auto* sel = new juce::DynamicObject(); sel->setProperty("g", "A"); sel->setProperty("n", 0);
    pr->setProperty("sel", juce::var(sel));
    pr->setProperty("chain", juce::Array<juce::var>());
    auto* slot = new juce::DynamicObject();
    slot->setProperty("shuffle", shuffle.load()); slot->setProperty("gate", (double)pGate->load());
    juce::Array<juce::var> steps;
    for (auto& st : getPattern()) {
        auto* o = new juce::DynamicObject();
        o->setProperty("code", st.code); o->setProperty("gate", st.gate); o->setProperty("accent", st.accent); o->setProperty("slide", st.slide);
        steps.add(juce::var(o));
    }
    slot->setProperty("pattern", steps);
    auto* banks = new juce::DynamicObject(); banks->setProperty("A1", juce::var(slot));
    pr->setProperty("banks", juce::var(banks));
    return juce::var(pr);
}

juce::String TB303Processor::importFile(const juce::File& f) {
    const auto j = juce::JSON::parse(f.loadFileAsString());
    if (!j.isObject()) return "That file is not JSON from the web app.";
    const auto name = f.getFileNameWithoutExtension().substring(0, 40);
    // favourites file, or Export all (settings) file
    int added = 0;
    auto addAll = [&](const juce::var& list, const juce::String& kind) {
        if (auto* o = list.getDynamicObject())
            for (auto& kv : o->getProperties()) {
                if (!kv.value.isObject()) continue;
                juce::var v = kv.value;
                if (kind.isNotEmpty()) { auto* w = new juce::DynamicObject(); w->setProperty("kind", kind); w->setProperty(kind, kv.value); v = juce::var(w); }
                addUserPreset(kv.name.toString().trim().substring(0, 40), v); ++added;
            }
    };
    addAll(j.getProperty("projectPresets", {}), {});
    addAll(j.getProperty("soundPresets", {}), "sound");
    addAll(j.getProperty("patternPresets", {}), "pattern");
    auto project = j.getProperty("project", {});
    if (!project.isObject()) project = j.getProperty("current", {}).getProperty("project", {});
    if (project.isObject()) {
        applyPreset(project);
        addUserPreset(name, project);
        lastPresetName = name;
        return "Loaded " + name + (added ? " and added " + juce::String(added) + " presets" : juce::String()) + ".";
    }
    if (added) return "Added " + juce::String(added) + " preset" + (added == 1 ? "" : "s") + " to the menu.";
    if (j.getProperty("pattern", {}).isArray()) { applyPatternJson(j); return "Loaded the pattern."; }
    return "That file is not a preset export from the web app.";
}

void TB303Processor::exportFile(const juce::File& f) const {
    auto* o = new juce::DynamicObject();
    o->setProperty("app", "tb303-circuit-model"); o->setProperty("kind", "project"); o->setProperty("version", 1);
    o->setProperty("project", projectSnapshot());
    f.replaceWithText(juce::JSON::toString(juce::var(o)));
}

// ---------------------------------------------------------------------------------------------------
juce::File TB303Processor::userPresetFile() {
#if JUCE_MAC
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Application Support/TB303 Circuit Model/presets.json");
#else
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("TB303 Circuit Model/presets.json");
#endif
}
void TB303Processor::loadUserPresets() {
    userPresets = juce::JSON::parse(userPresetFile().loadFileAsString());
    if (!userPresets.isObject()) userPresets = juce::var(new juce::DynamicObject());
}
void TB303Processor::saveUserPresets() const {
    auto f = userPresetFile(); f.getParentDirectory().createDirectory();
    f.replaceWithText(juce::JSON::toString(userPresets));
}
juce::StringArray TB303Processor::userPresetNames() const {
    juce::StringArray s;
    if (auto* o = userPresets.getDynamicObject()) for (auto& kv : o->getProperties()) s.add(kv.name.toString());
    s.sortNatural();
    return s;
}
juce::var TB303Processor::userPreset(const juce::String& n) const { return userPresets.getProperty(n, {}); }
void TB303Processor::addUserPreset(const juce::String& n, const juce::var& p) {
    if (n.isEmpty()) return;
    loadUserPresets();
    userPresets.getDynamicObject()->setProperty(n, p);
    saveUserPresets();
}
void TB303Processor::removeUserPreset(const juce::String& n) {
    loadUserPresets();
    userPresets.getDynamicObject()->removeProperty(n);
    saveUserPresets();
}

// ---------------------------------------------------------------------------------------------------
void TB303Processor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = apvts.copyState();
    juce::ValueTree extra("Extra");
    juce::StringArray steps;
    for (auto& s : getPattern()) steps.add(juce::String(s.code) + "," + (s.gate ? "1" : "0") + (s.accent ? "1" : "0") + (s.slide ? "1" : "0"));
    extra.setProperty("pattern", steps.joinIntoString(";"), nullptr);
    extra.setProperty("shuffle", shuffle.load(), nullptr);
    extra.setProperty("preset", lastPresetName, nullptr);
    juce::String ps;
    {
        const juce::SpinLock::ScopedLockType lock(partsLock);
        for (int p = 0; p < P::COUNT; ++p)
            if (!isModPart(p) && extraParts[(size_t)p] != parts()[p].v) ps << parts()[p].id << "=" << juce::String(extraParts[(size_t)p], 9, true) << ";";
    }
    extra.setProperty("parts", ps, nullptr);
    state.removeChild(state.getChildWithName("Extra"), nullptr);
    state.appendChild(extra, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void TB303Processor::setStateInformation(const void* data, int size) {
    auto xml = getXmlFromBinary(data, size);
    if (!xml) return;
    auto state = juce::ValueTree::fromXml(*xml);
    if (!state.hasType(apvts.state.getType())) return;
    const auto extra = state.getChildWithName("Extra");
    apvts.replaceState(state);
    if (extra.isValid()) {
        std::vector<Step> p;
        for (auto& s : juce::StringArray::fromTokens(extra.getProperty("pattern").toString(), ";", "")) {
            const auto code = s.upToFirstOccurrenceOf(",", false, false).getIntValue();
            const auto f = s.fromFirstOccurrenceOf(",", false, false);
            if (f.length() >= 3) p.push_back({ juce::jlimit(0, 63, code), f[0] == '1', f[1] == '1', f[2] == '1' });
        }
        if (!p.empty()) setPattern(p);
        shuffle = (double)extra.getProperty("shuffle", 0.0);
        lastPresetName = extra.getProperty("preset").toString();
        {
            const juce::SpinLock::ScopedLockType lock(partsLock);
            for (int i = 0; i < P::COUNT; ++i) extraParts[(size_t)i] = parts()[i].v;
            for (auto& kv : juce::StringArray::fromTokens(extra.getProperty("parts").toString(), ";", "")) {
                const int i = partIndex(kv.upToFirstOccurrenceOf("=", false, false).toStdString());
                if (i >= 0 && !isModPart(i)) extraParts[(size_t)i] = kv.fromFirstOccurrenceOf("=", false, false).getDoubleValue();
            }
        }
        ++partsVersion;
    }
}

juce::AudioProcessorEditor* TB303Processor::createEditor() { return new TB303Editor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TB303Processor(); }
