#include "PluginProcessor.h"
#ifndef TB303_NO_EDITOR
#include "PluginEditor.h"
#endif
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

static juce::String formatPart(double v, const juce::String& unit) {
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
    if (s == "route") return "Routing ";
    if (s == "flg") return "Flanger ";
    return {};
}

// the reverb slot's stepped knobs: how many positions each has (0 = a plain dial)
static int revSteps(int k) {
    switch (k) {
    case K::revtype: return 4;
    case K::drvtype: return 3;
    case K::fxorder: return 2;
    case K::fxret: return 3;
    case K::flgmode: return 2;
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
            else if (k == K::fxorder) names = { "Folder > Drive", "Drive > Folder" };
            else if (k == K::fxret) names = { "Parallel", "Delay > Reverb", "Reverb > Delay" };
            else if (k == K::flgmode) names = { "Flanger", "Chorus" };
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
            case K::flgman: txt = [](float v, int) { return String(flgMs(v), 1) + " ms"; }; break;
            case K::flgrate: txt = [](float v, int) { const double f = flgRate(v); return String(f, f < 1 ? 2 : 1) + " Hz"; }; break;
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
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "fx_flg", 1 }, "Flanger on", false));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "declick", 1 }, "De-click", true));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "seq", 1 }, "Sequencer", true));
    L.add(std::make_unique<AudioParameterFloat>(ParameterID { "declickms", 1 }, "De-click fade", NormalisableRange<float>(0.5f, 3.0f, 0.25f), 1.5f,
        AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return String(v, 2) + " ms"; })));
    return L;
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


static bool isFixedPart(int part) { return part >= 0 && part < PARTS_META_COUNT && PARTS_META[part].fixed; }
static const char* const FX_NAMES[] = { "rat", "lex", "dly", "fold", "flg" };

// the page's value for a dial (0..1) as the parameter's plain value
static float plainFor(int k, double v) {
    v = juce::jlimit(0.0, 1.0, v);
    if (k == K::lexprog) return (float)jsRound(v * 8);
    if (const int n = revSteps(k)) return (float)jsRound(v * (n - 1));
    if (k == K::dlytime) return (float)dlyDivIndex(v);
    if (k == K::dlypp) return v >= 0.5 ? 1.0f : 0.0f;
    return (float)v;
}

// ---------------------------------------------------------------------------------------------------
// localStorage
bool isProjectKey(const juce::String& key) {
    static const juce::StringArray keys { "tb303.session", "tb303.banks", "tb303.bankSel", "tb303.chain", "tb303.pattern", "tb303.tuneRoot", "tb303.mainTab" };
    return keys.contains(key);
}
juce::File SharedStore::file() {
#if JUCE_MAC
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Application Support/TB303 Circuit Model/web-store.json");
#else
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("TB303 Circuit Model/web-store.json");
#endif
}
SharedStore& SharedStore::get() { static SharedStore* s = new SharedStore(); return *s; } // never deleted: may outlive the message loop
SharedStore::SharedStore() {
    const auto j = juce::JSON::parse(file().loadFileAsString());
    if (auto* o = j.getDynamicObject()) {
        for (auto& kv : o->getProperties()) data[kv.name.toString()] = kv.value.toString();
        return;
    }
    // first run: the presets saved by the earlier plug-in (its own preset browser) become My presets
    const auto old = juce::JSON::parse(file().getSiblingFile("presets.json").loadFileAsString());
    auto presets = old.getProperty("presets", {});
    if (!presets.isObject() && old.isObject()) presets = old; // the first version kept them at the top level
    if (auto* p = presets.getDynamicObject(); p && p->getProperties().size() > 0) {
        data["tb303.projectPresets"] = juce::JSON::toString(presets, true);
        if ((bool)old.getProperty("soundOnly", false)) data["tb303.prjSoundOnly"] = "true";
        dirty = true; flush();
    }
}
std::map<juce::String, juce::String> SharedStore::all() { const juce::ScopedLock sl(lock); return data; }
void SharedStore::set(const juce::String& key, const juce::var& value) {
    {
        const juce::ScopedLock sl(lock);
        if (value.isVoid() || value.isUndefined()) data.erase(key); else data[key] = value.toString();
        dirty = true;
    }
    startTimer(400);
}
void SharedStore::flush() {
    stopTimer();
    juce::var out;
    {
        const juce::ScopedLock sl(lock);
        if (!dirty) return;
        dirty = false;
        auto* o = new juce::DynamicObject();
        for (auto& [k, v] : data) o->setProperty(k, v);
        out = juce::var(o);
    }
    auto f = file(); f.getParentDirectory().createDirectory();
    f.replaceWithText(juce::JSON::toString(out));
}

// ---------------------------------------------------------------------------------------------------
TB303Processor::TB303Processor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "TB303", createLayout()) {
    jassert(PARTS_META_COUNT == P::COUNT);
    for (int i = 0; i < P::COUNT; ++i) jassert(juce::String(PARTS_META[i].id) == parts()[i].id);
    for (int k = 0; k < K::COUNT; ++k) knobParams[(size_t)k] = apvts.getRawParameterValue(knobs()[k].id);
    for (auto& m : frontMods()) modParams.push_back({ partIndex(m.part), apvts.getRawParameterValue(juce::String("m_") + m.part) });
    lastMod.assign(modParams.size(), -1.0f);
    seenMod.assign(modParams.size(), 0.0f);
    pWave = apvts.getRawParameterValue("wave"); pRat = apvts.getRawParameterValue("fx_rat"); pLex = apvts.getRawParameterValue("fx_lex");
    pFold = apvts.getRawParameterValue("fx_fold"); pFlg = apvts.getRawParameterValue("fx_flg"); pDly = apvts.getRawParameterValue("fx_dly");
    pDeclick = apvts.getRawParameterValue("declick"); pDeclickMs = apvts.getRawParameterValue("declickms");
    pSeq = apvts.getRawParameterValue("seq");
    apvts.addParameterListener("declick", this);
    for (int i = 0; i < P::COUNT; ++i) extraParts[(size_t)i] = parts()[i].v;
    for (auto& c : ccPending) c = -1.0f;
    // until the page sends its pattern: the web app's example line (EXAMPLE in state.js)
    static const int ex[16][4] = { {21,1,0,1},{21,0,0,1},{33,0,1,1},{31,0,0,1},{21,1,0,1},{0,0,0,0},{24,0,1,1},{26,0,0,1},
                                   {21,0,0,1},{33,1,0,1},{28,0,1,1},{24,0,0,1},{21,1,1,1},{19,0,0,1},{0,0,0,0},{36,1,0,1} };
    for (auto& s : ex) pubPattern.push_back({ s[0] ? s[0] : 21, s[3] != 0, s[1] != 0, s[2] != 0 });
    pubPattern.reserve(64 * 8); audioPattern.reserve(64 * 8); held.reserve(128);
    markAllSeen();
}

TB303Processor::~TB303Processor() {
    cancelPendingUpdate(); apvts.removeParameterListener("declick", this);
    for (auto& [p, t] : gestures) p->endChangeGesture();
}

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
double TB303Processor::partValue(int part) const {
    for (auto& m : modParams) if (m.first == part) return partFromT(part, m.second->load());
    const juce::SpinLock::ScopedLockType lock(partsLock);
    return extraParts[(size_t)part];
}
std::vector<Step> TB303Processor::pattern() const { const juce::SpinLock::ScopedLockType lock(patternLock); return pubPattern; }
std::map<juce::String, juce::String> TB303Processor::projectStore() const { const juce::ScopedLock sl(storeLock); return projStore; }

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
    e.v.fxRat = *pRat > 0.5f; e.v.fxLex = *pLex > 0.5f; e.v.fxDly = *pDly > 0.5f; e.v.fxFold = *pFold > 0.5f; e.v.fxFlg = *pFlg > 0.5f;
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

    // transport and tempo: the host's
    bool hostPlay = false; double ppq = 0, bpm = 128;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) {
            if (auto b = pos->getBpm()) bpm = juce::jlimit(20.0, 400.0, *b);
            if (auto q = pos->getPpqPosition(); q && pos->getIsPlaying()) { hostPlay = true; ppq = *q; }
        }
    if (bpm != e.bpm) e.setBpm(bpm);
    hostBpm = bpm;
    const bool seq = *pSeq > 0.5f;

    struct Ev { int pos; int type; int code; bool accent, slide; }; // type 0 = off, 1 = on
    Ev evs[512]; int nev = 0;
    auto add = [&](Ev ev) { if (nev < 512) evs[nev++] = ev; };

    // the sequencer: step i of the pattern starts at 16th note i (shuffle delays the odd ones), as TB303Engine walks it
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
    currentStep = hostSynced ? syncStep : -1;
}

// ---------------------------------------------------------------------------------------------------
// the page
static double num(const juce::var& v, double d = 0) { return v.isDouble() || v.isInt() || v.isInt64() || v.isBool() ? (double)v : d; }

void TB303Processor::touch(juce::RangedAudioParameter* p) {
    if (gestures.find(p) == gestures.end()) p->beginChangeGesture();
    gestures[p] = juce::Time::getMillisecondCounter();
}
void TB303Processor::endGestures() {
    const auto now = juce::Time::getMillisecondCounter();
    for (auto it = gestures.begin(); it != gestures.end();)
        if (now - it->second > 400) { it->first->endChangeGesture(); it = gestures.erase(it); } else ++it;
}
void TB303Processor::setKnobFromPage(int k, double v) {
    if (!std::isfinite(v)) return;
    auto* p = apvts.getParameter(knobs()[k].id);
    const float n = p->convertTo0to1(plainFor(k, v));
    if (std::abs(p->getValue() - n) > 1e-7f) { touch(p); p->setValueNotifyingHost(n); }
    seenKnob[(size_t)k] = knobParams[(size_t)k]->load();
}
void TB303Processor::setPartFromPage(int part, double v) {
    if (!std::isfinite(v) || v < 0 || isFixedPart(part)) return;
    if (part == P::NOISE) v = std::min(v, 1.0);
    for (size_t i = 0; i < modParams.size(); ++i)
        if (modParams[i].first == part) {
            auto* p = apvts.getParameter(juce::String("m_") + parts()[part].id);
            const float n = p->convertTo0to1((float)partToT(part, v));
            if (std::abs(p->getValue() - n) > 1e-7f) { touch(p); p->setValueNotifyingHost(n); }
            seenMod[i] = modParams[i].second->load();
            return;
        }
    {
        const juce::SpinLock::ScopedLockType lock(partsLock);
        if (extraParts[(size_t)part] == v) return;
        extraParts[(size_t)part] = v;
    }
    ++partsVersion;
}
void TB303Processor::setBoolParam(const juce::String& id, bool on) {
    auto* p = apvts.getParameter(id);
    if ((p->getValue() > 0.5f) != on) { p->beginChangeGesture(); p->setValueNotifyingHost(on ? 1.0f : 0.0f); p->endChangeGesture(); }
}

void TB303Processor::handleMessage(const juce::var& m) {
    const auto type = m.getProperty("type", {}).toString();
    const auto name = m.getProperty("name", {}).toString();
    const auto value = m.getProperty("value", {});
    if (type == "knob") { const int k = knobIndex(name.toStdString()); if (k >= 0) setKnobFromPage(k, num(value, knobs()[k].def)); }
    else if (type == "component") { const int p = partIndex(name.toStdString()); if (p >= 0) setPartFromPage(p, num(value, -1)); }
    else if (type == "wave") { setBoolParam("wave", value.toString() == "square"); seenWave = pWave->load(); }
    else if (type == "fx") {
        for (size_t i = 0; i < 5; ++i)
            if (name == FX_NAMES[i]) { setBoolParam(juce::String("fx_") + FX_NAMES[i], (bool)value); seenFx[i] = apvts.getRawParameterValue(juce::String("fx_") + FX_NAMES[i])->load(); }
    }
    else if (type == "declick") {
        setBoolParam("declick", (bool)m.getProperty("on", true));
        auto* p = apvts.getParameter("declickms");
        p->setValueNotifyingHost(p->convertTo0to1((float)juce::jlimit(0.5, 3.0, num(m.getProperty("ms", 1.5), 1.5))));
    }
    else if (type == "tune") {
        if (const auto* a = value.getArray(); a && a->size() == 12) {
            { const juce::SpinLock::ScopedLockType lock(tuneLock);
              for (int i = 0; i < 12; i++) { const double c = num((*a)[i]); tuneCents[(size_t)i] = std::isfinite(c) ? jsRound(juce::jlimit(-100.0, 100.0, c) * 10) / 10 : 0.0; } }
            ++tuneVersion;
        }
    }
    else if (type == "pattern") {
        if (const auto* a = value.getArray(); a && !a->isEmpty()) {
            std::vector<Step> p; p.reserve((size_t)a->size());
            for (auto& s : *a) p.push_back({ juce::jlimit(0, 59, (int)num(s.getProperty("code", 33), 33)), (bool)s.getProperty("gate", false),
                                             (bool)s.getProperty("accent", false), (bool)s.getProperty("slide", false) });
            { const juce::SpinLock::ScopedLockType lock(patternLock); pubPattern = std::move(p); }
            ++patternVersion;
        }
    }
    else if (type == "groove") {
        { const juce::SpinLock::ScopedLockType lock(patternLock);
          pubShuffle = juce::jlimit(0.0, 0.5, num(m.getProperty("shuffle", 0))); pubGate = juce::jlimit(0.1, 0.95, num(m.getProperty("gate", 0.5), 0.5)); }
        ++patternVersion;
    }
    else if (type == "store") {
        const auto key = m.getProperty("key", {}).toString();
        if (!key.startsWith("tb303.")) return;
        if (isProjectKey(key)) {
            const juce::ScopedLock sl(storeLock);
            if (value.isVoid() || value.isUndefined()) projStore.erase(key); else projStore[key] = value.toString();
        } else SharedStore::get().set(key, value);
    }
    // 'bpm' and 'play': the host's; 'ir': the plug-in loads the impulse responses itself
}

void TB303Processor::markAllSeen() {
    for (int k = 0; k < K::COUNT; ++k) seenKnob[(size_t)k] = knobParams[(size_t)k]->load();
    for (size_t i = 0; i < modParams.size(); ++i) seenMod[i] = modParams[i].second->load();
    seenWave = pWave->load();
    for (size_t i = 0; i < 5; ++i) seenFx[i] = apvts.getRawParameterValue(juce::String("fx_") + FX_NAMES[i])->load();
}

void TB303Processor::collectChanges(juce::Array<juce::var>& out) {
    auto msg = [&](const char* what, const juce::String& id, const juce::var& v) {
        auto* o = new juce::DynamicObject();
        o->setProperty("type", "set"); o->setProperty("what", what); o->setProperty("id", id); o->setProperty("value", v);
        out.add(juce::var(o));
    };
    for (int k = 0; k < K::COUNT; ++k) {
        const float r = knobParams[(size_t)k]->load();
        if (r != seenKnob[(size_t)k]) { seenKnob[(size_t)k] = r; msg("knob", knobs()[k].id, knobValue(k)); }
    }
    for (size_t i = 0; i < modParams.size(); ++i) {
        const float r = modParams[i].second->load();
        if (r != seenMod[i]) { seenMod[i] = r; msg("part", parts()[modParams[i].first].id, partFromT(modParams[i].first, r)); }
    }
    if (const float r = pWave->load(); r != seenWave) { seenWave = r; msg("wave", {}, r > 0.5f ? "square" : "saw"); }
    for (size_t i = 0; i < 5; ++i) {
        const float r = apvts.getRawParameterValue(juce::String("fx_") + FX_NAMES[i])->load();
        if (r != seenFx[i]) { seenFx[i] = r; msg("fx", FX_NAMES[i], r > 0.5f); }
    }
}

// The page's localStorage at start: the shared keys, this project's keys, and the session's sound replaced by the
// processor's (the host may have moved dials, or loaded a state, while the page was closed). A value the page set
// is kept as it was when the parameter still holds it, so nothing drifts by float rounding.
juce::String TB303Processor::bootScript() {
    auto store = SharedStore::get().all();
    for (auto& [k, v] : projectStore()) store[k] = v;
    auto sess = juce::JSON::parse(store.count("tb303.session") ? store["tb303.session"] : juce::String());
    if (!sess.isObject()) sess = juce::var(new juce::DynamicObject());
    auto* so = sess.getDynamicObject();
    auto oldKnobs = so->getProperty("knobs"), oldParts = so->getProperty("parts");
    auto* kn = new juce::DynamicObject();
    for (int k = 0; k < K::COUNT; ++k) {
        const double raw = knobParams[(size_t)k]->load(), was = num(oldKnobs.getProperty(knobs()[k].id, {}), -1), def = knobs()[k].def;
        kn->setProperty(knobs()[k].id, was >= 0 && was <= 1 && plainFor(k, was) == raw ? was : plainFor(k, def) == raw ? def : knobValue(k));
    }
    so->setProperty("knobs", juce::var(kn));
    auto* pa = new juce::DynamicObject();
    for (int p = 0; p < P::COUNT; ++p) {
        if (isFixedPart(p)) continue;
        double v = partValue(p);
        for (auto& [mp, raw] : modParams)
            if (mp == p) {
                const double was = num(oldParts.getProperty(parts()[p].id, {}), parts()[p].v), t = raw->load();
                if ((float)partToT(p, was) == t) v = was; else if ((float)partToT(p, parts()[p].v) == t) v = parts()[p].v;
            }
        const double d = parts()[p].v;
        if (std::abs(v - d) > (d != 0 ? 1e-6 * std::abs(d) : 1e-9)) pa->setProperty(parts()[p].id, v);
    }
    so->setProperty("parts", juce::var(pa));
    so->setProperty("wave", *pWave > 0.5f ? "square" : "saw");
    auto* fx = new juce::DynamicObject();
    for (auto* n : FX_NAMES) fx->setProperty(n, apvts.getRawParameterValue(juce::String("fx_") + n)->load() > 0.5f);
    so->setProperty("fx", juce::var(fx));
    {
        const juce::SpinLock::ScopedLockType lock(tuneLock);
        if (std::any_of(tuneCents.begin(), tuneCents.end(), [](double c) { return c != 0; })) { juce::Array<juce::var> t; for (double c : tuneCents) t.add(c); so->setProperty("tune", t); }
        else so->removeProperty("tune");
    }
    store["tb303.session"] = juce::JSON::toString(sess, true);
    markAllSeen();
    auto* st = new juce::DynamicObject();
    for (auto& [k, v] : store) st->setProperty(k, v);
    auto* boot = new juce::DynamicObject(); boot->setProperty("store", juce::var(st));
    return "<script>window.TB303_BOOT = " + juce::JSON::toString(juce::var(boot), true).replace("</", "<\\/") + ";</script>";
}

// ---------------------------------------------------------------------------------------------------
// state: the parameters, plus the parts that are not parameters, the tuning, the pattern and the page's project keys
void TB303Processor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = apvts.copyState();
    juce::ValueTree page("Page");
    {
        auto* o = new juce::DynamicObject();
        for (auto& [k, v] : projectStore()) o->setProperty(k, v);
        page.setProperty("store", juce::JSON::toString(juce::var(o), true), nullptr);
    }
    {
        auto* o = new juce::DynamicObject();
        const juce::SpinLock::ScopedLockType lock(partsLock);
        for (int p = 0; p < P::COUNT; ++p) if (!isModPart(p) && extraParts[(size_t)p] != parts()[p].v) o->setProperty(parts()[p].id, extraParts[(size_t)p]);
        page.setProperty("parts", juce::JSON::toString(juce::var(o), true), nullptr);
    }
    {
        juce::Array<juce::var> t;
        const juce::SpinLock::ScopedLockType lock(tuneLock);
        for (double c : tuneCents) t.add(c);
        page.setProperty("tune", juce::JSON::toString(t, true), nullptr);
    }
    {
        juce::Array<juce::var> a;
        const juce::SpinLock::ScopedLockType lock(patternLock);
        for (auto& s : pubPattern) {
            auto* o = new juce::DynamicObject();
            o->setProperty("code", s.code); o->setProperty("gate", s.gate); o->setProperty("accent", s.accent); o->setProperty("slide", s.slide);
            a.add(juce::var(o));
        }
        page.setProperty("pattern", juce::JSON::toString(a, true), nullptr);
        page.setProperty("gate", pubGate, nullptr); page.setProperty("shuffle", pubShuffle, nullptr);
    }
    state.removeChild(state.getChildWithName("Page"), nullptr);
    state.removeChild(state.getChildWithName("Extra"), nullptr);
    state.appendChild(page, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void TB303Processor::setStateInformation(const void* data, int size) {
    auto xml = getXmlFromBinary(data, size);
    if (!xml) return;
    auto state = juce::ValueTree::fromXml(*xml);
    if (!state.hasType(apvts.state.getType())) return;
    if (!state.getChildWithProperty("id", "cmpamt").isValid()) { // sessions saved before the two-dial Compactor: 40%, 20 ms release
        juce::ValueTree a("PARAM"); a.setProperty("id", "cmpamt", nullptr).setProperty("value", 0.4, nullptr); state.appendChild(a, nullptr);
        auto r = state.getChildWithProperty("id", "cmprel"); if (r.isValid()) r.setProperty("value", 0.14142, nullptr);
    }
    const auto page = state.getChildWithName("Page"), extra = state.getChildWithName("Extra");
    apvts.replaceState(state);
    Parts ps; for (int i = 0; i < P::COUNT; ++i) ps[(size_t)i] = parts()[i].v;
    std::array<double, 12> tn {};
    std::map<juce::String, juce::String> st;
    if (page.isValid()) {
        const auto pj = juce::JSON::parse(page.getProperty("parts").toString()), tj = juce::JSON::parse(page.getProperty("tune").toString()),
                   sj = juce::JSON::parse(page.getProperty("store").toString());
        if (auto* o = pj.getDynamicObject())
            for (auto& kv : o->getProperties()) { const int p = partIndex(kv.name.toString().toStdString()); if (p >= 0 && !isModPart(p)) ps[(size_t)p] = num(kv.value, parts()[p].v); }
        if (const auto* a = tj.getArray(); a && a->size() == 12)
            for (int i = 0; i < 12; i++) tn[(size_t)i] = num((*a)[i]);
        if (auto* o = sj.getDynamicObject())
            for (auto& kv : o->getProperties()) if (isProjectKey(kv.name.toString())) st[kv.name.toString()] = kv.value.toString();
        juce::var pm(new juce::DynamicObject());
        pm.getDynamicObject()->setProperty("type", "pattern"); pm.getDynamicObject()->setProperty("value", juce::JSON::parse(page.getProperty("pattern").toString()));
        handleMessage(pm);
        juce::var g(new juce::DynamicObject());
        g.getDynamicObject()->setProperty("type", "groove"); g.getDynamicObject()->setProperty("gate", page.getProperty("gate", 0.5)); g.getDynamicObject()->setProperty("shuffle", page.getProperty("shuffle", 0));
        handleMessage(g);
    } else if (extra.isValid()) { // saved by the earlier plug-in: keep its parts and tuning (its pattern banks are not the web app's)
        for (auto& s : juce::StringArray::fromTokens(extra.getProperty("parts").toString(), ";", ""))
            if (const int p = partIndex(s.upToFirstOccurrenceOf("=", false, false).toStdString()); p >= 0 && !isModPart(p)) ps[(size_t)p] = s.fromFirstOccurrenceOf("=", false, false).getDoubleValue();
        const auto ts = juce::StringArray::fromTokens(extra.getProperty("tune").toString(), ";", "");
        if (ts.size() == 12) for (int i = 0; i < 12; i++) tn[(size_t)i] = ts[i].getDoubleValue();
    }
    { const juce::SpinLock::ScopedLockType lock(partsLock); extraParts = ps; }
    ++partsVersion;
    { const juce::SpinLock::ScopedLockType lock(tuneLock); tuneCents = tn; }
    ++tuneVersion;
    { const juce::ScopedLock sl(storeLock); projStore = std::move(st); }
    ++stateVersion;
}

#ifndef TB303_NO_EDITOR
juce::AudioProcessorEditor* TB303Processor::createEditor() { return new TB303Editor(*this); }
#else
juce::AudioProcessorEditor* TB303Processor::createEditor() { return nullptr; } // the host test has no window
#endif
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TB303Processor(); }
