#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PartsMeta.h"
#include "BinaryData.h"

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
        { "KT", "Key track", "higher notes open the filter" },
        { "A_Q21", "Reso limit", "up to self-oscillation" },
        { "R_IN", "Drive", "push the filter harder" },
        { "C18", "Ladder cap", "resonance character" },
        { "WOW", "Accent wow", "swell on accents at high reso" },
        { "NOISE", "Noise", "hiss of the old parts" },
        { "I_REF", "Filter trim", "TM3: cutoff calibration" },
    };
    return m;
}

// the recorded impulse responses for the Conv reverb (irs/*.flac, embedded), decoded once
struct ConvIR { juce::String id; double rate; juce::AudioBuffer<float> data; };
static const std::vector<ConvIR>& convIRs() {
    static const std::vector<ConvIR> v = [] {
        std::vector<ConvIR> out;
        juce::FlacAudioFormat flac;
        for (auto& P_ : cnvPrograms()) {
            if (!P_.ir) continue;
            int size = 0;
            const juce::String res = juce::String(P_.ir) + "_flac";
            const char* d = BinaryData::getNamedResource(res.toRawUTF8(), size);
            if (!d || size <= 0) continue;
            std::unique_ptr<juce::AudioFormatReader> r(flac.createReaderFor(new juce::MemoryInputStream(d, (size_t)size, false), true));
            if (!r || r->lengthInSamples <= 0) continue;
            ConvIR ir { P_.ir, r->sampleRate, juce::AudioBuffer<float>((int)r->numChannels, (int)r->lengthInSamples) };
            r->read(&ir.data, 0, (int)r->lengthInSamples, 0, true, true);
            out.push_back(std::move(ir));
        }
        return out;
    }();
    return v;
}

bool isModPart(int part) {
    for (auto& m : frontMods()) if (partIndex(m.part) == part) return true;
    return false;
}
// parts the web app never lets you change (pot values that only set a taper)
bool isFixedPart(int part) { return part >= 0 && part < PARTS_META_COUNT && PARTS_META[part].fixed; }

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
    if (unit == "%") return juce::String((juce::int64)std::llround(v * 100)) + " %";
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
    if (s == "plate") return "Plate ";
    if (s == "spring") return "Spring ";
    if (s == "conv") return "Conv ";
    if (s == "drv") return "Drive ";
    if (s == "ds") return "DS-1 ";
    if (s == "ts") return "TS808 ";
    if (s == "fold") return "Folder ";
    return {};
}

// the reverb slot's stepped knobs: how many positions each has (0 = a plain dial)
static int revSteps(int k) {
    switch (k) {
    case K::revtype: return 4;
    case K::drvtype: return 3;
    case K::plateprog: return (int)platePrograms().size();
    case K::sprprog: return (int)springPrograms().size();
    case K::cnvprog: return (int)cnvPrograms().size();
    default: return 0;
    }
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
        } else if (const int n = revSteps(k)) {
            StringArray names;
            if (k == K::revtype) names = { "Lexicon 224", "Plate", "Spring", "Convolution" };
            else if (k == K::drvtype) names = { "ProCo RAT", "Boss DS-1", "Ibanez TS808" };
            else if (k == K::plateprog) for (auto& p : platePrograms()) names.add(p.shortName);
            else if (k == K::sprprog) for (auto& p : springPrograms()) names.add(p.shortName);
            else for (auto& p : cnvPrograms()) names.add(p.shortName);
            L.add(std::make_unique<AudioParameterChoice>(ParameterID { id, 1 }, name, names, (int)jsRound(info.def * (n - 1))));
        } else if (k == K::dlytime) {
            StringArray names; for (auto* n : DLY_NAMES) names.add(n);
            L.add(std::make_unique<AudioParameterChoice>(ParameterID { id, 1 }, name, names, 7));
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
            case K::cmpamt: txt = [](float v, int) { return v <= 0 ? String("Off") : String(juce::roundToInt(v * 100)) + "%"; }; break;
            case K::cmprel: txt = [](float v, int) { return String(juce::roundToInt(cmpRelMs(v))) + " ms"; }; break;
            case K::platepre: case K::cnvpre: txt = [](float v, int) { return String(juce::roundToInt(250 * v * v)) + " ms"; }; break;
            case K::platedecay: txt = [](float v, int) { return String(revEx(0.3, 20, v), 2) + " s"; }; break;
            case K::sprdecay: txt = [](float v, int) { return String(revEx(0.4, 6, v), 2) + " s"; }; break;
            case K::platedamp: case K::platebw: case K::cnvtone: txt = [](float v, int) { return String(revEx(1000, 20000, v) / 1000, 1) + " kHz"; }; break;
            case K::sprtone: txt = [](float v, int) { return String(revEx(1500, 6000, v) / 1000, 1) + " kHz"; }; break;
            case K::platelo: case K::cnvlo: txt = [](float v, int) { return String(juce::roundToInt(revEx(20, 1000, v))) + " Hz"; }; break;
            case K::sprlo: txt = [](float v, int) { return String(juce::roundToInt(revEx(50, 1000, v))) + " Hz"; }; break;
            case K::sprdwell: txt = [](float v, int) { return "x" + String(revEx(0.5, 8, v), 1); }; break;
            case K::platemod: txt = [](float v, int) { return String(juce::roundToInt(200 * v)) + "%"; }; break;
            case K::dsdist: txt = [](float v, int) { return String(juce::roundToInt(1 + PP::DS_VR1 * v / PP::DS_R13)) + "x"; }; break;
            case K::tsdrive: txt = [](float v, int) { return String(juce::roundToInt(1 + (PP::TS_R6 + PP::TS_P1 * audioTaper(v)) / PP::TS_R4)) + "x"; }; break;
            case K::fldtimbre: txt = [](float v, int) { return String(0.5 + 9.5 * v * v, 1) + " V"; }; break;
            case K::fldsym: txt = [](float v, int) { const double x = (v - 0.5) * 4; return (x >= 0 ? "+" : "") + String(x, 1) + " V"; }; break;
            case K::cnvwidth: txt = [](float v, int) { return String(juce::roundToInt(150 * v)) + "%"; }; break;
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
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "fx_rat", 1 }, "Drive on", false)); // the drive slot (RAT, DS-1 or TS808); id kept
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "fx_dly", 1 }, "Delay on", false));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "fx_lex", 1 }, "224 on", false));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "fx_fold", 1 }, "Folder on", false));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "declick", 1 }, "De-click", true));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "seq", 1 }, "Sequencer", true));
    L.add(std::make_unique<AudioParameterFloat>(ParameterID { "declickms", 1 }, "De-click fade", NormalisableRange<float>(0.5f, 3.0f, 0.25f), 1.5f,
        AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return String(v, 2) + " ms"; })));
    return L;
}

// ---------------------------------------------------------------------------------------------------
double partTolerance(int k) {
    const auto& m = PARTS_META[k]; const juce::String id = m.id, block = m.block, unit = juce::String::fromUTF8(parts()[k].unit);
    if (m.fixed || block == "224") return 0;
    if (block == "CV") return 0.005;
    if (id == "F_TUNE" || id == "CV_TUNE") return 0.003;
    if (id.startsWith("VR")) return 0.2;
    if (unit == juce::String::fromUTF8("Ω")) return 0.05;
    if (unit == "F") return parts()[k].v >= 1e-6 ? 0.2 : 0.1;
    if (id == "RAT_GBW" || id == "RAT_SR") return 0.2;
    return 0.05;
}

std::vector<double> seriesValues(int k) {
    static const double E24[] = { 1.0, 1.1, 1.2, 1.3, 1.5, 1.6, 1.8, 2.0, 2.2, 2.4, 2.7, 3.0, 3.3, 3.6, 3.9, 4.3, 4.7, 5.1, 5.6, 6.2, 6.8, 7.5, 8.2, 9.1 };
    static const double E12[] = { 1.0, 1.2, 1.5, 1.8, 2.2, 2.7, 3.3, 3.9, 4.7, 5.6, 6.8, 8.2 };
    const auto& m = PARTS_META[k]; const juce::String src = m.source, unit = juce::String::fromUTF8(parts()[k].unit);
    std::vector<double> out;
    if (m.fixed || !(src == "schematic" || src == "td3" || src == "rat")) return out;
    const bool ohm = unit == juce::String::fromUTF8("Ω");
    if (!ohm && unit != "F") return out;
    const auto r = partRange(k);
    for (int dec = (int)std::floor(std::log10(r.lo)) - 1; dec <= (int)std::ceil(std::log10(r.hi)); dec++) {
        auto add = [&](double mm) { const double v = std::stod(juce::String(mm * std::pow(10.0, dec), 3, true).toStdString()); if (v >= r.lo * 0.999 && v <= r.hi * 1.001) out.push_back(v); };
        if (ohm) for (double mm : E24) add(mm); else for (double mm : E12) add(mm);
    }
    return out;
}
double snapReal(int k, double v) {
    const auto vs = seriesValues(k); if (vs.empty() || v <= 0) return v;
    double best = v, bd = 1e300;
    for (double x : vs) { const double d = std::fabs(std::log(x / v)); if (d < bd) { bd = d; best = x; } }
    return best;
}

const std::vector<CcTarget>& ccMap() {
    static const std::vector<CcTarget> m = [] {
        std::vector<CcTarget> v;
        // same order as the web app (MIDI_MAP in midi.js): CC 1-8 the voice dials with Key track after Reso, CC 9 Filter trim, then the mods
        for (int k = 0; k <= K::volume; ++k) {
            v.push_back({ knobs()[k].id, knobs()[k].name });
            if (k == K::resonance) v.push_back({ "m_KT", "Key track" });
        }
        v.push_back({ "m_I_REF", "Filter trim" });
        for (auto& f : frontMods()) { const juce::String p(f.part); if (p != "I_REF" && p != "KT") v.push_back({ "m_" + p, f.name }); }
        return v;
    }();
    return m;
}

TB303Processor::TB303Processor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "TB303", createLayout()) {
    jassert(PARTS_META_COUNT == P::COUNT);
    for (int i = 0; i < P::COUNT; ++i) jassert(juce::String(PARTS_META[i].id) == parts()[i].id);
    for (int k = 0; k < K::COUNT; ++k) knobParams[(size_t)k] = apvts.getRawParameterValue(knobs()[k].id);
    for (auto& m : frontMods()) modParams.push_back({ partIndex(m.part), apvts.getRawParameterValue(juce::String("m_") + m.part) });
    lastMod.assign(modParams.size(), -1.0f);
    pWave = apvts.getRawParameterValue("wave"); pRat = apvts.getRawParameterValue("fx_rat"); pLex = apvts.getRawParameterValue("fx_lex");
    pFold = apvts.getRawParameterValue("fx_fold"); pDly = apvts.getRawParameterValue("fx_dly"); pDeclick = apvts.getRawParameterValue("declick"); pDeclickMs = apvts.getRawParameterValue("declickms");
    pSeq = apvts.getRawParameterValue("seq");
    apvts.addParameterListener("declick", this);
    for (int i = 0; i < P::COUNT; ++i) extraParts[(size_t)i] = parts()[i].v;
    for (auto& c : ccPending) c = -1.0f;
    pubPattern.reserve(64 * 48); audioPattern.reserve(64 * 48); held.reserve(128);
    loadUserData();
    publish();
}

TB303Processor::~TB303Processor() { cancelPendingUpdate(); apvts.removeParameterListener("declick", this); }

bool TB303Processor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

double TB303Processor::knobValue(int k) const {
    const double raw = knobParams[(size_t)k]->load();
    if (k == K::lexprog) return raw / 8.0;
    if (const int n = revSteps(k)) return raw / (n - 1);
    if (k == K::dlytime) return raw / 10.0;
    return raw;
}
tb303::Knobs TB303Processor::allKnobs() const { Knobs k; for (int i = 0; i < K::COUNT; ++i) k[(size_t)i] = knobValue(i); return k; }
tb303::Parts TB303Processor::allParts() const { Parts p; for (int i = 0; i < P::COUNT; ++i) p[(size_t)i] = partValue(i); return p; }

void TB303Processor::prepareToPlay(double sampleRate, int) {
    fs = sampleRate;
    engine = std::make_unique<Engine>(sampleRate, (uint32_t)juce::Random::getSystemRandom().nextInt() | 1u);
    // the recorded impulse responses for Conv, at this sample rate
    for (const auto& ir : convIRs()) {
        std::vector<float> ch[2];
        for (int c = 0; c < 2; ++c) {
            const float* src = ir.data.getReadPointer(juce::jmin(c, ir.data.getNumChannels() - 1));
            const int n = ir.data.getNumSamples();
            if (std::abs(sampleRate - ir.rate) < 0.5) ch[c].assign(src, src + n);
            else {
                const double ratio = ir.rate / sampleRate; const int m = (int)std::ceil(n / ratio);
                ch[c].assign((size_t)m, 0.0f);
                juce::LagrangeInterpolator li; li.process(ratio, src, ch[c].data(), m, n, 0);
            }
        }
        engine->v.conv.setIR(ir.id.toStdString(), std::move(ch[0]), std::move(ch[1]));
    }
    lastKnob.fill(-1.0f);
    std::fill(lastMod.begin(), lastMod.end(), -1.0f);
    partsSeen = -1; tuneSeen = -1; patternSeen = -1; sentParts.fill(-1.0); lastFade = -1;
    held.clear(); hostSynced = false;
    engine->dc.setOn(*pDeclick > 0.5f);
    lastDeclick = *pDeclick > 0.5f;
    pushAllParams(true);
    setLatencySamples(engine->dc.latency());
}

void TB303Processor::parameterChanged(const juce::String& id, float) { if (id == "declick") triggerAsyncUpdate(); }

// message thread: de-click latency, and MIDI CC moves (parameters are set here, not on the audio thread)
void TB303Processor::handleAsyncUpdate() {
    const int D = juce::jmax(1, (int)jsRound(3e-3 * fs));
    setLatencySamples(*pDeclick > 0.5f ? D : 0);
    const auto& map = ccMap();
    for (size_t i = 0; i < map.size() && i < ccPending.size(); ++i) {
        const float v = ccPending[i].exchange(-1.0f);
        if (v >= 0) if (auto* p = apvts.getParameter(map[i].paramId)) p->setValueNotifyingHost(v);
    }
}

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
    const int tv = tuneVersion.load();
    if (tv != tuneSeen) {
        const juce::SpinLock::ScopedTryLockType lock(tuneLock);
        if (lock.isLocked()) { tuneSeen = tv; e.v.setTune(tuneCents.data()); }
    }
    e.v.square = *pWave > 0.5f;
    e.v.fxRat = *pRat > 0.5f; e.v.fxLex = *pLex > 0.5f; e.v.fxDly = *pDly > 0.5f; e.v.fxFold = *pFold > 0.5f;
    const bool dc = *pDeclick > 0.5f;
    if (dc != lastDeclick) { lastDeclick = dc; e.dc.setOn(dc); }
    if (*pDeclickMs != lastFade) { lastFade = *pDeclickMs; e.dc.setFade(lastFade); }
    const int patv = patternVersion.load();
    if (patv != patternSeen) {
        const juce::SpinLock::ScopedTryLockType lock(patternLock);
        if (lock.isLocked()) {
            patternSeen = patv; audioPattern.assign(pubPattern.begin(), pubPattern.end());
            e.pattern.assign(pubPattern.begin(), pubPattern.end()); e.gateLen = pubGate; e.shuffle = pubShuffle;
        }
    }
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

    // transport: the sequencer follows the host
    bool hostPlay = false; double ppq = 0, bpm = 128;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) {
            if (auto b = pos->getBpm()) bpm = juce::jlimit(20.0, 400.0, *b);
            if (auto q = pos->getPpqPosition(); q && pos->getIsPlaying()) { hostPlay = true; ppq = *q; }
        }
    if (bpm != e.bpm) e.setBpm(bpm);
    const bool seq = *pSeq > 0.5f;

    struct Ev { int pos; int type; int code; bool accent, slide; }; // type 0 = off, 1 = on
    Ev evs[512]; int nev = 0;
    auto add = [&](Ev ev) { if (nev < 512) evs[nev++] = ev; };

    int syncStep = -1;
    if (seq && hostPlay) {
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
                    if (s.gate) add({ pos, 1, s.code, s.accent, prev.gate && prev.slide });
                    else add({ pos, 0, 0, false, false });
                }
                if (s.gate && !s.slide) {
                    const double off = start + len * gl;
                    if (off >= ppq && off < ppq1) add({ juce::jlimit(0, N - 1, (int)((off - ppq) * spq)), 0, 0, false, false });
                }
                if (start < ppq1) syncStep = idx;
            }
        }
    } else if (hostSynced) { hostSynced = false; e.noteOff(); }

    // MIDI: mono, last note priority; a note played while another is held slides (legato). CCs move the dials.
    bool ccs = false;
    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        const int pos = juce::jlimit(0, N - 1, meta.samplePosition);
        if (m.isNoteOn()) {
            const int note = m.getNoteNumber();
            const bool accent = m.getVelocity() >= 100, slide = !held.empty();
            held.erase(std::remove(held.begin(), held.end(), note), held.end());
            if (held.size() < 127) held.push_back(note);
            if (!slide) midiAccent = accent;
            add({ pos, 1, note - 12, accent, slide });
        } else if (m.isNoteOff()) {
            const int note = m.getNoteNumber();
            const bool wasLast = !held.empty() && held.back() == note;
            held.erase(std::remove(held.begin(), held.end(), note), held.end());
            if (held.empty()) add({ pos, 0, 0, false, false });
            else if (wasLast) add({ pos, 1, held.back() - 12, midiAccent, true });
        } else if (m.isAllNotesOff() || m.isAllSoundOff()) {
            held.clear(); add({ pos, 0, 0, false, false });
        } else if (m.isController()) {
            const int cc = m.getControllerNumber();
            if (cc >= 1 && cc <= (int)ccMap().size()) { ccPending[(size_t)cc - 1] = m.getControllerValue() / 127.0f; ccs = true; }
        }
    }
    if (ccs) triggerAsyncUpdate();
    std::stable_sort(evs, evs + nev, [](const Ev& a, const Ev& b) { return a.pos != b.pos ? a.pos < b.pos : (a.type == 0) > (b.type == 0); });

    int i = 0;
    for (int k = 0; k < nev; ++k) {
        const Ev& ev = evs[k];
        if (ev.pos > i) { e.render(L, R, i, ev.pos); i = ev.pos; }
        if (ev.type == 1) e.noteOn(ev.code, ev.accent, ev.slide); else e.noteOff();
    }
    if (i < N) e.render(L, R, i, N);
    if (!R && buffer.getNumChannels() > 1) buffer.copyFrom(1, 0, buffer, 0, 0, N);

    int w = scopeWrite.load();
    for (int n = 0; n < N; ++n) { scope[w] = L[n]; w = (w + 1) & (SCOPE - 1); }
    scopeWrite = w;

    currentStep = hostSynced ? syncStep : -1;
    seqRunning = hostSynced;
}

// ---------------------------------------------------------------------------------------------------
void TB303Processor::publish() {
    std::vector<Step> p; double g, sh;
    {
        const juce::ScopedLock sl(modelLock);
        p = seq.sequence(chainOffs);
        g = seq.cur().gate; sh = seq.cur().shuffle;
    }
    {
        const juce::SpinLock::ScopedLockType lock(patternLock);
        pubPattern = std::move(p); pubGate = g; pubShuffle = sh;
    }
    ++patternVersion; ++modelVersion;
}

double TB303Processor::partValue(int part) const {
    for (auto& m : modParams) if (m.first == part) return partFromT(part, m.second->load());
    const juce::SpinLock::ScopedLockType lock(partsLock);
    return extraParts[(size_t)part];
}

static void setParam(juce::AudioProcessorValueTreeState& s, const juce::String& id, float plain) {
    if (auto* p = s.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(plain));
}

void TB303Processor::setPart(int part, double v) {
    if (part < 0 || part >= P::COUNT || isFixedPart(part)) return;
    if (isModPart(part)) { setParam(apvts, juce::String("m_") + parts()[part].id, (float)partToT(part, v)); return; }
    { const juce::SpinLock::ScopedLockType lock(partsLock); extraParts[(size_t)part] = v; }
    ++partsVersion;
}

void TB303Processor::restoreParts() {
    partsUndo.push_back(allParts());
    for (int p = 0; p < P::COUNT; ++p) setPart(p, parts()[p].v);
}

// New unit: every part lands inside its real tolerance (triangular). Messed up: circuit-bent, each part
// swings a quarter of its dial travel either way and about a third swing up to 60%. Pitch parts stay in
// tolerance so it plays in tune. Only sets that still make a sound are kept (functions.js parts()).
juce::String TB303Processor::randomiseParts(bool wild) {
    auto& R = juce::Random::getSystemRandom();
    std::vector<int> keys;
    for (int k = 0; k < P::COUNT; ++k) if (!isFixedPart(k) && partTolerance(k) > 0) keys.push_back(k);
    auto pitch = [](int k) { const juce::String id = parts()[k].id; return juce::String(PARTS_META[k].block) == "CV" || id == "F_TUNE" || id == "CV_TUNE"; };
    auto nudge = [&](int k) {
        const auto r = partRange(k); const double d = parts()[k].v, tri = R.nextDouble() + R.nextDouble() - 1;
        if (!wild || pitch(k)) return juce::String(juce::jlimit(r.lo, r.hi, d * (1 + partTolerance(k) * tri)), 4, true).getDoubleValue();
        const double t = partToT(k, d) + (R.nextDouble() * 2 - 1) * (R.nextDouble() < 0.3 ? 0.6 : 0.25);
        return juce::String(partFromT(k, juce::jlimit(0.0, 1.0, t)), 4, true).getDoubleValue();
    };
    const auto kn = allKnobs();
    Parts cur = allParts(), found {}; bool ok = false;
    for (int tries = 0; tries < (wild ? 24 : 8) && !ok; tries++) {
        Parts cand; for (int k = 0; k < P::COUNT; ++k) cand[(size_t)k] = parts()[k].v;
        for (int k : keys) cand[(size_t)k] = nudge(k);
        Voice v(22050, 12345u);
        v.c = cand; v.knobs = kn; v.square = *pWave > 0.5f; v.derive();
        std::vector<float> buf(11025, 0.0f);
        for (int n = 0; n < 4; n++) { v.noteOn(24 + 7 * (n & 1), n == 2, false); v.process(buf.data(), n * 2756, n * 2756 + 1500, nullptr); v.noteOff(); v.process(buf.data(), n * 2756 + 1500, n * 2756 + 2756, nullptr); }
        double e = 0; bool fin = true;
        for (float x : buf) { if (!std::isfinite(x)) fin = false; e += (double)x * x; }
        if (fin && std::sqrt(e / buf.size()) > 0.015) { found = cand; ok = true; }
    }
    if (!ok) return "Could not find a random set that still sounds. Try again.";
    partsUndo.push_back(cur); if (partsUndo.size() > 30) partsUndo.erase(partsUndo.begin());
    for (int k = 0; k < P::COUNT; ++k) setPart(k, found[(size_t)k]); // like applySound({ parts: set }): the rest go back to stock
    return wild ? "Messed up: " + juce::String((int)keys.size()) + " parts bent well outside spec (tuning left alone). Undo puts them back."
                : "Built a new machine: " + juce::String((int)keys.size()) + " parts somewhere inside their real tolerances. The Circuit tab shows them, and Undo puts them back.";
}

juce::String TB303Processor::undoParts() {
    if (partsUndo.empty()) return {};
    const auto p = partsUndo.back(); partsUndo.pop_back();
    for (int k = 0; k < P::COUNT; ++k) setPart(k, p[(size_t)k]);
    return "Part values put back.";
}

// ---------------------------------------------------------------------------------------------------
// microtuning
TB303Processor::Tune TB303Processor::tune() const { const juce::SpinLock::ScopedLockType lock(tuneLock); return tuneCents; }
void TB303Processor::setTune(const Tune& t) {
    {
        const juce::SpinLock::ScopedLockType lock(tuneLock);
        for (size_t i = 0; i < 12; i++) { const double c = std::isfinite(t[i]) ? juce::jlimit(-100.0, 100.0, t[i]) : 0.0; tuneCents[i] = jsRound(c * 10) / 10; }
    }
    ++tuneVersion;
}
void TB303Processor::pushTuneUndo() { tuneUndo.push_back(tune()); if (tuneUndo.size() > 50) tuneUndo.erase(tuneUndo.begin()); }
bool TB303Processor::undoTune() { if (tuneUndo.empty()) return false; const auto t = tuneUndo.back(); tuneUndo.pop_back(); setTune(t); return true; }

// ---------------------------------------------------------------------------------------------------
// web-app JSON. Same rules as cleanSettings() / applySound() / applyProject().
void TB303Processor::applySound(const juce::var& x) {
    auto* kn = x.getProperty("knobs", {}).getDynamicObject();
    // sounds saved before the two-dial Compactor (no Amount) load with 40%, 20 ms release (cleanSettings in state.js)
    juce::DynamicObject::Ptr knMig;
    if (kn && !kn->hasProperty("cmpamt")) { knMig = kn->clone().release(); knMig->setProperty("cmpamt", 0.4); knMig->setProperty("cmprel", 0.14142); kn = knMig.get(); }
    for (int k = 0; k < K::COUNT; ++k) {
        double v = knobs()[k].def;
        if (kn) { const auto j = kn->getProperty(knobs()[k].id); if ((j.isDouble() || j.isInt() || j.isInt64()) && (double)j >= 0 && (double)j <= 1) v = j; }
        float plain = (float)v;
        if (k == K::lexprog) plain = (float)jsRound(v * 8);
        else if (const int n = revSteps(k)) plain = (float)jsRound(v * (n - 1));
        else if (k == K::dlytime) plain = (float)dlyDivIndex(v);
        else if (k == K::dlypp) plain = v >= 0.5 ? 1.0f : 0.0f;
        setParam(apvts, knobs()[k].id, plain);
    }
    const auto* pa = x.getProperty("parts", {}).getDynamicObject();
    for (int p = 0; p < P::COUNT; ++p) {
        double v = parts()[p].v;
        if (pa && !isFixedPart(p)) {
            const auto j = pa->getProperty(parts()[p].id);
            if ((j.isDouble() || j.isInt() || j.isInt64()) && std::isfinite((double)j) && (double)j >= 0) v = j;
            if (p == P::NOISE) v = std::min(v, 1.0);
        }
        setPart(p, v);
    }
    Tune tn {};
    if (const auto* ta = x.getProperty("tune", {}).getArray(); ta && ta->size() == 12)
        for (int i = 0; i < 12; i++) { const auto j = (*ta)[i]; if (j.isDouble() || j.isInt() || j.isInt64()) tn[(size_t)i] = j; }
    setTune(tn);
    setParam(apvts, "wave", x.getProperty("wave", "saw").toString() == "square" ? 1.0f : 0.0f);
    const auto fx = x.getProperty("fx", {});
    setParam(apvts, "fx_rat", (bool)fx.getProperty("rat", false) ? 1.0f : 0.0f);
    setParam(apvts, "fx_lex", (bool)fx.getProperty("lex", false) ? 1.0f : 0.0f);
    setParam(apvts, "fx_dly", (bool)fx.getProperty("dly", false) ? 1.0f : 0.0f);
    setParam(apvts, "fx_fold", (bool)fx.getProperty("fold", false) ? 1.0f : 0.0f);
}

void TB303Processor::applyProject(const juce::var& x) {
    applySound(x.getProperty("sound", juce::var(new juce::DynamicObject())));
    {
        const juce::ScopedLock sl(modelLock);
        const auto banks = x.getProperty("banks", {});
        if (banks.isObject()) seq.fromVar(banks, x.getProperty("sel", {}), x.getProperty("chain", {}));
        else { seq.chain.clear(); }
    }
    publish();
}

void TB303Processor::applyPreset(const juce::var& x) {
    const auto kind = x.getProperty("kind", "").toString();
    if (kind == "pattern") {
        if (soundOnly()) return;
        const auto pt = x.getProperty("pattern", {});
        auto p = SeqModel::patternFromVar(pt.getProperty("pattern", {}));
        {
            const juce::ScopedLock sl(modelLock);
            if (!p.empty()) { seq.cur().pattern = p; seq.tail.clear(); seq.edited(); }
            const auto g = pt.getProperty("gate", {}), sh = pt.getProperty("shuffle", {});
            if (g.isDouble() || g.isInt()) seq.cur().gate = juce::jlimit(0.1, 0.95, (double)g);
            if (sh.isDouble() || sh.isInt()) seq.cur().shuffle = juce::jlimit(0.0, 0.5, (double)sh);
        }
        publish(); return;
    }
    if (kind == "sound" || soundOnly()) { applySound(x.getProperty("sound", juce::var(new juce::DynamicObject()))); return; }
    applyProject(x);
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
        if (std::fabs(v - d) > (d != 0 ? 1e-4 * std::fabs(d) : 1e-9)) pa->setProperty(parts()[p].id, juce::String(v, 4, true).getDoubleValue());
    }
    s->setProperty("parts", juce::var(pa));
    const auto tn = tune();
    if (std::any_of(tn.begin(), tn.end(), [](double c) { return c != 0; })) { juce::Array<juce::var> ta; for (double c : tn) ta.add(c); s->setProperty("tune", ta); }
    s->setProperty("wave", *pWave > 0.5f ? "square" : "saw");
    auto* fx = new juce::DynamicObject();
    fx->setProperty("rat", *pRat > 0.5f); fx->setProperty("lex", *pLex > 0.5f); fx->setProperty("dly", *pDly > 0.5f); fx->setProperty("fold", *pFold > 0.5f);
    s->setProperty("fx", juce::var(fx));
    return juce::var(s);
}

juce::var TB303Processor::projectSnapshot() const {
    auto* pr = new juce::DynamicObject();
    pr->setProperty("sound", soundSnapshot());
    pr->setProperty("bpm", engine ? juce::roundToInt(engine->bpm) : 128);
    const juce::ScopedLock sl(modelLock);
    auto* sel = new juce::DynamicObject(); sel->setProperty("g", juce::String::charToString((juce::juce_wchar)('A' + seq.selG))); sel->setProperty("n", seq.selN);
    pr->setProperty("sel", juce::var(sel));
    juce::Array<juce::var> ch; for (auto& k : seq.chain) ch.add(k);
    pr->setProperty("chain", ch);
    pr->setProperty("banks", seq.toVar());
    return juce::var(pr);
}

juce::String TB303Processor::importFile(const juce::File& f) {
    const auto j = juce::JSON::parse(f.loadFileAsString());
    if (!j.isObject()) return "That file is not JSON from the web app.";
    const auto name = f.getFileNameWithoutExtension().trim().substring(0, 40);
    loadUserData();
    auto* presets = userData.getProperty("presets", {}).getDynamicObject();
    int added = 0;
    auto addAll = [&](const juce::var& list, const juce::String& kind) {
        if (auto* o = list.getDynamicObject())
            for (auto& kv : o->getProperties()) {
                const auto nm = kv.name.toString().trim().substring(0, 40);
                if (!kv.value.isObject() || nm.isEmpty()) continue;
                juce::var v = kv.value;
                if (kind.isNotEmpty()) { auto* w = new juce::DynamicObject(); w->setProperty("kind", kind); w->setProperty(kind, kv.value); v = juce::var(w); }
                presets->setProperty(nm, v); ++added;
            }
    };
    addAll(j.getProperty("projectPresets", {}), {});
    addAll(j.getProperty("soundPresets", {}), "sound");
    addAll(j.getProperty("patternPresets", {}), "pattern");
    auto project = j.getProperty("project", {});
    if (!project.isObject()) project = j.getProperty("current", {}).getProperty("project", {});
    if (project.isObject()) {
        presets->setProperty(name, project);
        saveUserData();
        applyProject(project);
        lastPresetName = name;
        return "Loaded " + name + " and saved it to My presets" + (added ? juce::String(", with ") + juce::String(added) + " more" : juce::String()) + ".";
    }
    saveUserData();
    const auto cur = j.getProperty("current", {});
    if (cur.getProperty("sound", {}).isObject()) applySound(cur.getProperty("sound", {}));
    if (added) return "Added " + juce::String(added) + " preset" + (added == 1 ? "" : "s") + " to My presets.";
    const auto pat = SeqModel::patternFromVar(j.getProperty("pattern", {}));
    if (!pat.empty()) { { const juce::ScopedLock sl(modelLock); seq.cur().pattern = pat; seq.edited(); } publish(); return "Loaded the pattern into " + seq.selKey() + "."; }
    return "That file is not a preset export from the web app.";
}

void TB303Processor::exportProject(const juce::File& f) const {
    auto* o = new juce::DynamicObject();
    o->setProperty("app", "tb303-circuit-model"); o->setProperty("kind", "project"); o->setProperty("version", 1);
    o->setProperty("project", projectSnapshot());
    f.replaceWithText(juce::JSON::toString(juce::var(o)));
}

// Options > File > Export all: the current sound and pattern plus every saved preset
void TB303Processor::exportAll(const juce::File& f) const {
    auto* o = new juce::DynamicObject();
    o->setProperty("app", "tb303-circuit-model"); o->setProperty("version", 2);
    auto* cur = new juce::DynamicObject();
    cur->setProperty("sound", soundSnapshot());
    cur->setProperty("project", projectSnapshot());
    o->setProperty("current", juce::var(cur));
    o->setProperty("soundPresets", juce::var(new juce::DynamicObject()));
    o->setProperty("patternPresets", juce::var(new juce::DynamicObject()));
    o->setProperty("projectPresets", userData.getProperty("presets", juce::var(new juce::DynamicObject())));
    f.replaceWithText(juce::JSON::toString(juce::var(o)));
}

juce::String TB303Processor::exportFavourites(const juce::File& f) const {
    auto* out = new juce::DynamicObject(); int c = 0;
    for (auto& n : userPresetNames()) if (isFavourite(n)) { out->setProperty(n, userPreset(n)); ++c; }
    if (!c) return "No favourites yet: star some presets first.";
    auto* o = new juce::DynamicObject();
    o->setProperty("app", "tb303-circuit-model"); o->setProperty("kind", "presets"); o->setProperty("version", 1);
    o->setProperty("projectPresets", juce::var(out));
    f.replaceWithText(juce::JSON::toString(juce::var(o)));
    return "Exported " + juce::String(c) + " favourite" + (c == 1 ? "" : "s") + " to " + f.getFileName() + ".";
}

// ---------------------------------------------------------------------------------------------------
juce::File TB303Processor::userPresetFile() {
#if JUCE_MAC
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Application Support/TB303 Circuit Model/presets.json");
#else
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("TB303 Circuit Model/presets.json");
#endif
}
void TB303Processor::loadUserData() {
    auto j = juce::JSON::parse(userPresetFile().loadFileAsString());
    if (!j.isObject()) j = juce::var(new juce::DynamicObject());
    // the first version of the plug-in kept presets at the top level
    if (!j.hasProperty("presets")) {
        auto* p = new juce::DynamicObject();
        if (auto* o = j.getDynamicObject()) for (auto& kv : o->getProperties()) if (kv.value.isObject()) p->setProperty(kv.name, kv.value);
        j = juce::var(new juce::DynamicObject()); j.getDynamicObject()->setProperty("presets", juce::var(p));
    }
    if (!j.getProperty("presets", {}).isObject()) j.getDynamicObject()->setProperty("presets", juce::var(new juce::DynamicObject()));
    if (!j.getProperty("fav", {}).isArray()) j.getDynamicObject()->setProperty("fav", juce::Array<juce::var>());
    userData = j;
}
void TB303Processor::saveUserData() const {
    auto f = userPresetFile(); f.getParentDirectory().createDirectory();
    f.replaceWithText(juce::JSON::toString(userData));
}
juce::StringArray TB303Processor::userPresetNames() const {
    juce::StringArray s;
    if (auto* o = userData.getProperty("presets", {}).getDynamicObject()) for (auto& kv : o->getProperties()) s.add(kv.name.toString());
    s.sortNatural();
    return s;
}
juce::var TB303Processor::userPreset(const juce::String& n) const { return userData.getProperty("presets", {}).getProperty(n, {}); }
static const juce::var& factoryData() {
    static const juce::var j = juce::JSON::parse(juce::String::fromUTF8(BinaryData::factorypresets_json, BinaryData::factorypresets_jsonSize));
    return j;
}
juce::StringArray TB303Processor::factoryPresetNames() {
    juce::StringArray s;
    if (auto* o = factoryData().getDynamicObject()) for (auto& kv : o->getProperties()) if (kv.value.isObject()) s.add(kv.name.toString());
    s.sortNatural();
    return s;
}
juce::var TB303Processor::factoryPreset(const juce::String& n) { return factoryData().getProperty(n, {}); }
juce::var TB303Processor::anyPreset(const juce::String& n) const {
    const auto u = userPreset(n);
    return u.isObject() ? u : factoryPreset(n);
}
void TB303Processor::savePreset(const juce::String& n) {
    if (n.isEmpty()) return;
    loadUserData();
    userData.getProperty("presets", {}).getDynamicObject()->setProperty(n, projectSnapshot());
    saveUserData();
    lastPresetName = n;
}
void TB303Processor::removeUserPreset(const juce::String& n) {
    loadUserData();
    userData.getProperty("presets", {}).getDynamicObject()->removeProperty(n);
    setFavourite(n, false);
    saveUserData();
}
bool TB303Processor::isFavourite(const juce::String& n) const {
    if (auto* a = userData.getProperty("fav", {}).getArray()) return a->contains(n);
    return false;
}
void TB303Processor::setFavourite(const juce::String& n, bool on) {
    auto* a = userData.getProperty("fav", {}).getArray(); if (!a) return;
    a->removeAllInstancesOf(n);
    if (on) a->add(n);
    saveUserData();
}
bool TB303Processor::soundOnly() const { return (bool)userData.getProperty("soundOnly", false); }
void TB303Processor::setSoundOnly(bool on) { userData.getDynamicObject()->setProperty("soundOnly", on); saveUserData(); }

// ---------------------------------------------------------------------------------------------------
void TB303Processor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = apvts.copyState();
    juce::ValueTree extra("Extra");
    {
        const juce::ScopedLock sl(modelLock);
        auto* o = new juce::DynamicObject();
        o->setProperty("banks", seq.toVar());
        auto* sel = new juce::DynamicObject(); sel->setProperty("g", juce::String::charToString((juce::juce_wchar)('A' + seq.selG))); sel->setProperty("n", seq.selN);
        o->setProperty("sel", juce::var(sel));
        juce::Array<juce::var> ch; for (auto& k : seq.chain) ch.add(k);
        o->setProperty("chain", ch);
        o->setProperty("genDens", seq.genDens); o->setProperty("genBars", seq.genBars);
        extra.setProperty("seq", juce::JSON::toString(juce::var(o), true), nullptr);
    }
    extra.setProperty("preset", lastPresetName, nullptr);
    juce::String ps;
    {
        const juce::SpinLock::ScopedLockType lock(partsLock);
        for (int p = 0; p < P::COUNT; ++p)
            if (!isModPart(p) && extraParts[(size_t)p] != parts()[p].v) ps << parts()[p].id << "=" << juce::String(extraParts[(size_t)p], 9, true) << ";";
    }
    extra.setProperty("parts", ps, nullptr);
    { juce::StringArray ts; for (double t : tune()) ts.add(juce::String(t)); extra.setProperty("tune", ts.joinIntoString(";"), nullptr); extra.setProperty("tuneRoot", tuneRoot, nullptr); }
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
    if (!state.getChildWithProperty("id", "cmpamt").isValid()) { // sessions saved before the two-dial Compactor: 40%, 20 ms release
        juce::ValueTree a("PARAM"); a.setProperty("id", "cmpamt", nullptr).setProperty("value", 0.4, nullptr); state.appendChild(a, nullptr);
        auto r = state.getChildWithProperty("id", "cmprel"); if (r.isValid()) r.setProperty("value", 0.14142, nullptr);
    }
    apvts.replaceState(state);
    if (!extra.isValid()) return;
    const auto j = juce::JSON::parse(extra.getProperty("seq").toString());
    if (j.isObject()) {
        const juce::ScopedLock sl(modelLock);
        seq.fromVar(j.getProperty("banks", {}), j.getProperty("sel", {}), j.getProperty("chain", {}));
        seq.genDens = juce::jlimit(0, 7, (int)j.getProperty("genDens", 0)); seq.genBars = juce::jlimit(1, 4, (int)j.getProperty("genBars", 1));
    } else if (extra.hasProperty("pattern")) { // first plug-in version: one pattern
        std::vector<Step> p;
        for (auto& s : juce::StringArray::fromTokens(extra.getProperty("pattern").toString(), ";", "")) {
            const auto code = s.upToFirstOccurrenceOf(",", false, false).getIntValue();
            const auto f = s.fromFirstOccurrenceOf(",", false, false);
            if (f.length() >= 3) p.push_back({ juce::jlimit(0, 59, code), f[0] == '1', f[1] == '1', f[2] == '1' });
        }
        const juce::ScopedLock sl(modelLock);
        if (!p.empty()) seq.cur().pattern = p;
    }
    lastPresetName = extra.getProperty("preset").toString();
    {
        const juce::SpinLock::ScopedLockType lock(partsLock);
        for (int i = 0; i < P::COUNT; ++i) if (!isModPart(i)) extraParts[(size_t)i] = parts()[i].v;
        for (auto& kv : juce::StringArray::fromTokens(extra.getProperty("parts").toString(), ";", "")) {
            const int i = partIndex(kv.upToFirstOccurrenceOf("=", false, false).toStdString());
            if (i >= 0 && !isModPart(i)) extraParts[(size_t)i] = kv.fromFirstOccurrenceOf("=", false, false).getDoubleValue();
        }
    }
    ++partsVersion;
    Tune tn {};
    const auto ts = juce::StringArray::fromTokens(extra.getProperty("tune").toString(), ";", "");
    if (ts.size() == 12) for (int i = 0; i < 12; i++) tn[(size_t)i] = ts[i].getDoubleValue();
    setTune(tn); tuneRoot = juce::jlimit(0, 11, (int)extra.getProperty("tuneRoot", 9));
    publish();
}

juce::AudioProcessorEditor* TB303Processor::createEditor() { return new TB303Editor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TB303Processor(); }
