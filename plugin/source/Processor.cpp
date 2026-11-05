#include "Processor.h"
#include "Editor.h"
#include <BinaryData.h>

namespace {
// page keys that belong to this instance (its project); everything else is the library every instance shares
bool isInstanceKey (const juce::String& k) {
    return k == "tb303.session" || k == "tb303.pattern" || k == "tb303.banks" || k == "tb303.bankSel" || k == "tb303.chain";
}
const char* kIRs[] = { "plate140", "plate140b", "drumroom", "silo", "garage", "musikverein", "star" };

const char* findResource (const juce::String& file, int& size) {
    for (int i = 0; i < BinaryData::namedResourceListSize; i++)
        if (file == BinaryData::originalFilenames[i]) return BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
    size = 0; return nullptr;
}
}

TB303Processor::TB303Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)) {}

TB303Processor::~TB303Processor() {
    *alive = false;
    if (irThread) irThread->stopThread (2000);
    delete pendingEngine.exchange (nullptr);
    delete retiredEngine.exchange (nullptr);
}

bool TB303Processor::isBusesLayoutSupported (const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    return layouts.getMainInputChannelSet().isDisabled() && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
}

// ------------------------------------------------------------------------------------------------ messages
bool TB303Processor::parse (const juce::var& m, Msg& out, std::string& key) {
    const juce::String type = m["type"].toString();
    const std::string name = m["name"].toString().toStdString();
    auto num = [] (const juce::var& v, double& d) { if (!(v.isDouble() || v.isInt() || v.isInt64() || v.isBool())) return false; d = (double) v; return true; };
    if (type == "knob" || type == "component") {
        out.kind = type == "knob" ? Msg::Knob : Msg::Component; out.name = name;
        if (!num (m["value"], out.a)) out.a = NAN; // JSON has no NaN; a non-number is set at once, as ParamSlew does
        key = (type == "knob" ? "k:" : "c:") + name; return !name.empty();
    }
    if (type == "wave") { out.kind = Msg::Wave; out.name = m["value"].toString().toStdString(); key = "wave"; return true; }
    if (type == "tune") {
        out.kind = Msg::Tune; out.vals.clear();
        if (auto* a = m["value"].getArray()) for (auto& x : *a) { double d; out.vals.push_back (num (x, d) ? d : NAN); }
        key = "tune"; return true;
    }
    if (type == "fx") { out.kind = Msg::Fx; out.name = name; out.on = (bool) m["value"]; key = "fx:" + name; return !name.empty(); }
    if (type == "declick") { out.kind = Msg::Declick; out.on = (bool) m["on"]; if (!num (m["ms"], out.a)) out.a = 1.5; key = "declick"; return true; }
    if (type == "groove") {
        out.kind = Msg::Groove; if (!num (m["shuffle"], out.a)) out.a = 0; if (!num (m["gate"], out.b)) out.b = 0.5;
        key = "groove"; return true;
    }
    if (type == "pattern") {
        out.kind = Msg::Pattern; out.steps.clear();
        if (auto* a = m["value"].getArray())
            for (auto& s : *a) {
                tb303::Step st; double c = 21; num (s["code"], c);
                st.code = (int) c; st.accent = (bool) s["accent"]; st.slide = (bool) s["slide"]; st.gate = (bool) s["gate"];
                out.steps.push_back (st);
            }
        key = "pattern"; return true;
    }
    return false; // bpm and play come from the DAW; ir from the plug-in itself
}

void TB303Processor::apply (Engine& e, Msg& m, bool instant) {
    switch (m.kind) {
        case Msg::Knob: if (instant) e.v.setKnob (m.name, m.a); else e.knob (m.name, m.a); break;
        case Msg::Component: if (instant) e.v.setComponent (m.name, m.a); else e.component (m.name, m.a); break;
        case Msg::Wave: e.setWave (m.name); break;
        case Msg::Tune: e.tune (m.vals.data(), (int) m.vals.size()); break;
        case Msg::Fx: e.setFx (m.name, m.on); break;
        case Msg::Declick: e.declick (m.on, m.a); break;
        case Msg::Groove: e.groove (m.a, m.b); break;
        case Msg::Pattern: e.setPattern (std::move (m.steps)); break;
        case Msg::IR: e.setIR (m.name, m.ir); break;
    }
}

void TB303Processor::handleUiMessage (const juce::var& m) {
    const juce::String type = m["type"].toString();
    if (type == "store") {
        const juce::String k = m["key"].toString(); const juce::var v = m["value"];
        library->set (k, v);
        if (isInstanceKey (k)) { std::lock_guard<std::mutex> g (snapshotLock); if (v.isString()) instStore[k] = v.toString(); else instStore.erase (k); }
        return;
    }
    if (type == "save") { if (onSave) onSave (m["name"].toString(), m["text"].toString()); return; }
    Msg msg; std::string key;
    if (!parse (m, msg, key)) return;
    { std::lock_guard<std::mutex> g (snapshotLock); snapshot[key] = m; }
    std::lock_guard<std::mutex> g (queueLock);
    queue.push_back (std::move (msg));
}

juce::var TB303Processor::pageInitData() const {
    juce::DynamicObject::Ptr store = new juce::DynamicObject();
    for (const auto& [k, v] : library->data) store->setProperty (k, v);
    {
        std::lock_guard<std::mutex> g (snapshotLock);
        for (const auto& [k, v] : instStore) store->setProperty (k, v);
    }
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty ("store", juce::var (store.get()));
    o->setProperty ("plugin", JucePlugin_VersionString);
    return juce::var (o.get());
}

std::optional<juce::WebBrowserComponent::Resource> TB303Processor::getResource (const juce::String& url) const {
    juce::String path = url.upToFirstOccurrenceOf ("?", false, false).fromFirstOccurrenceOf ("/", false, false);
    if (path.isEmpty() || path == "index.html") path = "index.html";
    const juce::String file = path.fromLastOccurrenceOf ("/", false, false);
    int size = 0; const char* data = findResource (file, size);
    if (!data) return std::nullopt;
    const juce::String ext = file.fromLastOccurrenceOf (".", false, false).toLowerCase();
    const juce::String mime = ext == "html" ? "text/html; charset=utf-8" : ext == "json" ? "application/json; charset=utf-8" : ext == "ttf" ? "font/ttf"
                            : ext == "flac" ? "audio/flac" : "application/octet-stream";
    std::vector<std::byte> bytes ((size_t) size);
    std::memcpy (bytes.data(), data, (size_t) size);
    return juce::WebBrowserComponent::Resource { std::move (bytes), mime };
}

bool TB303Processor::popMidi (int& status, int& d1, int& d2) {
    int s1, n1, s2, n2; midiFifo.prepareToRead (1, s1, n1, s2, n2);
    if (n1 + n2 < 1) return false;
    const auto& m = midiBuf[(size_t) (n1 ? s1 : s2)]; status = m[0]; d1 = m[1]; d2 = m[2];
    midiFifo.finishedRead (1);
    return true;
}

// ------------------------------------------------------------------------------------------------ engines
std::unique_ptr<TB303Processor::Engine> TB303Processor::makeEngine (double fs) {
    auto e = std::make_unique<Engine> (fs);
    e->v.rng = (uint32_t) juce::Random::getSystemRandom().nextInt(); // the web voice seeds its noise at random too
    e->onStep = [this] (int step) { stepForUi.store (step); firedG = pendingG; pendingG++; }; // audio thread
    e->v.conv.alloc(); // up front, so choosing Conv later never allocates on the audio thread (same state as the lazy alloc)
    std::lock_guard<std::mutex> g (snapshotLock);
    // the same order the page sends them in when audio starts: knobs, parts, wave, tuning, pattern, groove, fx, de-click
    static const char* order[] = { "k:", "c:", "wave", "tune", "pattern", "groove", "fx:", "declick" };
    for (const char* pre : order)
        for (const auto& [key, m] : snapshot)
            if (key.rfind (pre, 0) == 0) { Msg msg; std::string k; if (parse (m, msg, k)) apply (*e, msg, true); }
    if (irsRate == fs) for (const auto& [id, d] : irs) e->setIR (id, d);
    return e;
}

void TB303Processor::installEngine (std::unique_ptr<Engine> e) {
    delete pendingEngine.exchange (e.release());
}

void TB303Processor::handleAsyncUpdate() { delete retiredEngine.exchange (nullptr); }

void TB303Processor::startIRLoad (double fs) {
    if (irThread) irThread->stopThread (4000);
    // decode the FLACs from the web app's irs/ at this rate (the page decodes them at the audio rate the same way)
    struct Loader : juce::Thread {
        TB303Processor& p; double fs;
        Loader (TB303Processor& p_, double fs_) : juce::Thread ("303 IRs"), p (p_), fs (fs_) {}
        void run() override {
            juce::FlacAudioFormat flac;
            std::map<std::string, std::shared_ptr<tb303::IRData>> out;
            for (const char* id : kIRs) {
                if (threadShouldExit()) return;
                int size = 0; const char* data = findResource (juce::String (id) + ".flac", size);
                if (!data) continue;
                std::unique_ptr<juce::AudioFormatReader> r (flac.createReaderFor (new juce::MemoryInputStream (data, (size_t) size, false), true));
                if (!r || r->lengthInSamples <= 0) continue;
                const int n = (int) r->lengthInSamples, ch = (int) juce::jmin (2u, r->numChannels);
                juce::AudioBuffer<float> b (ch, n); r->read (&b, 0, n, 0, true, ch > 1);
                auto d = std::make_shared<tb303::IRData>();
                const double ratio = r->sampleRate / fs;
                for (int c = 0; c < ch; c++) {
                    std::vector<float>& dst = c ? d->R : d->L;
                    if (std::abs (ratio - 1) < 1e-9) dst.assign (b.getReadPointer (c), b.getReadPointer (c) + n);
                    else {
                        const int m = (int) std::ceil (n / ratio);
                        std::vector<float> src (b.getReadPointer (c), b.getReadPointer (c) + n); src.resize ((size_t) n + 256, 0.f);
                        dst.assign ((size_t) m, 0.f);
                        juce::Interpolators::WindowedSinc ws; ws.process (ratio, src.data(), dst.data(), m);
                    }
                }
                out[id] = d;
            }
            juce::MessageManager::callAsync ([&p = p, fs = fs, out, alive = p.alive] {
                if (!*alive) return;
                { std::lock_guard<std::mutex> g (p.snapshotLock); p.irs = out; p.irsRate = fs; }
                if (p.sampleRate != fs) return;
                std::lock_guard<std::mutex> g (p.queueLock);
                for (const auto& [id, d] : out) { Msg m; m.kind = Msg::IR; m.name = id; m.ir = d; p.queue.push_back (std::move (m)); }
            });
        }
    };
    irThread = std::make_unique<Loader> (*this, fs);
    irThread->startThread();
}

void TB303Processor::prepareToPlay (double sr, int samplesPerBlock) {
    sampleRate = sr;
    scratchR.assign ((size_t) juce::jmax (1, samplesPerBlock) * 2, 0.f);
    if (!engine || engine->fs != sr) {
        delete pendingEngine.exchange (nullptr);
        engine = makeEngine (sr);
        { std::lock_guard<std::mutex> g (queueLock); queue.clear(); } // the snapshot already has them
        playing = false; lastBpm = 0; expectPpq = NAN; firedG = LLONG_MIN;
    }
    bool need; { std::lock_guard<std::mutex> g (snapshotLock); need = irsRate != sr; }
    if (need) startIRLoad (sr);
}

// ------------------------------------------------------------------------------------------------ audio
// The DAW drives the sequencer: play and stop follow its transport, the tempo its tempo, and every 16th (with the
// pattern's shuffle on the odd ones) lands on the DAW's grid, counted from the start of the song.
void TB303Processor::syncToHost (Engine& e, int numSamples) {
    auto* ph = getPlayHead(); if (!ph) return;
    const auto pos = ph->getPosition(); if (!pos) return;
    const bool hostPlaying = pos->getIsPlaying();
    double bpm = pos->getBpm().orFallback (lastBpm > 0 ? lastBpm : 120.0);
    bpm = juce::jlimit (20.0, 999.0, bpm);
    if (std::abs (bpm - lastBpm) > 1e-9) { e.setBpm (bpm); lastBpm = bpm; bpmForUi.store (bpm); }
    if (hostPlaying != playing) {
        playing = hostPlaying; e.play (playing); firedG = LLONG_MIN; expectPpq = NAN;
        if (!playing) stepForUi.store (-1);
    }
    if (!playing) return;
    const auto ppqOpt = pos->getPpqPosition(); if (!ppqOpt) return; // no song position: the engine keeps its own count
    const double ppq = *ppqOpt, fs = e.fs;
    if (!(std::abs (ppq - expectPpq) < 1e-3)) firedG = LLONG_MIN; // a jump (loop, locate): start counting afresh
    expectPpq = ppq + numSamples * bpm / (60.0 * fs);
    const int n = (int) e.pattern.size(); if (!n) return;
    const double sh = e.shuffle, spq = fs * 60 / bpm / 4, q = ppq * 4, eps = 0.5 / spq;
    auto start = [sh] (long long g) { return (double) g + ((g & 1) ? sh : 0.0); };
    const long long g = (long long) std::floor (q);
    long long next = start (g) >= q - eps ? g : g + 1;
    if (firedG != LLONG_MIN && next <= firedG) next = firedG + 1;
    const double toNext = juce::jmax (0.0, (start (next) - q) * spq);
    e.step = (int) (((next - 1) % n + n) % n);
    e.ctr = (long long) std::llround (toNext);
    pendingG = next;
}

void TB303Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    if (auto* e = pendingEngine.exchange (nullptr)) {
        Engine* old = engine.release(); engine.reset (e);
        playing = false; lastBpm = 0; expectPpq = NAN; firedG = LLONG_MIN;
        delete retiredEngine.exchange (old);
        triggerAsyncUpdate();
    }
    const int n = buffer.getNumSamples();
    if (!engine) { buffer.clear(); return; }
    Engine& e = *engine;
    {
        std::unique_lock<std::mutex> lk (queueLock, std::try_to_lock);
        if (lk.owns_lock() && !queue.empty()) inbox.swap (queue);
    }
    for (auto& m : inbox) apply (e, m, false);
    inbox.clear();
    for (const auto meta : midi) {
        const auto msg = meta.getMessage();
        if (msg.isController()) {
            int s1, n1, s2, n2; midiFifo.prepareToWrite (1, s1, n1, s2, n2);
            if (n1 + n2 > 0) { midiBuf[(size_t) (n1 ? s1 : s2)] = { 0xB0 | (msg.getChannel() - 1), msg.getControllerNumber(), msg.getControllerValue() }; midiFifo.finishedWrite (1); }
        }
    }
    syncToHost (e, n);
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    if (!R) { if ((int) scratchR.size() < n) { buffer.clear(); return; } R = scratchR.data(); }
    e.render (L, R, 0, n);
    for (int c = 2; c < buffer.getNumChannels(); c++) buffer.copyFrom (c, 0, buffer, 0, 0, n);
}

// ------------------------------------------------------------------------------------------------ state
void TB303Processor::getStateInformation (juce::MemoryBlock& dest) {
    juce::DynamicObject::Ptr o = new juce::DynamicObject(), st = new juce::DynamicObject();
    juce::Array<juce::var> msgs;
    {
        std::lock_guard<std::mutex> g (snapshotLock);
        for (const auto& [k, m] : snapshot) msgs.add (m);
        for (const auto& [k, v] : instStore) st->setProperty (k, v);
    }
    o->setProperty ("format", 1);
    o->setProperty ("engine", msgs);
    o->setProperty ("store", juce::var (st.get()));
    const auto json = juce::JSON::toString (juce::var (o.get()), true);
    dest.replaceAll (json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void TB303Processor::setStateInformation (const void* data, int size) {
    const char* text = (const char*) data; int len = 0;
    while (len < size && text[len] != 0) len++; // the JSON, without anything a host or wrapper put after it
    const auto v = juce::JSON::parse (juce::String::fromUTF8 (text, len));
    if (!v.isObject()) return;
    {
        std::lock_guard<std::mutex> g (snapshotLock);
        snapshot.clear(); instStore.clear();
        if (auto* a = v["engine"].getArray())
            for (auto& m : *a) { Msg msg; std::string key; if (parse (m, msg, key)) snapshot[key] = m; }
        if (auto* s = v["store"].getDynamicObject())
            for (const auto& p : s->getProperties()) if (p.value.isString()) instStore[p.name.toString()] = p.value.toString();
    }
    if (sampleRate > 0) installEngine (makeEngine (sampleRate));
    { std::lock_guard<std::mutex> g (queueLock); queue.clear(); }
    juce::MessageManager::callAsync ([this, alive = alive] { if (*alive && onStateLoaded) onStateLoaded(); });
}

juce::AudioProcessorEditor* TB303Processor::createEditor() { return new TB303Editor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TB303Processor(); }
